// =============================================================================
// AQM Bufferbloat Analysis Simulation
// BE CSE Mini Project - Performance Analysis of AQM Algorithms
//
// This script creates a dumbbell topology with FlentApplication traffic generators
// and tests 4 queue disciplines on the bottleneck link (BOTH directions):
//   1. PfifoFast (baseline FIFO)
//   2. FqCoDel (Flow Queue Controlled Delay)
//   3. FqPie (Flow Queue Proportional Integral controller Enhanced)
//   4. FqCobalt (Flow Queue Cobalt - CoDel + BLUE)
//
// Traffic is generated ONLY via ns-3's FlentApplication (from MR !2968).
// Supported Flent tests: tcp_upload, tcp_download, rrul (mixed TCP+UDP)
//
// Topology: N client nodes (C1-CN) on left, N server nodes (D1-DN) on right,
//           routers R1-R2 with one bottleneck link. Default N=3.
//           Each client Ci paired with server Di.
//
// Flow counts per test (per client):
//   tcp_upload:      1 TCP flow (client->server)                          [R1->R2]
//   tcp_download:    1 TCP flow (server->client)                          [R2->R1]
//   rrul:            8 TCP flows (4 up + 4 down) + 3 UDP echo flows       [bidirectional]
//
// Total flows per run (with --nClients=3):
//   tcp_upload:      3 TCP flows
//   tcp_download:    3 TCP flows
//   rrul:            24 TCP flows (8 per client × 3) + 9 UDP flows (3 per client × 3)
//
// Build: Place in scratch/, then:
//   ./ns3 configure -d optimized
//   ./ns3 build
//   ./ns3 run "scratch/aqm-bufferbloat-simulation --queueDisc=FqCoDel --test=rrul --seed=1 --nClients=3"
// =============================================================================

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/flent-application.h"  // From GSoC-2026-flent-application branch
#include "ns3/netanim-module.h"     // NetAnim visualization

#include <filesystem>
#include <fstream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AqmBufferbloatSimulation");

// =============================================================================
// Command-line arguments structure
// =============================================================================
struct SimulationParams {
  std::string queueDiscType = "FqCoDel";   // PfifoFast | FqCoDel | FqPie | FqCobalt
  std::string testName = "rrul";           // tcp_upload | tcp_download | rrul
  uint32_t nClients = 3;                   // Number of client/server pairs (default 3)
  uint32_t seed = 1;                       // RNG seed for reproducibility
  uint32_t run = 1;                        // RNG run number
  double simTime = 60.0;                   // Simulation duration (seconds)
  std::string outputDir = ".";             // Output directory for results
  bool enablePcap = false;                 // Enable PCAP tracing
  bool enableBql = true;                   // Enable BQL (DynamicQueueLimits) on bottleneck
  bool enableNetAnim = false;              // Enable NetAnim visualization
};

// =============================================================================
// Parse command line arguments
// =============================================================================
SimulationParams ParseArguments(int argc, char* argv[]) {
  SimulationParams params;

  CommandLine cmd(__FILE__);
  cmd.AddValue("queueDisc", "Queue discipline: PfifoFast, FqCoDel, FqPie, FqCobalt",
               params.queueDiscType);
  cmd.AddValue("test", "Flent test name: tcp_upload, tcp_download, rrul",
               params.testName);
  cmd.AddValue("nClients", "Number of client/server pairs (default 3)",
               params.nClients);
  cmd.AddValue("seed", "RNG seed", params.seed);
  cmd.AddValue("run", "RNG run number", params.run);
  cmd.AddValue("simTime", "Simulation time in seconds", params.simTime);
  cmd.AddValue("outputDir", "Output directory", params.outputDir);
  cmd.AddValue("pcap", "Enable PCAP tracing", params.enablePcap);
  cmd.AddValue("enableBql", "Enable BQL (DynamicQueueLimits) on bottleneck", params.enableBql);
  cmd.AddValue("enableNetAnim", "Enable NetAnim visualization output", params.enableNetAnim);
  cmd.Parse(argc, argv);

  // Validate nClients
  if (params.nClients < 1 || params.nClients > 10) {
    NS_FATAL_ERROR("nClients must be between 1 and 10, got " << params.nClients);
  }

  // Validate queueDisc
  const std::vector<std::string> validQueueDiscs = {"PfifoFast", "FqCoDel", "FqPie", "FqCobalt"};
  bool valid = false;
  for (const auto& v : validQueueDiscs) {
    if (params.queueDiscType == v) { valid = true; break; }
  }
  if (!valid) {
    std::ostringstream oss;
    oss << "Unknown queue disc type: '" << params.queueDiscType << "'.\n"
        << "Valid options are:";
    for (const auto& v : validQueueDiscs) {
      oss << " " << v;
    }
    oss << "\nNote: names are case-sensitive (e.g., 'FqCoDel' not 'FqCodel').";
    NS_FATAL_ERROR(oss.str());
  }

  return params;
}

