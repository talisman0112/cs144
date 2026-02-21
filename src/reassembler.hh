#pragma once
#include <map>
#include "byte_stream.hh"

class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  explicit Reassembler( ByteStream&& output )
  : output_( std::move( output ) )
  , unassembled_()
  , next_expected_( 0 )
  , fin_received_( false )
  , fin_index_( 0 )
{}

  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring );
  // How many bytes are stored in the Reassembler itself?
  // This function is for testing only; don't add extra state to support it.
  uint64_t count_bytes_pending() const;

  // Access output stream reader
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  const Writer& writer() const { return output_.writer(); }

private:
  ByteStream output_;
  std::map<uint64_t, std::string> unassembled_; // 按起始下标缓存尚未写出的分片
  uint64_t next_expected_ = 0;                  // 下一个期望写入的字节下标
  bool fin_received_ = false;                   // 是否收到 FIN
  uint64_t fin_index_ = 0; 
  void flush_assembled();
  uint64_t bytes_pending_ {0};                       // 声明内部用的辅助函数                     // FIN 所在的下标（即最后一个字节之后的位置）
};
