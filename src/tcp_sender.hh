#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms )
  {
    _receiverMsg.ackno = isn_;
    _receiverMsg.window_size = 1;
  }

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  // Variables initialized in constructor
  ByteStream input_;        // outgoing stream of bytes that have not been sent
  Wrap32 isn_;              // initial sequence number
  uint64_t initial_RTO_ms_; // retransmission timer for the connection

  int _cur_RTO_ms = initial_RTO_ms_;

  bool _isStartTimer { false };

  // 记录所有准备发送的段
  std::deque<TCPSenderMessage> _segment_out {};

  // 记录所有已发送但没有完成的段
  std::deque<TCPSenderMessage> _outstanding_segments {};

  // 重传次数
  uint32_t _consecutive_retxs { 0 };

  // 已发送但未被确认的字节流的长度
  uint64_t _outstanding_bytes { 0 };

  // 记录现在接收器返回给发送器的最新消息
  TCPReceiverMessage _receiverMsg {};

  // 记录push时，每一段的绝对序列号
  uint64_t _abs_seqno { 0 };

  uint16_t _primitive_window_size { 1 };

  // whether syned
  bool _is_syned { false };

  // whether finish
  bool _is_fin { false };
};
