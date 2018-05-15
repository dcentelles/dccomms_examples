#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <dccomms_packets/SimplePacket.h>
#include <iostream>
#include <cpputils/SignalManager.h>

/*
 * This is a tool to study the communication link capabilities using the CommsDeviceService
 * as StreamCommsDevice. It uses the SimplePacket with CRC16.
 * Each packet sent encodes the sequence number in 16 bits.
 * It creates two different CommsDeviceServices: one to transmit and other to receive.
 */

using namespace dccomms_packets;
using namespace dccomms;
using namespace std;
using namespace cpputils;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", txName, rxName;
  bool enableTx = false, enableRx = false;
  uint32_t dataRate = 200, packetSize = 20, nPackets = 50, packetSizeOffset = 0;
  bool flush = false, asyncLog = true;
  try {
    cxxopts::Options options("dccomms_examples/example3",
                             " - command line options");
    options.add_options()
        ("f,log-file", "File to save the log", cxxopts::value<std::string>(logFile)->default_value("")->implicit_value("example2_log"))
        ("F,flush-log", "flush log", cxxopts::value<bool>(flush))
        ("a,async-log", "async-log", cxxopts::value<bool>(asyncLog))
        ("l,log-level", "log level: critical,debug,err,info,off,trace,warn", cxxopts::value<std::string>(logLevelStr)->default_value("info"))
        ( "help", "Print help")
        ("packet-size", "packet size in bytes (payload size = packet size - 3)", cxxopts::value<uint32_t>(packetSize))
        ("packet-size-offset", "packet size offset (default = 0)", cxxopts::value<uint32_t>(packetSizeOffset));
    options.add_options("Transmitter")
        ("enable-tx", "enable tx node", cxxopts::value<bool>(enableTx))
        ("num-packets", "number of packets to transmit", cxxopts::value<uint32_t>(nPackets))
        ("data-rate", "application data rate in bps (a high value could "
                     "saturate the output buffer", cxxopts::value<uint32_t>(dataRate))
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
  Ptr<Logger> log = CreateObject<Logger>(),
      rxLog  = CreateObject<Logger>(),
      txLog = CreateObject<Logger>();

  if (logFile != "") {
    log->LogToFile(logFile);
    txLog->LogToFile(logFile + "_" + txName);
    rxLog->LogToFile(logFile + "_" + rxName);
  }
  log->SetLogName("Main");
  log->SetLogLevel(logLevel);
  txLog->SetLogLevel(logLevel);
  rxLog->SetLogLevel(logLevel);

  if (asyncLog){
    log->SetAsyncMode();
    txLog->SetAsyncMode();
    rxLog->SetAsyncMode();
    log->Info("Async. log");
  }

  //https://github.com/gabime/spdlog/wiki/3.-Custom-formattingges
  //log->SetLogFormatter(std::make_shared<spdlog::pattern_formatter>("[%D %T.%F] %v"))
  auto logFormatter = std::make_shared<spdlog::pattern_formatter>("[%T.%F] %v");
  txLog->SetLogFormatter(logFormatter);
  rxLog->SetLogFormatter(logFormatter);

  PacketBuilderPtr pb = CreateObject<SimplePacketBuilder>(0, FCS::CRC16);
  auto emptyPacket = pb->Create();
  auto emptyPacketSize = emptyPacket->GetPacketSize();
  auto payloadSize = packetSize - emptyPacketSize;
  pb = CreateObject<SimplePacketBuilder>(payloadSize, FCS::CRC16);
  std::thread tx, rx;
  uint32_t totalPacketSize = packetSize + packetSizeOffset;

  if (enableTx) {
    Ptr<CommsDeviceService> txnode = CreateObject<CommsDeviceService>(pb);
    txnode->SetLogLevel(info);
    txnode->SetCommsDeviceId(txName);
    txnode->Start();

    double bytesPerSecond = dataRate / 8.;
    uint64_t nanosPerByte = 1e9 / bytesPerSecond;
    log->Info("data rate (bps) = {} ; packet size = {} (+offset = {}); num. packets = {} ; "
              "bytes/second = {}\nnanos/byte = {}",
              dataRate, packetSize, totalPacketSize, nPackets, bytesPerSecond, nanosPerByte);
    tx = std::thread([txnode, pb, txName, txLog, nPackets, nanosPerByte,
                      payloadSize, totalPacketSize]() {
      auto txPacket = pb->Create();
      uint16_t *seqPtr = (uint16_t *)(txPacket->GetPayloadBuffer());
      uint8_t *asciiMsg = (uint8_t *)(seqPtr + 1);
      uint32_t msgSize = payloadSize - 2;
      uint8_t *maxPtr = asciiMsg + msgSize;
      char digit = '0';
      for (uint8_t *pptr = asciiMsg; pptr < maxPtr; pptr++) {
        *pptr = digit++;
      }
      for (uint32_t npacket = 0; npacket < nPackets; npacket++) {
        *seqPtr = npacket;
        txPacket->PayloadUpdated(msgSize + 2); // == packetSize
        auto nanos = (uint64_t)round(totalPacketSize * nanosPerByte);
        txLog->Info("TX ; SEQ: {} ; SIZE: {}",
                    npacket, txPacket->GetPacketSize());
        txnode << txPacket;
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));
      }
    });
  }

  if (enableRx) {
    Ptr<CommsDeviceService> rxnode = CreateObject<CommsDeviceService>(pb);
    rxnode->SetLogLevel(info);
    rxnode->SetCommsDeviceId(rxName);
    rxnode->Start();
    rx = std::thread([rxnode, pb, rxLog, packetSizeOffset]() {
      PacketPtr dlf = pb->Create();
      while (true) {
        rxnode >> dlf;
        if (dlf->PacketIsOk()) {
          uint16_t *seqPtr = (uint16_t *)dlf->GetPayloadBuffer();
          rxLog->Info("RX ; SEQ: {} ; SIZE: {}", *seqPtr,
                      dlf->GetPacketSize());
        } else
          rxLog->Warn("ERR");
      }
    });
  }

  SignalManager::SetLastCallback(SIGINT, [&](int sig)
  {
      printf("Received %d signal.\nFlushing log messages...", sig);
      fflush(stdout);
      log->FlushLog();
      txLog->FlushLog();
      rxLog->FlushLog();
      Utils::Sleep(2000);
      printf("Log messages flushed.\n");
      exit(0);
  });

  if (enableTx)
    tx.join();
  if (enableRx)
    rx.join();

  exit(0);
}
