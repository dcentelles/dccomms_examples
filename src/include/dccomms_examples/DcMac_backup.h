#ifndef DCCOMMS_EXAMPLES_DCMAC_H
#define DCCOMMS_EXAMPLES_DCMAC_H

#include <chrono>
#include <cpplogging/cpplogging.h>
#include <cpputils/RelativeTime.h>
#include <dccomms/dccomms.h>
#include <dccomms_packets/SimplePacket.h>
#include <dccomms_packets/VariableLengthPacket.h>
#include <iostream>

using namespace dccomms;
using namespace std;
using namespace dccomms_packets;
using namespace std::chrono;
using namespace cpputils;

namespace dccomms_examples {

typedef uint8_t DcMacInfoField;
typedef uint32_t DcMacTimeField;

class DcMacPacket : public VariableLengthPacket {
public:
  enum Type { sync = 0, rts, cts, data };
  DcMacPacket();
  void SetDst(uint8_t add);
  void SetSrc(uint8_t add);
  void SetType(Type type);
  Type GetType();
  void SetTime(const uint32_t &tt);
  uint32_t GetTime();

  uint8_t GetDst();
  uint8_t GetSrc();
  uint8_t *GetMacPayloadBuffer();

  void MacPayloadUpdated(const uint32_t &size);

private:
  DcMacInfoField *_add;
  uint8_t *_payload;
  uint32_t _overhead;
  DcMacInfoField *_flags;
  DcMacTimeField *_time;
};

typedef std::shared_ptr<DcMacPacket> DcMacPacketPtr;

class DcMac : public StreamCommsDevice {
public:
  enum Mode { master, slave };
  enum State { waitrts, waitcts, waitdata, waitack, idle };
  DcMac(PacketBuilderPtr rxpb, PacketBuilderPtr txpb = NULL);
  void SetAddr(const uint16_t &addr);
  uint16_t GetAddr();
  void SetMode(const Mode &mode);
  void SetMaxNumerOfNodes(const uint16_t num);
  Mode GetMode();
  void SetStream(CommsDeviceServicePtr stream);
  void Start();
  void SetRtsSlotDur(const uint32_t &slotdur);
  void SetMaxDataSlotDur(const uint32_t &slotdur);

  virtual void ReadPacket(const PacketPtr &pkt) override;
  virtual void WritePacket(const PacketPtr &pkt) override;

  // Implemented Stream methods:
  virtual int Available();
  virtual bool IsOpen();

  // TODO: implement the missing Stream methods:
  virtual int Read(void *, uint32_t, unsigned long msTimeout = 0);
  virtual int Write(const void *, uint32_t, uint32_t msTimeout = 0);
  virtual void FlushInput();
  virtual void FlushOutput();
  virtual void FlushIO();

private:
  void DiscardPacketsInRxFIFO();
  void RunRx();
  void RunTx();
  void ProcessRxPacket(const DcMacPacketPtr &pkt);
  PacketPtr GetNextRxPacket();
  PacketPtr GetNextTxPacket();
  void PushNewRxPacket(PacketPtr);
  void PushNewTxPacket(PacketPtr);
  unsigned int GetRxFifoSize();

  Mode _mode;
  CommsDeviceServicePtr _stream;
  Ptr<VariableLengthPacketBuilder> _pb;
  PacketPtr _flushPkt;
  uint16_t _addr, _maxNodes;
  uint32_t _time; // millis

  std::mutex _rxfifo_mutex, _txfifo_mutex;
  std::condition_variable _rxfifo_cond, _txfifo_cond;
  std::queue<PacketPtr> _rxfifo, _txfifo;
  uint32_t _rxQueueSize, _txQueueSize;
  uint32_t _maxQueueSize;
  bool _sync, _started;
  std::thread _tx, _rx;
  uint32_t _syncSlotDur;    // millis
  uint32_t _maxDataSlotDur; // millis
  uint32_t _currentRtsSlot;
};

} // namespace dccomms_examples

#endif // DCCOMMS_EXAMPLES_DCMAC_H
