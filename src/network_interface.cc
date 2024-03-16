#include <algorithm>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "ipv4_datagram.hh"
#include "network_interface.hh"
#include "parser.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // Your code here.
  const uint32_t next_hop_ip = next_hop.ipv4_numeric(); // ipv4 address of the next hop

  auto it = _arp_table.find( next_hop_ip );
  if ( it != _arp_table.end() ) {
    // 如果ARP表命中，则直接发送数据报
    EthernetFrame eth_frm;
    eth_frm.header.src = ethernet_address_;
    eth_frm.header.dst = it->second.eth_addr;
    eth_frm.header.type = EthernetHeader::TYPE_IPv4;
    eth_frm.payload = serialize( dgram );
    transmit( eth_frm );
  } else {
    // arp表未命中，且最近没有对该ip发送过ARP查询数据，则发送该查询报文
    if ( _waiting_arp_response_ip_addr.find( next_hop_ip ) == _waiting_arp_response_ip_addr.end() ) {
      ARPMessage arp_msg;
      arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
      arp_msg.sender_ethernet_address = ethernet_address_;
      arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
      arp_msg.target_ethernet_address = {};
      arp_msg.target_ip_address = next_hop_ip;

      EthernetFrame eth_frm;
      eth_frm.header.type = EthernetHeader::TYPE_ARP;
      eth_frm.header.src = ethernet_address_;
      eth_frm.header.dst = ETHERNET_BROADCAST;
      eth_frm.payload = serialize( arp_msg );
      transmit( eth_frm );
      _waiting_arp_response_ip_addr[next_hop_ip] = ARP_RESPONSE_TTL_MS;
    }
    _waiting_internet_datagrams[next_hop_ip].emplace_back( next_hop, dgram );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Your code here.
  // 如果不是broadcast帧或者MAC地址不是本机，则直接丢弃
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    // 解析IPV4数据包并推入
    InternetDatagram ret;
    vector<string> buffers;
    // 检查解析是否有误
    if ( parse( ret, frame.payload ) ) {
      datagrams_received_.push( ret );
    } else {
      return;
    }
  }

  if ( frame.header.type == EthernetHeader::TYPE_ARP ) { // 解析ARP数据包
    ARPMessage arp_msg;
    // 检查解析是否有误
    if ( parse( arp_msg, frame.payload ) ) {
      // 先在接收方设置映射，即映射发送方的ip地址对应发送方自己的MAC地址
      // 在这里arp_msg是请求，他的sender是发送方
      const uint32_t my_ip = ip_address_.ipv4_numeric();
      const uint32_t src_ip = arp_msg.sender_ip_address;

      // 如果是发给本机的arp请求
      if ( arp_msg.opcode == ARPMessage::OPCODE_REQUEST && arp_msg.target_ip_address == my_ip ) {
        ARPMessage arp_reply;

        arp_reply.opcode = ARPMessage::OPCODE_REPLY;
        arp_reply.sender_ethernet_address = ethernet_address_;
        arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
        arp_reply.sender_ip_address = my_ip;
        arp_reply.target_ip_address = src_ip;

        EthernetFrame eth_frm;
        eth_frm.header.type = EthernetHeader::TYPE_ARP;
        eth_frm.header.src = ethernet_address_;
        eth_frm.header.dst = arp_msg.sender_ethernet_address;
        eth_frm.payload = serialize( arp_reply );
        transmit( eth_frm );
      }
      // 从 ARP 报文中学习新的 ARP 表项（即使不是发给我的也可以学，比如广播但目标 IP 不是本机）
      _arp_table[src_ip] = ARPEntry { arp_msg.sender_ethernet_address, ARP_ENTRY_TTL_MS };

      // 如果该 IP 地址有等待发送的数据报，则全部发送出去
      auto it = _waiting_internet_datagrams.find( src_ip );
      if ( it != _waiting_internet_datagrams.end() ) {
        for ( const auto& [next_hop, dgram] : it->second ) {
          EthernetFrame eth_frm;
          eth_frm.header.src = ethernet_address_;
          eth_frm.header.dst = arp_msg.sender_ethernet_address;
          eth_frm.header.type = EthernetHeader::TYPE_IPv4;
          eth_frm.payload = serialize( dgram );
          transmit( eth_frm );
        }
        _waiting_internet_datagrams.erase( it );
      }
    } else {
      return;
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Your code here.
  for ( auto it = _arp_table.begin(); it != _arp_table.end(); ) {
    if ( it->second.ttl <= ms_since_last_tick ) {
      it = _arp_table.erase( it );
    } else {
      it->second.ttl -= ms_since_last_tick;
      it = std::next( it );
    }
  }

  for ( auto it = _waiting_arp_response_ip_addr.begin(); it != _waiting_arp_response_ip_addr.end(); ) {
    if ( it->second <= ms_since_last_tick ) {
      auto it2 = _waiting_internet_datagrams.find( it->first );
      if ( it2 != _waiting_internet_datagrams.end() ) {
        _waiting_internet_datagrams.erase( it2 );
      }
      it = _waiting_arp_response_ip_addr.erase( it );
    } else {
      it->second -= ms_since_last_tick;
      it = std::next( it );
    }
  }
}
