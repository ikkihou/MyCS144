#pragma once

#include "byte_stream.hh"
#include <cstdint>
#include <set>
#include <string>

class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  explicit Reassembler( ByteStream&& output ) : output_( std::move( output ) ) {}

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
  uint64_t bytes_pending() const;

  // Access output stream reader
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  const Writer& writer() const { return output_.writer(); }

private:
  struct Segment
  {
    uint64_t _idx;
    std::string _data;

    Segment() : _idx( 0 ), _data() {}
    Segment( uint64_t index, const std::string& data ) : _idx( index ), _data( data ) {}

    uint64_t length() const { return _data.length(); }

    /**
     * @brief Overloaded less than operator for comparing Segment objects.
     *
     * This function defines the comparison operation between two Segment objects
     * based on their _idx member variables. It is used to determine the order of
     * Segment objects when stored in a std::set or other associative containers.
     *
     * @param seg The Segment object to compare against.
     * @return true if the current Segment's _idx is less than seg's _idx, false otherwise.
     */
    bool operator<( const Segment& seg ) const { return this->_idx < seg._idx; }
  };

  ByteStream output_;                      // the Reassembler writes to this ByteStream
  uint64_t _capacity = output_.capacity(); // the capacity of the Reassembler

  uint64_t _unassembled_bytes = 0; // unassembled but stored bytes
  bool _is_eof = false;
  uint64_t _eof_idx = 0;

  std::set<Segment> _buffer {};
  void _buffer_erase( const std::set<Segment>::iterator& iter );
  void _buffer_insert( const Segment& seg );

  // handle out-of-order and overlapping substrings, try to put them in the cache
  void _handle_substring( Segment& seg );
  void _handle_overlapping( Segment& seg );
  void _merge_seg( Segment& seg, const Segment& cache );

  uint64_t _1st_unread_idx() const { return output_.reader().bytes_popped(); }      // initial value is 0
  uint64_t _1st_unassembled_idx() const { return output_.writer().bytes_pushed(); } // initial value is 0
  uint64_t _1st_unacceptable_idx() const
  {
    return _1st_unread_idx() + _capacity;
  } // inital value is the full capacity of output_
};
