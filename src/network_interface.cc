#include "network_interface.hh"

#include "arp_message.hh"
#include "exception.hh"
#include "helpers.hh"

#include <algorithm>
#include <iostream>

using namespace std;

namespace {
constexpr uint64_t ARP_REQUEST_TIMEOUT = 5000;
constexpr uint64_t ARP_CACHE_TIMEOUT = 30000;

EthernetFrame make_arp_frame( const EthernetAddress& source,
                              const EthernetAddress& destination,
                              const ARPMessage& arp )
{
  return { .header = { .dst = destination, .src = source, .type = EthernetHeader::TYPE_ARP },
           .payload = serialize( arp ) };
}
} // namespace

NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{}

void NetworkInterface::send_arp_request( const uint32_t target_ip )
{
  const ARPMessage request { .opcode = ARPMessage::OPCODE_REQUEST,
                              .sender_ethernet_address = ethernet_address_,
                              .sender_ip_address = ip_address_.ipv4_numeric(),
                              .target_ethernet_address = {},
                              .target_ip_address = target_ip };
  transmit( make_arp_frame( ethernet_address_, ETHERNET_BROADCAST, request ) );
  arp_requests_[target_ip] = current_time_;
}

void NetworkInterface::send_datagram( InternetDatagram dgram, const Address& next_hop )
{
  const uint32_t next_hop_ip = next_hop.ipv4_numeric();
  const auto cache_entry = arp_cache_.find( next_hop_ip );
  if ( cache_entry != arp_cache_.end() && current_time_ - cache_entry->second.learned_at < ARP_CACHE_TIMEOUT ) {
    transmit( { .header = { .dst = cache_entry->second.ethernet_address,
                            .src = ethernet_address_,
                            .type = EthernetHeader::TYPE_IPv4 },
                .payload = serialize( dgram ) } );
    return;
  }

  arp_cache_.erase( next_hop_ip );
  waiting_[next_hop_ip].push_back( { .datagram = move( dgram ), .created_at = current_time_ } );
  const auto request = arp_requests_.find( next_hop_ip );
  if ( request == arp_requests_.end() || current_time_ - request->second >= ARP_REQUEST_TIMEOUT ) {
    send_arp_request( next_hop_ip );
  }
}

void NetworkInterface::learn( const uint32_t ip, const EthernetAddress& ethernet_address )
{
  arp_cache_[ip] = { .ethernet_address = ethernet_address, .learned_at = current_time_ };
  arp_requests_.erase( ip );
  flush_waiting( ip );
}

void NetworkInterface::flush_waiting( const uint32_t target_ip )
{
  const auto cache_entry = arp_cache_.find( target_ip );
  auto pending = waiting_.find( target_ip );
  if ( cache_entry == arp_cache_.end() || pending == waiting_.end() ) {
    return;
  }

  for ( auto& item : pending->second ) {
    transmit( { .header = { .dst = cache_entry->second.ethernet_address,
                            .src = ethernet_address_,
                            .type = EthernetHeader::TYPE_IPv4 },
                .payload = serialize( item.datagram ) } );
  }
  waiting_.erase( pending );
}

void NetworkInterface::recv_frame( EthernetFrame frame )
{
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram datagram;
    if ( parse( datagram, move( frame.payload ) ) ) {
      datagrams_received_.push( move( datagram ) );
    }
    return;
  }

  if ( frame.header.type != EthernetHeader::TYPE_ARP ) {
    return;
  }

  ARPMessage arp;
  if ( !parse( arp, move( frame.payload ) ) ) {
    return;
  }

  learn( arp.sender_ip_address, arp.sender_ethernet_address );
  if ( arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == ip_address_.ipv4_numeric() ) {
    const ARPMessage reply { .opcode = ARPMessage::OPCODE_REPLY,
                             .sender_ethernet_address = ethernet_address_,
                             .sender_ip_address = ip_address_.ipv4_numeric(),
                             .target_ethernet_address = arp.sender_ethernet_address,
                             .target_ip_address = arp.sender_ip_address };
    transmit( make_arp_frame( ethernet_address_, arp.sender_ethernet_address, reply ) );
  }
}

void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  current_time_ += ms_since_last_tick;

  for ( auto waiting = waiting_.begin(); waiting != waiting_.end(); ) {
    auto& pending = waiting->second;
    while ( !pending.empty() && current_time_ - pending.front().created_at >= ARP_REQUEST_TIMEOUT ) {
      pending.pop_front();
    }
    if ( pending.empty() ) {
      waiting = waiting_.erase( waiting );
    } else {
      ++waiting;
    }
  }

  for ( auto it = arp_cache_.begin(); it != arp_cache_.end(); ) {
    if ( current_time_ - it->second.learned_at >= ARP_CACHE_TIMEOUT ) {
      it = arp_cache_.erase( it );
    } else {
      ++it;
    }
  }

  for ( auto it = arp_requests_.begin(); it != arp_requests_.end(); ) {
    if ( current_time_ - it->second >= ARP_REQUEST_TIMEOUT ) {
      it = arp_requests_.erase( it );
    } else {
      ++it;
    }
  }
}
