#include "tcp_sender.hh"
#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <algorithm>
#include <asm-generic/errno.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>

using namespace std;

//*******************************//

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return _outstanding_bytes;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return _consecutive_retxs;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // fill the window
  while ( _outstanding_bytes < _receiverMsg.window_size ) {
    TCPSenderMessage msg;

    if ( input_.has_error() ) {
      msg.RST = true;
    }

    // 1.check if SYN was sent
    if ( !_is_syned ) {
      _is_syned = true;
      msg.SYN = true;
      msg.seqno = isn_;
    } else {
      msg.seqno = Wrap32::wrap( _abs_seqno, isn_ );
    }

    // 2.set the length of bytestream that can be read
    size_t len = min(
      min( static_cast<size_t>( _receiverMsg.window_size - _outstanding_bytes ), TCPConfig::MAX_PAYLOAD_SIZE ),
      static_cast<size_t>( reader().bytes_buffered() ) );

    // 3.extract from the input ByteStream
    read( input_.reader(), len, msg.payload );

    // 4.check if eof of the input ByteStream, and is there still extra window size for adding the FIN flag
    if ( reader().is_finished() == true && msg.sequence_length() + _outstanding_bytes < _receiverMsg.window_size ) {
      if ( _is_fin == false ) {
        _is_fin = true;
        msg.FIN = true;
      }
    }

    // 5. push msg to the two sets (_segment_out & _outstanding_segments)
    if ( msg.sequence_length() == 0 ) {
      break;
    } else {
      _segment_out.push_back( msg );
      _outstanding_segments.push_back( msg );
      _outstanding_bytes += msg.sequence_length();
    }

    // 6. update absolute sequence number in TCPsender
    _abs_seqno += msg.sequence_length();

    // 7. transmit the packaged message
    transmit( msg );
    if ( _isStartTimer == false ) {
      _isStartTimer = true;
    }
  }
}

// send_empty_message函数意思是发一个消息，告诉接收器，发送者这边的abs_seqno到哪里了。
TCPSenderMessage TCPSender::make_empty_message() const
{
  // Your code here.
  TCPSenderMessage segment;
  segment.seqno = Wrap32::wrap( _abs_seqno, isn_ );
  if ( input_.has_error() ) {
    segment.RST = true;
  }
  return segment;
}

// 收到接收者发来的消息。先设置窗口大小，方便push的时候控制从流中提取的数据的长度（这就是流量控制的本质）。
// 然后根据ackno，删除outstanding集合中缓存的已经确认的段。
// 一旦有删除操作执行，就重置RTO计时器。
void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  _receiverMsg = msg;

  if ( msg.RST ) {
    input_.set_error();
  }

  // Update the window_size for next sending.
  // This will keep sending a single byte segment which might be rejected or acknowledged by the recevier,
  // but this can also provoke the recevier into sending a new acknowledgment segment where it reveals that
  // more space has opened up in its window
  if ( _receiverMsg.window_size == 0 ) {
    _receiverMsg.window_size = 1;
  }
  _primitive_window_size = msg.window_size; // 保留这个可能变化的值的原始值，用来判断是否执行“指数退避”

  if ( msg.ackno.has_value() == true ) { // ackno有值才需要删除确认的段
    if ( msg.ackno.value().unwrap( isn_, _abs_seqno ) > _abs_seqno ) {
      return;
    }
    // 删除任何现在已经完全确认的段
    while ( _outstanding_bytes != 0
            && _outstanding_segments.front().seqno.unwrap( isn_, _abs_seqno )
                   + _outstanding_segments.front().sequence_length()
                 <= msg.ackno.value().unwrap( isn_, _abs_seqno ) ) {

      // 当ackno越过“已发送但未确认”队列中某个元素的右边界时，删除这个元素
      _outstanding_bytes -= _outstanding_segments.front().sequence_length();
      _outstanding_segments.pop_front();

      // 有未完成的段被确认时，(即outstanding集合发生pop)才会设置RTO
      // outstanding集合为空(全部未完成的段都完全确认)，则停止计时，重置计时器;否则(当仍有没确认的段时)重启计时器，继续计时
      if ( _outstanding_bytes == 0 ) {
        _isStartTimer = false;
      } else {
        _isStartTimer = true;
      }
      _consecutive_retxs = 0;

      _cur_RTO_ms = initial_RTO_ms_;
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // Your code here.
  if ( _isStartTimer ) {
    _cur_RTO_ms -= ms_since_last_tick;
  }

  if ( _cur_RTO_ms <= 0 ) {
    // retransmit
    _segment_out.push_front( _outstanding_segments.front() );
    transmit( _segment_out.front() );
    _consecutive_retxs++;

    if ( _primitive_window_size > 0 ) {
      _cur_RTO_ms = pow( 2, _consecutive_retxs ) * initial_RTO_ms_;
    } else {
      _cur_RTO_ms = initial_RTO_ms_;
    }
  }
}
