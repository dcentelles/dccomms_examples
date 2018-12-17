#include <chrono>
#include <cpplogging/cpplogging.h>
#include <cpputils/SignalManager.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <dccomms_packets/SimplePacket.h>
#include <dccomms_packets/VariableLengthPacket.h>
#include <iostream>

/*
 * This is a tool to study the communication link capabilities using the
 * CommsDeviceService as StreamCommsDevice.
 * It uses the DataLinkFramePacket or SimplePacket (all with CRC16).
 * Each packet sent encodes the sequence number in 16 bits.
 * It creates one CommsDeviceService (for transmitting and receiving)
 */

using namespace dccomms;
using namespace std;
using namespace cpputils;
using namespace dccomms_packets;

using namespace std::chrono;

Ptr<Logger> Log;

class BtpTime {
public:
  typedef steady_clock clock;
  typedef time_point<clock, nanoseconds> netsim_time;
  static inline void Reset();
  static inline netsim_time GetTime();
  static inline uint64_t GetSeconds();
  static inline uint64_t GetMillis();
  static inline uint64_t GetMicros();
  static inline uint64_t GetNanos();

private:
  static netsim_time _startSimTime;

  template <typename T> static uint64_t GetTimeUnitsSinceEpoch() {
    auto ts = duration_cast<T>(clock::now() - _startSimTime);
    return ts.count();
  }
};

BtpTime::netsim_time BtpTime::_startSimTime;

void BtpTime::Reset() { _startSimTime = clock::now(); }

BtpTime::netsim_time BtpTime::GetTime() { return clock::now(); }
uint64_t BtpTime::GetSeconds() { return GetTimeUnitsSinceEpoch<seconds>(); }
uint64_t BtpTime::GetMillis() { return GetTimeUnitsSinceEpoch<milliseconds>(); }
uint64_t BtpTime::GetMicros() { return GetTimeUnitsSinceEpoch<microseconds>(); }
uint64_t BtpTime::GetNanos() { return GetTimeUnitsSinceEpoch<nanoseconds>(); }

enum BtpState {
  fast_decrease,
  stability_ipg,
  stability_max,
  look,
  increase_ipg,
  slow_decrease,
  req_rtt_ack,
  req_rtt,
  req_waitrtt,
  req_waitrtt_ack,
  req_peerrttinit,
  req_peerrttinit_ack,
  waitrtt,
  wait_waitrtt,
  init_rtt,
  vague,
  unknown,
  slaveinit,
  slaveinit_ack,
  wait_req_peerrttinit
};
enum BtpMode { master, slave };

typedef uint32_t btpField;
typedef uint16_t btpStateFieldType;
typedef uint16_t btpFlagsFieldType;

class BtpPacket : public VariableLengthPacket {
public:
  BtpPacket();
  void SetDst(uint8_t add);
  void SetSrc(uint8_t add);
  void SetEt(const btpField &t);
  void SetRt(const btpField &r);
  void SetSeq(const uint16_t &seq);
  void SetReqIpg(const btpField &ipg);

  void UpdateSeq();

  uint8_t GetDst();
  uint8_t GetSrc();
  btpField GetEt();
  btpField GetRt();
  uint16_t GetSeq();
  btpField GetReqIpg();
  uint8_t *GetBtpPayloadBuffer();

  void BtpPayloadUpdated(const uint32_t &size);

  void SetState(const BtpState &state);
  BtpState GetState();
  void SetInitFlag(bool v) {
    *_flags = v ? *_flags | INIT_F : *_flags & ~INIT_F;
  }
  bool GetInitFlag() { return *_flags & INIT_F; }

private:
  uint8_t *_add;
  uint16_t *_seq;
  btpField *_et, *_rt, *_ipgReq;
  uint8_t *_payload;
  btpStateFieldType *_state;
  uint32_t _overhead;
  btpFlagsFieldType *_flags;
  static const btpStateFieldType INIT_F = 0x8000;
};

BtpPacket::BtpPacket() {
  uint8_t *buffer = VariableLengthPacket::GetPayloadBuffer();
  _add = buffer;
  _seq = (uint16_t *)(_add + 1);
  _et = (btpField *)(_seq + 1);
  _rt = _et + 1;
  _ipgReq = _rt + 1;
  _state = (btpStateFieldType *)(_ipgReq + 1);
  _flags = (btpFlagsFieldType *)(_state + 1);
  _payload = (uint8_t *)(_state + 1);

  _overhead = 1 + 2 + sizeof(btpField) * 3 + sizeof(btpStateFieldType) +
              sizeof(btpFlagsFieldType);
  VariableLengthPacket::PayloadUpdated(_overhead);
}

