#include <cpplogging/cpplogging.h>
#include <dccomms/dccomms.h>
#include <iostream>

using namespace dccomms;
using namespace std;

int main(void) {
  Ptr<Logger> log = CreateObject<Logger>();
  log->SetLogName("Main");
  log->SetLogLevel(info);

  auto checksumType = DataLinkFrame::fcsType::crc16;
  Ptr<IPacketBuilder> pb =
      CreateObject<DataLinkFramePacketBuilder>(checksumType);

  Ptr<CommsDeviceService> node1 = CreateObject<CommsDeviceService>(pb);
  node1->SetLogLevel(info);
  node1->SetCommsDeviceId("node1");
  node1->Start();

  Ptr<CommsDeviceService> node0 = CreateObject<CommsDeviceService>(pb);
  node0->SetLogLevel(info);
  node0->SetCommsDeviceId("node0");
  node0->Start();

  std::thread tx([node0, pb]() {
    Ptr<Logger> log = CreateObject<Logger>();
    log->SetLogName("node0");
    log->SetLogLevel(info);
    string msg = "Hello! I'm node0!";
    auto txPacket = pb->Create();
    uint8_t *seqPtr = txPacket->GetPayloadBuffer();
    uint8_t *asciiMsg = seqPtr + 1;
    uint8_t seq = 0;
    while (true) {
      *seqPtr = seq;
      memcpy(asciiMsg, msg.c_str(), msg.size());
      txPacket->PayloadUpdated(msg.size() + 1);
      node0->WaitForDeviceReadyToTransmit();
      log->Info("Transmitting packet (Seq. Num: {})", seq);
      seq++;
      node0 << txPacket;
    }

  });

  std::thread rx([node1, pb]() {
    Ptr<Logger> log = CreateObject<Logger>();
    log->SetLogName("node1");
    log->SetLogLevel(info);
    PacketPtr dlf = pb->Create();
    char msg[100];
    while (true) {
      node1 >> dlf;
      if (dlf->PacketIsOk()) {
        uint8_t *seqPtr = dlf->GetPayloadBuffer();
        uint8_t *asciiMsg = seqPtr + 1;
        memcpy(msg, asciiMsg, dlf->GetPayloadSize() - 1);
        msg[dlf->GetPacketSize() - 1] = 0;
        log->Info("Packet received!: Seq. Num: {} ; Msg: '{}'", *seqPtr, msg);
      } else
        log->Warn("Packet received with errors!");
    }
  });

  while (1) {
    this_thread::sleep_for(chrono::milliseconds(5000));
    log->Debug("I'm alive");
  }
  exit(0);
}
