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

  std::thread node0Worker([node0, checksumType]() {
    Ptr<Logger> log = CreateObject<Logger>();
    log->SetLogName("node0");
    log->SetLogLevel(info);
    Ptr<DataLinkFrame> dlf = CreateObject<DataLinkFrame>(checksumType);
    dlf->SetSrcDir(1);
    dlf->SetDesDir(2);
    char msg[5000];
    int size = 1000;
    for (int i = 0; i < size; i++) {
      msg[i] = 'a' + i;
    }
    int cont = 0;
    while (true) {
      msg[0] = '0' + cont;
      cont = (cont + 1) % 10;
      memcpy(dlf->GetPayloadBuffer(), msg, size);
      dlf->PayloadUpdated(size);
      log->Info("Transmitting packet... {}", msg[0]);
      log->FlushLog();
      node0 << dlf;
      std::this_thread::sleep_for(chrono::milliseconds(200));
    }

  });

  std::thread node1Worker([node1, checksumType]() {
    Ptr<Logger> log = CreateObject<Logger>();
    log->SetLogName("node1");
    log->SetLogLevel(info);

    Ptr<DataLinkFrame> dlf = CreateObject<DataLinkFrame>(checksumType);
    char msg[5000];
    while (true) {
      node1 >> dlf;
      memcpy(msg, dlf->GetPayloadBuffer(), dlf->GetPayloadSize());
      msg[100] = 0;
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