void BtpPacket::SetState(const BtpState &state) { *_state = state; }
BtpState BtpPacket::GetState() { return static_cast<BtpState>(*_state); }

void BtpPacket::SetDst(uint8_t add) { *_add = (*_add & 0xf0) | (add & 0xf); }
uint8_t BtpPacket::GetDst() { return *_add & 0xf; }

void BtpPacket::SetSrc(uint8_t add) {
  *_add = (*_add & 0xf) | ((add & 0xf) << 4);
}
uint8_t BtpPacket::GetSrc() { return (*_add & 0xf0) >> 4; }

void BtpPacket::SetEt(const btpField &t) { *_et = t; }
void BtpPacket::SetRt(const btpField &r) { *_rt = r; }
void BtpPacket::SetSeq(const uint16_t &seq) { *_seq = seq; }
void BtpPacket::UpdateSeq() { *_seq = *_seq + 1; }
void BtpPacket::SetReqIpg(const btpField &ipg) { *_ipgReq = ipg; }

btpField BtpPacket::GetEt() { return *_et; }
btpField BtpPacket::GetRt() { return *_rt; }
uint16_t BtpPacket::GetSeq() { return *_seq; }
btpField BtpPacket::GetReqIpg() { return *_ipgReq; }
uint8_t *BtpPacket::GetBtpPayloadBuffer() { return _payload; }
void BtpPacket::BtpPayloadUpdated(const uint32_t &size) {
  PayloadUpdated(_overhead + size);
}

typedef std::shared_ptr<BtpPacket> BtpPacketPtr;

class Btp {
public:
  Btp();
  void Init();

  void SetState(const BtpState &state);
  void SetPeerIpg(const uint32_t &ipg);
  void SetIpg(const uint32_t &ipg);
  void SetMaxIat(const uint32_t &iat);
  void SetMinIat(const uint32_t &iat);
  void ProcessRxPacket(const BtpPacketPtr &pkt);
  void SetMode(const BtpMode &mode);
  void SetDst(const uint8_t &dst);
  void SetSrc(const uint8_t &src);
  void SetMinTr(const uint64_t &tr);
  void SetTrCount(const uint32_t &count);

  BtpState GetState();
  BtpMode GetMode();
  uint32_t GetIat();
  uint32_t GetMaxIat();
  uint32_t GetMinIat();
  uint32_t GetIpg();
  uint32_t GetReqIpg();
  uint8_t GetSrc();
  uint8_t GetDst();
  uint16_t GetEseq();
  uint64_t GetMinTr();
  uint64_t GetLastTr();
  uint64_t GetTr();
  void SetRt(const btpField &v) { _rt = v; }
  btpField GetRt() { return _rt; };

  void SetPeerInit(bool v) { _peerInit = v; }
  bool GetPeerInit() { return _peerInit; }

  BtpPacketPtr WaitForNextPacket();
  void UpdateWorkState(const BtpPacketPtr &pkt);

  template <class Rep, class Period>
  BtpPacketPtr WaitForNextPacket(const std::chrono::duration<Rep, Period> &t) {
    std::unique_lock<std::mutex> lock(_pktRcv_mutex);
    _pktRcv = false;
    auto res = _pktRcv_cond.wait_for(lock, t);
    BtpPacketPtr pkt;
    if (_pktRcv) {
      pkt = CreateObject<BtpPacket>();
      pkt->CopyFromRawBuffer(_lastRxPkt->GetBuffer());
    }
    return pkt;
  }

  bool RttInit;
  bool StartRttInit();

  void RunTx();
  void RunRx();

  void SetTxDevName(const string &name) { _txDevName = name; }
  void SetRxDevName(const string &name) { _rxDevName = name; }

  void FlushInput() {
    // Clean input fifo
    auto pkt = _rxPb->Create();
    while (_rxDev->GetRxFifoSize() > 0) {
      _rxDev >> pkt;
    }
  }

