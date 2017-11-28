#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>

using namespace dccomms;
using namespace std;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr, txName, rxName;
  try {
    cxxopts::Options options("dccomms_examples/example2",
                             " - command line options");

    options.add_options()(
        "f,log-file", "File to save the log",
        cxxopts::value<std::string>(logFile)->default_value("")->implicit_value(
            "example2_log"))(
        "l,log-level", "log level: critical,debug,err,info,off,trace,warn",
        cxxopts::value<std::string>(logLevelStr)->default_value("info"))(
        "tx-name", "dccomms id for the tx node",
        cxxopts::value<std::string>(txName)->default_value("node0"))(
        "rx-name", "dccomms id for the rx node",
        cxxopts::value<std::string>(rxName)->default_value("node1"))(
        "help", "Print help");
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help({""}) << std::endl;
      exit(0);
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }

  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Ptr<Logger> log = CreateObject<Logger>();
  Ptr<Logger> txLog = CreateObject<Logger>();
  Ptr<Logger> rxLog = CreateObject<Logger>();

  if (logFile != "") {
    log->LogToFile(logFile);
    rxLog->LogToFile(logFile+"_"+rxName);
    txLog->LogToFile(logFile+"_"+rxName);
  }

  log->SetLogName("Main");
  txLog->SetLogName(txName);
  rxLog->SetLogName(rxName);
  rxLog->SetLogLevel(logLevel);
  txLog->SetLogLevel(logLevel);
  log->SetLogLevel(logLevel);

  auto checksumType = DataLinkFrame::fcsType::crc16;
  Ptr<IPacketBuilder> pb =
      CreateObject<DataLinkFramePacketBuilder>(checksumType);

  Ptr<CommsDeviceService> node1 = CreateObject<CommsDeviceService>(pb);
  node1->SetLogLevel(info);
  node1->SetCommsDeviceId(txName);
  node1->Start();

  Ptr<CommsDeviceService> node0 = CreateObject<CommsDeviceService>(pb);
  node0->SetLogLevel(info);
  node0->SetCommsDeviceId(rxName);
  node0->Start();

  std::thread tx([node0, pb, txLog]() {
    string msg = "Hello! I'm node0!";
    auto txPacket = pb->Create();
    uint8_t *seqPtr = txPacket->GetPayloadBuffer();
    uint8_t *asciiMsg = seqPtr + 1;
    uint8_t seq = 0;
    while (true) {
      *seqPtr = seq;
      memcpy(asciiMsg, msg.c_str(), msg.size());
      txPacket->PayloadUpdated(msg.size() + 1);
      txLog->Info("Transmitting packet (Seq. Num: {} ; Size: {})", seq,
                  txPacket->GetPacketSize());
      node0->WaitForDeviceReadyToTransmit();
      seq++;
      node0 << txPacket;
    }

  });

  std::thread rx([node1, pb, rxLog]() {
    PacketPtr dlf = pb->Create();
    char msg[100];
    while (true) {
      node1 >> dlf;
      if (dlf->PacketIsOk()) {
        uint8_t *seqPtr = dlf->GetPayloadBuffer();
        uint8_t *asciiMsg = seqPtr + 1;
        memcpy(msg, asciiMsg, dlf->GetPayloadSize() - 1);
        msg[dlf->GetPacketSize() - 1] = 0;
        rxLog->Info("Packet received!: Seq. Num: {} ; Msg: '{}' ; Size: {}",
                  *seqPtr, msg, dlf->GetPacketSize());
      } else
        rxLog->Warn("Packet received with errors!");
    }
  });

  while (1) {
    this_thread::sleep_for(chrono::milliseconds(5000));
    log->Debug("I'm alive");
  }
  exit(0);
}
