#include "tcp_receiver.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <optional>

using namespace std;

/**
 * @brief There are 3 states of the TCPReceiver:
 *        1) LISTEN, SYN packet hasn't arrived yet, determined by the _set_syn_flag;
 *
 *        2) SYN_RECV, SYN packer has arrived, determined by whether in the state 1 or 3
 *
 *        3) FIN_RECV, FIN packet has arrived, determined by the _set_fin_flag
 * @param message TCP packet
 */
void TCPReceiver::receive( TCPSenderMessage message )
{
  // process the RST flag within the message
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  // process the SYN flag
  if ( !_set_syn_flag ) {
    // all packet must be dropped off before the TCP connection was constructed
    if ( !message.SYN ) {
      return;
    } else {
      _isn = message.seqno; // get the initial sequence number
      _set_syn_flag = true; // set the SYN flag
    }
  }

  uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1; // the index of the first unassembled byte
  uint64_t curr_abs_seqno = message.seqno.unwrap( _isn, checkpoint );

  uint64_t stream_idx = curr_abs_seqno - 1 + message.SYN;
  reassembler_.insert( stream_idx, message.payload, message.FIN );
}

optional<Wrap32> TCPReceiver::ackno() const
{

  // check if the TCPRecevier stays in the LISTEM state
  if ( !_set_syn_flag ) {
    return nullopt;
  }

  // if the TCPRecevier not in the LISTEN state
  uint64_t abs_ackno = reassembler_.writer().bytes_pushed() + 1;
  if ( reassembler_.writer().is_closed() ) {
    ++abs_ackno;
  }
  return Wrap32( _isn ) + abs_ackno;
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage RECVMessage {};

  RECVMessage.window_size = reassembler_.writer().available_capacity() >= UINT16_MAX
                              ? UINT16_MAX
                              : reassembler_.writer().available_capacity();

  RECVMessage.RST = reassembler_.writer().has_error() ? true : false;

  RECVMessage.ackno = ackno();

  return RECVMessage;
}
