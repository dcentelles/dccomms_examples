#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>

/*
 * This is an util to gather sta
 */

using namespace dccomms;
using namespace std;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr, txName, rxName;
  bool disableTx, disableRx;
  uint32_t dataRate = 200, packetSize = 20, nPackets = 50;
  try {
    cxxopts::Options options("dccomms_examples/example2",
                             " - command line options");

    options.add_options()(
        "num-packets", "number of packets to transmit",
        cxxopts::value<uint32_t>(nPackets)) //->default_value (200))
        ("data-rate", "application data rate in bps (a high value could "
                      "saturate the output buffer",
         cxxopts::value<uint32_t>(dataRate)) //->default_value (200))
        ("packet-size", "packet size in bytes",
         cxxopts::value<uint32_t>(packetSize)) //->default_value(20))
        ("disable-tx", "only rx node", cxxopts::value<bool>(disableTx))(
            "disable-rx", "only tx node", cxxopts::value<bool>(disableRx))(
            "f,log-file", "File to save the log",
            cxxopts::value<std::string>(logFile)
                ->default_value("")
                ->implicit_value("example2_log"))(
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

  std::thread tx, rx;

  Ptr<CommsDeviceService> txnode = CreateObject<CommsDeviceService>(pb);
  if (!disableTx) {
    txnode->SetLogLevel(info);
    txnode->SetCommsDeviceId(txName);
    txnode->Start();

    double bytesPerSecond = dataRate / 8.;
    double microsPerByte = 1000000 / bytesPerSecond;
    log->Info("data rate (bps) = {} ; packet size = {} ; num. packets = {} ; "
              "bytes/second = {}\nmicros/byte = {}",
              dataRate, packetSize, nPackets, bytesPerSecond, microsPerByte);
    tx = std::thread([txnode, pb, txName, txLog, nPackets, packetSize,
                      microsPerByte]() {
      auto txPacket = pb->Create();
      uint16_t *seqPtr = (uint16_t *)(txPacket->GetPayloadBuffer());
      uint8_t *asciiMsg = (uint8_t *)(seqPtr + 1);
      txPacket->PayloadUpdated(0);
      uint32_t payloadSize = packetSize - txPacket->GetPacketSize();
      uint32_t msgSize = payloadSize - 2;
      uint8_t *maxPtr = asciiMsg + msgSize;
      char digit = '0';
      for (uint8_t *pptr = asciiMsg; pptr < maxPtr; pptr++) {
        *pptr = digit++;
      }
      auto pktSize = txPacket->GetPacketSize();
      for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
        *seqPtr = npacket;
        txPacket->PayloadUpdated(msgSize + 2);
        auto micros = (uint32_t)round(pktSize * microsPerByte);
        txLog->Info("Transmitting packet (Seq. Num: {} ; Size: {} ; ETA: {})",
                    npacket, pktSize, micros);
        txnode << txPacket;
        std::this_thread::sleep_for(chrono::microseconds(micros));
      }
    });
  }

  Ptr<CommsDeviceService> rxnode = CreateObject<CommsDeviceService>(pb);
  if (!disableRx) {
    rxnode->SetLogLevel(info);
    rxnode->SetCommsDeviceId(rxName);
    rxnode->Start();
    rx = std::thread([rxnode, pb, rxLog]() {
      PacketPtr dlf = pb->Create();
      while (true) {
        rxnode >> dlf;
        if (dlf->PacketIsOk()) {
          uint16_t *seqPtr = (uint16_t *)dlf->GetPayloadBuffer();
          rxLog->Info("Packet received!: Seq. Num: {} ; Size: {}", *seqPtr,
                      dlf->GetPacketSize());
        } else
          rxLog->Warn("Packet received with errors!");
      }
    });
  }
  if(!disableTx)
    tx.join();
  if(!disableRx)
    rx.join();

  exit(0);
}