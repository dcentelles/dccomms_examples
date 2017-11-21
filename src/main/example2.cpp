#include <cpplogging/cpplogging.h>
#include <dccomms/dccomms.h>
#include <iostream>

using namespace dccomms;
using namespace std;

int main(void) {
  Ptr<Logger> log = CreateObject<Logger>();
  log->SetLogName("Main");
  log->SetLogLevel(info);

  std::thread tx([]() {
    Ptr<Logger> log = CreateObject<Logger>();
    log->SetLogName("node0");
    log->SetLogLevel(info);
    auto checksumType = DataLinkFrame::fcsType::crc16;
    Ptr<IPacketBuilder> pb =
        CreateObject<DataLinkFramePacketBuilder>(checksumType);
    Ptr<CommsDeviceService> comms = CreateObject<CommsDeviceService>(pb);
    comms->SetLogLevel(info);
    comms->SetCommsDeviceId("node0");
    comms->Start();
    Ptr<DataLinkFrame> dlf = CreateObject<DataLinkFrame>(checksumType);
    string msg = "Hello! I'm node0!";
    uint8_t *seqPtr = dlf->GetPayloadBuffer();
    uint8_t *asciiMsg = seqPtr + 1;
    uint8_t seq = 0;
    while (true) {
      *seqPtr = seq;
      memcpy(asciiMsg, msg.c_str(), msg.size());
      dlf->PayloadUpdated(msg.size() + 1);
      comms->WaitForDeviceReadyToTransmit();
      log->Info("Transmitting packet (Seq. Num: {})", seq);
      seq++;
      comms << dlf;
    }

  });

  std::thread rx([]() {
    Ptr<Logger> log = CreateObject<Logger>();
    log->SetLogName("node1");
    log->SetLogLevel(info);
    auto checksumType = DataLinkFrame::fcsType::crc16;
    Ptr<IPacketBuilder> pb =
        CreateObject<DataLinkFramePacketBuilder>(checksumType);
    Ptr<CommsDeviceService> comms = CreateObject<CommsDeviceService>(pb);
    comms->SetLogLevel(info);
    comms->SetCommsDeviceId("node1");
    comms->Start();
    Ptr<DataLinkFrame> dlf = CreateObject<DataLinkFrame>(checksumType);
    char msg[100];
    while (true) {
      comms >> dlf;
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