  BtpPacketPtr GetLastRxPacket() {
    std::unique_lock<std::mutex> lock(_pktRcv_mutex);
    BtpPacketPtr pkt;
    if (_pktRcv) {
      pkt = CreateObject<BtpPacket>();
      pkt->CopyFromRawBuffer(_lastRxPkt->GetBuffer());
    }
    return pkt;
  }

  void SetLastRxPacket(const BtpPacketPtr &pkt) {
    _pktRcv_mutex.lock();
    _lastRxPkt = pkt;
    _peerBtpState = pkt->GetState();
    _peerInit = pkt->GetInitFlag();
    _pktRcv = true;

    if (GetMode() == slave) {
      SetDst(pkt->GetSrc());
    }

    if (pkt->GetState() < req_rtt_ack && GetState() < req_rtt_ack)
      UpdateWorkState(pkt);

    _pktRcv_mutex.unlock();
    _pktRcv_cond.notify_all();
  }

  void SendBtpMsg(const BtpState &state) {
    _txPkt->SetEt(BtpTime::GetMillis());
    _txPkt->SetRt(GetRt());
    _txPkt->SetDst(GetDst());
    _txPkt->SetSrc(GetSrc());
    _txPkt->SetReqIpg(GetReqIpg());
    _txPkt->SetState(state);
    _txPkt->UpdateSeq();
    _txPkt->SetInitFlag(RttInit);
    _txPkt->UpdateFCS();
    _txDev << _txPkt;
  }

  void ReinitBtpVars() {
    _firstPktRecv = false;
    _lastPeerEt = 0;
    _peerIpg = 0;
    _lastTr = _minTr;
  }

private:
  void _SetInitialIpg();
  void _SetInitialReqIpg();
  void _InitTr();
  BtpState _btpState, _peerBtpState;
  int64_t _reqIpg, _ipg, _maxIat, _minIat, _iat, _trCount, _lastPeerEt,
      _peerIpg;
  btpField _rt;
  int64_t _lastTr, _newTr, _minTr;
  BtpMode _btpMode;
  uint8_t _src, _dst;
  uint16_t _eseq = 0;
  std::list<int64_t> _iats, _trs;
  uint64_t t0, t1;
  bool _pktRcv, _peerInit;
  std::mutex _pktRcv_mutex;
  std::condition_variable _pktRcv_cond;
  Ptr<VariableLengthPacketBuilder> _rxPb, _txPb;
  Ptr<CommsDeviceService> _rxDev, _txDev;
  string _txDevName, _rxDevName;

  PacketPtr _flushPkt;
  BtpPacketPtr _lastRxPkt, _txPkt;
  bool _firstPktRecv;
};

Btp::Btp() {}
void Btp::Init() {
  _SetInitialIpg();
  _SetInitialReqIpg();
  _InitTr();
  t0 = BtpTime::GetMillis();
  _lastPeerEt = 0;
  _peerIpg = 0;
  _pktRcv = false;
  _firstPktRecv = false;
  _btpState = req_rtt;
  RttInit = false;
  _peerInit = false;
  _btpState = vague;
  _peerBtpState = vague;

  _rxPb = CreateObject<VariableLengthPacketBuilder>();
  _txPb = CreateObject<VariableLengthPacketBuilder>();

  _flushPkt = _rxPb->Create();
  _txPkt = CreateObject<BtpPacket>();
  _lastRxPkt = CreateObject<BtpPacket>();
  _txPkt->SetSeq(0);

  _txDev = CreateObject<CommsDeviceService>(_txPb);
  _txDev->SetCommsDeviceId(_txDevName);
  _txDev->Start();

  if (_txDevName == _rxDevName) {
    _rxDev = _txDev;
  } else {
    _rxDev = CreateObject<CommsDeviceService>(_rxPb);
    _rxDev->SetCommsDeviceId(_rxDevName);
    _rxDev->Start();
  }

  FlushInput();
}

