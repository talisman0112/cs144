#include "tcp_receiver.hh"
#include "debug.hh"
#include <algorithm>
#include <cstdint>
using namespace std;
void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reader().set_error();
    return;
  }
  if ( !syn_seen_ ) {
    if ( !message.SYN ) return;
    syn_seen_ = true;
    isn_ = message.seqno;
  } else if ( message.SYN ) {
    // SYN is only meaningful while establishing the receiver's stream.
    return;
  }
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1;
  const uint64_t abs_seq = message.seqno.unwrap( *isn_, checkpoint );
  if ( !message.SYN && abs_seq == 0 ) return;
  const uint64_t payload_abs_seq = abs_seq + ( message.SYN ? 1 : 0 );
  const uint64_t stream_index = payload_abs_seq - 1;
  reassembler_.insert( stream_index, std::move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage msg {};
  const uint64_t avail = reassembler_.writer().available_capacity();
  msg.window_size = static_cast<uint16_t>( std::min<uint64_t>( avail, UINT16_MAX ) );

  msg.RST = reader().has_error();

  if ( !syn_seen_ ) {
    msg.ackno = std::nullopt;
    return msg;
  }
  uint64_t ack_abs = 1 + reassembler_.writer().bytes_pushed();
  if ( reassembler_.writer().is_closed() )
  ack_abs += 1;
  msg.ackno = Wrap32::wrap( ack_abs, *isn_ );
  return msg;
}
