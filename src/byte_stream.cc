#include "byte_stream.hh"
#include "debug.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

// Push data to stream, but only as much as available capacity allows.
void Writer::push( string data )
{
  if(is_closed()){
    return;
  }
  size_t remain=available_capacity();
  const size_t len=std::min(remain,data.size());
  if(len==0){
    return;
  }
  buffer_.append( data.data(), len );
  bytes_pushed_ += len;
}


// Signal that the stream has reached its ending. Nothing more will be written.
void Writer::close()
{
  closed_=true;
}

// Has the stream been closed?
bool Writer::is_closed() const
{
  return closed_; // Your code here.
}

// How many bytes can be pushed to the stream right now?
uint64_t Writer::available_capacity() const
{
  return capacity_-buffer_.size(); // Your code here.
}

// Total number of bytes cumulatively pushed to the stream
uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_; // Your code here.
}

// Peek at the next bytes in the buffer -- ideally as many as possible.
// It's not required to return a string_view of the *whole* buffer, but
// if the peeked string_view is only one byte at a time, it will probably force
// the caller to do a lot of extra work.
string_view Reader::peek() const
{
  if ( buffer_.empty() ) {
  return {};
  }
  return std::string_view( buffer_.data(), buffer_.size() );
}

// Remove `len` bytes from the buffer.
void Reader::pop( uint64_t len )
{
  const uint64_t n = std::min<uint64_t>( len, buffer_.size() );
  if ( n == 0 ) {
    return;
  }
  buffer_.erase( 0, n );
  bytes_popped_ += n;
}

// Is the stream finished (closed and fully popped)?
bool Reader::is_finished() const
{
  return closed_&&buffer_.empty(); // Your code here.
}

// Number of bytes currently buffered (pushed and not popped)
uint64_t Reader::bytes_buffered() const
{
  return buffer_.length(); // Your code here.
}

// Total number of bytes cumulatively popped from stream
uint64_t Reader::bytes_popped() const
{
  return bytes_popped_; // Your code here.
}