bool Btp::StartRttInit() {
  BtpPacketPtr pkt = CreateObject<BtpPacket>();
  uint16_t rttMillis = UINT16_MAX;

  BtpState peerState = unknown;
  while (rttMillis == UINT16_MAX) {
    while (peerState != req_waitrtt_ack) {
      SendBtpMsg(req_waitrtt);
      Log->Info("SEND req_waitrtt");
      auto res = WaitForNextPacket(std::chrono::milliseconds(5000));
      if (res) {
        peerState = res->GetState();
        if (peerState == req_peerrttinit) {
          SendBtpMsg(req_peerrttinit_ack);
          Log->Info("SEND req_peerrttinit_ack");
        } else if (peerState == req_waitrtt) {
          if (GetMode() == slave) {
            SendBtpMsg(req_waitrtt_ack);
            Log->Info("SEND req_waitrtt_ack");
            SetState(waitrtt);
            return false;
          }
        }
      } else {
        Log->Warn("TIMEOUT REQ WAITRTT");
      }
    }
    int rtts = 0, rttsReq = 4;
    rttMillis = 0;
    while (rtts < rttsReq) {
      FlushInput();
      auto t0 = BtpTime::GetMillis();
      SendBtpMsg(req_rtt);
      Log->Info("SEND req_rtt");
      auto res = WaitForNextPacket(std::chrono::milliseconds(5000));
      if (res) {
        peerState = res->GetState();
        if (peerState == req_rtt_ack) {
          auto t1 = BtpTime::GetMillis();
          uint16_t lastRtt = static_cast<uint16_t>(t1 - t0);
          rttMillis += lastRtt;
          rtts += 1;
          Log->Info("LRTT: {} ms  RTT: {} ms  COUNT: {}", lastRtt,
                    rttMillis / rtts, rtts);
        } else if (peerState == req_peerrttinit) {
          SendBtpMsg(req_peerrttinit_ack);
          Log->Info("SEND req_peerrttinit_ack");
          SetState(init_rtt);
          return false;
        } else if (peerState == req_waitrtt) {
          SendBtpMsg(req_waitrtt_ack);
          Log->Info("SEND req_waitrtt_ack");
          SetState(waitrtt);
          return false;
        }
      } else {

        Log->Warn("RTTREQ TIMEOUT");
      }
    }
    if (rtts == rttsReq) {
      rttMillis /= rtts;
      SetIpg(rttMillis * 2);
      SetPeerIpg(GetIpg());
      SetMinTr(static_cast<uint16_t>(rttMillis / 2));
      RttInit = true;
      ReinitBtpVars();
      if (GetMode() == master) {
        SetState(req_peerrttinit);
      } else {
        SetState(slaveinit);
      }
    } else {
      RttInit = false;
      rttMillis = UINT16_MAX;
    }
  }
  return RttInit;
}

