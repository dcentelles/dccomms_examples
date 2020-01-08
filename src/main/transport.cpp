#include <cpplogging/cpplogging.h>
#include <cpputils/SignalManager.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <dccomms_packets/SimplePacket.h>
#include <dccomms_packets/VariableLength2BPacket.h>
#include <dccomms_packets/VariableLengthPacket.h>
#include <iostream>
#include <umci/DcMac.h>

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
using namespace umci;
/*
macLayer->SetDevBitRate(1800);
macLayer->SetDevIntrinsicDelay(85.282906);
macLayer->SetMaxDistance(15);
macLayer->SetPropSpeed(3e8);
*/
int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", nodeName;
  uint32_t dataRate = 200, txPacketSize = 20, rxPacketSize = 20, nPackets = 0,
           nTrunks = 2, add = 1, dstadd = 2, packetSizeOffset = 0,
           dcmacMaxNodes = 4, devBitRate = 1800;
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
        cxxopts::value<uint32_t>(nPackets))

        ("num-trunks",
         std::string("number of trunks to transmit (default: ") +
             std::to_string(nTrunks) + ")",
         cxxopts::value<uint32_t>(nTrunks))(
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
            std::string(
                "received packet size in bytes (overhead + payload). Only "
                "needed if "
                "packet type is 1 (default: ") +
                std::to_string(rxPacketSize) + ")",
            cxxopts::value<uint32_t>(rxPacketSize))(
            "data-rate",
            std::string(
                "application data rate in bps. A high value could saturate "
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
            cxxopts::value<bool>(disableRx))(
            "dcmac", "(DcMac) adds a DcMAC layer", cxxopts::value<bool>(dcmac))(
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
  txPacketSize = txPacketSize / nTrunks;
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

  uint32_t totalPacketSize = txPacketSize + packetSizeOffset;
  Ptr<CommsDeviceService> node;

  if (dcmac) {
    auto macLayer = CreateObject<DcMac>();

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
    node = CreateObject<CommsDeviceService>(rxpb);
    node->SetBlockingTransmission(false);
    node->SetCommsDeviceId(nodeName);
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
  node->Start();

  std::thread tx, rx;

  double bytesPerSecond = dataRate / 8.;
  double nanosPerByte = 1e9 / bytesPerSecond;
  log->Info("data rate (bps) = {} ; packet size = {} (+offset = {}) ; num. "
            "packets = {} ; "
            "bytes/second = {}\nnanos/byte = {}  Num. Trunks = {}",
            dataRate, txPacketSize, totalPacketSize, nPackets, bytesPerSecond,
            nanosPerByte, nTrunks);
  int lastcseq = nTrunks - 1;
  tx = std::thread([&]() {
    std::this_thread::sleep_for(chrono::milliseconds(msStart));
    uint8_t *trunkseqPtr = txPacket->GetPayloadBuffer();
    uint32_t imgSize = txPacketSize * nTrunks, cseq;
    for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
      uint64_t nanos = round(imgSize * nanosPerByte);
      txPacket->SetSeq(npacket);
      cseq = 0;
      txPacket->SetDestAddr(dstadd);
      while (cseq <= lastcseq) {
        *trunkseqPtr = cseq;
        txPacket->PayloadUpdated(payloadSize);
        node << txPacket;
        cseq++;
      }
      log->Info("TX TO {} SEQ {} SIZE {}", dstadd, npacket, imgSize);
      std::this_thread::sleep_for(chrono::nanoseconds(nanos));
    }
  });

  int currentSeqs[10];
  for (auto i = 0; i < 10; i++) {
    currentSeqs[i] = 0;
  }

  if (!disableRx) {
    rx = std::thread([&]() {
      PacketPtr dlf = rxpb->Create();
      uint8_t *trunkseq = dlf->GetPayloadBuffer();
      int srcAddr, cseq;
      uint32_t seq;
      while (true) {
        node >> dlf;
        if (dlf->PacketIsOk()) {
          seq = dlf->GetSeq();
          srcAddr = dlf->GetSrcAddr();
          cseq = currentSeqs[srcAddr];
          if (cseq == *trunkseq) {
            if (cseq < lastcseq) {
              currentSeqs[srcAddr] = cseq + 1;
            } else {
              log->Info("RX FROM {} SEQ {} SIZE {}", dlf->GetSrcAddr(), seq,
                        dlf->GetPacketSize() * nTrunks);
              currentSeqs[srcAddr] = 0;
            }
          } else {
            log->Warn("SEQERR {}", trunkseq);
          }
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
