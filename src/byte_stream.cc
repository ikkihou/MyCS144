#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  // Your code here.
  return _input_ended_flag;
}

void Writer::push( string data )
{
  // Your code here.
  uint64_t data_length = data.length();
  if ( is_closed() || data.empty() ) {
    return;
  }

  if ( available_capacity() < data_length ) {
    data_length = available_capacity();
  }

  _buffered_bytes += data_length;
  _written_cnt += data_length;
  _buffer += data.substr( 0, data_length );
}

void Writer::close()
{
  // Your code here.
  _input_ended_flag = true;
}

uint64_t Writer::available_capacity() const
{
  // Your code here.
  return capacity_ - _buffered_bytes;
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return _written_cnt;
}

bool Reader::is_finished() const
{
  // Your code here.
  return _input_ended_flag == true && _buffered_bytes == 0;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return _read_cnt;
}

string_view Reader::peek() const
{
  // Your code here.
  return _buffer;
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  if ( len == 0 ) {
    return;
  }
  len = min( len, _buffer.length() );
  _buffer.erase( 0, len );
  _buffered_bytes -= len;
  _read_cnt += len;
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return _buffered_bytes;
}

void ByteStream::set_error()
{
  _error = true;
};