void Btp::RunTx() {
  /*
   * Este  proceso  se  encarga  de  dirigir  el  envió  de  paquetes.  El
  reloj  cuenta  de  forma descendiente desde un valor establecido y cuando
  llega a cero, rellena la cabecera del paquete BTP  para  después  enviar
  el  paquete  a  su  destino.  Tras  enviar  el  paquete,  el  reloj vuelve
  a inicializarse  al  valor  propuesto  por  el  receptor  de  nuestros
  paquetes.  Este  valor  será  el  IPG propuesto en el campo “IPG Request”
  de la cabecera del protocolo BTP.
     */

  BtpPacketPtr pktrecv;

  while (1) {
    if (!RttInit) {
      Log->Info("RTT NOT INITILIZED");
      if (GetMode() == master) {
        Log->Info("MASTER: SET REQ WAITRTT");
        SetState(init_rtt);
      }
    }

    auto state = GetState();
    switch (state) {
    case init_rtt: {
      Log->Info("START RTT REQ RUTINE");
      RttInit = StartRttInit();
      break;
    }
    case waitrtt: {
      Log->Info("WAIT RTT REQ");
      pktrecv = WaitForNextPacket(chrono::seconds(5));
      if (pktrecv) {
        switch (pktrecv->GetState()) {

        case req_rtt: {
          SendBtpMsg(req_rtt_ack);
          Log->Info("SEND req_rtt_ack {}", GetDst());
          break;
        }
        case req_peerrttinit: {
          SendBtpMsg(req_peerrttinit_ack);
          SetState(init_rtt);
          Log->Info("SEND req_peerrttinit_ack {}", GetDst());
          break;
        }
        case req_waitrtt: {
          SendBtpMsg(req_waitrtt_ack);
          Log->Info("SEND req_waitrtt_ack {}", GetDst());
          break;
        }
        case slaveinit: {
          SetState(look);
        }
        }
      } else {
        if (GetMode() == master) {
          Log->Warn("TIMEOUT WAITING FOR req_rtt (MASTER)");
          SetState(req_peerrttinit);
          Log->Info("SEND req_peerrttinit");
        } else {
          Log->Warn("TIMEOUT WAITING FOR req_rtt.");
        }
      }
      break;
    }
    case slaveinit: {
      Log->Info("SLAVE INIT");
      SendBtpMsg(slaveinit);
      pktrecv = WaitForNextPacket(chrono::seconds(5));
      if (pktrecv) {
        if (pktrecv->GetState() == slaveinit_ack) {
          SetState(look);
        } else {
          Log->Warn("EXPECTED slaveinit_ack PKT");
        }
      } else {
        Log->Warn("TIMEOUT WAITING FOR slaveinit_ack");
      }
      break;
    }
    case req_peerrttinit: {
      SendBtpMsg(req_peerrttinit);
      Log->Info("SEND req_peerrttinit");
      pktrecv = WaitForNextPacket(chrono::seconds(5));
      if (pktrecv) {
        auto state = pktrecv->GetState();
        if (state == req_peerrttinit_ack) {
          SetState(waitrtt);
        } else if (state == req_peerrttinit) {
          if (GetMode() == master) {
            SendBtpMsg(req_peerrttinit_ack);
            Log->Info("SEND req_peerrttinit_ack");
            SetState(init_rtt);
          }
        } else if (state == slaveinit) {
          // Si recibimos slaveinit aquí quiere decir que RttInit es True,
          // que nosotros somos el master y que el esclavo ya ha calculado el
          // RTT. Lo que podemos hacer aquí es obligarle a que lo vuelva a
          // calcular con req_peerrttinit o reconocer su estado y empezar la
          // ejecucion normal de BTP. Vamos a hacer lo último.
          SendBtpMsg(slaveinit_ack);
          Log->Info("SEND slaveinit_ack");
          SetState(look);
        } else {
          Log->Warn("EXPECTED req_peerrttinit_ack OR req_peerrttinit{}",
                    GetMode() == master ? " OR slaveinit" : "");
        }
      } else {
        Log->Warn("TIMEOUT WAITING FOR req_peerrttinit_ack");
      }
      break;
    }
    case vague: {
      pktrecv = WaitForNextPacket(chrono::seconds(5));
      if (pktrecv && pktrecv->GetState() == req_waitrtt) {
        SetState(waitrtt);
      } else {
        SendBtpMsg(req_peerrttinit);
        Log->Info("SEND req_peerrttinit");
      }
      break;
    }
    default: {
      auto pkt = GetLastRxPacket();
      auto peerState = pkt->GetState();
      switch (peerState) {
      case req_waitrtt: {
        if (GetMode() == slave)
          RttInit = false;
        SetState(waitrtt);
        break;
      }
      case req_rtt: {
        if (GetMode() == slave)
          RttInit = false;
        SetState(waitrtt);
        break;
      }
      case req_peerrttinit: {
        RttInit = false;
        SetState(waitrtt);
        break;
      }
      case slaveinit: {
        SendBtpMsg(slaveinit_ack);
        Log->Info("SEND slaveinit_ack");
        break;
      }
      default:
        break;
      }

      switch (state) {
      case fast_decrease: {
        break;
      }
      case stability_ipg: {
        break;
      }
      case stability_max: {
        break;
      }
      case look: {
        SendBtpMsg(GetState());
        Log->Info("TX PKT SEQ {}  IPG {}  RIPG {}  MINTR {}", _txPkt->GetSeq(),
                  GetIpg(), _txPkt->GetReqIpg(), GetMinTr());
        std::this_thread::sleep_for(chrono::milliseconds(GetIpg()));
        break;
      }
      case increase_ipg: {
        break;
      }
      case slow_decrease: {
        break;
      }
      default: { continue; }
      }
    }
    }
  }
}

void Btp::RunRx() {
  /*
   * Este proceso se encarga de recibir los paquetes y
   * de actualizar el IPG, el IAT y la DST ADDR.
   */
  uint32_t npkts = 0;
  BtpPacketPtr pkt = CreateObject<BtpPacket>();
  while (1) {
    _rxDev >> pkt;
    if (pkt->PacketIsOk()) {
      npkts += 1;
      ProcessRxPacket(pkt);
    }
  }
}

