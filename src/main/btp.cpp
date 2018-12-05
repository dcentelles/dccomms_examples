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
  slow_decrease
};
enum BtpMode { master, slave };

class BtpPacket : public VariableLengthPacket {
public:
  typedef uint32_t btpField;
  BtpPacket();
  void SetDst(uint8_t add);
  void SetSrc(uint8_t add);
  void SetEt(const btpField &t);
  void SetRt(const btpField &r);
  void SetSeq(const uint16_t &seq);
  void SetIpgReq(const btpField &ipg);

  void UpdateSeq();

  uint8_t GetDst();
  uint8_t GetSrc();
  btpField GetEt();
  btpField GetRt();
  uint16_t GetSeq();
  btpField GetIpgReq();
  uint8_t *GetBtpPayloadBuffer();

  void BtpPayloadUpdated(const uint32_t &size);

private:
  uint8_t *_add;
  uint16_t *_seq;
  btpField *_et, *_rt, *_ipgReq;
  uint8_t *_payload;
  uint32_t _overhead;
};

BtpPacket::BtpPacket() {
  uint8_t *buffer = VariableLengthPacket::GetPayloadBuffer();
  _add = buffer;
  _seq = (uint16_t *)(_add + 1);
  _et = (btpField *)(_seq + 1);
  _rt = _et + 1;
  _ipgReq = _rt + 1;
  _payload = (uint8_t *)(_ipgReq + 1);

  _overhead = 1 + 2 + sizeof(btpField) * 3;
  VariableLengthPacket::PayloadUpdated(_overhead);
}

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
void BtpPacket::SetIpgReq(const btpField &ipg) { *_ipgReq = ipg; }

BtpPacket::btpField BtpPacket::GetEt() { return *_et; }
BtpPacket::btpField BtpPacket::GetRt() { return *_rt; }
uint16_t BtpPacket::GetSeq() { return *_seq; }
BtpPacket::btpField BtpPacket::GetIpgReq() { return *_ipgReq; }
uint8_t *BtpPacket::GetBtpPayloadBuffer() { return _payload; }
void BtpPacket::BtpPayloadUpdated(const uint32_t &size) {
  PayloadUpdated(_overhead + size);
}

class Btp {
public:
  Btp();
  void Init();

  void SetState(const BtpState &state);
  void SetPeerIpg(const uint32_t &ipg);
  void SetIpg(const uint32_t &ipg);
  void SetMaxIat(const uint32_t &iat);
  void SetMinIat(const uint32_t &iat);
  void SetIat(const uint32_t &iat);
  void SetMode(const BtpMode &mode);
  void SetDst(const uint8_t &dst);
  void SetSrc(const uint8_t &src);
  void SetMinTr(const uint32_t &tr);

  BtpState GetState();
  BtpMode GetMode();
  uint32_t GetIat();
  uint32_t GetMaxIat();
  uint32_t GetMinIat();
  uint32_t GetIpg();
  uint32_t GetPeerIpg();
  uint8_t GetSrc();
  uint8_t GetDst();
  uint16_t GetEseq();
  uint32_t GetMinTr();

  uint16_t UpdateEseq(const uint16_t &seq);

private:
  void _SetInitialIpg();
  void _SetInitialPeerIpg();
  BtpState _btpState;
  uint32_t _peerIpg, _ipg, _maxIat, _minIat, _iat, _minTr;
  BtpMode _btpMode;
  uint8_t _src, _dst;
  uint16_t _eseq = 0;
};

Btp::Btp() {}
void Btp::Init() {
  _SetInitialIpg();
  _SetInitialPeerIpg();
}

uint16_t Btp::GetEseq() { return _eseq; }
uint16_t Btp::UpdateEseq(const uint16_t &seq) {
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
  return _eseq;
}

void Btp::SetMinTr(const uint32_t &tr) { _minTr = tr; }

uint32_t Btp::GetMinTr() { return _minTr; }

void Btp::SetDst(const uint8_t &addr) { _dst = addr; }

void Btp::SetSrc(const uint8_t &addr) { _src = addr; }

uint8_t Btp::GetSrc() { return _src; }

uint8_t Btp::GetDst() { return _dst; }

void Btp::_SetInitialIpg() {
  if (GetMode() == master)
    _ipg = 2 * _maxIat;
}
void Btp::_SetInitialPeerIpg() { _peerIpg = _ipg; }

void Btp::SetState(const BtpState &state) { _btpState = state; }
void Btp::SetPeerIpg(const uint32_t &ipg) { _peerIpg = ipg; }

void Btp::SetIpg(const uint32_t &ipg) { _ipg = ipg; }
void Btp::SetIat(const uint32_t &iat) { _iat = iat; }
void Btp::SetMaxIat(const uint32_t &iat) { _maxIat = iat; }
void Btp::SetMinIat(const uint32_t &iat) { _minIat = iat; }
void Btp::SetMode(const BtpMode &mode) { _btpMode = mode; }