// =============================================================================
// Install queue disc on a specific device (bottleneck egress interface)
// =============================================================================
Ptr<QueueDisc> InstallQueueDiscOnDevice(Ptr<NetDevice> device, const std::string& type, bool enableBql) {
  TrafficControlHelper tch;

  // TypeId strings for queue discs (ns-3 standard)
  std::string typeId;
  if (type == "PfifoFast") {
    typeId = "ns3::PfifoFastQueueDisc";
  } else if (type == "FqCoDel") {
    typeId = "ns3::FqCoDelQueueDisc";
  } else if (type == "FqPie") {
    typeId = "ns3::FqPieQueueDisc";
  } else if (type == "FqCobalt") {
    typeId = "ns3::FqCobaltQueueDisc";
  } else {
    NS_FATAL_ERROR("Unknown queue disc type: " << type);
  }

  // Set root queue disc
  tch.SetRootQueueDisc(typeId);

  // None of our 4 queue discs need AddInternalQueues:
  // - PfifoFast: classful, manages 3 bands internally
  // - FqCoDel/FqPie/FqCobalt: classless flow-queue AQMs, manage per-flow queues internally

  // Enable BQL (DynamicQueueLimits) on the device transmission queues if requested
  // This is the ns-3 equivalent of Linux's BQL (Byte Queue Limits)
  if (enableBql) {
    tch.SetQueueLimits("ns3::DynamicQueueLimits");
  }

  // Uninstall any existing QueueDisc (default PfifoFast installed by InternetStackHelper)
  // before installing our configured one
  tch.Uninstall(device);

  QueueDiscContainer qdiscs = tch.Install(device);
  return qdiscs.Get(0);
}

// =============================================================================
// Trace callback for queue disc drops
// =============================================================================
void QueueDiscDropTrace(Ptr<const QueueDiscItem> item) {
  NS_LOG_INFO("Queue disc dropped packet: " << item->GetPacket()->GetUid());
}

