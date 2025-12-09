#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("GoBackNExample");

// ---------------- Sender (Go-Back-N) ----------------
class GoBackNSender : public Application {
public:
  GoBackNSender();
  virtual ~GoBackNSender();

  void Setup(Ptr<Socket> socket, Address address,
             uint32_t packetSize, uint32_t nPackets,
             uint32_t windowSize, Time timeout);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void SendPacket(uint32_t seq);
  void Timeout(uint32_t seq);
  void HandleRead(Ptr<Socket> socket);
  void SendWindow();

  Ptr<Socket> m_socket;
  Address m_peerAddress;
  uint32_t m_packetSize;
  uint32_t m_nPackets;
  uint32_t m_packetsSent;
  uint32_t m_windowSize;
  Time m_timeout;

  uint32_t m_base;   // oldest unacknowledged packet
  uint32_t m_nextSeq; // next seq number to send

  std::map<uint32_t, EventId> m_timers;
};

GoBackNSender::GoBackNSender()
    : m_socket(0), m_packetSize(0), m_nPackets(0),
      m_packetsSent(0), m_windowSize(0),
      m_base(0), m_nextSeq(0) {}

GoBackNSender::~GoBackNSender() {
  m_socket = 0;
}

void GoBackNSender::Setup(Ptr<Socket> socket, Address address,
                          uint32_t packetSize, uint32_t nPackets,
                          uint32_t windowSize, Time timeout) {
  m_socket = socket;
  m_peerAddress = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_windowSize = windowSize;
  m_timeout = timeout;
}

void GoBackNSender::StartApplication(void) {
  m_socket->Connect(m_peerAddress);
  m_socket->SetRecvCallback(MakeCallback(&GoBackNSender::HandleRead, this));
  SendWindow();
}

void GoBackNSender::StopApplication(void) {
  if (m_socket) {
    m_socket->Close();
  }
  for (auto &kv : m_timers) {
    Simulator::Cancel(kv.second);
  }
}

void GoBackNSender::SendWindow() {
  while (m_nextSeq < m_base + m_windowSize && m_nextSeq < m_nPackets) {
    SendPacket(m_nextSeq);
    m_nextSeq++;
  }
}

void GoBackNSender::SendPacket(uint32_t seq) {
  Ptr<Packet> packet = Create<Packet>(m_packetSize);

  SeqTsHeader header;
  header.SetSeq(seq);
  packet->AddHeader(header);

  m_socket->Send(packet);
  NS_LOG_INFO("Sender: Sent packet Seq=" << seq);

  // Start timer if not already running
  if (m_timers.find(seq) == m_timers.end()) {
    m_timers[seq] = Simulator::Schedule(m_timeout, &GoBackNSender::Timeout, this, seq);
  }
}

void GoBackNSender::Timeout(uint32_t seq) {
  if (seq >= m_base) {
    NS_LOG_INFO("Sender: Timeout, retransmitting from Seq=" << m_base);
    // Cancel timers
    for (auto &kv : m_timers) {
      Simulator::Cancel(kv.second);
    }
    m_timers.clear();
    // Retransmit window
    m_nextSeq = m_base;
    SendWindow();
  }
}

void GoBackNSender::HandleRead(Ptr<Socket> socket) {
  Ptr<Packet> packet = socket->Recv();

  SeqTsHeader header;
  packet->RemoveHeader(header);
  uint32_t ackSeq = header.GetSeq();

  NS_LOG_INFO("Sender: Received ACK for Seq=" << ackSeq);

  if (ackSeq >= m_base) {
    m_base = ackSeq + 1;
    // Cancel timers for acknowledged packets
    for (auto it = m_timers.begin(); it != m_timers.end();) {
      if (it->first <= ackSeq) {
        Simulator::Cancel(it->second);
        it = m_timers.erase(it);
      } else {
        ++it;
      }
    }
    SendWindow();
  }
}

