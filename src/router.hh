#pragma once

#include "exception.hh"
#include "network_interface.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// A router that forwards IPv4 datagrams using longest-prefix matching.
class Router
{
public:
  size_t add_interface( std::shared_ptr<NetworkInterface> interface )
  {
    interfaces_.push_back( notnull( "add_interface", std::move( interface ) ) );
    return interfaces_.size() - 1;
  }

  std::shared_ptr<NetworkInterface> interface( const size_t N ) { return interfaces_.at( N ); }

  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );
  void route();

private:
  struct Route
  {
    uint32_t prefix {};
    uint8_t length {};
    std::optional<Address> next_hop {};
    size_t interface_num {};
  };

  std::vector<std::shared_ptr<NetworkInterface>> interfaces_ {};
  std::vector<Route> routes_ {};
};
