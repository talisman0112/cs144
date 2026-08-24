#include "router.hh"

#include "address.hh"

#include <algorithm>

using namespace std;

namespace {
uint32_t prefix_mask( const uint8_t length )
{
  if ( length == 0 ) {
    return 0;
  }
  if ( length == 32 ) {
    return UINT32_MAX;
  }
  return UINT32_MAX << ( 32 - length );
}
} // namespace

void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  if ( prefix_length > 32 ) {
    throw runtime_error( "route prefix length must be at most 32" );
  }
  if ( interface_num >= interfaces_.size() ) {
    throw runtime_error( "route references an unknown interface" );
  }

  const uint32_t mask = prefix_mask( prefix_length );
  routes_.push_back( { .prefix = route_prefix & mask,
                       .length = prefix_length,
                       .next_hop = next_hop,
                       .interface_num = interface_num } );
}

void Router::route()
{
  for ( const auto& source : interfaces_ ) {
    auto& received = source->datagrams_received();
    while ( !received.empty() ) {
      InternetDatagram datagram = move( received.front() );
      received.pop();

      if ( datagram.header.ttl <= 1 ) {
        continue;
      }

      const Route* best = nullptr;
      for ( const auto& candidate : routes_ ) {
        const uint32_t mask = prefix_mask( candidate.length );
        if ( ( datagram.header.dst & mask ) == candidate.prefix
             && ( best == nullptr || candidate.length > best->length ) ) {
          best = &candidate;
        }
      }

      if ( best == nullptr ) {
        continue;
      }

      --datagram.header.ttl;
      datagram.header.compute_checksum();
      const Address next_hop = best->next_hop.value_or( Address::from_ipv4_numeric( datagram.header.dst ) );
      interfaces_.at( best->interface_num )->send_datagram( move( datagram ), next_hop );
    }
  }
}
