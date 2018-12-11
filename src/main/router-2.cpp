#include <algorithm>
#include <cpplogging/cpplogging.h>
#include <cpputils/SignalManager.h>
#include <cxxopts.hpp>
#include <dccomms/dccomms.h>
#include <dccomms_packets/VariableLengthPacket.h>
#include <fstream>
#include <iostream>
#include <math.h>
#include <regex>

/*
 * This is a tool to build a router with 2 interfaces
 * CommsDeviceService as StreamCommsDevice.
 * It uses the VariableLengthPacket with CRC16.
 *
 */
#define MAX_LINE_LENGTH 200

using namespace dccomms;
using namespace dccomms_packets;
using namespace std;
using namespace cpputils;

struct Route {
  int addr, mask;
  CommsDeviceServicePtr dev;
};

typedef std::shared_ptr<Route> RoutePtr;
typedef std::vector<RoutePtr> RouteList;
typedef std::map<string, CommsDeviceServicePtr> DevMap;

int main(int argc, char **argv) {
  std::string logFile, logLevelStr = "info", dev0Name, dev1Name, routingTable,
                       nodeName;
  bool flush = false, syncLog = false;
  try {
    cxxopts::Options options("dccomms_examples/example3",
                             " - command line options");
    options.add_options()(
        "f,log-file", "File to save the log",
        cxxopts::value<std::string>(logFile)->default_value(""))(
        "t,table", "File with the routing table",
        cxxopts::value<std::string>(routingTable)->default_value(""))(
        "F,flush-log", "flush log", cxxopts::value<bool>(flush))(
        "s,sync-log", "sync-log", cxxopts::value<bool>(syncLog))(
        "l,log-level", "log level: critical,debug,err,info,off,trace,warn",
        cxxopts::value<std::string>(logLevelStr)->default_value("info"))(
        "help", "Print help");
    options.add_options("node_comms")(
        "dev0", "dccomms id for the interface number 0",
        cxxopts::value<std::string>(dev0Name)->default_value("dev0"))(
        "dev1", "dccomms id for the interface number 1",
        cxxopts::value<std::string>(dev1Name)->default_value("dev1"))(
        "node", "node name in routing table",
        cxxopts::value<std::string>(nodeName)->default_value("node"));

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help({"", "node_comms"}) << std::endl;
      exit(0);
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }
  DevMap devMap;

  Ptr<VariableLengthPacketBuilder> pb =
      CreateObject<VariableLengthPacketBuilder>();

  Ptr<CommsDeviceService> dev0 = CreateObject<CommsDeviceService>(pb);
  dev0->SetCommsDeviceId(dev0Name);

  devMap[dev0Name] = dev0;

  Ptr<CommsDeviceService> dev1 = CreateObject<CommsDeviceService>(pb);
  dev1->SetCommsDeviceId(dev1Name);

  devMap[dev1Name] = dev1;

  auto logFormatter =
      std::make_shared<spdlog::pattern_formatter>("%D %T.%F %v");
  LogLevel logLevel = cpplogging::GetLevelFromString(logLevelStr);
  Ptr<Logger> log = CreateObject<Logger>();
  if (logFile != "") {
    log->LogToFile(logFile);
  }
  log->SetLogLevel(logLevel);
  log->SetLogName(dev0Name + dev1Name);
  log->LogToConsole(true);
  log->SetLogFormatter(logFormatter);

  if (flush) {
    log->FlushLogOn(info);
    log->Info("Flush log on info");
  }
  if (!syncLog) {
    log->SetAsyncMode();
    log->Info("Async. log");
  }

  std::smatch mr;
  std::string rnode, gw, all;
  int addr, mask;
  std::regex routex("^([^\\s]+)\\s+(\\d+)/(\\d+)\\s+([^\\s]+)$");

  RouteList routes;
  // Load routing table
  fstream tablefile;
  string line;
  tablefile.open(routingTable, fstream::in);

  if (!tablefile.is_open()) {
    log->Error("Cannot open routing table file '{}'", routingTable);
  }
  std::getline(tablefile, line);
  while (!tablefile.eof()) {
    bool res = std::regex_match(line, mr, routex);
    if (res) {
      all = mr[0];
      rnode = mr[1];
      addr = std::stoi(mr[2]);
      mask = std::stoi(mr[3]);
      gw = mr[4];
      if (nodeName == rnode) {
        auto route = dccomms::CreateObject<Route>();
        route->addr = addr & mask;
        route->mask = mask;
        auto it = devMap.find(gw);
        if (it != devMap.end()) {
          route->dev = it->second;
          routes.push_back(route);
          log->Info("{} --- {}/{} --- {}", rnode, addr, mask, gw);
        } else
          log->Error("Device '{}' does not exist");
      }
    }
    std::getline(tablefile, line);
  }
  tablefile.close();

  std::sort(
      routes.begin(), routes.end(),
      [](const RoutePtr &a, const RoutePtr &b) { return a->mask > b->mask; });

  dev0->SetLogLevel(warn);
  dev0->Start();

  dev1->SetLogLevel(warn);
  dev1->Start();
  std::thread dev0Rx, dev1Rx;

  dev0Rx = std::thread([&]() {
    PacketPtr packet = pb->Create();
    while (true) {
      dev0 >> packet;
      if (packet->PacketIsOk()) {
        uint8_t *dstPtr = packet->GetPayloadBuffer();
        uint8_t src = (*dstPtr & 0xf0) >> 4;
        uint8_t dst = *dstPtr & 0xf;
        uint16_t *seqPtr = (uint16_t *)(dstPtr + 1);

        log->Info("PKT {}  SEQ {}  DST {}  SRC {}", packet->GetPacketSize(),
                  *seqPtr, dst, src);
        for (auto route : routes) {
          if ((dst & route->mask) == route->addr) {
            route->dev << packet;
          }
        }
      }
      else{
          log->Error("PKT ERROR");
      }
    }
  });

  dev1Rx = std::thread([&]() {
    PacketPtr packet = pb->Create();
    while (true) {
      dev1 >> packet;
      if (packet->PacketIsOk()) {
        uint8_t *dstPtr = packet->GetPayloadBuffer();
        uint8_t src = (*dstPtr & 0xf0) >> 4;
        uint8_t dst = *dstPtr & 0xf;
        uint16_t *seqPtr = (uint16_t *)dstPtr + 1;

        for (auto route : routes) {
          if ((dst & route->mask) == route->addr) {
            route->dev << packet;
          }
        }
      }
    }
  });

  SignalManager::SetLastCallback(SIGINT, [&](int sig) {
    printf("Received %d signal.\nFlushing log messages...", sig);
    fflush(stdout);
    log->FlushLog();
    Utils::Sleep(2000);
    printf("Log messages flushed.\n");
    exit(0);
  });

  dev0Rx.join();
  dev1Rx.join();
  return 0;
}
