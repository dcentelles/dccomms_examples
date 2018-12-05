#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>
#include <cpputils/SignalManager.h>
#include <dccomms_packets/SimplePacket.h>
#include <dccomms_packets/VariableLengthPacket.h>

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
  uint32_t dataRate = 200, txPacketSize = 20, rxPacketSize = 20, nPackets = 0, add = 1, dstadd = 2, packetSizeOffset = 0;
  uint64_t msStart = 0;
  enum PktType { DLF = 0, VL = 1, SP = 2};
  uint32_t txPktTypeInt = 1, rxPktTypeInt = 1;
  bool flush = false, syncLog = false, disableRx = false;
  try {
    cxxopts::Options options("dccomms_examples/example4",
                             " - command line options");
    options.add_options()
        ("f,log-file", "File to save the log",cxxopts::value<std::string>(logFile)->default_value(""))
        ("F,flush-log", "flush log", cxxopts::value<bool>(flush))
        ("s,sync-log", "ssync-log", cxxopts::value<bool>(syncLog))
        ("l,log-level", "log level: critical,debug,err,info,off,trace,warn",cxxopts::value<std::string>(logLevelStr)->default_value(logLevelStr))
        ("help", "Print help");
    options.add_options("node_comms")
        ("tx-packet-type", "0: DataLinkFrame, 1: VariableLengthPacket (default), 2: SimplePacket.", cxxopts::value<uint32_t>(txPktTypeInt))
        ("rx-packet-type", "0: DataLinkFrame, 1: VariableLengthPacket (default), 2: SimplePacket", cxxopts::value<uint32_t>(rxPktTypeInt))
        ("add", "Device address (only used when packet type is DataLinkFrame)", cxxopts::value<uint32_t>(add))
        ("dstadd", "Destination device address (if the packet type is not DataLinkFrame the src and dst addr is set in the first payload byte. Not used in SimplePacket)", cxxopts::value<uint32_t>(dstadd))
        ("num-packets", "number of packets to transmit (default: 0)", cxxopts::value<uint32_t>(nPackets))
        ("ms-start", "It will begin to transmit num-packets packets after ms-start millis (default: 0 ms)", cxxopts::value<uint64_t>(msStart))
        ("tx-packet-size", "transmitted packet size in bytes (overhead + payload) (default: 20 Bytes)", cxxopts::value<uint32_t>(txPacketSize))
        ("rx-packet-size", "received packet size in bytes (overhead + payload). Only needed if packet type is 1 (default: 20 Bytes)", cxxopts::value<uint32_t>(rxPacketSize))
        ("data-rate", "application data rate in bps. A high value could saturate the output buffer (default: 200 bps)", cxxopts::value<uint32_t>(dataRate))
        ("packet-size-offset", "packet size offset in bytes (default: 0)", cxxopts::value<uint32_t>(packetSizeOffset))
        ("disable-rx", "disable packets reception (default: false)", cxxopts::value<bool>(disableRx))
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
  PktType txPktType = static_cast<PktType>(txPktTypeInt);
  PktType rxPktType = static_cast<PktType>(rxPktTypeInt);
  PacketBuilderPtr rxpb, txpb;
  Ptr<Packet> txPacket;
  uint32_t payloadSize;
  switch(txPktType){
    case DLF:{
        auto checksumType = DataLinkFrame::fcsType::crc16;
        txpb = CreateObject<DataLinkFramePacketBuilder>(checksumType);
        txPacket = txpb->Create();
        txPacket->PayloadUpdated(0);
        auto emptyPacketSize = txPacket->GetPacketSize();
        payloadSize = txPacketSize - emptyPacketSize;
        std::static_pointer_cast<DataLinkFrame>(txPacket)->SetSrcAddr(add);
        std::static_pointer_cast<DataLinkFrame>(txPacket)->SetDestAddr(dstadd);
        break;
    }
    case VL:{
        txpb = CreateObject<VariableLengthPacketBuilder>();
        txPacket = txpb->Create();
        txPacket->PayloadUpdated(0);
        auto emptyPacketSize = txPacket->GetPacketSize();
        payloadSize = txPacketSize - emptyPacketSize;
        break;
    }
    case SP:{
        txpb = CreateObject<SimplePacketBuilder>(0, FCS::CRC16);
        txPacket = txpb->Create();
        auto emptyPacketSize = txPacket->GetPacketSize();
        payloadSize = txPacketSize - emptyPacketSize;
        txpb = CreateObject<SimplePacketBuilder>(payloadSize, FCS::CRC16);
        txPacket = txpb->Create();
        break;
    }
    default:
      std::cerr << "wrong tx packet type: "<< txPktType << std::endl;
      return 1;
  }
  switch(rxPktType){
    case DLF:{
        auto checksumType = DataLinkFrame::fcsType::crc16;
        rxpb = CreateObject<DataLinkFramePacketBuilder>(checksumType);
        auto rxPacket = rxpb->Create();
        rxPacket->PayloadUpdated(0);
        auto emptyPacketSize = rxPacket->GetPacketSize();
        payloadSize = rxPacketSize - emptyPacketSize;
        std::static_pointer_cast<DataLinkFrame>(rxPacket)->SetSrcAddr(add);
        std::static_pointer_cast<DataLinkFrame>(rxPacket)->SetDestAddr(dstadd);
        break;
    }
    case VL:{
        rxpb = CreateObject<VariableLengthPacketBuilder>();
        break;
    }
    case SP:{
        rxpb = CreateObject<SimplePacketBuilder>(0, FCS::CRC16);
        auto rxPacket = rxpb->Create();
        auto emptyPacketSize = rxPacket->GetPacketSize();
        payloadSize = rxPacketSize - emptyPacketSize;
        rxpb = CreateObject<SimplePacketBuilder>(payloadSize, FCS::CRC16);
        break;
    }
    default:
      std::cerr << "wrong rx packet type: "<< rxPktType << std::endl;
      return 1;
  }
  uint8_t *dstPtr = txPacket->GetPayloadBuffer();
  uint16_t *seqPtr = (uint16_t *)(dstPtr + 1);
  uint8_t *asciiMsg = (uint8_t*) (seqPtr + 1);
  uint32_t msgSize = payloadSize - 2;
  uint8_t *maxPtr = asciiMsg + msgSize;
  char digit = '0';
  for (uint8_t *pptr = asciiMsg; pptr < maxPtr; pptr++) {
    *pptr = digit++;
  }
  uint32_t totalPacketSize = txPacketSize + packetSizeOffset;

  Ptr<CommsDeviceService> node = CreateObject<CommsDeviceService>(rxpb);
  node->SetBlockingTransmission(false);
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

  if (!syncLog){
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
            dataRate, txPacketSize, totalPacketSize, nPackets, bytesPerSecond, nanosPerByte);


  tx = std::thread([totalPacketSize, node, seqPtr, dstPtr, dstadd, txPacket, log, nPackets, txPacketSize, nanosPerByte, msStart, msgSize]() {
    std::this_thread::sleep_for(chrono::milliseconds(msStart));
    for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
      *seqPtr = npacket;
      *dstPtr = dstadd;
      txPacket->PayloadUpdated(msgSize + 3);
      uint64_t nanos = round(totalPacketSize * nanosPerByte);
      log->Info("TX SEQ {} SIZE {}", npacket, txPacket->GetPacketSize());
      node << txPacket;
      std::this_thread::sleep_for(chrono::nanoseconds(nanos));
    }
  });

  if (!disableRx) {
    rx = std::thread([node, rxpb, log]() {
      PacketPtr dlf = rxpb->Create();
      while (true) {
        node >> dlf;
        if (dlf->PacketIsOk()) {
          uint16_t *seqPtr = (uint16_t *)(dlf->GetPayloadBuffer()+1);
          log->Info("RX SEQ {} SIZE {}", *seqPtr, dlf->GetPacketSize());
        } else
          log->Warn("ERR");
      }
    });
  }

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
  if (!disableRx)
    rx.join();
  return 0;
}
