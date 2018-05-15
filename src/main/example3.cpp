#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/CommsDeviceSocket.h>
#include <dccomms/dccomms.h>
#include <iostream>
#include <cpputils/SignalManager.h>

/*
 * This example sends Hello World messages through several methods of
 * a CommsDeviceSocket instance using the CommsDeviceService as StreamCommsDevice.
 * It uses the DataLinkFrame with CRC16.
 * It creates two different CommsDeviceServices: one to transmit and other to receive.
 */

using namespace dccomms;
using namespace std;
using namespace cpputils;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", txName = "tx", rxName = "rx";
  bool enableTx = false, enableRx = false;
  uint32_t dataRate = 20, payloadSize = 5, txmac = 2, rxmac = 1;
  bool flush = false, asyncLog = true;
  try {
    cxxopts::Options options("dccomms_examples/example3",
                             " - command line options");
    options.add_options()
        ("f,log-file", "File to save the log",cxxopts::value<std::string>(logFile)->default_value("")->implicit_value("example2_log"))
        ("F,flush-log", "flush log", cxxopts::value<bool>(flush))
        ("a,async-log", "async-log", cxxopts::value<bool>(asyncLog))
        ("l,log-level", "log level: critical,debug,err,info,off,trace,warn",cxxopts::value<std::string>(logLevelStr)->default_value("info"))
        ("help", "Print help");


    options.add_options("Transmitter")
        ("enable-tx", "enable tx node",cxxopts::value<bool>(enableTx))
        ("data-rate", "application data rate in bps (a high value could ""saturate the output buffer",cxxopts::value<uint32_t>(dataRate))
        ("tx-mac", "tx mac address",cxxopts::value<uint32_t>(txmac))
        ("tx-name", "dccomms id for the tx node",cxxopts::value<std::string>(txName)->default_value("txNode"))
        ("payload-size", "payload size in bytes", cxxopts::value<uint32_t>(payloadSize));

    options.add_options("Receiver")
        ("enable-rx", "enable rx node",cxxopts::value<bool>(enableRx))
        ("rx-mac", "rx mac address",cxxopts::value<uint32_t>(rxmac))
        ("rx-name", "dccomms id for the rx node",cxxopts::value<std::string>(rxName)->default_value("rxNode"));

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

  if (logFile != "") {
    log->LogToFile(logFile);
  }
  log->SetLogName("main");
  log->SetLogLevel(logLevel);

  if (asyncLog){
    log->SetAsyncMode();
    log->Info("Async. log");
  }

  //https://github.com/gabime/spdlog/wiki/3.-Custom-formattingges
  //log->SetLogFormatter(std::make_shared<spdlog::pattern_formatter>("[%D %T.%F] %v"))
  auto logFormatter = std::make_shared<spdlog::pattern_formatter>("[%T.%F] %v");
  log->SetLogFormatter(logFormatter);

  double bytesPerSecond = dataRate / 8.;
  double nanosPerByte = 1e9 / bytesPerSecond;
  log->Debug("{} bytes/second ; {} ns/byte", bytesPerSecond, nanosPerByte);
  PacketBuilderPtr pb = CreateObject<DataLinkFrameBuilderCRC16>();

  if(enableTx){
      //Compute and show the packet size for a packet with a payload size of 'payloadSize' bytes
      PacketPtr pkt = pb->Create();
      pkt->PayloadUpdated(payloadSize);
      uint32_t packetSize = pkt->GetPacketSize();
      log->Info("TX node enabled: [ tx-name: {} ; Payload size: {} bytes (packet size: {} bytes) ; App. data rate: {} bps ]",
            txName, payloadSize, packetSize, dataRate);
  }
  if(enableRx){
      log->Info("RX node enabled: [ rx-name: {} ]",
            rxName);
  }

  std::thread tx, rx;

  if (enableTx) {

    tx = std::thread([flush, asyncLog, pb, nanosPerByte, logLevel, txName, txmac, payloadSize, log]() {

      CommsDeviceServicePtr service(new CommsDeviceService(pb));
      service->SetCommsDeviceId(txName);
      service->Start();

      CommsDeviceSocketPtr dev(new CommsDeviceSocket(txmac));
      dev->SetPacketBuilder(pb);
      dev->SetStreamCommsDevice(service);
      dev->SetDestAddr(1);
      dev->SetPayloadSize(payloadSize);

      uint64_t nanos;
      while (1) {
        std::string msg = "c++ Hello World!\n";
        log->Debug("Test 0 begin");
        dev << msg;
        log->Debug("Test 0 end");

        nanos = (uint64_t)round(msg.length() * nanosPerByte);
        log->Debug("{} ns/byte ; {} bytes ; waiting for {} nanos ; {} seconds", nanosPerByte, msg.length(), nanos, nanos/(double)1e9);
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));


        msg[msg.size()-1] = ' ';
        msg += "(using Send(...) method)\n";
        log->Debug("Test 1 begin");
        dev->Send(msg.c_str(), msg.length());
        log->Debug("Test 1 end");

        nanos = (uint64_t)round(msg.length() * nanosPerByte);
        log->Debug("{} ns/byte ; {} bytes ; waiting for {} nanos ; {} seconds", nanosPerByte, msg.length(), nanos, nanos/(double)1e9);
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));

        log->Debug("Test 2 begin");
        dev << "c Hello World!\n";
        log->Debug("Test 2 end");

        nanos = (uint64_t)round(msg.length() * nanosPerByte);
        log->Debug("{} ns/byte ; {} bytes ; waiting for {} nanos ; {} seconds", nanosPerByte, msg.length(), nanos, nanos/(double)1e9);
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));
      }
    });
  }

  if (enableRx) {
      rx = std::thread([pb, nanosPerByte, rxName, rxmac]() {
        CommsDeviceServicePtr service(new CommsDeviceService(pb));
        service->SetCommsDeviceId(rxName);
        service->Start();

        CommsDeviceSocketPtr dev(new CommsDeviceSocket(rxmac));
        dev->SetPacketBuilder(pb);
        dev->SetStreamCommsDevice(service);
        dev->SetDestAddr(2);

        char byte;
        while (1) {
          dev >> byte;
          std::cout << byte << std::flush;
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

  if (enableTx)
    tx.join();
  if (enableRx)
    rx.join();

  exit(0);
}
