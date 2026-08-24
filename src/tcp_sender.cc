
#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const { return bytes_in_flight_; }
uint64_t TCPSender::consecutive_retransmissions() const { return consecutive_retx_; }

void TCPSender::push(const TransmitFunction& transmit) {
  // === 1. 检查是否有错误需要发送 RST ===
  if (reader().has_error() && !rst_sent_) {
    TCPSenderMessage rst_msg = make_empty_message();
    rst_msg.RST = true;  // 显式设置 RST
    transmit(rst_msg);
    rst_sent_ = true;

    // 设置 writer 错误状态
    writer().set_error();

    // abort 后清理状态
    outstanding_.clear();
    bytes_in_flight_ = 0;
    timer_running_ = false;
    timer_ms_ = 0;
    consecutive_retx_ = 0;
    rto_ms_ = initial_RTO_ms_;

    return;  // 不再发送任何正常段
  }

  // === 2. 正常发送逻辑 ===
  // 窗口为0时，将其视为1进行零窗口探测
  uint64_t effective_win = std::max<uint64_t>(1ULL, peer_window_);

  while (bytes_in_flight_ < effective_win) {
    uint64_t remaining_space = effective_win - bytes_in_flight_;

    TCPSenderMessage seg = make_empty_message();

    // SYN（仅第一次）
    if (next_abs_ == 0) {
      seg.SYN = true;
      remaining_space -= 1;
    }

    // 填充 payload
    std::string_view avail = reader().peek();
    size_t take = std::min({remaining_space, TCPConfig::MAX_PAYLOAD_SIZE, avail.size()});
    if (take > 0) {
      seg.payload = std::string(avail.substr(0, take));
      reader().pop(take);
      remaining_space -= take;
    }

    // FIN
    if (reader().is_finished() && remaining_space >= 1 && !fin_sent_) {
      seg.FIN = true;
      fin_sent_ = true;
    }

    // 零窗口探测保护：如果窗口为0，我们至少发送一个字节
    if (seg.sequence_length() == 0) {
      break;
    }

    transmit(seg);
    outstanding_.push_back(seg);
    bytes_in_flight_ += seg.sequence_length();
    next_abs_ += seg.sequence_length();

    if (!timer_running_) {
      timer_running_ = true;
      timer_ms_ = 0;
    }
    
    // 如果窗口为0，我们只发送一个探测包后就退出
    if (peer_window_ == 0) {
      break;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg{};
  msg.seqno = Wrap32::wrap(next_abs_, isn_);
  msg.RST = reader().has_error();
  return msg;
}

void TCPSender::receive(const TCPReceiverMessage& msg) {
  peer_window_ = msg.window_size;

  if (msg.RST) {
    writer().set_error();
    return;
  }

  if (!msg.ackno.has_value()) {
    return;
  }

  const uint64_t ack_abs = msg.ackno->unwrap(isn_, acked_abs_);

  if (ack_abs > next_abs_ || ack_abs <= acked_abs_) {
    return;
  }

  const uint64_t old_ack_abs = acked_abs_;
  uint64_t newly_acked = ack_abs - old_ack_abs;
  while (newly_acked > 0 && !outstanding_.empty()) {
    auto& seg = outstanding_.front();
    const size_t seg_len = seg.sequence_length();

    if (newly_acked >= seg_len) {
      newly_acked -= seg_len;
      bytes_in_flight_ -= seg_len;
      outstanding_.pop_front();
      continue;
    }

    // An ACK may split a segment. Keep only the sequence space that remains
    // outstanding so retransmission and the advertised window stay accurate.
    size_t consumed = static_cast<size_t>(newly_acked);
    if (seg.SYN && consumed > 0) {
      seg.SYN = false;
      --consumed;
    }

    const size_t payload_consumed = std::min(consumed, seg.payload.size());
    seg.payload.erase(0, payload_consumed);
    consumed -= payload_consumed;
    if (consumed > 0) {
      seg.FIN = false;
    }

    seg.seqno = Wrap32::wrap(ack_abs, isn_);
    bytes_in_flight_ -= newly_acked;
    newly_acked = 0;
  }

  acked_abs_ = ack_abs;
  if (ack_abs > old_ack_abs) {
    consecutive_retx_ = 0;
    rto_ms_ = initial_RTO_ms_;
    timer_ms_ = 0;
    timer_running_ = !outstanding_.empty();
  }
}

void TCPSender::tick(const size_t ms_since_last_tick, const TransmitFunction& transmit) {
  if (!timer_running_) return;

  timer_ms_ += ms_since_last_tick;

  if (outstanding_.empty()) {
    timer_running_ = false;
    timer_ms_ = 0;
    return;
  }

  if (timer_ms_ >= rto_ms_) {
    transmit(outstanding_.front());

    if (peer_window_ > 0) {
      rto_ms_ *= 2;
      consecutive_retx_++;
    }

    timer_ms_ = 0;
  }
}
