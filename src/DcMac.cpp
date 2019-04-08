#include <class_loader/multi_library_class_loader.hpp>
#include <dccomms_examples/DcMac.h>

namespace dccomms_examples {

DcMacPacket::DcMacPacket() {
  _prefixSize = ADD_SIZE + FLAGS_SIZE;
  _overheadSize = PRE_SIZE + _prefixSize + FCS_SIZE;
  _maxPacketSize =
      _overheadSize + TIME_SIZE + PAYLOAD_SIZE_FIELD + MAX_PAYLOAD_SIZE;
  _AllocBuffer(_maxPacketSize);
  _Init();
}

void DcMacPacket::_Init() {
  _pre = GetBuffer();
  *_pre = 0x55;
  _add = _pre + 1;
  _flags = _add + 1;
  _variableArea = _flags + 1;
  _ackMask = _variableArea;
  _time = (DcMacTimeField *)(_variableArea);
  _payloadSize = _variableArea;
  *_payloadSize = 0;
  _payload = _payloadSize + 1;
  _fcs = _payload + *_payloadSize;
}

int DcMacPacket::GetPayloadSizeFromPacketSize(int size) {
  return size - DcMacPacket::PRE_SIZE - DcMacPacket::ADD_SIZE -
         DcMacPacket::FLAGS_SIZE - DcMacPacket::PAYLOAD_SIZE_FIELD -
         DcMacPacket::FCS_SIZE;
}

int DcMacPacket::_GetTypeSize(Type ptype, uint8_t *buffer) {
  int size;
  if (ptype == Type::cts || ptype == Type::rts) {
    size = TIME_SIZE;
  } else if (ptype == data) {
    uint8_t payloadSize = *buffer;
    size = PAYLOAD_SIZE_FIELD + payloadSize;
  } else if (ptype == sync) { // sync
    size = SYNCFIELD_SIZE;
  }
  return size;
}

void DcMacPacket::SetAckMask(const uint8_t &mask) { *_ackMask = mask; }

uint8_t DcMacPacket::GetAckMask() { return *_ackMask; }

void DcMacPacket::DoCopyFromRawBuffer(void *buffer) {
  uint8_t *type = (uint8_t *)buffer + PRE_SIZE + ADD_SIZE;
  uint8_t *bufferPtr = (uint8_t *)buffer;
  Type ptype = _GetType(type);
  int size = PRE_SIZE + _prefixSize + _GetTypeSize(ptype, type + FLAGS_SIZE) +
             FCS_SIZE;
  memcpy(GetBuffer(), buffer, size);
}

inline uint8_t *DcMacPacket::GetPayloadBuffer() { return _payload; }

inline uint32_t DcMacPacket::GetPayloadSize() { return *_payloadSize; }

inline int DcMacPacket::GetPacketSize() {
  Type ptype = GetType();
  return _overheadSize + _GetTypeSize(ptype, _variableArea);
}

void DcMacPacket::Read(Stream *stream) {
  stream->WaitFor(_pre, PRE_SIZE);
  stream->Read(_add, ADD_SIZE);
  stream->Read(_flags, FLAGS_SIZE);
  Type type = _GetType(_flags);
  int size;
  uint8_t *end;
  if (type != unknown) {
    if (type == cts || type == rts) {
      stream->Read(_variableArea, TIME_SIZE);
      size = 0;
      end = _variableArea + TIME_SIZE;
    } else if (type == data) {
      stream->Read(_variableArea, PAYLOAD_SIZE_FIELD);
      size = GetPayloadSize();
      end = _variableArea + PAYLOAD_SIZE_FIELD;
    } else if (type == sync) { // sync (ack in)
      stream->Read(_variableArea, SYNCFIELD_SIZE);
      size = 0;
      end = _variableArea + SYNCFIELD_SIZE;
    }
  } else {
    size = 0;
    end = _variableArea;
  }

  if (type != unknown) {
    _fcs = end + size;
    stream->Read(end, size + FCS_SIZE);
  }
} // namespace dccomms_examples

void DcMacPacket::PayloadUpdated(uint32_t payloadSize) {
  SetType(data);
  *_payloadSize = payloadSize;
  _fcs = _payload + *_payloadSize;
  UpdateFCS();
}

uint32_t DcMacPacket::SetPayload(uint8_t *data, uint32_t size) {
  SetType(Type::data);
  auto copySize = MAX_PAYLOAD_SIZE < size ? MAX_PAYLOAD_SIZE : size;
  *_payloadSize = size;
  memcpy(_payload, data, copySize);
  _fcs = _payload + *_payloadSize;
  return copySize;
}

void DcMacPacket::UpdateFCS() {
  uint16_t crc;
  Type type = GetType();
  if (type != unknown) {
    if (type == cts || type == rts) {
      crc = Checksum::crc16(_add, _prefixSize + TIME_SIZE);
      _fcs = _variableArea + TIME_SIZE;
    } else if (type == data) {
      crc = Checksum::crc16(_add,
                            _prefixSize + PAYLOAD_SIZE_FIELD + *_payloadSize);
      _fcs = _variableArea + PAYLOAD_SIZE_FIELD + *_payloadSize;
    } else if (type == sync) { // sync
      crc = Checksum::crc16(_add, _prefixSize + SYNCFIELD_SIZE);
      _fcs = _variableArea + SYNCFIELD_SIZE;
    }
  } else {
    _fcs = _variableArea;
  }
  if (type != unknown) {
    *_fcs = (uint8_t)(crc >> 8);
    *(_fcs + 1) = (uint8_t)(crc & 0xff);
  }
}

bool DcMacPacket::_CheckFCS() {
  uint16_t crc;
  Type type = GetType();
  if (type != unknown) {
    if (type == cts || type == rts) {
      crc = Checksum::crc16(_add, _prefixSize + TIME_SIZE + FCS_SIZE);
    } else if (type == data) {
      crc = Checksum::crc16(_add, _prefixSize + PAYLOAD_SIZE_FIELD +
                                      *_payloadSize + FCS_SIZE);
    } else if (type == sync) { // sync
      crc = Checksum::crc16(_add, _prefixSize + SYNCFIELD_SIZE + FCS_SIZE);
    }
  } else {
    crc = 1;
  }
  return crc == 0;
}

bool DcMacPacket::PacketIsOk() { return _CheckFCS(); }

void DcMacPacket::SetDst(uint8_t add) {
  *_add = (*_add & 0xf0) | (add & 0xf);
  SetVirtualDestAddr(add);
}

uint8_t DcMacPacket::GetDst() { return *_add & 0xf; }

void DcMacPacket::_SetType(uint8_t *flags, Type type) {
  uint8_t ntype = static_cast<uint8_t>(type);
  *flags = (*flags & 0xf8) | (ntype & 0x7);
}

void DcMacPacket::SetType(Type type) { _SetType(_flags, type); }

DcMacPacket::Type DcMacPacket::_GetType(uint8_t *flags) {
  uint8_t ntype = (*flags & 0x7);
  Type value;
  if (ntype < 4)
    value = static_cast<Type>(ntype);
  else
    value = unknown;
  return value;
}

DcMacPacket::Type DcMacPacket::GetType() { return _GetType(_flags); }

void DcMacPacket::SetSrc(uint8_t add) {
  *_add = (*_add & 0xf) | ((add & 0xf) << 4);
  SetVirtualSrcAddr(add);
}

uint8_t DcMacPacket::GetSrc() { return (*_add & 0xf0) >> 4; }

DcMacTimeField DcMacPacket::GetTime() {
  Type type = GetType();
  if (type != data && type != unknown) {
    return *_time;
  } else {
    return 0;
  }
}

void DcMacPacket::SetTime(const DcMacTimeField &tt) { *_time = tt; }

CLASS_LOADER_REGISTER_CLASS(DcMacPacketBuilder, IPacketBuilder)

/***************************/
/*       END PACKET        */
/***************************/

DcMac::DcMac() {
  _maxQueueSize = 1024;
  _started = false;
  _pb = CreateObject<DcMacPacketBuilder>();
  _devIntrinsicDelay = 0;
  _devBitRate = 1e4;
  SetLogName("DcMac");
  LogToConsole(true);
  _txDataPacket = CreateObject<DcMacPacket>();
  _txDataPacket->SetType(DcMacPacket::data);
  _sendingDataPacket = false;
}

void DcMac::SetAddr(const uint16_t &addr) {
  _addr = addr;
  Log->debug("Addr: {}", _addr);
}

uint16_t DcMac::GetAddr() { return _addr; }

void DcMac::SetMode(const Mode &mode) {
  _mode = mode;
  Log->debug("Mode: {}", _mode);
}

void DcMac::SetNumberOfNodes(const uint16_t num) {
  _maxSlaves = num;
  _slaveRtsReqs.clear();
  for (int add = 0; add < _maxSlaves; add++) {
    SlaveRTS slaveRts;
    _slaveRtsReqs.push_back(slaveRts);
  }
  Log->debug("Max. nodes: {}", _maxSlaves);
}

void DcMac::SetCommsDeviceId(const string &id) { _dccommsId = id; }

void DcMac::SetPktBuilder(const PacketBuilderPtr &pb) { _highPb = pb; }

void DcMac::InitSlaveRtsReqs(bool reinitCtsCounter) {
  if (!reinitCtsCounter)
    for (SlaveRTS &data : _slaveRtsReqs) {
      data.req = false;
      data.reqmillis = 0;
    }
  else
    for (SlaveRTS &data : _slaveRtsReqs) {
      data.req = false;
      data.reqmillis = 0;
      data.ctsBytes = 0;
    }
}

DcMac::Mode DcMac::GetMode() {
  return _mode;
  Log->debug("Mode: {}", _mode);
}

void DcMac::SetDevBitRate(const uint32_t &bitrate) { _devBitRate = bitrate; }
void DcMac::SetDevIntrinsicDelay(const double &delay) {
  _devIntrinsicDelay = delay;
}

void DcMac::Start() {
  _time = RelativeTime::GetMillis();
  if (!_highPb)
    return;
  _stream = CreateObject<CommsDeviceService>(_pb);
  _stream->SetBlockingTransmission(false);
  _stream->SetCommsDeviceId(_dccommsId);
  _stream->Start();
  InitSlaveRtsReqs(true);
  DiscardPacketsInRxFIFO();
  if (_mode == master) {
    MasterRunRx();
    MasterRunTx();
  } else {
    SlaveRunRx();
    SlaveRunTx();
  }
  _started = true;
  _currentRtsSlot = 0;
}

void DcMac::SetMaxDataSlotDur(const uint32_t &slotdur) {
  _maxDataSlotDur = slotdur;
}

void DcMac::SetRtsSlotDur(const uint32_t &slotdur) { _rtsCtsSlotDur = slotdur; }
void DcMac::ReadPacket(const PacketPtr &pkt) {
  PacketPtr npkt = WaitForNextRxPacket();
  pkt->CopyFromRawBuffer(npkt->GetBuffer());
}

void DcMac::WritePacket(const PacketPtr &pkt) { PushNewTxPacket(pkt); }

void DcMac::DiscardPacketsInRxFIFO() {
  if (!_flushPkt)
    _flushPkt = _pb->Create();
  while (_stream->GetRxFifoSize() > 0) {
    _stream >> _flushPkt;
  }
}

PacketPtr DcMac::WaitForNextRxPacket() {
  std::unique_lock<std::mutex> lock(_rxfifo_mutex);
  while (_rxfifo.empty()) {
    _rxfifo_cond.wait(lock);
  }
  PacketPtr dlf = _rxfifo.front();
  auto size = dlf->GetPacketSize();
  _rxfifo.pop();
  _rxQueueSize -= size;
  return dlf;
}

PacketPtr DcMac::WaitForNextTxPacket() {
  std::unique_lock<std::mutex> lock(_txfifo_mutex);
  while (_txfifo.empty()) {
    _txfifo_cond.wait(lock);
  }
  PacketPtr dlf = _txfifo.front();
  auto size = dlf->GetPacketSize();
  _txfifo.pop();
  _txQueueSize -= size;
  return dlf;
}

PacketPtr DcMac::GetLastTxPacket() {
  std::unique_lock<std::mutex> lock(_txfifo_mutex);
  PacketPtr dlf;
  if (!_txfifo.empty())
    dlf = _txfifo.front();
  return dlf;
}

PacketPtr DcMac::PopLastTxPacket() {
  std::unique_lock<std::mutex> lock(_txfifo_mutex);
  PacketPtr dlf;
  if (!_txfifo.empty()) {
    dlf = _txfifo.front();
    auto size = dlf->GetPacketSize();
    _txQueueSize -= size;
    _txfifo.pop();
  }
  return dlf;
}

void DcMac::PushNewRxPacket(PacketPtr dlf) {
  _rxfifo_mutex.lock();
  auto size = dlf->GetPacketSize();
  if (size + _rxQueueSize <= _maxQueueSize) {
    _rxQueueSize += size;
    _rxfifo.push(dlf);
  } else {
    Log->warn("Rx queue full. Packet dropped");
  }
  _rxfifo_cond.notify_one();
  _rxfifo_mutex.unlock();
}

void DcMac::PushNewTxPacket(PacketPtr dlf) {
  _txfifo_mutex.lock();
  auto size = dlf->GetPacketSize();
  if (size + _txQueueSize <= _maxQueueSize) {
    _txQueueSize += size;
    _txfifo.push(dlf);
  } else {
    Log->warn("Tx queue full. Packet dropped");
  }
  _txfifo_cond.notify_one();
  _txfifo_mutex.unlock();
}

double DcMac::GetPktTransmissionMillis(const uint32_t &size) {
  return (size * 8. / _devBitRate) * 1000 + _devIntrinsicDelay;
}

void DcMac::SetPropSpeed(const double &propspeed) {
  _propSpeed = propspeed; // m/s
}

void DcMac::SetMaxDistance(const double &distance) {
  _maxDistance = distance; // m
}

void DcMac::UpdateSlotDurFromEstimation() {
  DcMacPacket pkt;
  pkt.SetType(DcMacPacket::rts);
  auto size = pkt.GetPacketSize();
  auto tt = GetPktTransmissionMillis(size);
  auto maxPropDelay = _maxDistance / _propSpeed * 1000; // ms
  auto error = 45;
  auto slotdur = (tt + maxPropDelay) + error;
  Log->debug(
      "RTS/CTS size: {} ; TT: {} ms ; MP: {} ms ; Err: +{} ms ; ESD: {} ms",
      size, tt, maxPropDelay, error, slotdur);
  _rtsCtsSlotDur = slotdur;
}

void DcMac::SlaveRunTx() {
  /*
   * Este proceso se encarga de enviar los paquetes
   */
  _tx = std::thread([this]() {
    auto rtsSlotDelay = milliseconds((_addr - 1) * _rtsCtsSlotDur);
    auto ctsSlotDelay = milliseconds(_maxSlaves * _rtsCtsSlotDur);
    while (1) {
      std::unique_lock<std::mutex> lock(_status_mutex);
      while (_status != syncreceived) {
        _status_cond.wait(lock);
      }
      _status = waitcts;
      if (_sendingDataPacket) {
        if (_ackMask & (1 << (_addr - 1))) {
          Log->debug("DATA SUCCESS");
          _sendingDataPacket = false;
        } else {
          Log->warn("DATA LOST");
        }
      }
      if (!_sendingDataPacket) {
        Log->debug("Check data in tx buffer");
        PacketPtr pkt = GetLastTxPacket();
        PopLastTxPacket();
        if (pkt) {
          Log->debug("Data in tx buffer");
          _sendingDataPacketSize = pkt->GetPacketSize();
          _txDataPacket->SetDestAddr(pkt->GetDestAddr());
          _txDataPacket->SetSrcAddr(_addr);
          _txDataPacket->SetPayload(pkt->GetBuffer(), _sendingDataPacketSize);
          _txDataPacket->UpdateFCS();
          _sendingDataPacket = true;
          Log->debug("Start iteration for sending packet");
        }
      }
      if (!_sendingDataPacket) {
        continue;
      }

      auto now = std::chrono::system_clock::now();
      Log->debug("TX: SYNC RX!");
      lock.unlock();
      DcMacPacketPtr pkt(new DcMacPacket());
      pkt->SetDst(0);
      pkt->SetSrc(_addr);
      pkt->SetType(DcMacPacket::rts);
      pkt->SetTime(_sendingDataPacketSize * 8. / _devBitRate * 1000 +
                   _devIntrinsicDelay);
      pkt->UpdateFCS();
      auto rtsWakeUp = now + rtsSlotDelay;
      auto ctsWakeUp = now + ctsSlotDelay;

      this_thread::sleep_until(rtsWakeUp);
      _stream << pkt;
      Log->debug("Send RTS {}", RelativeTime::GetMillis());
      this_thread::sleep_until(ctsWakeUp);
      while (_status == waitcts) {
        std::unique_lock<std::mutex> statusLock(_status_mutex);
        _status_cond.wait(statusLock);
        if (_status == ctsreceived) {
          if (_txDataPacket->PacketIsOk()) {
            _stream << _txDataPacket;
            Log->debug("SEND DATA {}", _sendingDataPacketSize);
          } else {
            Log->critical("data packet corrupt before transmitting");
          }
        }
      }
    }
  });

  _tx.detach();
}

void DcMac::SlaveRunRx() {
  /*
   * Este proceso se encarga de recibir los paquetes
   */
  _rx = std::thread([this]() {
    uint32_t npkts = 0;
    DiscardPacketsInRxFIFO();
    DcMacPacketPtr pkt = CreateObject<DcMacPacket>();
    while (1) {
      _stream >> pkt;
      if (pkt->PacketIsOk()) {
        npkts += 1;
        Log->debug("S: RX DCMAC PKT {}", pkt->GetPacketSize());
        SlaveProcessRxPacket(pkt);
      } else {
        Log->warn("Errors on packet");
      }
    }
  });
  _rx.detach();
}

void DcMac::MasterRunTx() {
  /*
   * Este proceso se encarga de enviar los paquetes
   */
  _tx = std::thread([this]() {
    PacketPtr pkt = 0;
    while (1) {
      Log->debug("iteration start");
      if (_rxfifo.size() > 0) {
        Log->warn("RX fifo not empty at the beginning of the iteration. "
                  "Discard packets...");
        DiscardPacketsInRxFIFO();
      }
      RelativeTime::Reset();
      _time = RelativeTime::GetMillis();
      _currentRtsSlot = 0;
      Log->debug("Send sync signal. Slot: {} ; time: {}", _currentRtsSlot,
                 _time);

      DcMacPacketPtr syncPkt(new DcMacPacket());
      syncPkt->SetDst(0xf);
      syncPkt->SetSrc(_addr);
      syncPkt->SetType(DcMacPacket::sync);
      syncPkt->SetAckMask(_ackMask);
      syncPkt->UpdateFCS();
      if (syncPkt->PacketIsOk()) {
        _stream << syncPkt;
      } else {
        Log->critical("Internal error. packet has errors");
      }

      auto pktSize = syncPkt->GetPacketSize();
      uint32_t minEnd2End =
          ((pktSize * 8) / _devBitRate) * 1000 + _devIntrinsicDelay;
      this_thread::sleep_for(milliseconds(minEnd2End));

      InitSlaveRtsReqs();
      for (int s = 0; s < _maxSlaves; s++) {
        bool slotEnd = false;
        _currentRtsSlot += 1;
        _status = waitrts;
        auto wakeuptime =
            std::chrono::system_clock::now() + milliseconds(_rtsCtsSlotDur);

        while (!slotEnd) {
          std::unique_lock<std::mutex> statusLock(_status_mutex);

          auto res = _status_cond.wait_until(statusLock, wakeuptime);
          if (res == std::cv_status::no_timeout && _status == rtsreceived) {
            if (_rtsSlave == _currentRtsSlot) {
              Log->debug("RTS received from slave {}", _currentRtsSlot);
              SlaveRTS *rts = &_slaveRtsReqs[_rtsSlave - 1];
              rts->req = true;
              rts->reqmillis = _givenDataTime;

            } else {
              Log->warn("RTS received from wrong slave");
            }
          } else if (res == std::cv_status::timeout) {
            _time = RelativeTime::GetMillis();
            if (_status != rtsreceived)
              Log->warn(
                  "Timeout waiting for rts packet from slave {}. Time: {}",
                  _currentRtsSlot, _time);
            slotEnd = true;
          }
        }
      }
      bool req = true;
      _ackMask = 0;
      while (req) {
        SlaveRTS *winnerSlave = 0;
        uint32_t ctsBytes = UINT32_MAX;
        uint16_t slaveAddr;
        req = false;
        for (int i = 0; i < _maxSlaves; i++) {
          SlaveRTS *data = &_slaveRtsReqs[i];
          if (data->req) {
            req = true;
            if (ctsBytes > data->ctsBytes) {
              ctsBytes = data->ctsBytes;
              winnerSlave = data;
              slaveAddr = i + 1;
            }
          }
        }
        if (winnerSlave) {
          _status = DcMac::waitdata; // Should be set in mutex context
          winnerSlave->req = false;
          DcMacPacketPtr ctsPkt(new DcMacPacket());
          ctsPkt->SetDst(slaveAddr);
          ctsPkt->SetSrc(_addr);
          ctsPkt->SetType(DcMacPacket::cts);
          ctsPkt->SetTime(winnerSlave->reqmillis);
          ctsPkt->UpdateFCS();
          winnerSlave->ctsBytes += winnerSlave->reqmillis;
          if (ctsPkt->PacketIsOk()) {
            Log->debug("Send CTS to {}", slaveAddr);
            _stream << ctsPkt;
            auto wakeuptime =
                std::chrono::system_clock::now() +
                milliseconds(static_cast<int>(
                    std::ceil(_rtsCtsSlotDur + winnerSlave->reqmillis * 1)));
            bool slotEnd = false;
            while (_status != datareceived && !slotEnd) {
              std::unique_lock<std::mutex> statusLock(_status_mutex);
              auto res = _status_cond.wait_until(statusLock, wakeuptime);
              if (res == std::cv_status::no_timeout &&
                  _status == datareceived) {
                Log->debug("Data received from slave {}", slaveAddr);
                _ackMask |= (1 << (slaveAddr - 1));
              } else if (res == std::cv_status::timeout) {
                _time = RelativeTime::GetMillis();
                if (_status != datareceived)
                  Log->warn(
                      "Timeout waiting for data packet from slave {}. Time: {}",
                      slaveAddr, _time);
                slotEnd = true;
              }
            }

          } else {
            Log->critical("Internal error. packet has errors");
          }
        }
      }
      this_thread::sleep_for(milliseconds(10));

      // Check if there are packets in txfifo
      std::unique_lock<std::mutex> txfifoLock(_txfifo_mutex);
      if (!_txfifo.empty()) {
        pkt = _txfifo.front();
        _txfifo.pop();
      }
    }
  });
  _tx.detach();
}

void DcMac::MasterRunRx() {
  /*
   * Este proceso se encarga de recibir los paquetes
   */
  _rx = std::thread([this]() {
    uint32_t npkts = 0;
    DiscardPacketsInRxFIFO();
    DcMacPacketPtr pkt = CreateObject<DcMacPacket>();
    while (1) {
      _stream >> pkt;
      if (pkt->PacketIsOk()) {
        npkts += 1;
        Log->debug("M: RX DCMAC PKT {}", pkt->GetPacketSize());
        MasterProcessRxPacket(pkt);
      } else {
        Log->warn("Errors on packet");
      }
    }
  });
  _rx.detach();
}

void DcMac::MasterProcessRxPacket(const DcMacPacketPtr &pkt) {
  std::unique_lock<std::mutex> lock(_status_mutex);
  DcMacPacket::Type type = pkt->GetType();
  auto dst = pkt->GetDst();

  switch (type) {
  case DcMacPacket::sync: {
    Log->warn("SYNC detected");
    break;
  }
  case DcMacPacket::cts: {
    Log->warn("CTS detected");
    break;
  }
  case DcMacPacket::rts: {
    _status = rtsreceived;
    _givenDataTime = pkt->GetTime();
    _rtsSlave = pkt->GetSrcAddr();
    Log->debug("RTS received {} - {}", RelativeTime::GetMillis(),
               _givenDataTime);
    break;
  }
  case DcMacPacket::data: {
    _status = datareceived;
    if (dst == _addr) {
      Log->debug("DATA received");
      auto npkt = _highPb->Create();
      uint8_t *payload = pkt->GetPayloadBuffer();
      npkt->CopyFromRawBuffer(payload);
      if (!npkt->PacketIsOk()) {
        Log->critical("Data packet corrupted");
      }
      PushNewRxPacket(npkt);
    } else {
      Log->debug("DATA detected");
    }
    break;
  }
  }
  _status_cond.notify_all();
}
void DcMac::SlaveProcessRxPacket(const DcMacPacketPtr &pkt) {
  std::unique_lock<std::mutex> lock(_status_mutex);
  DcMacPacket::Type type = pkt->GetType();
  auto dst = pkt->GetDst();

  switch (type) {
  case DcMacPacket::sync: {
    RelativeTime::Reset();
    _ackMask = pkt->GetAckMask();
    _status = DcMac::syncreceived;
    Log->debug("SYNC received");
    break;
  }
  case DcMacPacket::cts: {
    _givenDataTime = pkt->GetTime();
    Log->debug("CTS detected");
    if (dst == _addr) {
      Log->debug("CTS received");
      _status = DcMac::ctsreceived;
    } else {
      Log->debug("CTS detected");
      _status = DcMac::waitcts;
    }
    break;
  }
  case DcMacPacket::rts: {
    break;
  }
  case DcMacPacket::data: {
    if (dst == _addr) {
      Log->debug("DATA received");
      PushNewRxPacket(pkt);
    }
    break;
  }
  }
  _status_cond.notify_all();
}

int DcMac::Available() {
  // TODO: return the payload bytes in the rx fifo instead
  return GetRxFifoSize();
}
unsigned int DcMac::GetRxFifoSize() {
  unsigned int size;
  _rxfifo_mutex.lock();
  size = _rxQueueSize;
  _rxfifo_mutex.unlock();
  return size;
}

unsigned int DcMac::GetTxFifoSize() {
  unsigned int size;
  _txfifo_mutex.lock();
  size = _txQueueSize;
  _txfifo_mutex.unlock();
  return size;
}

bool DcMac::IsOpen() { return _started; }

int DcMac::Read(void *, uint32_t, unsigned long msTimeout) {
  throw CommsException("int CommsDeviceService::Read() Not implemented",
                       COMMS_EXCEPTION_NOTIMPLEMENTED);
}
int DcMac::Write(const void *, uint32_t, uint32_t msTimeout) {
  throw CommsException("int CommsDeviceService::Write() Not implemented",
                       COMMS_EXCEPTION_NOTIMPLEMENTED);
}

void DcMac::FlushInput() {
  throw CommsException("void CommsDeviceService::FlushInput() Not implemented",
                       COMMS_EXCEPTION_NOTIMPLEMENTED);
}
void DcMac::FlushOutput() {
  throw CommsException("void CommsDeviceService::FlushOutput() Not implemented",
                       COMMS_EXCEPTION_NOTIMPLEMENTED);
}
void DcMac::FlushIO() {
  throw CommsException("void CommsDeviceService::FlushIO() Not implemented",
                       COMMS_EXCEPTION_NOTIMPLEMENTED);
}
} // namespace dccomms_examples