// ---------------- Receiver (Go-Back-N) ----------------
class GoBackNReceiver : public Application {
public:
  GoBackNReceiver();
  virtual ~GoBackNReceiver();

  void Setup(Ptr<Socket> socket);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void HandleRead(Ptr<Socket> socket);

  Ptr<Socket> m_socket;
  uint32_t m_expectedSeq;
};

GoBackNReceiver::GoBackNReceiver() : m_socket(0), m_expectedSeq(0) {}

GoBackNReceiver::~GoBackNReceiver() {
  m_socket = 0;
}

void GoBackNReceiver::Setup(Ptr<Socket> socket) {
  m_socket = socket;
}

void GoBackNReceiver::StartApplication(void) {
  m_socket->SetRecvCallback(MakeCallback(&GoBackNReceiver::HandleRead, this));
}

void GoBackNReceiver::StopApplication(void) {
  if (m_socket) {
    m_socket->Close();
  }
}

void GoBackNReceiver::HandleRead(Ptr<Socket> socket) {
  Ptr<Packet> packet = socket->Recv();

  SeqTsHeader header;
  packet->RemoveHeader(header);
  uint32_t seq = header.GetSeq();

  if (seq == m_expectedSeq) {
    NS_LOG_INFO("Receiver: Got expected packet Seq=" << seq);
    m_expectedSeq++;
  } else {
    NS_LOG_INFO("Receiver: Got out-of-order packet Seq=" << seq << " (expected " << m_expectedSeq << ")");
  }

  // Send ACK for last in-order packet
  Ptr<Packet> ack = Create<Packet>(10);
  SeqTsHeader ackHeader;
  ackHeader.SetSeq(m_expectedSeq - 1);
  ack->AddHeader(ackHeader);
  socket->Send(ack);

  NS_LOG_INFO("Receiver: Sent ACK=" << (m_expectedSeq - 1));
}

// ---------------- Main ----------------
int main(int argc, char *argv[]) {
  LogComponentEnable("GoBackNExample", LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create(2);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));

  NetDeviceContainer devices = p2p.Install(nodes);

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");

  // Receiver
  Ptr<Socket> recvSocket = Socket::CreateSocket(nodes.Get(1), tid);
  InetSocketAddress recvAddr = InetSocketAddress(Ipv4Address::GetAny(), 8080);
  recvSocket->Bind(recvAddr);
  recvSocket->Connect(InetSocketAddress(interfaces.GetAddress(0), 8081));

  Ptr<GoBackNReceiver> receiverApp = CreateObject<GoBackNReceiver>();
  receiverApp->Setup(recvSocket);
  nodes.Get(1)->AddApplication(receiverApp);
  receiverApp->SetStartTime(Seconds(0.0));
  receiverApp->SetStopTime(Seconds(20.0));

  // Sender
  Ptr<Socket> sendSocket = Socket::CreateSocket(nodes.Get(0), tid);
  InetSocketAddress senderAddr = InetSocketAddress(Ipv4Address::GetAny(), 8081);
  sendSocket->Bind(senderAddr);
  sendSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());

  Ptr<GoBackNSender> senderApp = CreateObject<GoBackNSender>();
  senderApp->Setup(sendSocket, InetSocketAddress(interfaces.GetAddress(1), 8080),
                   1024, 10, 4, Seconds(2.0));
  nodes.Get(0)->AddApplication(senderApp);
  senderApp->SetStartTime(Seconds(1.0));
  senderApp->SetStopTime(Seconds(20.0));

  // NetAnim
  AnimationInterface anim("gobackn-netanim.xml");
  anim.SetConstantPosition(nodes.Get(0), 10, 20);
  anim.SetConstantPosition(nodes.Get(1), 50, 20);
  anim.UpdateNodeDescription(nodes.Get(0), "Sender");
  anim.UpdateNodeDescription(nodes.Get(1), "Receiver");
  anim.EnablePacketMetadata(true);

  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