BtpState Btp::GetState() { return _btpState; }
BtpMode Btp::GetMode() { return _btpMode; }
uint32_t Btp::GetIat() { return _iat; }
uint32_t Btp::GetMaxIat() { return _maxIat; }
uint32_t Btp::GetMinIat() { return _minIat; }
uint32_t Btp::GetIpg() { return _ipg; }
uint32_t Btp::GetPeerIpg() { return _peerIpg; }

typedef std::shared_ptr<BtpPacket> BtpPacketPtr;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", nodeName, btpModeStr = "slave";
  uint32_t add = 1, dstadd = 2, maxIat, minIat;
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
        cxxopts::value<uint32_t>(maxIat)->default_value("100"));

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
  Ptr<Logger> log = CreateObject<Logger>();
  if (logFile != "") {
    log->LogToFile(logFile);
  }
  log->SetLogLevel(logLevel);
  log->SetLogName(nodeName);
  log->SetLogFormatter(logFormatter);

  if (flush) {
    log->FlushLogOn(info);
    log->Info("Flush log on info");
  }

  if (!syncLog) {
    log->SetAsyncMode();
    log->Info("Async. log");
  }

  std::thread send, timeOut, iPGPropose, receive;

  Btp btp;
  btp.SetMode(mode);
  btp.SetMaxIat(maxIat);
  btp.SetMinIat(minIat);
  btp.SetIpg(btp.GetMaxIat() * 2);
  btp.SetPeerIpg(btp.GetIpg());
  btp.SetDst(dstadd);
  btp.SetSrc(add);

  std::mutex btpLock;
  std::condition_variable pktRcvCond;
  bool pktReceived = false;

  Ptr<VariableLengthPacketBuilder> pb =
      CreateObject<VariableLengthPacketBuilder>();
  Ptr<CommsDeviceService> dev = CreateObject<CommsDeviceService>(pb);
  dev->SetCommsDeviceId(nodeName);
  dev->Start();

  send = std::thread([&]() {
    BtpPacketPtr pkt = CreateObject<BtpPacket>();
    pkt->SetSeq(0);
    while (1) {
      // TODO:
      /*
       * Este  proceso  se  encarga  de  dirigir  el  envió  de  paquetes.  El
      reloj  cuenta  de  forma descendiente desde un valor establecido y cuando
      llega a cero, rellena la cabecera del paquete BTP  para  después  enviar
      el  paquete  a  su  destino.  Tras  enviar  el  paquete,  el  reloj vuelve
      a inicializarse  al  valor  propuesto  por  el  receptor  de  nuestros
      paquetes.  Este  valor  será  el  IPG propuesto en el campo “IPG Request”
      de la cabecera del protocolo BTP.
       */
      pkt->SetIpgReq(btp.GetPeerIpg());
      pkt->SetEt(BtpTime::GetMillis());
      pkt->SetDst(btp.GetDst());
      pkt->SetSrc(btp.GetSrc());
      pkt->UpdateSeq();
      //...
      pkt->UpdateFCS();
      dev << pkt;
      log->Info("TX PKT {}  SEQ {}  RIPG {}", pkt->GetPacketSize(),
                pkt->GetSeq(), pkt->GetIpgReq());
      std::this_thread::sleep_for(chrono::milliseconds(btp.GetIpg()));
    }
  });

  receive = std::thread([&]() {
    /*
     * Este proceso se encara de recibir los paquetes y
     * de actualizar el IPG, el IAT y la DST ADDR.
     */
    uint32_t npkts = 0;
    uint32_t iat, liat;
    std::list<uint32_t> iats;
    BtpPacketPtr pkt = CreateObject<BtpPacket>();
    uint64_t t0, t1;
    t0 = BtpTime::GetMillis();
    while (1) {
      dev >> pkt;
      if (pkt->PacketIsOk()) {
        npkts += 1;
        t1 = BtpTime::GetMillis();
        liat = static_cast<uint32_t>(t1 - t0);
        iats.push_back(liat);
        if (iats.size() > 10)
          iats.pop_front();
        iat = 0;
        for (auto tiat : iats) {
          iat += tiat;
        }
        iat /= iats.size();
        btp.SetIat(iat);
        if (btp.GetMode() == slave) {
          btp.SetDst(pkt->GetSrc());
        }
        btp.SetIpg(pkt->GetIpgReq());
        btp.UpdateEseq(pkt->GetSeq());
        log->Info("RX - PKT {}  LIAT {}  IAT {}  RIPG {}", pkt->GetPacketSize(),
                  liat, iat, pkt->GetIpgReq());
        pktRcvCond.notify_all();
      }
    }
  });

  timeOut = std::thread([&]() {

  });

  iPGPropose = std::thread([&]() {

  });

  SignalManager::SetLastCallback(SIGINT, [&](int sig) {
    printf("Received %d signal.\nFlushing log messages...", sig);
    fflush(stdout);
    log->FlushLog();
    Utils::Sleep(2000);
    printf("Log messages flushed.\n");
    exit(0);
  });

  send.join();
  timeOut.join();
  iPGPropose.join();
  return 0;
}
