#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <iostream>

/*
 * This is a tool to study the communication link capabilities using the
 * CommsDeviceService
 * as StreamCommsDevice.
 * It uses the DataLinkFramePacket with CRC16.
 * Each packet sent encodes the sequence number in 16 bits.
 */

using namespace dccomms;
using namespace std;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", nodeName;
  uint32_t dataRate = 200, packetSize = 20, nPackets = 50, mac = 1;
  uint64_t msStart = 0;
  try {
    cxxopts::Options options("dccomms_examples/example3",
                             " - command line options");
    options.add_options()(
        "f,log-file", "File to save the log",
        cxxopts::value<std::string>(logFile)->default_value("")->implicit_value(
            "example4_log"))(
        "l,log-level", "log level: critical,debug,err,info,off,trace,warn",
        cxxopts::value<std::string>(logLevelStr)->default_value("info"))(
        "help", "Print help");
    options.add_options("node_comms")("mac", "MAC address",
                                      cxxopts::value<uint32_t>(mac))(
        "num-packets", "number of packets to transmit",
        cxxopts::value<uint32_t>(nPackets))("ms-start", "packet size in bytes",
                                            cxxopts::value<uint64_t>(msStart))(
        "packet-size", "packet size in bytes",
        cxxopts::value<uint32_t>(packetSize))(
        "data-rate", "application data rate in bps (a high value could "
                     "saturate the output buffer",
        cxxopts::value<uint32_t>(dataRate))(
        "node-name", "dccomms id for the tx node",
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

  auto checksumType = DataLinkFrame::fcsType::crc16;
  Ptr<DataLinkFramePacketBuilder> pb =
      CreateObject<DataLinkFramePacketBuilder>(checksumType);

  Ptr<CommsDeviceService> node = CreateObject<CommsDeviceService>(pb);
  node->SetCommsDeviceId(nodeName);

  auto logFormatter = std::make_shared<spdlog::pattern_formatter>("[%T.%F] %v");
  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Ptr<Logger> log = CreateObject<Logger>();
  if (logFile != "") {
    log->LogToFile(logFile);
  }
  log->SetLogLevel(logLevel);
  log->FlushLogOn(info);
  log->SetLogName(nodeName);
  log->SetLogFormatter(logFormatter);

  node->SetLogLevel(info);
  node->Start();

  std::thread tx, rx;

  double bytesPerSecond = dataRate / 8.;
  double nanosPerByte = 1e9 / bytesPerSecond;
  log->Info("data rate (bps) = {} ; packet size = {} ; num. packets = {} ; "
            "bytes/second = {}\nmicros/byte = {}",
            dataRate, packetSize, nPackets, bytesPerSecond, nanosPerByte);
  tx = std::thread([node, pb, nodeName, log, nPackets, packetSize,
                    nanosPerByte, mac, msStart]() {
    auto txPacket = pb->Create();
    std::static_pointer_cast<DataLinkFrame>(txPacket)->SetDesDir(mac);
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
    std::this_thread::sleep_for(chrono::milliseconds(msStart));
    for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
      *seqPtr = npacket;
      txPacket->PayloadUpdated(msgSize + 2);
      auto pktSize = txPacket->GetPacketSize();
      auto nanos = (uint32_t)round(pktSize * nanosPerByte);
      log->Info("TX ; SEQ: {} ; SIZE: {}",
                  npacket, txPacket->GetPacketSize());
      node << txPacket;
      std::this_thread::sleep_for(chrono::nanoseconds(nanos));
    }
  });

  rx = std::thread([node, nodeName, pb, log]() {
    PacketPtr dlf = pb->Create();
    while (true) {
      node >> dlf;
      if (dlf->PacketIsOk()) {
        uint16_t *seqPtr = (uint16_t *)dlf->GetPayloadBuffer();
        log->Info("RX ; SEQ: {} ; SIZE: {}", *seqPtr,
                    dlf->GetPacketSize());
      } else
        log->Warn("ERR");
    }
  });
  tx.join();
  rx.join();
  exit(0);
}
