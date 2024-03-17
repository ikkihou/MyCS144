#include "router.hh"
#include "address.hh"
// #include "address.hh"
// #include "ipv4_datagram.hh"

#include <bit>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <queue>
#include <ranges>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  // routing_table_[prefix_length][rotr( route_prefix, 32 - prefix_length )] = { interface_num, next_hop };
  _router_table.emplace_back( route_prefix, prefix_length, next_hop, interface_num );
}

void Router::route_one_dgram( InternetDatagram& dgram )
{
  const uint32_t dst_ip_addr = dgram.header.dst;
  auto max_matched_entry = _router_table.end();

  for ( auto router_entry_iter = _router_table.begin(); router_entry_iter != _router_table.end();
        router_entry_iter++ ) {
    if ( router_entry_iter->prefix_length == 0
         || ( router_entry_iter->route_prefix ^ dst_ip_addr ) >> ( 32 - router_entry_iter->prefix_length ) == 0 ) {
      if ( max_matched_entry == _router_table.end()
           || max_matched_entry->prefix_length < router_entry_iter->prefix_length ) {
        max_matched_entry = router_entry_iter;
      }
    }
  }

  if ( max_matched_entry != _router_table.end() && dgram.header.ttl-- > 1 ) {
    dgram.header.compute_checksum(); // 大坑
    const optional<Address> next_hop = max_matched_entry->next_hop;
    auto& next_interface = _interfaces.at( max_matched_entry->interface_num );

    if ( next_hop.has_value() ) {
      next_interface->send_datagram( dgram, next_hop.value() );
    } else {
      next_interface->send_datagram( dgram, Address::from_ipv4_numeric( dst_ip_addr ) );
    }
  }
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( const auto& interface : _interfaces ) {
    auto&& queue = interface->datagrams_received();
    while ( not queue.empty() ) {
      route_one_dgram( queue.front() );
      queue.pop();
    }
  }
}

// // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
// void Router::route()
// {
//   for ( const auto& interface : _interfaces ) {
//     auto&& datagrams_received { interface->datagrams_received() };
//     while ( not datagrams_received.empty() ) {
//       InternetDatagram datagram { move( datagrams_received.front() ) };
//       datagrams_received.pop();

//       if ( datagram.header.ttl <= 1 ) {
//         continue;
//       }
//       datagram.header.ttl -= 1;
//       datagram.header.compute_checksum();

//       const optional<info>& mp { match( datagram.header.dst ) };
//       if ( not mp.has_value() ) {
//         continue;
//       }
//       const auto& [num, next_hop] { mp.value() };
//       _interfaces[num]->send_datagram( datagram,
//                                        next_hop.value_or( Address::from_ipv4_numeric( datagram.header.dst ) ) );
//     }
//   }
// }

// [[nodiscard]] auto Router::match( uint32_t addr ) const noexcept -> optional<info>
// {
//   auto adaptor = views::filter( [&addr]( const auto& mp ) { return mp.contains( addr >>= 1 ); } )
//                  | views::transform( [&addr]( const auto& mp ) -> info { return mp.at( addr ); } );
//   auto res { routing_table_ | views::reverse | adaptor | views::take( 1 ) }; // just kidding
//   return res.empty() ? nullopt : optional<info> { res.front() };
// }