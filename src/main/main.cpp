#include <cpplogging/cpplogging.h>
#include <dccomms/dccomms.h>
#include <iostream>

using namespace dccomms;
using namespace std;

int main(void) {
  Ptr<Logger> log = CreateObject<Logger>();
  log->SetLogName("Main");
  log->SetLogLevel(info);

  cout << "Hello World!" << endl;
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
    while (true) {
      memcpy(dlf->GetPayloadBuffer(), msg.c_str(), msg.size());
      dlf->PayloadUpdated(msg.size());
      log->Info("Transmitting packet...");
      comms << dlf;
      this_thread::sleep_for(chrono::milliseconds(1000));
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
      memcpy(msg, dlf->GetPayloadBuffer(), dlf->GetPayloadSize());
      msg[dlf->GetPacketSize()] = 0;
      log->Info("Packet received!: {}", msg);
    }
  });

  while (1) {
    this_thread::sleep_for(chrono::milliseconds(5000));
    log->Debug("I'm alive");
  }
  exit(0);
}
