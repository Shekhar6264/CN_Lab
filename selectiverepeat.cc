#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/seq-ts-header.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SelectiveRepeatARQ");

class SelectiveRepeatSender
{
public:
  SelectiveRepeatSender(Ptr<Socket> socket, uint32_t windowSize, Time timeout)
    : m_socket(socket), m_windowSize(windowSize), m_timeout(timeout), m_nextSeq(0), m_base(0) {}

  void SendPacket();
  void AckReceived(uint32_t ackSeq);
  void Timeout(uint32_t seq);

private:
  Ptr<Socket> m_socket;
  uint32_t m_windowSize;
  Time m_timeout;
  uint32_t m_nextSeq;
  uint32_t m_base;
  std::map<uint32_t, EventId> m_timers;
};

void SelectiveRepeatSender::SendPacket()
{
  if (m_nextSeq < m_base + m_windowSize)
    {
      Ptr<Packet> packet = Create<Packet>(100);
      SeqTsHeader header;
      header.SetSeq(m_nextSeq);
      packet->AddHeader(header);

      m_socket->Send(packet);
      NS_LOG_INFO("Sent packet seq=" << m_nextSeq);

      if (m_timers.find(m_nextSeq) == m_timers.end() || !m_timers[m_nextSeq].IsPending())
        {
          m_timers[m_nextSeq] = Simulator::Schedule(m_timeout, &SelectiveRepeatSender::Timeout, this, m_nextSeq);
        }

      m_nextSeq++;
    }
}

void SelectiveRepeatSender::AckReceived(uint32_t ackSeq)
{
  NS_LOG_INFO("Received ACK for seq=" << ackSeq);

  auto it = m_timers.find(ackSeq);
  if (it != m_timers.end() && it->second.IsPending())
    {
      it->second.Cancel();
    }

  if (ackSeq == m_base)
    {
      m_base++;
    }
}

void SelectiveRepeatSender::Timeout(uint32_t seq)
{
  NS_LOG_INFO("Timeout for seq=" << seq << ", retransmitting...");

  Ptr<Packet> packet = Create<Packet>(100);
  SeqTsHeader header;
  header.SetSeq(seq);
  packet->AddHeader(header);

  m_socket->Send(packet);

  m_timers[seq] = Simulator::Schedule(m_timeout, &SelectiveRepeatSender::Timeout, this, seq);
}

class SelectiveRepeatReceiver
{
public:
  SelectiveRepeatReceiver(Ptr<Socket> socket, uint32_t windowSize)
    : m_socket(socket), m_windowSize(windowSize), m_expectedSeq(0) {}

  void HandleRead(Ptr<Socket> socket);

private:
  Ptr<Socket> m_socket;
  uint32_t m_windowSize;
  uint32_t m_expectedSeq;
  std::set<uint32_t> m_buffer;
};

void SelectiveRepeatReceiver::HandleRead(Ptr<Socket> socket)
{
  Ptr<Packet> packet = socket->Recv();
  SeqTsHeader header;
  packet->RemoveHeader(header);
  uint32_t seq = header.GetSeq();

  NS_LOG_INFO("Receiver got packet seq=" << seq);

  if (seq == m_expectedSeq)
    {
      m_expectedSeq++;
      while (m_buffer.find(m_expectedSeq) != m_buffer.end())
        {
          m_buffer.erase(m_expectedSeq);
          m_expectedSeq++;
        }
    }
  else if (seq > m_expectedSeq)
    {
      m_buffer.insert(seq);
    }

  Ptr<Packet> ack = Create<Packet>(10);
  SeqTsHeader ackHeader;
  ackHeader.SetSeq(seq);
  ack->AddHeader(ackHeader);
  socket->Send(ack);

  NS_LOG_INFO("Sent ACK for seq=" << seq);
}

int main(int argc, char *argv[])
{
  LogComponentEnable("SelectiveRepeatARQ", LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create(2);

  // ✅ Assign constant positions to avoid NetAnim warnings
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);
  nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(10, 25, 0));
  nodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(40, 25, 0));

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));

  NetDeviceContainer devices = p2p.Install(nodes);

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  uint16_t port = 8080;
  Ptr<Socket> senderSocket = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
  Ptr<Socket> receiverSocket = Socket::CreateSocket(nodes.Get(1), UdpSocketFactory::GetTypeId());

  InetSocketAddress remoteAddress(interfaces.GetAddress(1), port);
  senderSocket->Connect(remoteAddress);

  receiverSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));

  SelectiveRepeatSender sender(senderSocket, 4, MilliSeconds(200));
  SelectiveRepeatReceiver receiver(receiverSocket, 4);

  receiverSocket->SetRecvCallback(MakeCallback(&SelectiveRepeatReceiver::HandleRead, &receiver));

  Simulator::Schedule(Seconds(1), &SelectiveRepeatSender::SendPacket, &sender);
  Simulator::Schedule(Seconds(2), &SelectiveRepeatSender::SendPacket, &sender);
  Simulator::Schedule(Seconds(3), &SelectiveRepeatSender::SendPacket, &sender);
  Simulator::Schedule(Seconds(4), &SelectiveRepeatSender::SendPacket, &sender);

  AnimationInterface anim("selectiverepeat.xml");

  Simulator::Stop(Seconds(10));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

