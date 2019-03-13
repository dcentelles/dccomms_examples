#include <dccomms_examples/DcMac.h>

namespace dccomms_examples {

DcMacPacket::DcMacPacket() {
  uint8_t *buffer = VariableLengthPacket::GetPayloadBuffer();
  _add = buffer;
  _flags = _add + 1;
  _payload = (uint8_t *)(_flags + 1);
  _time = (DcMacTimeField *)_payload;
  _overhead = 2;
  VariableLengthPacket::PayloadUpdated(_overhead);
}

void DcMacPacket::SetDst(uint8_t add) { *_add = (*_add & 0xf0) | (add & 0xf); }
uint8_t DcMacPacket::GetDst() { return *_add & 0xf; }

void DcMacPacket::SetType(Type type) {
  uint8_t ntype = static_cast<uint8_t>(type);
  *_flags = (*_flags & 0xf3) | (ntype << 2);
  if (type == Type::sync) {
    MacPayloadUpdated(sizeof(DcMacTimeField));
  }
}

DcMacPacket::Type DcMacPacket::GetType() {
  uint8_t ntype = (*_flags & 0x0c) >> 2;
  Type value = static_cast<Type>(ntype);

  return value;
}

void DcMacPacket::SetSrc(uint8_t add) {
  *_add = (*_add & 0xf) | ((add & 0xf) << 4);
}
uint8_t DcMacPacket::GetSrc() { return (*_add & 0xf0) >> 4; }

uint32_t DcMacPacket::GetTime() { return 0; }

void DcMacPacket::SetTime(const uint32_t &tt) {}

uint8_t *DcMacPacket::GetMacPayloadBuffer() { return _payload; }
void DcMacPacket::MacPayloadUpdated(const uint32_t &size) {
  PayloadUpdated(_overhead + size);
}

DcMac::DcMac(PacketBuilderPtr rxpb, PacketBuilderPtr txpb) {
  _maxQueueSize = 1024;
  _started = false;

  // TODO: allow different packet builders for tx and rx
  _pb = CreateObject<VariableLengthPacketBuilder>();
}

void DcMac::SetAddr(const uint16_t &addr) { _addr = addr; }

uint16_t DcMac::GetAddr() { return _addr; }

void DcMac::SetMode(const Mode &mode) { _mode = mode; }

void DcMac::SetMaxNumerOfNodes(const uint16_t num) { _maxNodes = num; }

DcMac::Mode DcMac::GetMode() { return _mode; }

void DcMac::SetStream(CommsDeviceServicePtr stream) { _stream = stream; }

void DcMac::Start() {
  _time = RelativeTime::GetMillis();
  DiscardPacketsInRxFIFO();
  _started = true;
  _currentRtsSlot = 0;
}

void DcMac::SetMaxDataSlotDur(const uint32_t &slotdur) {
  _maxDataSlotDur = slotdur;
}

void DcMac::SetRtsSlotDur(const uint32_t &slotdur) { _syncSlotDur = slotdur; }
void DcMac::ReadPacket(const PacketPtr &pkt) {
  PacketPtr npkt = GetNextRxPacket();
  pkt->CopyFromRawBuffer(npkt->GetBuffer());
}

void DcMac::WritePacket(const PacketPtr &pkt) {
  PacketPtr npkt;
  npkt->CopyFromRawBuffer(pkt->GetBuffer());
  PushNewTxPacket(npkt);
}

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

void DcMac::RunRx() {
  /*
   * Este proceso se encarga de recibir los paquetes
   */
  uint32_t npkts = 0;
  DcMacPacketPtr pkt = CreateObject<DcMacPacket>();
  while (1) {
    _stream >> pkt;
    if (pkt->PacketIsOk()) {
      npkts += 1;
      ProcessRxPacket(pkt);
    }
  }
}

void DcMac::RunTx() {
  /*
   * Este proceso se encarga de enviar los paquetes
   */

  if (_mode == master) {
    _tx = std::thread([this]() {
      while (1) {
        if (_txfifo.size() > 0) {
        }
      }
    });
  } else {

    _tx = std::thread([this]() {
      while (1) {
        if (_txfifo.size() > 0) {
        }
      }
    });
  }
}

void DcMac::ProcessRxPacket(const DcMacPacketPtr &pkt) {}

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
