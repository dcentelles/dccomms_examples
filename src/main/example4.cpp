#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>
#include <cpputils/SignalManager.h>
#include <dccomms_packets/SimplePacket.h>

/*
 * This is a tool to study the communication link capabilities using the
 * CommsDeviceService as StreamCommsDevice.
 * It uses the DataLinkFramePacket or SimplePacket (all with CRC16).
 * Each packet sent encodes the sequence number in 16 bits.
 * It creates one CommsDeviceService (for transmitting and receiving)
 */

using namespace dccomms;
using namespace std;
using namespace cpputils;
using namespace dccomms_packets;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", nodeName;
  uint32_t dataRate = 200, packetSize = 20, nPackets = 0, add = 1, dstadd = 2, packetSizeOffset = 0;
  uint64_t msStart = 0;
  enum PktType { DLF = 0, SP };
  PktType pktType;
  uint32_t pktTypeInt = 1;
  bool flush = false, asyncLog = true;
  try {
    cxxopts::Options options("dccomms_examples/example4",
                             " - command line options");
    options.add_options()
        ("f,log-file", "File to save the log",cxxopts::value<std::string>(logFile)->default_value("")->implicit_value(
            "example4_log"))
        ("F,flush-log", "flush log", cxxopts::value<bool>(flush))
        ("a,async-log", "async-log", cxxopts::value<bool>(asyncLog))
        ("l,log-level", "log level: critical,debug,err,info,off,trace,warn",cxxopts::value<std::string>(logLevelStr)->default_value(logLevelStr))
        ("help", "Print help");
    options.add_options("node_comms")
        ("packet-type", "0: DataLinkFrame, 1: SimplePacket (default).", cxxopts::value<uint32_t>(pktTypeInt))
        ("add", "Device address (only used when packet type is DataLinkFrame)", cxxopts::value<uint32_t>(add))
        ("dstadd", "Destination device address (only used when packet type is DataLinkFrame)", cxxopts::value<uint32_t>(dstadd))
        ("num-packets", "number of packets to transmit (default 0)", cxxopts::value<uint32_t>(nPackets))
        ("ms-start", "It will begin to transmit num-packets packets after ms-start millis (default 0)", cxxopts::value<uint64_t>(msStart))
        ("packet-size", "packet size in bytes (overhead + payload) (default 20 Bytes)", cxxopts::value<uint32_t>(packetSize))
        ("data-rate", "application data rate in bps. A high value could saturate the output buffer (default 200 bps)", cxxopts::value<uint32_t>(dataRate))
        ("packet-size-offset", "packet size offset in bytes (default = 0)", cxxopts::value<uint32_t>(packetSizeOffset))
        ("node-name", "dccomms id", cxxopts::value<std::string>(nodeName)->default_value("node0"));

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help({"", "node_comms"}) << std::endl;
      exit(0);
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }
  pktType = static_cast<PktType>(pktTypeInt);
  PacketBuilderPtr pb;
  Ptr<Packet> txPacket;
  uint32_t payloadSize;
  switch(pktType){
    case DLF:{
        auto checksumType = DataLinkFrame::fcsType::crc16;
        pb = CreateObject<DataLinkFramePacketBuilder>(checksumType);
        txPacket = pb->Create();
        txPacket->PayloadUpdated(0);
        auto emptyPacketSize = txPacket->GetPacketSize();
        payloadSize = packetSize - emptyPacketSize;
        std::static_pointer_cast<DataLinkFrame>(txPacket)->SetSrcAddr(add);
        std::static_pointer_cast<DataLinkFrame>(txPacket)->SetDestAddr(dstadd);
        break;
    }
    case SP:{
        pb = CreateObject<SimplePacketBuilder>(0, FCS::CRC16);
        txPacket = pb->Create();
        auto emptyPacketSize = txPacket->GetPacketSize();
        payloadSize = packetSize - emptyPacketSize;
        pb = CreateObject<SimplePacketBuilder>(payloadSize, FCS::CRC16);
        txPacket = pb->Create();
    }
    default:
      std::cerr << "wrong packet type" << std::endl;
  }
  uint16_t *seqPtr = (uint16_t *)(txPacket->GetPayloadBuffer());
  uint8_t *asciiMsg = (uint8_t *)(seqPtr + 1);
  uint32_t msgSize = payloadSize - 2;
  uint8_t *maxPtr = asciiMsg + msgSize;
  char digit = '0';
  for (uint8_t *pptr = asciiMsg; pptr < maxPtr; pptr++) {
    *pptr = digit++;
  }
  uint32_t totalPacketSize = packetSize + packetSizeOffset;

  Ptr<CommsDeviceService> node = CreateObject<CommsDeviceService>(pb);
  node->SetCommsDeviceId(nodeName);

  auto logFormatter = std::make_shared<spdlog::pattern_formatter>("%D %T.%F %v");
  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Ptr<Logger> log = CreateObject<Logger>();
  if (logFile != "") {
    log->LogToFile(logFile);
  }
  log->SetLogLevel(logLevel);
  log->SetLogName(nodeName);
  log->SetLogFormatter(logFormatter);

  if (flush) {
    log->FlushLogOn(info);
    log->Info("Flush log on info");
  }

  if (asyncLog){
    log->SetAsyncMode();
    log->Info("Async. log");
  }
  node->SetLogLevel(info);
  node->Start();

  std::thread tx, rx;

  double bytesPerSecond = dataRate / 8.;
  double nanosPerByte = 1e9 / bytesPerSecond;
  log->Info("data rate (bps) = {} ; packet size = {} (+offset = {}) ; num. packets = {} ; "
            "bytes/second = {}\nnanos/byte = {}",
            dataRate, packetSize, totalPacketSize, nPackets, bytesPerSecond, nanosPerByte);


  tx = std::thread([totalPacketSize, node, seqPtr, txPacket, log, nPackets, packetSize, nanosPerByte, msStart, msgSize]() {
    std::this_thread::sleep_for(chrono::milliseconds(msStart));
    for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
      *seqPtr = npacket;
      txPacket->PayloadUpdated(msgSize + 2);
      auto pktSize = txPacket->GetPacketSize();
      auto nanos = (uint32_t)round(totalPacketSize * nanosPerByte);
      log->Info("TX SEQ {} SIZE {}", npacket, txPacket->GetPacketSize());
      node << txPacket;
      std::this_thread::sleep_for(chrono::nanoseconds(nanos));
    }
  });

  rx = std::thread([node, pb, log]() {
    PacketPtr dlf = pb->Create();
    while (true) {
      node >> dlf;
      if (dlf->PacketIsOk()) {
        uint16_t *seqPtr = (uint16_t *)dlf->GetPayloadBuffer();
        log->Info("RX SEQ {} SIZE {}", *seqPtr, dlf->GetPacketSize());
      } else
        log->Warn("ERR");
    }
  });

  SignalManager::SetLastCallback(SIGINT, [&](int sig)
  {
      printf("Received %d signal.\nFlushing log messages...", sig);
      fflush(stdout);
      log->FlushLog();
      Utils::Sleep(2000);
      printf("Log messages flushed.\n");
      exit(0);
  });

  tx.join();
  rx.join();
  return 0;
}
