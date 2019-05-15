#include <cpplogging/cpplogging.h>
#include <cpputils/SignalManager.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <dccomms_examples/DcMac.h>
#include <dccomms_packets/SimplePacket.h>
#include <dccomms_packets/VariableLength2BPacket.h>
#include <dccomms_packets/VariableLengthPacket.h>
#include <iostream>

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
using namespace dccomms_examples;
/*
macLayer->SetDevBitRate(1800);
macLayer->SetDevIntrinsicDelay(85.282906);
macLayer->SetMaxDistance(15);
macLayer->SetPropSpeed(3e8);
*/
int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", nodeName;
  uint32_t dataRate = 200, txPacketSize = 20, rxPacketSize = 20, nPackets = 0,
           add = 1, dstadd = 2, packetSizeOffset = 0, dcmacMaxNodes = 4,
           devBitRate = 1800;
  double devIntrinsicDelay = 0, maxDistance = 15, propSpeed = 3e8;
  uint64_t msStart = 0;
  enum PktType { DLF = 0, VL = 1, SP = 2, VL2 = 3 };
  uint32_t txPktTypeInt = 3, rxPktTypeInt = 3;
  bool flush = false, syncLog = false, disableRx = false, dcmac = false,
       dcmacmaster = false;
  try {
    cxxopts::Options options("dccomms_examples/example4",
                             " - command line options");
    options.add_options()(
        "f,log-file", "File to save the log",
        cxxopts::value<std::string>(logFile)->default_value(""))(
        "F,flush-log", "flush log", cxxopts::value<bool>(flush))(
        "s,sync-log", "ssync-log", cxxopts::value<bool>(syncLog))(
        "l,log-level", "log level: critical,debug,err,info,off,trace,warn",
        cxxopts::value<std::string>(logLevelStr)->default_value(logLevelStr))(
        "help", "Print help");
    options.add_options("node_comms")(
        "devDelay",
        std::string("(DcMac) device intrinsic delay (default: ") +
            std::to_string(devIntrinsicDelay) + ")",
        cxxopts::value<double>(devIntrinsicDelay))(
        "maxRange",
        std::string("(DcMac) set max device max. range (default: ") +
            std::to_string(maxDistance) + ")",
        cxxopts::value<double>(maxDistance))(
        "propSpeed",
        std::string("(DcMac) set prop. speed (default: ") +
            std::to_string(propSpeed) + ")",
        cxxopts::value<double>(propSpeed))(
        "devBitRate",
        std::string("(DcMac) bit rate of the device (default: ") +
            std::to_string(devBitRate) + ")",
        cxxopts::value<uint32_t>(devBitRate))(
        "tx-packet-type",
        "0: DataLinkFrame, 1: VariableLengthPacket, 2: SimplePacket, 3: "
        "VariableLengthPacket2B (default)",
        cxxopts::value<uint32_t>(txPktTypeInt))(
        "rx-packet-type",
        "0: DataLinkFrame, 1: VariableLengthPacket, 2: SimplePacket, 3: "
        "VariableLengthPacket2B (default)",
        cxxopts::value<uint32_t>(rxPktTypeInt))("add", "Device address",
                                                cxxopts::value<uint32_t>(add))(
        "dstadd",
        "Destination device address (if the packet type is not DataLinkFrame "
        "the src and dst addr is set in the first payload byte. Not used in "
        "SimplePacket)",
        cxxopts::value<uint32_t>(dstadd))(
        "num-packets",
        std::string("number of packets to transmit (default: ") +
            std::to_string(nPackets) + ")",
        cxxopts::value<uint32_t>(nPackets))(
        "ms-start",
        std::string("It will begin to transmit num-packets packets after "
                    "ms-start millis (default: ") +
            std::to_string(msStart) + ")",
        cxxopts::value<uint64_t>(msStart))(
        "tx-packet-size",
        std::string("transmitted packet size in bytes (overhead + payload) "
                    "(default: ") +
            std::to_string(txPacketSize) + ")",
        cxxopts::value<uint32_t>(txPacketSize))(
        "rx-packet-size",
        std::string("received packet size in bytes (overhead + payload). Only "
                    "needed if "
                    "packet type is 1 (default: ") +
            std::to_string(rxPacketSize) + ")",
        cxxopts::value<uint32_t>(rxPacketSize))(
        "data-rate",
        std::string("application data rate in bps. A high value could saturate "
                    "the output "
                    "buffer (default: ") +
            std::to_string(dataRate) + ")",
        cxxopts::value<uint32_t>(dataRate))(
        "packet-size-offset",
        std::string("packet size offset in bytes (default: ") +
            std::to_string(packetSizeOffset) + ")",
        cxxopts::value<uint32_t>(packetSizeOffset))(
        "disable-rx",
        std::string("disable packets reception (default: ") +
            (disableRx ? "true" : "false") + ")",
        cxxopts::value<bool>(disableRx))("dcmac", "(DcMac) adds a DcMAC layer",
                                         cxxopts::value<bool>(dcmac))(
        "master",
        std::string("(DcMac) set DcMac mode to master (default: ") +
            (dcmacmaster ? "master" : "slave") + ")",
        cxxopts::value<bool>(dcmacmaster))(
        "maxnodes",
        std::string("(DcMac) set DcMac max. num of nodes (default: ") +
            std::to_string(dcmacMaxNodes) + ")",
        cxxopts::value<uint32_t>(dcmacMaxNodes))(
        "node-name", "dccomms id",
        cxxopts::value<std::string>(nodeName)->default_value("node0"));

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
  switch (txPktType) {
  case DLF: {
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
  case VL: {
    txpb = CreateObject<VariableLengthPacketBuilder>();
    txPacket = txpb->Create();
    txPacket->PayloadUpdated(0);
    auto emptyPacketSize = txPacket->GetPacketSize();
    payloadSize = txPacketSize - emptyPacketSize;
    break;
  }
  case VL2: {
    txpb = CreateObject<VariableLength2BPacketBuilder>();
    txPacket = txpb->Create();
    txPacket->PayloadUpdated(0);
    auto emptyPacketSize = txPacket->GetPacketSize();
    payloadSize = txPacketSize - emptyPacketSize;
    break;
  }
  case SP: {
    txpb = CreateObject<SimplePacketBuilder>(0, FCS::CRC16);
    txPacket = txpb->Create();
    auto emptyPacketSize = txPacket->GetPacketSize();
    payloadSize = txPacketSize - emptyPacketSize;
    txpb = CreateObject<SimplePacketBuilder>(payloadSize, FCS::CRC16);
    txPacket = txpb->Create();
    break;
  }
  default:
    std::cerr << "wrong tx packet type: " << txPktType << std::endl;
    return 1;
  }
  switch (rxPktType) {
  case DLF: {
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
  case VL: {
    rxpb = CreateObject<VariableLengthPacketBuilder>();
    break;
  }
  case VL2: {
    rxpb = CreateObject<VariableLength2BPacketBuilder>();
    break;
  }
  case SP: {
    rxpb = CreateObject<SimplePacketBuilder>(0, FCS::CRC16);
    auto rxPacket = rxpb->Create();
    auto emptyPacketSize = rxPacket->GetPacketSize();
    payloadSize = rxPacketSize - emptyPacketSize;
    rxpb = CreateObject<SimplePacketBuilder>(payloadSize, FCS::CRC16);
    break;
  }
  default:
    std::cerr << "wrong rx packet type: " << rxPktType << std::endl;
    return 1;
  }

  uint8_t *dstPtr = txPacket->GetPayloadBuffer();
  uint16_t *seqPtr = (uint16_t *)(dstPtr + 1);
  uint8_t *asciiMsg = (uint8_t *)(seqPtr + 1);
  uint32_t msgSize = payloadSize - 2;
  uint8_t *maxPtr = asciiMsg + msgSize;
  char digit = '0';
  for (uint8_t *pptr = asciiMsg; pptr < maxPtr; pptr++) {
    *pptr = digit++;
  }
  uint32_t totalPacketSize = txPacketSize + packetSizeOffset;
  Ptr<StreamCommsDevice> node;
  Ptr<CommsDeviceService> service;
  Ptr<DcMac> macLayer;

  if (dcmac) {
    macLayer = CreateObject<DcMac>();

    // uint32_t syncSlotDur = std::ceil(5 / (1500 / 8.) * 1000);
    // uint32_t maxDataSlotDur = std::ceil(300 / (1500 / 8.) * 1000);
    uint32_t syncSlotDur = 200;
    uint32_t maxDataSlotDur = 1000;

    macLayer->SetCommsDeviceId(nodeName);
    macLayer->SetPktBuilder(rxpb);
    macLayer->SetAddr(add);
    macLayer->SetDevBitRate(devBitRate);
    macLayer->SetDevIntrinsicDelay(devIntrinsicDelay);
    macLayer->SetMaxDistance(maxDistance);
    macLayer->SetPropSpeed(propSpeed);
    macLayer->SetNumberOfNodes(dcmacMaxNodes);

    macLayer->SetMaxDataSlotDur(maxDataSlotDur);
    macLayer->SetRtsSlotDur(syncSlotDur);
    macLayer->UpdateSlotDurFromEstimation();
    DcMac::Mode mode;
    if (dcmacmaster) {
      mode = DcMac::Mode::master;
    } else {
      mode = DcMac::Mode::slave;
    }
    macLayer->SetMode(mode);
    node = macLayer;
  } else {
    service = CreateObject<CommsDeviceService>(rxpb);
    service->SetBlockingTransmission(false);
    service->SetCommsDeviceId(nodeName);
    node = service;
  }

  auto logFormatter =
      std::make_shared<spdlog::pattern_formatter>("%D %T.%F %v");
  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Ptr<Logger> log = CreateObject<Logger>();
  if (logFile != "") {
    log->LogToFile(logFile);
    node->LogToFile(logFile + ".node.log");
  }
  log->SetLogLevel(logLevel);
  log->SetLogName(nodeName);
  log->SetLogFormatter(logFormatter);

  if (flush) {
    log->FlushLogOn(info);
    log->Info("Flush log on info");
  }

  if (!syncLog) {
    log->SetAsyncMode();
    log->Info("Async. log");
  }

  node->SetLogLevel(logLevel);
  if (dcmac) {
    macLayer->Start();
  } else {
    service->Start();
  }

  std::thread tx, rx;

  double bytesPerSecond = dataRate / 8.;
  double nanosPerByte = 1e9 / bytesPerSecond;
  log->Info("data rate (bps) = {} ; packet size = {} (+offset = {}) ; num. "
            "packets = {} ; "
            "bytes/second = {}\nnanos/byte = {}",
            dataRate, txPacketSize, totalPacketSize, nPackets, bytesPerSecond,
            nanosPerByte);

  tx = std::thread([totalPacketSize, node, seqPtr, dstPtr, dstadd, txPacket,
                    log, nPackets, txPacketSize, nanosPerByte, msStart, msgSize,
                    payloadSize]() {
    std::this_thread::sleep_for(chrono::milliseconds(msStart));
    for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
      uint64_t nanos = round(totalPacketSize * nanosPerByte);
      txPacket->SetSeq(npacket);

      txPacket->SetDestAddr(1); // FOLLOWER
      txPacket->PayloadUpdated(payloadSize);
      log->Info("TX TO {} SEQ {} SIZE {}", txPacket->GetDestAddr(), npacket,
                txPacket->GetPacketSize());

      node << txPacket;

      txPacket->SetDestAddr(0); // MASTER
      txPacket->PayloadUpdated(payloadSize);
      log->Info("TX TO {} SEQ {} SIZE {}", txPacket->GetDestAddr(), npacket,
                txPacket->GetPacketSize());

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
          uint32_t seq = dlf->GetSeq();
          log->Info("RX FROM {} SEQ {} SIZE {}", dlf->GetSrcAddr(), seq,
                    dlf->GetPacketSize());
        } else
          log->Warn("ERR");
      }
    });
  }

  SignalManager::SetLastCallback(SIGINT, [&](int sig) {
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