// =============================================================================
// Main simulation function
// =============================================================================
int main(int argc, char* argv[]) {
  // -------------------------------------------------------------------------
  // Parse command line
  // -------------------------------------------------------------------------
  SimulationParams params = ParseArguments(argc, argv);

  // -------------------------------------------------------------------------
  // Set RNG seeds for reproducibility (critical for fair comparison)
  // -------------------------------------------------------------------------
  SeedManager::SetSeed(params.seed);
  SeedManager::SetRun(params.run);

  // -------------------------------------------------------------------------
  // Create nodes: Dumbbell topology
  //   Left side:  N client nodes (C1-CN)
  //   Right side: N server nodes (D1-DN)
  //   Routers:    R1, R2
  //   Bottleneck: R1 <-> R2 (10Mbps, 10ms) - AQM on BOTH egress directions
  // -------------------------------------------------------------------------
  NodeContainer leftClients;
  leftClients.Create(params.nClients);

  NodeContainer rightServers;
  rightServers.Create(params.nClients);

  NodeContainer routers;
  routers.Create(2);  // R1, R2

  Ptr<Node> R1 = routers.Get(0);
  Ptr<Node> R2 = routers.Get(1);

  // -------------------------------------------------------------------------
  // Configure point-to-point links
  // -------------------------------------------------------------------------
  // Access links (client <-> R1, server <-> R2): 100Mbps, 1ms delay
  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  accessLink.SetChannelAttribute("Delay", StringValue("1ms"));

  // Bottleneck link (R1 <-> R2): 10Mbps, 10ms delay (creates queue buildup)
  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  bottleneckLink.SetChannelAttribute("Delay", StringValue("10ms"));
  // Limit the device TxQueue so queuing happens in the AQM, not the device.
  // 10 packets ~ 1 BDP at 10Mbps/10ms (10Mbps * 10ms / 8 / 1500B ≈ 83 packets BDP;
  // 10p is small but non-zero to avoid excessive drops).
  bottleneckLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("10p")));

  // -------------------------------------------------------------------------
  // Install devices
  // -------------------------------------------------------------------------
  std::vector<NetDeviceContainer> leftDevices(params.nClients);
  std::vector<NetDeviceContainer> rightDevices(params.nClients);

  // Connect each client Ci to R1
  for (uint32_t i = 0; i < params.nClients; ++i) {
    leftDevices[i] = accessLink.Install(leftClients.Get(i), R1);
  }

  // Connect each server Di to R2
  for (uint32_t i = 0; i < params.nClients; ++i) {
    rightDevices[i] = accessLink.Install(rightServers.Get(i), R2);
  }

  // Connect R1 <-> R2 (bottleneck)
  // bottleneckDevices.Get(0) = R1 side, Get(1) = R2 side
  NetDeviceContainer bottleneckDevices = bottleneckLink.Install(R1, R2);

  // -------------------------------------------------------------------------
  // Install Internet stack
  // -------------------------------------------------------------------------
  InternetStackHelper stack;
  stack.Install(leftClients);
  stack.Install(rightServers);
  stack.Install(routers);

  // -------------------------------------------------------------------------
  // Assign IP addresses
  // -------------------------------------------------------------------------
  Ipv4AddressHelper address;

  // Left side subnets (10.1.x.0/24)
  std::vector<Ipv4InterfaceContainer> leftIfaces(params.nClients);
  for (uint32_t i = 0; i < params.nClients; ++i) {
    address.SetBase(Ipv4Address(("10.1." + std::to_string(i) + ".0").c_str()), "255.255.255.0");
    leftIfaces[i] = address.Assign(leftDevices[i]);
  }

  // Right side subnets (10.2.x.0/24)
  std::vector<Ipv4InterfaceContainer> rightIfaces(params.nClients);
  for (uint32_t i = 0; i < params.nClients; ++i) {
    address.SetBase(Ipv4Address(("10.2." + std::to_string(i) + ".0").c_str()), "255.255.255.0");
    rightIfaces[i] = address.Assign(rightDevices[i]);
  }

  // Bottleneck subnet (10.3.0.0/24)
  address.SetBase("10.3.0.0", "255.255.255.0");
  Ipv4InterfaceContainer bottleneckIfaces = address.Assign(bottleneckDevices);

  // -------------------------------------------------------------------------
  // Populate routing tables
  // -------------------------------------------------------------------------
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // -------------------------------------------------------------------------
  // Install Queue Disc on BOTH bottleneck egress interfaces
  // RRUL has bidirectional traffic:
  //   TCP downloads (4 per client): Remote(R2) -> Local(R1)  uses R2 egress toward R1
  //   TCP uploads (4 per client):  Local(R1) -> Remote(R2)  uses R1 egress toward R2
  //   UDP pings (3 per client):    Local(R1) -> Remote(R2)  uses R1 egress toward R2
  // So we need AQM on BOTH directions of the bottleneck.
  // -------------------------------------------------------------------------
  Ptr<NetDevice> r1ToR2 = bottleneckDevices.Get(0);  // R1's interface toward R2
  Ptr<NetDevice> r2ToR1 = bottleneckDevices.Get(1);  // R2's interface toward R1

  Ptr<QueueDisc> qdiscR1 = InstallQueueDiscOnDevice(r1ToR2, params.queueDiscType, params.enableBql);
  Ptr<QueueDisc> qdiscR2 = InstallQueueDiscOnDevice(r2ToR1, params.queueDiscType, params.enableBql);

  // Connect trace sources for queue disc metrics (on both)
  // Trace source name is "Drop" in ns-3 QueueDisc base class
  qdiscR1->TraceConnectWithoutContext("Drop", MakeCallback(&QueueDiscDropTrace));
  qdiscR2->TraceConnectWithoutContext("Drop", MakeCallback(&QueueDiscDropTrace));

  // Print bottleneck link configuration INCLUDING BQL status
  Ptr<PointToPointNetDevice> devR1 = DynamicCast<PointToPointNetDevice>(r1ToR2);
  Ptr<PointToPointNetDevice> devR2 = DynamicCast<PointToPointNetDevice>(r2ToR1);
  if (devR1 && devR2) {
    Ptr<Queue<Packet>> txQueue1 = devR1->GetQueue();
    Ptr<Queue<Packet>> txQueue2 = devR2->GetQueue();
    std::cout << "\n=== Bottleneck Link Configuration ===" << std::endl;
    std::cout << "  Data rate:     10 Mbps" << std::endl;
    std::cout << "  Delay:         10 ms (one-way)" << std::endl;
    if (txQueue1) {
      std::cout << "  Device queue (R1->R2): " << txQueue1->GetMaxSize() << std::endl;
    }
    if (txQueue2) {
      std::cout << "  Device queue (R2->R1): " << txQueue2->GetMaxSize() << std::endl;
    }
    std::cout << "  Queue disc:    " << params.queueDiscType << std::endl;
    std::cout << "  Qdisc limit:   " << qdiscR1->GetMaxSize() << " (R1->R2), "
              << qdiscR2->GetMaxSize() << " (R2->R1)" << std::endl;
    std::cout << "  BQL enabled:   " << (params.enableBql ? "yes (ns3::DynamicQueueLimits)" : "no") << std::endl;
  }

  // -------------------------------------------------------------------------
  // Install FlentApplication on client nodes (left side)
  // FlentApplication automatically installs corresponding sinks on server nodes
  // -------------------------------------------------------------------------
  ApplicationContainer flentApps;

  // FlentApplication uses fixed ports (per client pair):
  //   TCP upload:     port 9020 (client) -> 9020 (server)     [R1->R2 direction]
  //   TCP download:   port 9010 (server) -> 9010 (client)     [R2->R1 direction]
  //   RRUL UDP:       ports 9000-9002 (echo)                   [R1->R2 direction]
  //   RRUL TCP down:  ports 9010-9013 (server->client)         [R2->R1 direction]
  //   RRUL TCP up:    ports 9020-9023 (client->server)         [R1->R2 direction]
  //
  // IMPORTANT: Only ONE FlentApplication per host pair (port conflicts otherwise)
  // Each client Ci talks only to its paired server Di.

  // Create output directory using std::filesystem (portable, works on Windows and Linux)
  // NOTE: Batch script (run_all_sims.sh) already appends {QDISC}_{TEST}_s{SEED} to --outputDir.
  // Use params.outputDir directly to avoid double nesting.
  std::string outDir = params.outputDir;
  std::error_code ec;
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    NS_FATAL_ERROR("Failed to create output directory: " << outDir << " - " << ec.message());
  }

  for (uint32_t i = 0; i < params.nClients; ++i) {
    Ptr<FlentApplication> flentApp = CreateObject<FlentApplication>();

    // Client node Ci (left side) - MUST install on node explicitly
    Ptr<Node> clientNode = leftClients.Get(i);
    clientNode->AddApplication(flentApp);

    // Server (host) address - the corresponding server Di on the right side
    // For FlentApplication, m_hostAddress is the REMOTE host to communicate with
    // rightIfaces[i].GetAddress(0) = server Di's IP on its link to R2
    Ipv4Address serverAddr = rightIfaces[i].GetAddress(0);
    flentApp->SetAttribute("HostAddress", AddressValue(serverAddr));

    // Local bind address (client's own IP) - required for FlentApplication
    Ipv4Address clientAddr = leftIfaces[i].GetAddress(0);
    flentApp->SetAttribute("LocalBindAddress", AddressValue(clientAddr));

    // Test name: tcp_upload, tcp_download, or rrul
    flentApp->SetAttribute("TestName", StringValue(params.testName));

    // Output file (per-client) - ensure directory exists
    std::ostringstream oss;
    oss << outDir << "/flent-" << params.queueDiscType
        << "-" << params.testName << "-client" << i
        << "-s" << params.seed << ".json";
    std::string outFile = oss.str();
    flentApp->SetAttribute("OutputFilename", StringValue(outFile));
    std::cout << "Flent output will be written to: " << outFile << std::endl;

    // Simulation length
    flentApp->SetAttribute("Length", TimeValue(Seconds(params.simTime)));

    // Step size for goodput sampling (default 0.1s)
    flentApp->SetAttribute("StepSize", TimeValue(Seconds(0.1)));

    // Set explicit start time (apps start at 1.0s)
    // DO NOT set stop time here - FlentApplication::DoInitialize() sets its own
    // stop time = startTime + length + 10s for cleanup. We let it manage its lifetime.
    flentApp->SetStartTime(Seconds(1.0));

    // Add to container for management
    flentApps.Add(flentApp);
  }

  // -------------------------------------------------------------------------
  // Start applications (stop time managed by FlentApplication internally)
  // -------------------------------------------------------------------------
  flentApps.Start(Seconds(1.0));      // Start after routing converges
  // flentApps.Stop() NOT called - FlentApplication sets its own stop time in DoInitialize

  // -------------------------------------------------------------------------
  // Optional: PCAP tracing on bottleneck link
  // -------------------------------------------------------------------------
  if (params.enablePcap) {
    bottleneckLink.EnablePcapAll(params.outputDir + "/aqm-" + params.queueDiscType
                                  + "-" + params.testName);
  }

  // -------------------------------------------------------------------------
  // Flow Monitor for additional per-flow statistics
  // -------------------------------------------------------------------------
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // -------------------------------------------------------------------------
  // NetAnim visualization (optional)
  // -------------------------------------------------------------------------
  Ptr<AnimationInterface> anim = nullptr;
  if (params.enableNetAnim) {
    std::ostringstream animOss;
    animOss << params.outputDir << "/aqm-" << params.queueDiscType
            << "-" << params.testName << "-s" << params.seed << ".xml";
    anim = CreateObject<AnimationInterface>(animOss.str());
    std::cout << "NetAnim output will be written to: " << animOss.str() << std::endl;

    // Set node positions for dumbbell topology visualization
    // Left clients: x=100, y=100, 200, 300 (spread vertically)
    // Routers: R1 at x=400, y=150; R2 at x=500, y=150
    // Right servers: x=600, y=100, 200, 300 (spread vertically)
    double clientX = 100.0;
    double serverX = 600.0;
    double router1X = 400.0;
    double router2X = 500.0;
    double routerY = 150.0;
    double yStart = 100.0;
    double yStep = 100.0;

    for (uint32_t i = 0; i < params.nClients; ++i) {
      double y = yStart + i * yStep;
      anim->SetConstantPosition(leftClients.Get(i), clientX, y);
      anim->SetConstantPosition(rightServers.Get(i), serverX, y);
    }
    anim->SetConstantPosition(R1, router1X, routerY);
    anim->SetConstantPosition(R2, router2X, routerY);

    // Set node descriptions for clarity
    for (uint32_t i = 0; i < params.nClients; ++i) {
      anim->UpdateNodeDescription(leftClients.Get(i), "Client " + std::to_string(i));
      anim->UpdateNodeDescription(rightServers.Get(i), "Server " + std::to_string(i));
    }
    anim->UpdateNodeDescription(R1, "Router R1");
    anim->UpdateNodeDescription(R2, "Router R2");

    // Color coding
    for (uint32_t i = 0; i < params.nClients; ++i) {
      anim->UpdateNodeColor(leftClients.Get(i), 0, 255, 0);   // Green for clients
      anim->UpdateNodeColor(rightServers.Get(i), 0, 0, 255);  // Blue for servers
    }
    anim->UpdateNodeColor(R1, 255, 165, 0);  // Orange for R1
    anim->UpdateNodeColor(R2, 255, 0, 0);    // Red for R2

    // Enable packet metadata for animation
    anim->EnablePacketMetadata(true);
  }

  // -------------------------------------------------------------------------
  // Run simulation
  // -------------------------------------------------------------------------
  // FlentApplication runs for: startTime(1) + length(60) + 10s cleanup = 71s
  // Stop simulator slightly after to allow StopApplication() to write JSON
  Simulator::Stop(Seconds(params.simTime + 12));  // 60 + 12 = 72s
  Simulator::Run();

  // -------------------------------------------------------------------------
  // Post-simulation: Print queue disc statistics (both directions)
  // Use QueueDisc::GetStats() for accurate totals (GetNPackets() = current backlog)
  // -------------------------------------------------------------------------
  std::cout << "\n=== Queue Disc Statistics ===" << std::endl;
  std::cout << "Queue Disc Type: " << params.queueDiscType << std::endl;
  std::cout << "Test: " << params.testName << std::endl;
  std::cout << "Clients: " << params.nClients << std::endl;
  std::cout << "Seed: " << params.seed << ", Run: " << params.run << std::endl;
  std::cout << "BQL (DynamicQueueLimits): " << (params.enableBql ? "ENABLED" : "DISABLED") << std::endl;

  auto PrintQdiscStats = [](const std::string& label, Ptr<QueueDisc> qdisc) {
    QueueDisc::Stats st = qdisc->GetStats();
    std::cout << "\n--- " << label << " ---" << std::endl;
    std::cout << "  Total enqueued:   " << st.nTotalEnqueuedPackets << " packets (" << st.nTotalEnqueuedBytes << " bytes)" << std::endl;
    std::cout << "  Total dequeued:   " << st.nTotalDequeuedPackets << " packets (" << st.nTotalDequeuedBytes << " bytes)" << std::endl;
    std::cout << "  Total dropped:    " << st.nTotalDroppedPackets << " packets (" << st.nTotalDroppedBytes << " bytes)" << std::endl;
    if (st.nTotalDroppedPackets > 0) {
      std::cout << "    Drop breakdown (before enqueue):" << std::endl;
      for (const auto& drop : st.nDroppedPacketsBeforeEnqueue) {
        std::cout << "      " << drop.first << ": " << drop.second << std::endl;
      }
      std::cout << "    Drop breakdown (after dequeue):" << std::endl;
      for (const auto& drop : st.nDroppedPacketsAfterDequeue) {
        std::cout << "      " << drop.first << ": " << drop.second << std::endl;
      }
    }
    std::cout << "  Final backlog:    " << qdisc->GetNPackets() << " packets (" << qdisc->GetNBytes() << " bytes)" << std::endl;
  };

  PrintQdiscStats("R1->R2 (upload/UDP direction)", qdiscR1);
  PrintQdiscStats("R2->R1 (download direction)", qdiscR2);

  // -------------------------------------------------------------------------
  // Flow Monitor output
  // -------------------------------------------------------------------------
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

  std::cout << "\n=== Flow Monitor Statistics ===" << std::endl;
  for (auto const& flow : stats) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    std::cout << "Flow " << flow.first << ": "
              << t.sourceAddress << ":" << t.sourcePort << " -> "
              << t.destinationAddress << ":" << t.destinationPort
              << " (proto " << (uint16_t)t.protocol << ")" << std::endl;
    std::cout << "  Tx Packets: " << flow.second.txPackets
              << ", Rx Packets: " << flow.second.rxPackets
              << ", Lost: " << flow.second.lostPackets << std::endl;
    std::cout << "  Throughput: " << (flow.second.rxBytes * 8.0 / params.simTime / 1e6)
              << " Mbps" << std::endl;
    if (flow.second.delaySum.GetSeconds() > 0) {
      std::cout << "  Mean Delay: " << (flow.second.delaySum.GetSeconds() / flow.second.rxPackets * 1000)
                << " ms" << std::endl;
    }
    if (flow.second.jitterSum.GetSeconds() > 0) {
      std::cout << "  Mean Jitter: " << (flow.second.jitterSum.GetSeconds() / flow.second.rxPackets * 1000)
                << " ms" << std::endl;
    }
  }

  // -------------------------------------------------------------------------
  // Cleanup
  // -------------------------------------------------------------------------
  Simulator::Destroy();

  std::cout << "\nSimulation complete. Flent JSON outputs in: " << outDir << std::endl;

  return 0;
}