#include <cpplogging/cpplogging.h>
#include <cxxopts.hpp>
#include <dccomms/CommsDeviceSocket.h>
#include <dccomms/dccomms.h>
#include <iostream>

/*
 * This is a tool to study the communication link capabilities
 */

using namespace dccomms;
using namespace std;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr, txName, rxName;
  bool enableTx = true, enableRx = true;
  uint32_t dataRate = 200, payloadSize = 5;
  try {
    cxxopts::Options options("dccomms_examples/example3",
                             " - command line options");
    options.add_options()
        ("f,log-file", "File to save the log",cxxopts::value<std::string>(logFile)->default_value("")->implicit_value("example2_log"))
        ("l,log-level", "log level: critical,debug,err,info,off,trace,warn",cxxopts::value<std::string>(logLevelStr)->default_value("info"))
        ("help", "Print help");


    options.add_options("Transmitter")
        ("enable-tx", "enable tx node",cxxopts::value<bool>(enableTx))
        ("data-rate", "application data rate in bps (a high value could ""saturate the output buffer",cxxopts::value<uint32_t>(dataRate))
        ("tx-name", "dccomms id for the tx node",cxxopts::value<std::string>(txName)->default_value("txNode"))
        ("packet-size", "packet size in bytes", cxxopts::value<uint32_t>(payloadSize));

    options.add_options("Receiver")
        ("enable-rx", "enable rx node",cxxopts::value<bool>(enableRx))
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
  log->FlushLogOn(info);
  log->SetLogName("Main");
  log->SetLogLevel(logLevel);

  double bytesPerSecond = dataRate / 8.;
  double nanosPerByte = 1000000000 / bytesPerSecond;
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
  if (logFile != "") {
    log->LogToFile(logFile);
  }

  std::thread tx, rx;

  if (enableTx) {

    tx = std::thread([pb, nanosPerByte, logLevel]() {
      Ptr<Logger> log = CreateObject<Logger>();
      log->FlushLogOn(info);
      log->SetLogName("Tx");
      log->SetLogLevel(logLevel);

      CommsDeviceServicePtr service(new CommsDeviceService(pb));
      service->SetCommsDeviceId("buoy1_s2cr");
      service->Start();

      CommsDeviceSocketPtr dev(new CommsDeviceSocket(2));
      dev->SetPacketBuilder(pb);
      dev->SetCommsDevice(service);
      dev->SetDestAddr(1);
      dev->SetPayloadSize(5);

      uint32_t nanos;
      while (1) {
        std::string msg = "c++ Hello World!\n";
        log->Debug("Test 0 begin");
        dev << msg;
        log->Debug("Test 0 end");

        nanos = (uint32_t)round(msg.length() * nanosPerByte);
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));

        msg[msg.size()-1] = ' ';
        msg += "(using Send(...) method)\n";
        log->Debug("Test 1 begin");
        dev->Send(msg.c_str(), msg.length(), 1);
        log->Debug("Test 1 end");

        nanos = (uint32_t)round(msg.length() * nanosPerByte);
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));

        log->Debug("Test 2 begin");
        dev << "c Hello World!\n";
        log->Debug("Test 2 end");

        nanos = (uint32_t)round(msg.length() * nanosPerByte);
        std::this_thread::sleep_for(chrono::nanoseconds(nanos));
      }
    });
  }

  if (enableRx) {
      rx = std::thread([pb, nanosPerByte]() {
        CommsDeviceServicePtr service(new CommsDeviceService(pb));
        service->SetCommsDeviceId("bluerov2_s2cr");
        service->Start();

        CommsDeviceSocketPtr dev(new CommsDeviceSocket(1));
        dev->SetPacketBuilder(pb);
        dev->SetCommsDevice(service);
        dev->SetDestAddr(2);

        char byte;
        while (1) {
          dev >> byte;
          std::cout << byte << std::flush;
        }
      });
  }
  if (enableTx)
    tx.join();
  if (enableRx)
    rx.join();

  exit(0);
}