BtpPacketPtr Btp::WaitForNextPacket() {
  std::unique_lock<std::mutex> lock(_pktRcv_mutex);
  _pktRcv = false;
  while (!_pktRcv)
    _pktRcv_cond.wait(lock);
  return dynamic_pointer_cast<BtpPacket>(
      _rxPb->CreateFromBuffer(_lastRxPkt->GetBuffer()));
}

uint16_t Btp::GetEseq() { return _eseq; }

void Btp::SetTrCount(const uint32_t &count) { _trCount = count; }

uint64_t Btp::GetLastTr() { return _lastTr; }

uint64_t Btp::GetTr() {}

void Btp::SetMinTr(const uint64_t &tr) { _minTr = tr; }

uint64_t Btp::GetMinTr() { return _minTr; }

void Btp::SetDst(const uint8_t &addr) { _dst = addr; }

void Btp::SetSrc(const uint8_t &addr) { _src = addr; }

uint8_t Btp::GetSrc() { return _src; }

uint8_t Btp::GetDst() { return _dst; }

void Btp::_InitTr() {
  _lastTr = _minTr;
  _newTr = _minTr;
}
void Btp::_SetInitialIpg() {
  if (GetMode() == master)
    _ipg = 2 * _maxIat;
}
void Btp::_SetInitialReqIpg() { _reqIpg = _ipg; }

void Btp::SetState(const BtpState &state) { _btpState = state; }
void Btp::SetPeerIpg(const uint32_t &ipg) { _reqIpg = ipg; }

void Btp::SetIpg(const uint32_t &ipg) { _ipg = ipg; }

void Btp::UpdateWorkState(const BtpPacketPtr &pkt) {
  t1 = BtpTime::GetMillis();
  int64_t iat = static_cast<int64_t>(t1 - t0);
  t0 = BtpTime::GetMillis();

  int64_t et = pkt->GetEt();
  _peerIpg = et - _lastPeerEt;
  _lastPeerEt = et;

  if (!_firstPktRecv) {
    _trs.clear();
    _iats.clear();
    _lastTr = _minTr;
    _trs.push_back(_lastTr);
    _firstPktRecv = true;
    Log->Info("FIRST RX - PKT {}  IPG {}  TR {}", pkt->GetSeq(),
              pkt->GetReqIpg(), _newTr);
    return;
  }

  _iats.push_back(iat);
  if (_iats.size() > _trCount)
    _iats.pop_front();

  _iat = 0;
  for (auto tiat : _iats) {
    _iat += tiat;
  }
  _iat = static_cast<int64_t>(
      std::round(static_cast<double>(_iat) / _iats.size()));

  auto seq = pkt->GetSeq();
  if (seq == _eseq) {
    _eseq += 1;
    // Update IPGPropouse
  } else if (seq > _eseq) {
    // packet lost
    // Update IPGPropouse
    _eseq = seq + 1;
  } else if (seq < _eseq) {
    _eseq = seq + 1;
    // Do nothing
  }

  _lastTr = 0;
  for (auto t : _trs) {
    _lastTr += t;
  }

  _lastTr = static_cast<int64_t>(
      std::round(static_cast<double>(_lastTr) / _trs.size()));

  int64_t over = static_cast<int64_t>(_iat) - static_cast<int64_t>(_peerIpg);
  int64_t newTr = over + _lastTr;
  _newTr = newTr;

  _trs.push_back(_newTr);
  if (_trs.size() > _trCount)
    _trs.pop_front();

  SetIpg(pkt->GetReqIpg());
  SetRt(pkt->GetEt());

  Log->Info("RX - PKT {}  LIAT {}  IAT {}  IPG {}  PEERIPG {}  TR {}",
            pkt->GetSeq(), iat, _iat, pkt->GetReqIpg(), _peerIpg, _newTr);
}

void Btp::ProcessRxPacket(const BtpPacketPtr &pkt) { SetLastRxPacket(pkt); }

void Btp::SetMaxIat(const uint32_t &iat) { _maxIat = iat; }
void Btp::SetMinIat(const uint32_t &iat) { _minIat = iat; }
void Btp::SetMode(const BtpMode &mode) { _btpMode = mode; }

