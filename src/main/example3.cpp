#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>

/*
 * This is a tool to study the communication link capabilities
 */

using namespace dccomms;
using namespace std;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr, txName, rxName;
  bool enableTx = false, enableRx = false;
  uint32_t dataRate = 200, packetSize = 20, nPackets = 50;
  try {
    cxxopts::Options options("dccomms_examples/example3",
                             " - command line options");
    options.add_options()
     ("f,log-file", "File to save the log", cxxopts::value<std::string>(logFile)->default_value("")->implicit_value("example2_log"))
     ("l,log-level", "log level: critical,debug,err,info,off,trace,warn", cxxopts::value<std::string>(logLevelStr)->default_value("info"))
     ("help", "Print help");
    options.add_options("Transmitter")
     ("enable-tx", "enable tx node", cxxopts::value<bool>(enableTx))
     ("num-packets", "number of packets to transmit",cxxopts::value<uint32_t>(nPackets))
     ("packet-size", "packet size in bytes",cxxopts::value<uint32_t>(packetSize))
     ("data-rate", "application data rate in bps (a high value could saturate the output buffer",cxxopts::value<uint32_t>(dataRate))
     ("tx-name", "dccomms id for the tx node", cxxopts::value<std::string>(txName)->default_value("txNode"));
    options.add_options("Receiver")
     ("enable-rx", "enable rx node", cxxopts::value<bool>(enableRx))
     ("rx-name", "dccomms id for the rx node", cxxopts::value<std::string>(rxName)->default_value("rxNode"));

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help({"", "Receiver", "Transmitter"}) << std::endl;
      exit(0);
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }

  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Ptr<Logger> log = CreateObject<Logger>();
  log->FlushLogOn(info);
  if (logFile != "") {
    log->LogToFile(logFile);
  }
  log->SetLogName("Main");
  log->SetLogLevel(logLevel);

  auto checksumType = DataLinkFrame::fcsType::crc16;
  Ptr<IPacketBuilder> pb =
      CreateObject<DataLinkFramePacketBuilder>(checksumType);

  std::thread tx, rx;

  if (enableTx) {
    Ptr<CommsDeviceService> txnode = CreateObject<CommsDeviceService>(pb);
    Ptr<Logger> txLog = CreateObject<Logger>();
    if (logFile != "") {
      txLog->LogToFile(logFile + "_" + txName);
    }
    txLog->FlushLogOn(info);
    txLog->SetLogName(txName);
    txLog->SetLogLevel(logLevel);
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

  if (enableRx) {
    Ptr<CommsDeviceService> rxnode = CreateObject<CommsDeviceService>(pb);
    Ptr<Logger> rxLog = CreateObject<Logger>();
    if (logFile != "") {
      rxLog->LogToFile(logFile + "_" + rxName);
    }
    rxLog->FlushLogOn(info);
    rxLog->SetLogName(rxName);
    rxLog->SetLogLevel(logLevel);
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
  if (enableTx)
    tx.join();
  if (enableRx)
    rx.join();

  exit(0);
}
