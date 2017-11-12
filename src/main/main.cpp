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
    Ptr<CommsDeviceService> rxcomms = CreateObject<CommsDeviceService>(pb);
    comms->SetLogLevel(info);
    comms->SetCommsDeviceId("node0");
    comms->Start();

    //    rxcomms->SetLogLevel(info);
    //    rxcomms->SetCommsDeviceId("node1");
    //    rxcomms->Start();
    //    Ptr<DataLinkFrame> rxdlf = CreateObject<DataLinkFrame>(checksumType);
    //    char rxmsg[2000];

    Ptr<DataLinkFrame> dlf = CreateObject<DataLinkFrame>(checksumType);

    //    string msg = "Hello! I'm node0!";
    char msg[5000];
    int size = 300;
    for (int i = 0; i < size; i++) {
      msg[i] = 'a' + i;
    }

    //    rxcomms->WaitForDeviceReadyToTransmit();
    //    std::this_thread::sleep_for(chrono::seconds(5));
    //    while(rxcomms->GetRxFifoSize()>0)
    //    {
    //        rxcomms >> rxdlf;
    //    }

    int cont = 0;
    while (true) {

      msg[0] = '0' + cont;
      cont = (cont + 1) % 10;
      memcpy(dlf->GetPayloadBuffer(), msg, size);
      dlf->PayloadUpdated(size);
      log->Info("Transmitting packet... {}", msg[0]);
      log->FlushLog();
      comms << dlf;
      std::this_thread::sleep_for(chrono::milliseconds(10000));

//      log->Info("Waiting for packet...");
//      log->FlushLog();
//      rxcomms >> rxdlf;
//      memcpy(rxmsg, rxdlf->GetPayloadBuffer(), rxdlf->GetPayloadSize());
//      rxmsg[100] = 0;
//      rxmsg[rxdlf->GetPacketSize()] = 0;
//      log->Info("Packet received!: {}", rxmsg);
//      log->FlushLog();
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
      char msg[5000];
      while (true) {
        comms >> dlf;
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
