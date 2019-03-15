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
typedef uint8_t DcMacPSizeField;
typedef uint16_t DcMacTimeField;

class DcMacPacket : public Packet {
public:
  enum Type { sync = 0, rts, cts, data, unknown };
  DcMacPacket();
  void SetDst(uint8_t add);
  void SetSrc(uint8_t add);
  void SetType(Type type);
  Type GetType();
  void SetTime(const DcMacTimeField &tt);
  DcMacTimeField GetTime();

  uint8_t GetDst();
  uint8_t GetSrc();
  uint8_t GetCurrentSlot();
  void SetCurrentSlot(const uint8_t &);

  void SetDestAddr(uint32_t addr) { SetDst(addr); }
  void SetSrcAddr(uint32_t addr) { SetSrc(addr); }
  uint32_t GetDestAddr() { return GetDst(); }
  uint32_t GetSrcAddr() { return GetSrc(); }

  void DoCopyFromRawBuffer(void *buffer);
  uint8_t *GetPayloadBuffer();
  uint32_t GetPayloadSize();
  int GetPacketSize();
  void Read(Stream *comms);
  void PayloadUpdated(uint32_t payloadSize);
  uint32_t SetPayload(uint8_t *data, uint32_t size);

  bool PacketIsOk();

  void UpdateFCS();
  static const int PRE_SIZE = 1, ADD_SIZE = 1, FLAGS_SIZE = 1, TIME_SIZE = 2,
                   PAYLOAD_SIZE_FIELD = 1, MAX_PAYLOAD_SIZE = UINT8_MAX,
                   FCS_SIZE = 2; // CRC16
private:
  int _maxPacketSize;
  int _overheadSize;
  uint8_t *_pre;
  DcMacInfoField *_add;
  DcMacInfoField *_flags;
  DcMacTimeField *_time;
  DcMacPSizeField *_payloadSize;
  uint8_t *_variableArea;
  uint8_t *_payload;
  uint8_t *_fcs;
  int _prefixSize;
  Type _GetType(uint8_t *flags);
  void _SetType(uint8_t *flags, Type type);
  void _Init();
  bool _CheckFCS();
  int _GetTypeSize(Type type, uint8_t *buffer);
};

typedef std::shared_ptr<DcMacPacket> DcMacPacketPtr;

class DcMacPacketBuilder : public IPacketBuilder {
public:
  dccomms::PacketPtr CreateFromBuffer(void *buffer) {
    auto pkt = dccomms::CreateObject<DcMacPacket>();
    pkt->CopyFromRawBuffer(buffer);
    return pkt;
  }
  dccomms::PacketPtr Create() { return dccomms::CreateObject<DcMacPacket>(); }
};

class DcMac : public StreamCommsDevice {
public:
  enum Mode { master, slave };
  enum Status { waitrts, waitcts, waitdata, waitack, idle, syncreceived, waitnextcycle, ctsreceived, rtsreceived, datareceived};
  DcMac();
  void SetAddr(const uint16_t &addr);
  uint16_t GetAddr();
  void SetMode(const Mode &mode);
  void SetNumberOfNodes(const uint16_t num);
  Mode GetMode();
  void SetStream(CommsDeviceServicePtr stream);
  void Start();
  void SetRtsSlotDur(const uint32_t &slotdur);
  void SetMaxDataSlotDur(const uint32_t &slotdur);
  void SetDevBitRate(const uint32_t & bitrate); //bps
  void SetDevIntrinsicDelay(const double & delay); //millis
  void SetPropSpeed(const double & propspeed); //m/s
  void SetMaxDistance(const double & distance); //m
  void UpdateSlotDurFromEstimation();
  double GetPktTransmissionMillis(const uint32_t & size);

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
  void SlaveRunRx();
  void MasterRunRx();
  void SlaveRunTx();
  void MasterRunTx();
  void MasterProcessRxPacket(const DcMacPacketPtr &pkt);
  void SlaveProcessRxPacket(const DcMacPacketPtr &pkt);
  PacketPtr GetNextRxPacket();
  PacketPtr GetNextTxPacket();
  void PushNewRxPacket(PacketPtr);
  void PushNewTxPacket(PacketPtr);
  unsigned int GetRxFifoSize();

  Mode _mode;
  Status _status;
  CommsDeviceServicePtr _stream;
  Ptr<DcMacPacketBuilder> _pb;
  PacketPtr _flushPkt;
  uint16_t _addr, _maxNodes;
  DcMacTimeField _time; // millis

  std::mutex _rxfifo_mutex, _txfifo_mutex;
  std::condition_variable _rxfifo_cond, _txfifo_cond;
  std::queue<PacketPtr> _rxfifo, _txfifo;
  uint32_t _rxQueueSize, _txQueueSize;
  uint32_t _maxQueueSize;
  bool _sync, _started;
  std::thread _tx, _rx;
  std::mutex _status_mutex;
  std::condition_variable _status_cond;
  uint32_t _rtsCtsSlotDur;     // millis
  uint32_t _maxDataSlotDur; // millis
  uint32_t _currentRtsSlot;
  uint32_t _givenDataTime;
  uint32_t _devBitRate; //bps
  double _devIntrinsicDelay; //millis
  double _propSpeed; //m/s
  double _maxDistance; //m
};

} // namespace dccomms_examples

#endif // DCCOMMS_EXAMPLES_DCMAC_H
