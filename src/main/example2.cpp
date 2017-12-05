#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>

/*
 * This example transmits the same ASCII message in a loop
 */

using namespace dccomms;
using namespace std;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr, txName, rxName;
  bool disableTx, disableRx;
  try {
    cxxopts::Options options("dccomms_examples/example2",
                             " - command line options");

    options.add_options()("disable-tx", "only rx node",
                          cxxopts::value<bool>(disableTx))(
        "disable-rx", "only tx node", cxxopts::value<bool>(disableRx))(
        "f,log-file", "File to save the log",
        cxxopts::value<std::string>(logFile)->default_value("")->implicit_value(
            "example2_log"))(
        "l,log-level", "log level: critical,debug,err,info,off,trace,warn",
        cxxopts::value<std::string>(logLevelStr)->default_value("info"))(
        "tx-name", "dccomms id for the tx node",
        cxxopts::value<std::string>(txName)->default_value("txNode"))(
        "rx-name", "dccomms id for the rx node",
        cxxopts::value<std::string>(rxName)->default_value("rxNode"))(
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
    rxLog->LogToFile(logFile + "_" + rxName);
    txLog->LogToFile(logFile + "_" + rxName);
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

  if (!disableTx) {
    Ptr<CommsDeviceService> txnode = CreateObject<CommsDeviceService>(pb);
    txnode->SetLogLevel(info);
    txnode->SetCommsDeviceId(txName);
    txnode->Start();

    std::thread tx([txnode, pb, txName, txLog]() {
      string msg = "Hello! I'm "+ txName + "!";
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
        txnode->WaitForDeviceReadyToTransmit();
        seq++;
        txnode << txPacket;
      }

    });
    tx.detach();
  }

  if (!disableRx) {
    Ptr<CommsDeviceService> rxnode = CreateObject<CommsDeviceService>(pb);
    rxnode->SetLogLevel(info);
    rxnode->SetCommsDeviceId(rxName);
    rxnode->Start();

    std::thread rx([rxnode, pb, rxLog]() {
      PacketPtr dlf = pb->Create();
      char msg[100];
      while (true) {
        rxnode >> dlf;
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
    rx.detach();
  }
  while (1) {
    this_thread::sleep_for(chrono::milliseconds(5000));
    log->Debug("I'm alive");
  }
  exit(0);
}
