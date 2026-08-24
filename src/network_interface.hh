#pragma once

#include "address.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <string_view>

// A network interface connecting IPv4 datagrams to an Ethernet segment.
class NetworkInterface
{
public:
  class OutputPort
  {
  public:
    virtual void transmit( const NetworkInterface& sender, const EthernetFrame& frame ) = 0;
    virtual ~OutputPort() = default;
  };

  NetworkInterface( std::string_view name,
                    std::shared_ptr<OutputPort> port,
                    const EthernetAddress& ethernet_address,
                    const Address& ip_address );

  void send_datagram( InternetDatagram dgram, const Address& next_hop );
  void recv_frame( EthernetFrame frame );
  void tick( size_t ms_since_last_tick );

  const std::string& name() const { return name_; }
  const OutputPort& output() const { return *port_; }
  OutputPort& output() { return *port_; }
  std::queue<InternetDatagram>& datagrams_received() { return datagrams_received_; }

private:
  struct PendingDatagram
  {
    InternetDatagram datagram;
    uint64_t created_at {};
  };

  struct ARPEntry
  {
    EthernetAddress ethernet_address {};
    uint64_t learned_at {};
  };

  std::string name_;
  std::shared_ptr<OutputPort> port_;
  EthernetAddress ethernet_address_;
  Address ip_address_;
  std::queue<InternetDatagram> datagrams_received_ {};
  std::map<uint32_t, ARPEntry> arp_cache_ {};
  std::map<uint32_t, std::deque<PendingDatagram>> waiting_ {};
  std::map<uint32_t, uint64_t> arp_requests_ {};
  uint64_t current_time_ {};

  void transmit( const EthernetFrame& frame ) const { port_->transmit( *this, frame ); }
  void send_arp_request( uint32_t target_ip );
  void flush_waiting( uint32_t target_ip );
  void learn( uint32_t ip, const EthernetAddress& ethernet_address );
};
