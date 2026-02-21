
#include "reassembler.hh"
#include "debug.hh"

#include <algorithm>
#include <map>
#include <string>

using namespace std;
void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  const uint64_t head = next_expected_;
  const size_t cap = output_.writer().available_capacity();
  const uint64_t window_start = head;
  const uint64_t window_end = head + cap;

  if ( is_last_substring ) {
    fin_received_ = true;
    fin_index_ = first_index + data.size();
  }

  if ( data.empty() ) {
    flush_assembled();
    return;
  }

  uint64_t seg_start = first_index;
  uint64_t seg_end = first_index + data.size();

  if ( seg_end <= window_start ) {
    flush_assembled();
    return;
  }

  if ( seg_start >= window_end ) {
    flush_assembled();
    return;
  }

  // 剪裁到窗口
  seg_start = std::max(seg_start, window_start);
  seg_end   = std::min(seg_end,   window_end);

  if ( seg_start >= seg_end ) {
    flush_assembled();
    return;
  }

  const uint64_t cut_left = seg_start - first_index;
  const size_t   cut_len  = static_cast<size_t>(seg_end - seg_start);
  string new_data = data.substr(cut_left, cut_len);

  uint64_t new_start = seg_start;
  uint64_t new_end   = seg_end;

  // === 关键修改：只合并严格重叠的块（不合并相邻），避免 holes 场景边界 bug ===
  uint64_t merged_start = new_start;
  uint64_t merged_end   = new_end;

  // 找可能重叠的最左块（lower_bound 是第一个 >= new_start）
  auto it = unassembled_.lower_bound(new_start);
  if ( it != unassembled_.begin() ) {
    auto prev = std::prev(it);
    // 严格重叠：prev.end > new_start
    if ( prev->first + prev->second.size() > new_start ) {
      merged_start = prev->first;
      merged_end   = std::max(merged_end, prev->first + prev->second.size());
      it = prev;
    }
  }
  auto begin_merge = it;

  // 向右扩展：只有严格重叠 (s < merged_end) 才继续合并
  for ( ; it != unassembled_.end(); ++it ) {
    const uint64_t s = it->first;
    if ( s >= merged_end ) break;  // 相邻或 gap 都停止（不合并相邻）
    merged_end = std::max(merged_end, s + it->second.size());
  }
  auto end_merge = it;

  // 构造合并 buffer
  const size_t m_len = static_cast<size_t>(merged_end - merged_start);
  string merged_buf(m_len, '\0');  // 临时填0，实际会被完全覆盖（因为只合并重叠，无 hole）

  uint64_t old_bytes = 0;
  for ( auto j = begin_merge; j != end_merge; ++j ) {
    const size_t offset = static_cast<size_t>(j->first - merged_start);
    std::copy(j->second.begin(), j->second.end(), merged_buf.begin() + offset);
    old_bytes += j->second.size();
  }

  const size_t new_off = static_cast<size_t>(new_start - merged_start);
  std::copy(new_data.begin(), new_data.end(), merged_buf.begin() + new_off);

  // 更新 pending
  bytes_pending_ -= old_bytes;
  bytes_pending_ += m_len;

  // 删除旧块，插入新块
  unassembled_.erase(begin_merge, end_merge);
  if ( m_len > 0 ) {
    unassembled_.emplace(merged_start, std::move(merged_buf));
  }

  flush_assembled();
}

uint64_t Reassembler::count_bytes_pending() const
{
  return bytes_pending_;
}

void Reassembler::flush_assembled()
{
  while ( true ) {
    auto it = unassembled_.find( next_expected_ );
    if ( it == unassembled_.end() )
      break;

    size_t avail = output_.writer().available_capacity();
    if ( avail == 0 )
      break;

    const string& seg = it->second;
    size_t n = min( avail, seg.size() );

    output_.writer().push( seg.substr( 0, n ) );
    bytes_pending_ -= n;
    next_expected_ += n;

    if ( n == seg.size() ) {
      unassembled_.erase( it );
    } else {
      string rest = seg.substr( n );
      uint64_t new_start = next_expected_;
      unassembled_.erase( it );
      unassembled_.emplace( new_start, std::move( rest ) );
      break;  // 部分推送后不可能再有连续块
    }
  }

  // 统一检查 FIN
  if ( fin_received_ && next_expected_ == fin_index_ ) {
    output_.writer().close();
  }
}




