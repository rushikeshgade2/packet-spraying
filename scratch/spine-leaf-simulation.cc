/**
 * spine-leaf-simulation.cc
 * ========================
 * NS-3 simulation of a Spine-Leaf (2-tier Clos) data-centre network
 * comparing ECMP vs Packet Spraying for elephant flow load balancing.
 *
 * Topology
 * --------
 *   numSpine spine switches (default 4)
 *   numLeaf  leaf  switches (default 8) — fully connected to every spine
 *   hostsPerLeaf hosts per leaf (default 4)
 *   Total hosts = numLeaf * hostsPerLeaf
 *
 *   Link speeds:
 *     leaf -> host  : 1  Gbps, 10  us
 *     spine -> leaf : 10 Gbps, 5   us
 *
 * Command-line options
 * --------------------
 *   --numSpine      Number of spine switches      [4]
 *   --numLeaf       Number of leaf switches       [8]
 *   --hostsPerLeaf  Hosts per leaf switch         [4]
 *   --routing       "ecmp" | "spray"              [spray]
 *   --simTime       Simulation duration (s)       [5]
 *   --elephants     Number of elephant flows      [10]
 *   --mice          Number of mouse flows         [40]
 *   --seed          RNG run number                [1]
 *   --flowmon       Enable FlowMonitor output     [true]
 *   --pcap          Enable PCAP capture           [false]
 *
 * Build & run (same as fat-tree):
 *   ./ns3 run "spine-leaf-simulation --routing=spray"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-address-helper.h"

#include "../src/spray-routing/model/spray-routing.h"
#include "../src/spray-routing/model/elephant-tag.h"
#include "../src/spray-routing/helper/spray-routing-helper.h"

#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SpineLeafSimulation");

// ─────────────────────────────────────────────────────────────────────
//  Address pool helper
// ─────────────────────────────────────────────────────────────────────
static uint32_t g_subnet = 0;

static std::pair<Ipv4Address, Ipv4Address>
MakeLink (Ptr<Node> a, Ptr<Node> b,
          const std::string &bw, const std::string &delay,
          Ipv4AddressHelper &addrHelper)
{
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute  ("DataRate", StringValue (bw));
    p2p.SetChannelAttribute ("Delay",    StringValue (delay));

    NetDeviceContainer devs = p2p.Install (a, b);

    uint32_t A = (g_subnet >> 8) & 0xFF;
    uint32_t B =  g_subnet       & 0xFF;
    g_subnet++;

    std::ostringstream base;
    base << "10." << A << "." << B << ".0";
    addrHelper.SetBase (base.str ().c_str (), "255.255.255.252");

    Ipv4InterfaceContainer ifc = addrHelper.Assign (devs);
    return {ifc.GetAddress (0), ifc.GetAddress (1)};
}

// ─────────────────────────────────────────────────────────────────────
//  Topology state
// ─────────────────────────────────────────────────────────────────────
struct SpineLeaf {
    std::vector<Ptr<Node>> spine;
    std::vector<Ptr<Node>> leaf;
    // [leaf_idx][host_idx]
    std::vector<std::vector<Ptr<Node>>>    hosts;
    std::vector<std::vector<Ipv4Address>>  hostAddr;
};

// ─────────────────────────────────────────────────────────────────────
//  Build topology
// ─────────────────────────────────────────────────────────────────────
static SpineLeaf
BuildSpineLeaf (uint32_t numSpine, uint32_t numLeaf, uint32_t hostsPerLeaf,
                Ipv4AddressHelper &addrHelper, bool useSpray)
{
    SpineLeaf sl;
    sl.spine.resize (numSpine);
    sl.leaf.resize  (numLeaf);
    sl.hosts.resize (numLeaf);
    sl.hostAddr.resize (numLeaf);

    for (auto &n : sl.spine)    n = CreateObject<Node> ();
    for (uint32_t l = 0; l < numLeaf; ++l) {
        sl.leaf[l] = CreateObject<Node> ();
        sl.hosts[l].resize   (hostsPerLeaf);
        sl.hostAddr[l].resize(hostsPerLeaf);
        for (auto &n : sl.hosts[l]) n = CreateObject<Node> ();
    }

    // ── Install Internet stack ────────────────────────────────────────
    NodeContainer allNodes;
    for (auto &n : sl.spine)  allNodes.Add (n);
    for (uint32_t l = 0; l < numLeaf; ++l) {
        allNodes.Add (sl.leaf[l]);
        for (auto &n : sl.hosts[l]) allNodes.Add (n);
    }

    if (useSpray) {
        SprayRoutingHelper sh;
        InternetStackHelper internet;
        internet.SetRoutingHelper (sh);
        internet.Install (allNodes);
    } else {
        InternetStackHelper internet;
        internet.Install (allNodes);
    }

    // ── Links ─────────────────────────────────────────────────────────
    // spine[s] <-> leaf[l]  — full mesh
    // [spine][leaf] -> {spineAddr, leafAddr}
    std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>
        spineLeafAddr (numSpine,
            std::vector<std::pair<Ipv4Address,Ipv4Address>>(numLeaf));

    for (uint32_t s = 0; s < numSpine; ++s)
        for (uint32_t l = 0; l < numLeaf; ++l) {
            auto [sA, lA] = MakeLink (sl.spine[s], sl.leaf[l], "1Gbps", "5us", addrHelper);
            spineLeafAddr[s][l] = {sA, lA};
        }

    // leaf[l] <-> host[l][h]
    std::vector<std::vector<std::pair<Ipv4Address,Ipv4Address>>>
        leafHostAddr (numLeaf,
            std::vector<std::pair<Ipv4Address,Ipv4Address>>(hostsPerLeaf));

    for (uint32_t l = 0; l < numLeaf; ++l)
        for (uint32_t h = 0; h < hostsPerLeaf; ++h) {
            auto [lA, hA] = MakeLink (sl.leaf[l], sl.hosts[l][h], "1Gbps", "10us", addrHelper);
            leafHostAddr[l][h]  = {lA, hA};
            sl.hostAddr[l][h]   = hA;
        }

    // ── Routing ───────────────────────────────────────────────────────
    if (useSpray) {
        // Hosts: default via leaf
        for (uint32_t l = 0; l < numLeaf; ++l)
            for (uint32_t h = 0; h < hostsPerLeaf; ++h) {
                Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                    sl.hosts[l][h]->GetObject<Ipv4>()->GetRoutingProtocol());
                sr->AddRoute (Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"),
                              leafHostAddr[l][h].first, 1);
            }

        // Leaf switches: host routes + spray to all spine switches
        for (uint32_t l = 0; l < numLeaf; ++l) {
            Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                sl.leaf[l]->GetObject<Ipv4>()->GetRoutingProtocol());

            // Local host routes.
            // NOTE: spine<->leaf links were created first, so on a leaf the
            // SPINE uplinks occupy ifaces 1..numSpine and the HOST links
            // occupy numSpine+1..numSpine+hostsPerLeaf.
            for (uint32_t h = 0; h < hostsPerLeaf; ++h)
                sr->AddRoute (sl.hostAddr[l][h], Ipv4Mask("255.255.255.255"),
                              sl.hostAddr[l][h], 1 + numSpine + h);

            // Default: spray across all spine switches (ECMP set)
            for (uint32_t s = 0; s < numSpine; ++s)
                sr->AddRoute (Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"),
                              spineLeafAddr[s][l].first, 1 + s);
        }

        // Spine switches: default to each leaf
        for (uint32_t s = 0; s < numSpine; ++s) {
            Ptr<SprayRouting> sr = DynamicCast<SprayRouting>(
                sl.spine[s]->GetObject<Ipv4>()->GetRoutingProtocol());
            // Host-specific routes DOWN to the correct leaf (iface 1+l faces leaf l)
            for (uint32_t l = 0; l < numLeaf; ++l)
                for (uint32_t h = 0; h < hostsPerLeaf; ++h)
                    sr->AddRoute (sl.hostAddr[l][h], Ipv4Mask("255.255.255.255"),
                                  spineLeafAddr[s][l].second, 1 + l);
        }

    } else {
        // ECMP via Ipv4StaticRouting
        Ipv4StaticRoutingHelper staticHelper;
        auto getStatic = [&](Ptr<Node> node) {
            return staticHelper.GetStaticRouting (node->GetObject<Ipv4>());
        };

        // Hosts
        for (uint32_t l = 0; l < numLeaf; ++l)
            for (uint32_t h = 0; h < hostsPerLeaf; ++h)
                getStatic(sl.hosts[l][h])->AddNetworkRouteTo(
                    Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"),
                    leafHostAddr[l][h].first, 1);

        // Leaf switches
        for (uint32_t l = 0; l < numLeaf; ++l) {
            auto sr = getStatic(sl.leaf[l]);
            // Host links occupy ifaces numSpine+1.. (spine links came first)
            for (uint32_t h = 0; h < hostsPerLeaf; ++h)
                sr->AddHostRouteTo (sl.hostAddr[l][h], sl.hostAddr[l][h],
                                    1 + numSpine + h);
            for (uint32_t s = 0; s < numSpine; ++s)
                sr->AddNetworkRouteTo(Ipv4Address("0.0.0.0"),Ipv4Mask("0.0.0.0"),
                                      spineLeafAddr[s][l].first, 1 + s, s+1);
        }

        // Spine switches
        for (uint32_t s = 0; s < numSpine; ++s) {
            auto sr = getStatic(sl.spine[s]);
            for (uint32_t l = 0; l < numLeaf; ++l)
                for (uint32_t h = 0; h < hostsPerLeaf; ++h)
                    sr->AddHostRouteTo (sl.hostAddr[l][h],
                                        spineLeafAddr[s][l].second, 1 + l);
        }
    }

    return sl;
}

// ─────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────
int
main (int argc, char *argv[])
{
    uint32_t    numSpine     = 4;
    uint32_t    numLeaf      = 8;
    uint32_t    hostsPerLeaf = 4;
    std::string routing      = "spray";
    double      simTime      = 5.0;
    uint32_t    numElephant  = 10;
    uint32_t    numMice      = 40;
    uint32_t    seed         = 1;
    bool        enablePcap   = false;
    bool        enableFM     = true;

    CommandLine cmd;
    cmd.AddValue ("numSpine",     "Spine switch count",          numSpine);
    cmd.AddValue ("numLeaf",      "Leaf switch count",           numLeaf);
    cmd.AddValue ("hostsPerLeaf", "Hosts per leaf",              hostsPerLeaf);
    cmd.AddValue ("routing",      "ecmp | spray",                routing);
    cmd.AddValue ("simTime",      "Simulation duration (s)",     simTime);
    cmd.AddValue ("elephants",    "Elephant flow count",         numElephant);
    cmd.AddValue ("mice",         "Mouse flow count",            numMice);
    cmd.AddValue ("seed",         "RNG seed / run number",       seed);
    cmd.AddValue ("pcap",         "Enable PCAP",                 enablePcap);
    cmd.AddValue ("flowmon",      "Enable FlowMonitor",          enableFM);
    cmd.Parse (argc, argv);

    RngSeedManager::SetSeed (seed);
    RngSeedManager::SetRun  (seed);

    bool useSpray = (routing == "spray");

    NS_LOG_UNCOND ("[SpineLeaf] spine=" << numSpine
        << "  leaf=" << numLeaf
        << "  hostsPerLeaf=" << hostsPerLeaf
        << "  routing=" << routing
        << "  elephants=" << numElephant
        << "  mice=" << numMice);

    // ── Topology ──────────────────────────────────────────────────────
    Ipv4AddressHelper addrHelper;
    SpineLeaf sl = BuildSpineLeaf (numSpine, numLeaf, hostsPerLeaf, addrHelper, useSpray);

    std::vector<Ptr<Node>>   hostNodes;
    std::vector<Ipv4Address> hostAddrs;

    for (uint32_t l = 0; l < numLeaf; ++l)
        for (uint32_t h = 0; h < hostsPerLeaf; ++h) {
            hostNodes.push_back (sl.hosts[l][h]);
            hostAddrs.push_back (sl.hostAddr[l][h]);
        }

    uint32_t numHosts = hostNodes.size ();

    // ── Sinks on every host ───────────────────────────────────────────
    PacketSinkHelper sinkTcp ("ns3::TcpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), 5000));
    PacketSinkHelper sinkUdp ("ns3::UdpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), 6000));
    for (uint32_t i = 0; i < numHosts; ++i) {
        sinkTcp.Install (hostNodes[i]).Start (Seconds (0));
        sinkUdp.Install (hostNodes[i]).Start (Seconds (0));
    }

    // ── Elephant flows ────────────────────────────────────────────────
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
    rng->SetAttribute ("Min", DoubleValue (0.0));
    rng->SetAttribute ("Max", DoubleValue (1.0));

    uint16_t tcpPort = 5001;
    for (uint32_t i = 0; i < numElephant; ++i) {
        uint32_t srcIdx = i % numHosts;
        uint32_t dstIdx = (i + numHosts / 2) % numHosts;
        if (dstIdx == srcIdx) dstIdx = (srcIdx + 1) % numHosts;

        // Mark elephant flows via IP TOS — SprayRouting treats TOS != 0
        // as an elephant and sprays it per-packet across the ECMP set.
        InetSocketAddress dstSock (hostAddrs[dstIdx], tcpPort);
        dstSock.SetTos (0x10);
        BulkSendHelper bulk ("ns3::TcpSocketFactory", dstSock);
        bulk.SetAttribute ("MaxBytes",  UintegerValue (50 * 1024 * 1024));  // 50 MB
        bulk.SetAttribute ("SendSize",  UintegerValue (1448));

        double start = rng->GetValue () * 0.5;
        bulk.Install (hostNodes[srcIdx]).Start (Seconds (start));

        PacketSinkHelper sk ("ns3::TcpSocketFactory",
                              InetSocketAddress (Ipv4Address::GetAny (), tcpPort));
        sk.Install (hostNodes[dstIdx]).Start (Seconds (0));

        tcpPort++;
    }

    // ── Mouse flows ───────────────────────────────────────────────────
    uint16_t udpPort = 6001;
    for (uint32_t i = 0; i < numMice; ++i) {
        uint32_t srcIdx = (i * 7 + 3) % numHosts;
        uint32_t dstIdx = (i * 11 + 5) % numHosts;
        if (dstIdx == srcIdx) dstIdx = (srcIdx + 1) % numHosts;

        OnOffHelper onoff ("ns3::UdpSocketFactory",
                            InetSocketAddress (hostAddrs[dstIdx], udpPort));
        onoff.SetAttribute ("DataRate",   DataRateValue (DataRate ("10Mbps")));
        onoff.SetAttribute ("PacketSize", UintegerValue (512));
        onoff.SetAttribute ("OnTime",     StringValue ("ns3::ExponentialRandomVariable[Mean=0.1]"));
        onoff.SetAttribute ("OffTime",    StringValue ("ns3::ExponentialRandomVariable[Mean=0.1]"));

        double start = rng->GetValue () * 1.0;
        onoff.Install (hostNodes[srcIdx]).Start (Seconds (start));

        PacketSinkHelper sk ("ns3::UdpSocketFactory",
                              InetSocketAddress (Ipv4Address::GetAny (), udpPort));
        sk.Install (hostNodes[dstIdx]).Start (Seconds (0));

        udpPort++;
    }

    // ── PCAP ──────────────────────────────────────────────────────────
    if (enablePcap) {
        PointToPointHelper p2p;
        p2p.EnablePcapAll ("spine-leaf-" + routing);
    }

    // ── FlowMonitor ───────────────────────────────────────────────────
    Ptr<FlowMonitor>  flowMonitor;
    FlowMonitorHelper fmHelper;
    if (enableFM) {
        flowMonitor = fmHelper.InstallAll ();
    }

    // ── Run ───────────────────────────────────────────────────────────
    Simulator::Stop (Seconds (simTime + 1.0));
    Simulator::Run  ();

    // ── Output ────────────────────────────────────────────────────────
    if (enableFM && flowMonitor) {
        std::string xmlFile = "results/spine-leaf-" + routing + "-flowmon.xml";
        flowMonitor->CheckForLostPackets ();
        flowMonitor->SerializeToXmlFile  (xmlFile, true, true);
        NS_LOG_UNCOND ("[SpineLeaf] FlowMonitor saved to " << xmlFile);

        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier> (fmHelper.GetClassifier ());
        FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();

        uint64_t totalTx = 0, totalRx = 0;
        double   totalDelay = 0;
        uint32_t nFlows = 0;

        NS_LOG_UNCOND ("\nFlowID  Proto  Src->Dst                     "
                       "TxPkts  RxPkts  ThroughputMbps  AvgDelayMs");
        NS_LOG_UNCOND (std::string(80, '-'));

        for (auto &kv : stats) {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (kv.first);
            auto &s = kv.second;
            double duration = (s.timeLastRxPacket - s.timeFirstTxPacket).GetSeconds ();
            double tput     = (duration > 0) ? (s.rxBytes * 8.0) / duration / 1e6 : 0.0;
            double avgDelay = (s.rxPackets > 0)
                              ? s.delaySum.GetMilliSeconds () / s.rxPackets : 0.0;

            std::ostringstream sd;
            sd << t.sourceAddress << ":" << t.sourcePort
               << "->" << t.destinationAddress << ":" << t.destinationPort;

            NS_LOG_UNCOND (std::setw(6)  << kv.first
                << "  " << (t.protocol==6?"TCP":"UDP")
                << "  " << std::setw(30) << sd.str()
                << "  " << std::setw(6)  << s.txPackets
                << "  " << std::setw(6)  << s.rxPackets
                << "  " << std::setw(14) << std::fixed << std::setprecision(2) << tput
                << "  " << std::setw(11) << std::fixed << std::setprecision(2) << avgDelay);

            totalTx    += s.txPackets;
            totalRx    += s.rxPackets;
            totalDelay += avgDelay;
            nFlows++;
        }

        NS_LOG_UNCOND (std::string(80, '-'));
        NS_LOG_UNCOND ("Total flows: " << nFlows
            << "  TxPkts: " << totalTx
            << "  RxPkts: " << totalRx
            << "  AvgDelay: " << (nFlows ? totalDelay/nFlows : 0) << " ms");
    }

    Simulator::Destroy ();
    return 0;
}