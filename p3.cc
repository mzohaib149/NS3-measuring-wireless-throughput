/*
 *ECE 6110 - Lab 3
 * Justin Eng
 * Muhammad Zohaib 
 */

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <vector>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-sink.h"
#include "ns3/log.h"

//Wireless
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/aodv-helper.h"
#include "ns3/olsr-helper.h"
#include "ns3/dsr-helper.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("Lab3_WirelessThroughput");

unsigned int bytesTransmitted = 0;


//Need to use a callback in order to total the tx bytes
void pktTxHandler(Ptr<const Packet> pkt) {
  bytesTransmitted += pkt->GetSize(); //Add size of packet to total
}


int main(int argc, char *argv[]) {
  //Define parameters
  unsigned int nodeCount = 20;
  unsigned int lanArea = 1000;  
  double txPower = 1.0;          //default 1 mW
  double trafficIntensity = 0.1; //default 0.1
  unsigned int pktSize = 32;  
  string phyMode ("DsssRate1Mbps");
  string routeProtocol ("AODV");     //Default AODV (or OSLR)   
  //string networkRate = "5.5Mb/s";     //Taken from avg 802.11b value
  double networkRate = 1000000; //1mbps datarate

  //Set config options (frag, phymode, onoff packetsize)
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", 
                      StringValue("2200"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", 
                      StringValue(phyMode));
  Config::SetDefault ("ns3::OnOffApplication::PacketSize", 
                      UintegerValue(pktSize));

  //Parse cmd line
  CommandLine cmd;
  cmd.AddValue("nodeCount", "total # of nodes", nodeCount);
  cmd.AddValue("area", "size of area [in m^2]", lanArea);
  cmd.AddValue("txPower", "transmit power of each node [mW]", txPower);
  cmd.AddValue("routeProtocol", "AODV or OLSR", routeProtocol);
  cmd.AddValue("trafficIntensity", "desired sendrate / max sendrate", trafficIntensity);
  cmd.Parse(argc, argv);

  //*************************************
  //*  TOPOLOGY SETUP
  //************************************* 
  //Seed PRNG
  RngSeedManager::SetSeed(11223344);

  //Stringify area size
  string str_area = static_cast<ostringstream*>(&(ostringstream()<<lanArea))->str();

  //Randomly assign node positions using mobility
  NodeContainer nodes;
  nodes.Create(nodeCount);
  MobilityHelper mobility;
  string xpos = "ns3::UniformRandomVariable[Min=0.0|Max=" + str_area + "]";
  string ypos = "ns3::UniformRandomVariable[Min=0.0|Max=" + str_area + "]";
  mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                "X", StringValue(xpos),
                                "Y", StringValue(ypos));
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel"); //Not moving
  mobility.Install(nodes);

  //Configure Wireless PHY
  double decibels = 10 * log10(txPower); //phy helper expects dbm not mW
  WifiHelper wifi;
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();   
  wifiPhy.Set("TxPowerStart", DoubleValue(decibels));
  wifiPhy.Set("TxPowerEnd", DoubleValue(decibels));
  wifiPhy.Set("RxGain", DoubleValue(0));  //I am assuming we don't have any gain
  wifiPhy.Set("TxGain", DoubleValue(0));

  //Configure channel
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
  wifiPhy.SetChannel(wifiChannel.Create());

  //Configure MAC
  NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default();
  wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(phyMode),
                               "ControlMode", StringValue(phyMode));
  wifiMac.SetType("ns3::AdhocWifiMac");  

  //Install all and create adhoc wireless network
  NetDeviceContainer wifiDevices = wifi.Install (wifiPhy, wifiMac, nodes);

  //Assign routing protocol
  Ipv4ListRoutingHelper list;
  InternetStackHelper stack;
  if (routeProtocol == "AODV") {
    AodvHelper aodv;
    list.Add (aodv, nodeCount);
    stack.SetRoutingHelper(list);
  } else if (routeProtocol == "OLSR") {
    OlsrHelper olsr;
    list.Add (olsr, nodeCount);
    stack.SetRoutingHelper(list);
  } else {
    printf("Invalid protocol option\r\n");
  }
  stack.Install(nodes);

  //Assign IPs to all nodes
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer interfaces;
  interfaces = address.Assign(wifiDevices); 
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  //*****************************************
  //*  SIMULATION SETUP
  //*****************************************

  double maxSeconds = 5;
  uint32_t port = 9;
  ApplicationContainer sourceApps;
  ApplicationContainer sinkApps;
  unsigned int peerNode[nodeCount]; //Store the target peer for each node

  //For byte counts
  vector<Ptr<PacketSink> > pktSinks;
  vector<Ptr<OnOffApplication> > pktSources;

  //Create sender-receiver pairs and applications
  for (unsigned int i = 0; i < nodeCount; i++) {
    //Randomly choose a destination node
    unsigned int peer;
    Ptr<UniformRandomVariable> dst = CreateObject<UniformRandomVariable>();
    dst->SetAttribute("Min", DoubleValue(0));
    dst->SetAttribute("Max", DoubleValue(nodeCount-1));
    while ((peer = dst->GetValue()) == i);
    peerNode[i] = peer;
    
    //Create a onoff udp source
    Ptr<Node> sourceNode = nodes.Get(i);
    Ptr<Node> sinkNode = nodes.Get(peerNode[i]);
    OnOffHelper sender("ns3::UdpSocketFactory", InetSocketAddress(interfaces.GetAddress(peerNode[i]),
                          port));
    double sendRate = (trafficIntensity * networkRate) / (double)nodeCount;
    //printf("%f\r\n", sendRate);
    sender.SetConstantRate(DataRate(sendRate), pktSize);
    string str_intensity = static_cast<ostringstream*>(&(ostringstream()<<trafficIntensity))->str();
 
    //Adjust cycle timing to get desired intensity
    //string onTimeStr = "1.0";//str_intensity;
    //string offTimeStr = "0.0"; //static_cast<ostringstream*>(&(ostringstream()<<(1.0 - trafficIntensity)))->str();
    //sender.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=" + onTimeStr + "]"));
    //sender.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=" + offTimeStr + "]"));

    //Install the app on the sender
    sourceApps.Add(sender.Install(sourceNode));
    sourceApps.Start(Seconds(0.0));
    sourceApps.Stop(Seconds(maxSeconds)); 
    pktSources.push_back(DynamicCast<OnOffApplication>(sourceApps.Get(i)));
   
    //Create a sink application on the peer node
    PacketSinkHelper receiver("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    sinkApps.Add(receiver.Install(sinkNode));
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(maxSeconds));
    pktSinks.push_back(DynamicCast<PacketSink>(sinkApps.Get(i)));
  }

  //Register callback on packet TX
  for (unsigned int i = 0; i < nodeCount; i++) {
    Ptr<OnOffApplication> src = pktSources.at(i);
    src->TraceConnectWithoutContext("Tx", MakeCallback(&pktTxHandler));
  }

  //Run simulation
  Simulator::Stop(Seconds(maxSeconds));
  Simulator::Run();

  //Tally data
  unsigned int bytesReceived = 0;
  for (unsigned int i = 0; i < nodeCount; i++) {
    Ptr<PacketSink> sink = pktSinks.at(i);
    bytesReceived += sink->GetTotalRx();   
  }

  //Output
  //cout << "Received " << bytesReceived << endl;
  //cout << "Sent " << bytesTransmitted << endl;
  double efficiency = (double)bytesReceived/(double)bytesTransmitted;
  cout << "protocol," << routeProtocol << ",area," << lanArea << ",nodes," << nodeCount \
       << ",txpower," << txPower << ",intensity," << trafficIntensity \
       << ",efficiency," << efficiency << ",tx," << bytesTransmitted << ",rx," << bytesReceived << endl; 
}




