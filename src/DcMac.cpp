#include <class_loader/multi_library_class_loader.hpp>
#include <dccomms_examples/DcMac.h>

namespace dccomms_examples {

DcMacPacket::DcMacPacket() {
  _prefixSize = ADD_SIZE + FLAGS_SIZE;
  _overheadSize = _prefixSize + FCS_SIZE;
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
  _time = (uint32_t *)(_variableArea);
  _payloadSize = _variableArea;
  *_payloadSize = 0;
  _payload = _payloadSize + 1;
  _fcs = _payload + *_payloadSize;
}

int DcMacPacket::_GetTypeSize(Type ptype, uint8_t *buffer) {
  int size;
  if (ptype == Type::sync || ptype == Type::cts || ptype == Type::rts) {
    size = TIME_SIZE;
  } else if (ptype == data) {
    uint8_t payloadSize = *buffer;
    size = PAYLOAD_SIZE_FIELD + payloadSize;
  } else {
    size = 0;
  }
  return size;
}
void DcMacPacket::DoCopyFromRawBuffer(void *buffer) {
  uint8_t *type = (uint8_t *)buffer + PRE_SIZE + ADD_SIZE;
  Type ptype = _GetType(type);
  int size = PRE_SIZE + _prefixSize + _GetTypeSize(ptype, _variableArea) + FCS_SIZE;
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
    if (type != data) {
      stream->Read(_variableArea, TIME_SIZE);
      size = 0;
      end = _variableArea + TIME_SIZE;
    } else {
      stream->Read(_variableArea, PAYLOAD_SIZE_FIELD);
      size = GetPayloadSize();
      end = _variableArea + PAYLOAD_SIZE_FIELD;
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
    if (type != data) {
      crc = Checksum::crc16(_add, _prefixSize + TIME_SIZE);
      _fcs = _variableArea + TIME_SIZE;
    } else {
      crc = Checksum::crc16(_add,
                            _prefixSize + PAYLOAD_SIZE_FIELD + *_payloadSize);
      _fcs = _variableArea + PAYLOAD_SIZE_FIELD + *_payloadSize;
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
    if (type != data) {
      crc = Checksum::crc16(_add, _prefixSize + TIME_SIZE + FCS_SIZE);
    } else {
      crc = Checksum::crc16(_add, _prefixSize + PAYLOAD_SIZE_FIELD +
                                      *_payloadSize + FCS_SIZE);
    }
  } else {
    crc = 1;
  }
  return crc == 0;
}

bool DcMacPacket::PacketIsOk() { return _CheckFCS(); }

void DcMacPacket::SetCurrentSlot(const uint8_t &slot) {
  *_flags = *_flags | slot << 2;
}

uint8_t DcMacPacket::GetCurrentSlot() {
  uint8_t slot = *_flags >> 2;
  return slot;
}

void DcMacPacket::SetDst(uint8_t add) {
  *_add = (*_add & 0xf0) | (add & 0xf);
  SetVirtualDestAddr(add);
}

uint8_t DcMacPacket::GetDst() { return *_add & 0xf; }

void DcMacPacket::_SetType(uint8_t *flags, Type type) {
  uint8_t ntype = static_cast<uint8_t>(type);
  *flags = (*flags & 0xfc) | (ntype & 0x3);
}

void DcMacPacket::SetType(Type type) { _SetType(_flags, type); }

DcMacPacket::Type DcMacPacket::_GetType(uint8_t *flags) {
  uint8_t ntype = (*flags & 0x3);
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

uint32_t DcMacPacket::GetTime() {
  Type type = GetType();
  if (type != data && type != unknown) {
    return *_time;
  } else {
    return 0;
  }
}

void DcMacPacket::SetTime(const uint32_t &tt) { *_time = tt; }

CLASS_LOADER_REGISTER_CLASS(DcMacPacketBuilder, IPacketBuilder)

/***************************/
/*       END PACKET        */
/***************************/

DcMac::DcMac() {
  _maxQueueSize = 1024;
  _started = false;
  _pb = CreateObject<DcMacPacketBuilder>();

  SetLogName("DcMac");
  LogToConsole(true);
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
  _maxNodes = num;
  Log->debug("Max. nodes: {}", _maxNodes);
}

DcMac::Mode DcMac::GetMode() {
  return _mode;
  Log->debug("Mode: {}", _mode);
}

void DcMac::SetStream(CommsDeviceServicePtr stream) { _stream = stream; }

void DcMac::Start() {
  _time = RelativeTime::GetMillis();
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

void DcMac::SetRtsSlotDur(const uint32_t &slotdur) { _rtsSlotDur = slotdur; }
void DcMac::ReadPacket(const PacketPtr &pkt) {
  PacketPtr npkt = GetNextRxPacket();
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

PacketPtr DcMac::GetNextRxPacket() {
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

PacketPtr DcMac::GetNextTxPacket() {
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

void DcMac::SlaveRunTx() {
  /*
   * Este proceso se encarga de enviar los paquetes
   */
  _tx = std::thread([this]() {
    while (1) {
      std::unique_lock<std::mutex> lock(_status_mutex);
      while (_status != syncreceived) {
        _status_cond.wait(lock);
      }
      uint32_t rtsSlotDelay = _addr * _rtsSlotDur;
      this_thread::sleep_for(milliseconds(rtsSlotDelay));
      DcMacPacketPtr pkt(new DcMacPacket());
      pkt->SetDst(0);
      pkt->SetSrc(_addr);
      pkt->SetType(DcMacPacket::rts);
      pkt->SetTime(100);
      pkt->UpdateFCS();
      if (pkt->PacketIsOk()) {
        _stream << pkt;
      } else {
        Log->critical("Internal error. packet has errors");
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
    DcMacPacketPtr pkt = CreateObject<DcMacPacket>();
    while (1) {
      _stream >> pkt;
      if (pkt->PacketIsOk()) {
        npkts += 1;
        Log->debug("S: RX DCMAC PKT");
        SlaveProcessRxPacket(pkt);
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
      syncPkt->SetTime(_time);
      syncPkt->SetCurrentSlot(0);
      syncPkt->UpdateFCS();
      if (syncPkt->PacketIsOk()) {
        _stream << syncPkt;
      } else {
        Log->critical("Internal error. packet has errors");
      }

      for (int s = 0; s < _maxNodes; s++) {
        std::unique_lock<std::mutex> rxfifoLock(_rxfifo_mutex);
        _rxfifo_cond.wait_for(rxfifoLock, milliseconds(_rtsSlotDur));
        _currentRtsSlot += 1;
        if (_rxfifo.size() > 0) {
          Log->debug("rts received from slave {}", _currentRtsSlot);
          _rxfifo.pop();
        } else {
          _time = RelativeTime::GetMillis();
          Log->warn("Timeout waiting for rts packet from slave {}. Time: {}",
                    _currentRtsSlot, _time);
        }
      }

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
    DcMacPacketPtr pkt = CreateObject<DcMacPacket>();
    while (1) {
      _stream >> pkt;
      if (pkt->PacketIsOk()) {
        npkts += 1;
        Log->debug("M: RX DCMAC PKT");
        MasterProcessRxPacket(pkt);
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
    Log->warn("SYNC received");
    break;
  }
  case DcMacPacket::cts: {
    _givenDataTime = pkt->GetTime();
    Log->warn("CTS received");
    break;
  }
  case DcMacPacket::rts: {
    Log->debug("RTS received");
    _status = rtsreceived;
    break;
  }
  case DcMacPacket::data: {
    if (dst == _addr) {
      Log->debug("DATA received");
      _status = datareceived;
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
    _status = DcMac::syncreceived;
    Log->debug("SYNC received");
    break;
  }
  case DcMacPacket::cts: {
    _givenDataTime = pkt->GetTime();
    Log->debug("CTS received");
    if (dst == _addr) {
      _status = DcMac::ctsreceived;
    } else {
      _status = DcMac::waitnextcycle;
    }
    break;
  }
  case DcMacPacket::rts: {
    break;
  }
  case DcMacPacket::data: {
    if (dst == _addr) {
      Log->debug("DATA received");
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
