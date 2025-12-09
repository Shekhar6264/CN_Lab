#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"  // <-- Added for NetAnim

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("StopAndWaitExampleWithNetAnim");

// ---------------- Sender Application ----------------
class StopAndWaitSender : public Application {
public:
  StopAndWaitSender();
  virtual ~StopAndWaitSender();

  void Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, Time timeout);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void SendPacket();
  void ScheduleTimeout();
  void Timeout();
  void HandleRead(Ptr<Socket> socket);

  Ptr<Socket> m_socket;
  Address m_peerAddress;
  uint32_t m_packetSize;
  uint32_t m_nPackets;
  uint32_t m_packetsSent;
  EventId m_timeoutEvent;
  Time m_timeout;
  bool m_waitingAck;
};

StopAndWaitSender::StopAndWaitSender()
    : m_socket(0), m_packetSize(0), m_nPackets(0), m_packetsSent(0), m_waitingAck(false) {}

StopAndWaitSender::~StopAndWaitSender() {
  m_socket = 0;
}

void StopAndWaitSender::Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, Time timeout) {
  m_socket = socket;
  m_peerAddress = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_timeout = timeout;
}

void StopAndWaitSender::StartApplication(void) {
  m_socket->Connect(m_peerAddress);
  m_socket->SetRecvCallback(MakeCallback(&StopAndWaitSender::HandleRead, this));
  SendPacket();
}

void StopAndWaitSender::StopApplication(void) {
  if (m_socket) {
    m_socket->Close();
  }
  Simulator::Cancel(m_timeoutEvent);
}

void StopAndWaitSender::SendPacket() {
  if (m_packetsSent >= m_nPackets) return;

  Ptr<Packet> packet = Create<Packet>(m_packetSize);
  m_socket->Send(packet);
  m_waitingAck = true;

  NS_LOG_INFO("Sent packet " << m_packetsSent);

  ScheduleTimeout();
}

void StopAndWaitSender::ScheduleTimeout() {
  m_timeoutEvent = Simulator::Schedule(m_timeout, &StopAndWaitSender::Timeout, this);
}

void StopAndWaitSender::Timeout() {
  if (m_waitingAck) {
    NS_LOG_INFO("Timeout for packet " << m_packetsSent << ", retransmitting");
    SendPacket(); // retransmit same packet
  }
}

void StopAndWaitSender::HandleRead(Ptr<Socket> socket) {
  Ptr<Packet> packet = socket->Recv();

  NS_LOG_INFO("Received ACK for packet " << m_packetsSent);

  m_waitingAck = false;
  Simulator::Cancel(m_timeoutEvent);

  m_packetsSent++;
  Simulator::Schedule(Seconds(1.0), &StopAndWaitSender::SendPacket, this); // schedule next packet
}

// ---------------- Receiver Application ----------------
class StopAndWaitReceiver : public Application {
public:
  StopAndWaitReceiver();
  virtual ~StopAndWaitReceiver();

  void Setup(Ptr<Socket> socket);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void HandleRead(Ptr<Socket> socket);

  Ptr<Socket> m_socket;
};

StopAndWaitReceiver::StopAndWaitReceiver() : m_socket(0) {}

StopAndWaitReceiver::~StopAndWaitReceiver() {
  m_socket = 0;
}

void StopAndWaitReceiver::Setup(Ptr<Socket> socket) {
  m_socket = socket;
}

void StopAndWaitReceiver::StartApplication(void) {
  m_socket->SetRecvCallback(MakeCallback(&StopAndWaitReceiver::HandleRead, this));
}

void StopAndWaitReceiver::StopApplication(void) {
  if (m_socket) {
    m_socket->Close();
  }
}

void StopAndWaitReceiver::HandleRead(Ptr<Socket> socket) {
  Ptr<Packet> packet = socket->Recv();

  NS_LOG_INFO("Received data packet, sending ACK");

  Ptr<Packet> ack = Create<Packet>(10); // small ACK
  socket->Send(ack);
}

// ---------------- Main Simulation ----------------
int main(int argc, char *argv[]) {
  LogComponentEnable("StopAndWaitExampleWithNetAnim", LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create(2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  pointToPoint.SetChannelAttribute("Delay", StringValue("10ms"));

  NetDeviceContainer devices = pointToPoint.Install(nodes);

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  // Create UDP sockets
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");

  // ---------------- Receiver ----------------
  Ptr<Socket> recvSocket = Socket::CreateSocket(nodes.Get(1), tid);
  InetSocketAddress recvLocalAddr = InetSocketAddress(Ipv4Address::GetAny(), 8080);
  recvSocket->Bind(recvLocalAddr);
  recvSocket->Connect(InetSocketAddress(interfaces.GetAddress(0), 8081));

  Ptr<StopAndWaitReceiver> receiverApp = CreateObject<StopAndWaitReceiver>();
  receiverApp->Setup(recvSocket);
  nodes.Get(1)->AddApplication(receiverApp);
  receiverApp->SetStartTime(Seconds(0.0));
  receiverApp->SetStopTime(Seconds(20.0));

  // ---------------- Sender ----------------
  Ptr<Socket> sendSocket = Socket::CreateSocket(nodes.Get(0), tid);
  InetSocketAddress senderLocalAddr = InetSocketAddress(Ipv4Address::GetAny(), 8081);
  sendSocket->Bind(senderLocalAddr);
  sendSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());

  Ptr<StopAndWaitSender> senderApp = CreateObject<StopAndWaitSender>();
  senderApp->Setup(sendSocket, InetSocketAddress(interfaces.GetAddress(1), 8080), 1024, 5, Seconds(2.0));
  nodes.Get(0)->AddApplication(senderApp);
  senderApp->SetStartTime(Seconds(1.0));
  senderApp->SetStopTime(Seconds(20.0));

  // ------------- NetAnim Setup -------------
  AnimationInterface anim("stopwait-netanim.xml");

  // Set node positions (x,y)
  anim.SetConstantPosition(nodes.Get(0), 10, 20);  // Sender at (10,20)
  anim.SetConstantPosition(nodes.Get(1), 50, 20);  // Receiver at (50,20)

  // Add labels on nodes
  anim.UpdateNodeDescription(nodes.Get(0), "Sender");
  anim.UpdateNodeDescription(nodes.Get(1), "Receiver");

  // Enable packet metadata animation
  anim.EnablePacketMetadata(true);

  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

