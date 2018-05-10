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
  uint32_t dataRate = 999999, payloadSize = 20, mac = 1;
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
        "payload-size", "payload size in bytes",
        cxxopts::value<uint32_t>(payloadSize))(
        "data-rate", "application data rate in bps (a high value could "
                     "saturate the output buffer",
        cxxopts::value<uint32_t>(dataRate))(
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
  log->LogToConsole(false);
  log->SetLogFormatter(logFormatter);

  node->SetLogLevel(warn);
  node->Start();

  std::thread tx, rx;

  tx = std::thread([node, pb, nodeName, log, payloadSize, mac, dataRate]() {
    auto txPacket = pb->Create();
    std::static_pointer_cast<DataLinkFrame>(txPacket)->SetDesDir(mac);
    txPacket->PayloadUpdated(payloadSize);

    double bytesPerSecond = dataRate / 8.;
    double nanosPerByte = 1e9 / bytesPerSecond;
    log->Info("data rate (bps) = {} ; payload size = {} ; total size (payload "
              "+ overhead): {} ; "
              "bytes/second = {}\nmicros/byte = {}",
              dataRate, payloadSize, txPacket->GetPacketSize(), bytesPerSecond,
              nanosPerByte);

    uint8_t *data = new uint8_t[payloadSize];
    while (1) {
      uint32_t bytesInBuffer = 0;
      while (bytesInBuffer < payloadSize) {
        // http://man7.org/linux/man-pages/man2/read.2.html
        int n = read(0, data + bytesInBuffer, 1);
        bytesInBuffer += n;
        if (n == 0) {
          std::cerr << "End of File" << std::endl;
          exit(0);
        }
      }
      txPacket->SetPayload(data, bytesInBuffer);
      bytesInBuffer = 0;
      auto pktSize = txPacket->GetPacketSize();
      auto nanos = (uint32_t)round(pktSize * nanosPerByte);
      log->Info("TX ; SIZE: {}", pktSize);
      node << txPacket;
      std::this_thread::sleep_for(chrono::nanoseconds(nanos));
    }
  });

  rx = std::thread([node, nodeName, pb, log]() {
    PacketPtr dlf = pb->Create();
    while (true) {
      node >> dlf;
      if (dlf->PacketIsOk()) {
        write(1, dlf->GetPayloadBuffer(), dlf->GetPayloadSize());
      } /*
  else
         log->Warn("Packet received with errors!");
         */
    }
  });
  tx.join();
  rx.join();
  exit(0);
}
