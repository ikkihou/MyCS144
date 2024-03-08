#include "wrapping_integers.hh"
#include <cstdint>

using namespace std;

/**
 * @brief Seqno->AboSeqno
 *
 * @param n Sequence number in unsigned long type
 * @param zero_point The Initial Sequence Number ISN
 * @return Wrap32
 */
Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  return zero_point + static_cast<uint32_t>( n );
}

/**
 * @brief AboSeq->Seqno
 *
 */
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  int32_t offset = raw_value_ - wrap( checkpoint, zero_point ).raw_value_;
  int64_t res = checkpoint + offset;

  return res >= 0 ? static_cast<uint64_t>( res ) : res + ( 1ul << 32 );
  return res;
}
