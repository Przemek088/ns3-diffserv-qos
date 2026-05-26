#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/fifo-queue-disc.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/prio-queue-disc.h"
#include "ns3/traffic-control-module.h"
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DiffServFullExample");

int main(int argc, char *argv[]) {
    uint32_t efQueueSize = 100;
    uint32_t afQueueSize = 400;
    uint32_t beQueueSize = 500;

    CommandLine cmd;
    cmd.AddValue("ef", "EF queue size in packets", efQueueSize);
    cmd.AddValue("af", "AF queue size in packets", afQueueSize);
    cmd.AddValue("be", "BE queue size in packets", beQueueSize);
    cmd.Parse(argc, argv);

    // Creating nodes
    NodeContainer clients, servers;
    clients.Create(3); // VoIP, Video, FTP
    servers.Create(3);
    NodeContainer edge, core;
    edge.Create(1);
    core.Create(1);

    // Links
    PointToPointHelper p2pClient;
    p2pClient.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2pClient.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper p2pEdgeCore;
    p2pEdgeCore.SetDeviceAttribute("DataRate",
                                    StringValue("7Mbps")); // Bottleneck
    p2pEdgeCore.SetChannelAttribute("Delay", StringValue("5ms"));

    PointToPointHelper p2pServer;
    p2pServer.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2pServer.SetChannelAttribute("Delay", StringValue("2ms"));

    InternetStackHelper stack;
    stack.InstallAll();

    // Connections: Clients - Edge
    NetDeviceContainer devCli[3];
    for (int i = 0; i < 3; ++i) {
        NodeContainer pair(clients.Get(i), edge.Get(0));
        devCli[i] = p2pClient.Install(pair);
    }

    // Connection Edge - Core (queue will be here)
    NetDeviceContainer devEdgeCore =
        p2pEdgeCore.Install(edge.Get(0), core.Get(0));

    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::PrioQueueDisc");

    QueueDiscContainer qdiscs = tch.Install(devEdgeCore.Get(0));
    Ptr<QueueDisc> prio = qdiscs.Get(0);
    Ptr<PrioQueueDisc> realPrio = DynamicCast<PrioQueueDisc>(prio);
    realPrio->SetBandForPriority(11, 0); // EF
    realPrio->SetBandForPriority(8, 1);  // AF41
    realPrio->SetBandForPriority(0, 2);  // BE

    for (uint16_t band = 0; band < 3; ++band) {
        Ptr<QueueDiscClass> cls = CreateObject<QueueDiscClass>();
        Ptr<FifoQueueDisc> fifo = CreateObject<FifoQueueDisc>();
        if ( band == 0 ) {
            fifo->SetMaxSize(QueueSize(QueueSize(std::to_string(efQueueSize) + "p")));
        } else if ( band == 1 ) {
            fifo->SetMaxSize(QueueSize(QueueSize(std::to_string(afQueueSize) + "p")));
        } else {
            fifo->SetMaxSize(QueueSize(QueueSize(std::to_string(beQueueSize) + "p")));
        }
        cls->SetQueueDisc(fifo);
        prio->AddQueueDiscClass(cls);
        fifo->Initialize();
    }

    // Connections: Core - Servers
    NetDeviceContainer devSrv[3];
    for (int i = 0; i < 3; ++i) {
        NodeContainer pair(core.Get(0), servers.Get(i));
        devSrv[i] = p2pServer.Install(pair);
    }

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    std::vector<Ipv4InterfaceContainer> interfaces;

    for (int i = 0; i < 3; ++i) {
        std::string net = "10.1." + std::to_string(i) + ".0";
        ipv4.SetBase(net.c_str(), "255.255.255.0");
        interfaces.push_back(ipv4.Assign(devCli[i]));
    }

    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    interfaces.push_back(ipv4.Assign(devEdgeCore));

    for (int i = 0; i < 3; ++i) {
        std::string net = "10.1." + std::to_string(i + 4) + ".0";
        ipv4.SetBase(net.c_str(), "255.255.255.0");
        interfaces.push_back(ipv4.Assign(devSrv[i]));
    }

    // Routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // UDP Servers
    uint16_t ports[] = {8000, 8001, 8002}; // VoIP, Video, FTP
    ApplicationContainer sinks;
    for (int i = 0; i < 3; ++i) {
        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), ports[i]));
        sinks.Add(sink.Install(servers.Get(i)));
    }
    sinks.Start(Seconds(1.0));
    sinks.Stop(Seconds(9.0));

    // Clients: Creating sockets + setting DSCP
    uint8_t dscp[] = {0xB1, 0xB8, 0x88}; // {EF, AF41, BE}

    for (int i = 0; i < 3; ++i) {
        Ipv4Address dstAddr = interfaces[i + 4].GetAddress(1);
        Ptr<Socket> socket =
            Socket::CreateSocket(clients.Get(i), UdpSocketFactory::GetTypeId());
        socket->SetIpTos(dscp[i]);
        socket->Connect(InetSocketAddress(dstAddr, ports[i]));

        Simulator::Schedule(Seconds(2.0), [socket, i]() {
            for (int j = 0; j < 500; ++j) {
                Simulator::Schedule(Seconds(j * 0.001), [socket, i]() {
                    Ptr<Packet> packet = Create<Packet>(1024);

                    if (socket->Send(packet) == -1) {
                        std::cerr << "Warning: send failed for client " << i << "\n";
                    }
                });
            }
        });
    }

    // Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    // Mapping Flow ID to application name (VoIP / Video / FTP)
    std::map<FlowId, std::string> flowNames;
    double U_EF = -1, U_AF = -1, U_BE = -1; // unassigned scores

    for (const auto &flow : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        std::string appName;
        if (t.destinationPort == 8000)
            appName = "VoIP (EF)";
        else if (t.destinationPort == 8001)
            appName = "Video (AF41)";
        else if (t.destinationPort == 8002)
            appName = "FTP (BE)";
        else
            appName = "Unknown";

        std::cout << "- Flow [" << appName << "] ID: " << flow.first << " ("
                  << t.sourceAddress << " → " << t.destinationAddress << ")\n";
        std::cout << "   Tx Packets: " << flow.second.txPackets << "\n";
        std::cout << "   Rx Packets: " << flow.second.rxPackets << "\n";
        std::cout << "   Lost Packets: " << flow.second.lostPackets << "\n";

        double duration = flow.second.timeLastRxPacket.GetSeconds() -
                          flow.second.timeFirstTxPacket.GetSeconds();

        double throughput =
            (duration > 0) ? (flow.second.rxBytes * 8.0 / duration / 1024 / 1024)
                           : 0.0;

        std::cout << "   Throughput: " << throughput << " Mbps\n";

        double delay = -1, jitter = -1;
        if (flow.second.rxPackets > 0) {
            delay = flow.second.delaySum.GetSeconds() / flow.second.rxPackets;
            std::cout << "   Avg Delay: " << delay << " s\n";
            if (flow.second.rxPackets > 1) {
                jitter = flow.second.jitterSum.GetSeconds() /
                         (flow.second.rxPackets - 1);
                std::cout << "   Avg Jitter: " << jitter << " s\n";
            } else {
                std::cout << "   Avg Jitter: N/A (not enough packets)\n";
            }
        } else {
            std::cout << "   Avg Delay: N/A (no received packets)\n";
        }

        // QoS utility score calculation
        double lossRatio = (double)flow.second.lostPackets /
                           (flow.second.txPackets == 0 ? 1 : flow.second.txPackets);

        double U = 0;
        int count = 0;

        if (appName == "VoIP (EF)") {
            if (delay >= 0) {
                U += (delay <= 0.150) ? 1 : (delay <= 0.250 ? 0.8 : 0);
                count++;
            }
            U += (lossRatio == 0) ? 1 : 0;
            count++;
        } else if (appName == "Video (AF41)") {
            if (delay >= 0) {
                U += (delay <= 1.0) ? 1 : (delay <= 1.5 ? 0.8 : (delay <= 2.0 ? 0.5 : 0));
                count++;
            }
            U += (lossRatio == 0) ? 1 : (lossRatio <= 0.03 ? 0.8 : 0);
            count++;
        } else if (appName == "FTP (BE)") {
            U += (lossRatio <= 0.33) ? 1 : (lossRatio <= 0.5 ? 0.8 : (lossRatio <= 0.67 ? 0.5 : 0));
            count++;
        }

        double U_score = (count > 0) ? (U / count) : 0.0;

        std::cout << "   QoS Utility Score: " << U_score << "\n\n";

        if (appName == "VoIP (EF)") {
            U_EF = U_score;
        } else if (appName == "Video (AF41)") {
            U_AF = U_score;
        } else if (appName == "FTP (BE)") {
            U_BE = U_score;
        }
    }

    double QoS_score = 0.52 * U_EF + 0.32 * U_AF + 0.16 * U_BE;

    std::cout << "- Final aggregated QoS Score: " << QoS_score << " (0=worst, 1=best)\n";

    std::ofstream outFile("results", std::ios::app);
    outFile.is_open();
    outFile << efQueueSize << " " << afQueueSize << " " << beQueueSize << " " << QoS_score << "\n";
    outFile.close();

    Simulator::Destroy();
    return 0;
}