BtpState Btp::GetState() { return _btpState; }
BtpMode Btp::GetMode() { return _btpMode; }
uint32_t Btp::GetIat() { return _iat; }
uint32_t Btp::GetMaxIat() { return _maxIat; }
uint32_t Btp::GetMinIat() { return _minIat; }
uint32_t Btp::GetIpg() { return _ipg; }
uint32_t Btp::GetReqIpg() { return _reqIpg; }

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", nodeName, btpModeStr = "slave";
  uint32_t add = 1, dstadd = 2, maxIat, minIat, minTr, trCount;
  uint64_t msStart = 0;
  bool flush = false, syncLog = false;
  try {
    cxxopts::Options options("dccomms_examples/btp", " - command line options");
    options.add_options()(
        "f,log-file", "File to save the log",
        cxxopts::value<std::string>(logFile)->default_value(""))(
        "F,flush-log", "flush log", cxxopts::value<bool>(flush))(
        "s,sync-log", "ssync-log", cxxopts::value<bool>(syncLog))(
        "l,log-level", "log level: critical,debug,err,info,off,trace,warn",
        cxxopts::value<std::string>(logLevelStr)->default_value(logLevelStr))(
        "help", "Print help");
    options.add_options("node_comms")(
        "add", "Device address (only used when packet type is DataLinkFrame)",
        cxxopts::value<uint32_t>(add))(
        "dstadd",
        "Destination device address (if the packet type is not DataLinkFrame "
        "the src and dst addr is set in the first payload byte. Not used in "
        "SimplePacket)",
        cxxopts::value<uint32_t>(dstadd))(
        "ms-start",
        "It will begin to transmit num-packets packets after ms-start millis "
        "(default: 0 ms)",
        cxxopts::value<uint64_t>(msStart))(
        "node-name", "dccomms id",
        cxxopts::value<std::string>(nodeName)->default_value("node0"))(
        "mode", "MTP mode: master/slave",
        cxxopts::value<std::string>(btpModeStr)->default_value("slave"))(
        "maxiat", "BTP max. IAT (ms)",
        cxxopts::value<uint32_t>(maxIat)->default_value("2000"))(
        "miniat", "BTP min. IAT (ms)",
        cxxopts::value<uint32_t>(minIat)->default_value("100"))(
        "mintr", "BTP minimum end to end delay from peer (ms)",
        cxxopts::value<uint32_t>(minTr)->default_value("2000"))(
        "trcount", "BTP tr and iat counter for mean (ms)",
        cxxopts::value<uint32_t>(trCount)->default_value("4"));

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help({"", "node_comms"}) << std::endl;
      exit(0);
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }
  BtpMode mode;
  if (btpModeStr == "master")
    mode = master;
  else if (btpModeStr == "slave")
    mode = slave;
  else {
    std::cerr << "wrong mode: '" << btpModeStr << "'" << std::endl;
    exit(1);
  }
  BtpTime::Reset();

  auto logFormatter =
      std::make_shared<spdlog::pattern_formatter>("%D %T.%F %v");
  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Log = CreateObject<Logger>();
  if (logFile != "") {
    Log->LogToFile(logFile);
  }
  Log->SetLogLevel(logLevel);
  Log->SetLogName(nodeName);
  Log->SetLogFormatter(logFormatter);

  if (flush) {
    Log->FlushLogOn(info);
    Log->Info("Flush log on info");
  }

  if (!syncLog) {
    Log->SetAsyncMode();
    Log->Info("Async. log");
  }

  Btp btp;
  btp.SetMode(mode);
  btp.SetMinTr(minTr);
  btp.SetMaxIat(maxIat);
  btp.SetMinIat(minIat);
  btp.SetTrCount(trCount);
  btp.SetDst(dstadd);
  btp.SetSrc(add);
  btp.SetTxDevName(nodeName);
  btp.SetRxDevName(nodeName);

  btp.Init();

  std::thread send, timeOut, iPGPropose, receive;

  send = std::thread([&]() { btp.RunTx(); });

  receive = std::thread([&]() { btp.RunRx(); });

  timeOut = std::thread([&]() {});

  iPGPropose = std::thread([&]() {});

  SignalManager::SetLastCallback(SIGINT, [&](int sig) {
    printf("Received %d signal.\nFlushing log messages...", sig);
    fflush(stdout);
    Log->FlushLog();
    Utils::Sleep(2000);
    printf("Log messages flushed.\n");
    exit(0);
  });

  send.join();
  timeOut.join();
  iPGPropose.join();
  return 0;
}
