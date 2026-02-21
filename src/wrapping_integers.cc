#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  uint32_t squno=static_cast<uint32_t>(n+zero_point.raw_value_);
  return Wrap32{squno};
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  const uint32_t x = raw_value_;
  const uint32_t z = zero_point.raw_value_;
  const uint32_t offset = x - z;
  const uint64_t TWO32 = uint64_t{1} << 32;
  uint64_t base = checkpoint & ~uint64_t{0xffffffff};
  uint64_t candidate = base + offset;
  if ( candidate + (TWO32 / 2) <= checkpoint ) {
    candidate += TWO32;
  } else if ( candidate > checkpoint + (TWO32 / 2) ) {
    if ( candidate >= TWO32 ) {
      candidate -= TWO32;
    }
  }

  return candidate;
}
