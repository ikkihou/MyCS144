#include "reassembler.hh"
#include <cstdint>
#include <iostream>
#include <set>

using namespace std;

void Reassembler::_buffer_erase( const std::set<Segment>::iterator& iter )
{
  _unassembled_bytes -= iter->length();
  _buffer.erase( iter );
}

void Reassembler::_buffer_insert( const Segment& seg )
{
  _unassembled_bytes += seg.length();
  _buffer.insert( seg );
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // Your code here.

  // process the input segment
  if ( !data.empty() ) {
    Segment seg { first_index, data };
    _handle_substring( seg );
  }

  // write to 'ByteStream'
  while ( !_buffer.empty() && _buffer.begin()->_idx == _1st_unassembled_idx() ) {
    const auto& iter = _buffer.begin();
    output_.writer().push( iter->_data );
    _buffer_erase( iter );
  }

  // EOF
  if ( is_last_substring ) {
    _is_eof = is_last_substring;
    _eof_idx = first_index + data.length();
  }
  if ( _is_eof && _1st_unassembled_idx() == _eof_idx ) {
    output_.writer().close();
  }
}

void Reassembler::_handle_substring( Segment& seg )
{

  /**
   * @brief If the index of first bytes in the seg exceeds the right boundary of the buffer, then drop the seg
   *
   *           index
   *         │  ├─────────────┐
   *   ──────┼──┴─────────────┴──►
   *       first
   *     unacceptable
   */
  if ( seg._idx >= _1st_unacceptable_idx() ) {
    return;
  }

  /**
   * @brief If the index of the tail of the substring in the seg exceeds the right boundary of the buffer, then
   * truncate the substring
   *
   *    index
   *      ├────────┼────┐
   *   ───┴────────┼────┴────►
   *             first
   *           unacceptable
   */
  if ( seg._idx < _1st_unacceptable_idx() && seg._idx + seg.length() - 1 >= _1st_unacceptable_idx() ) {
    seg._data = seg._data.substr( 0, _1st_unacceptable_idx() - seg._idx );
  }

  /**
   * @brief If the index of the tail of the substring in the seg within the left boundary of the buffer, which means
   * the seg has already been written into the bytestream, then drop the seg
   *
   *   index
   *     ├───────────┐  │
   *   ──┴───────────┴──┼─────────►
   *                  first
   *               unassembled
   */
  if ( seg._idx + seg.length() - 1 < _1st_unassembled_idx() ) {
    return;
  }

  /**
   * @brief If the index of the head of the substring in the seg within the left boundary of the buffer but the
   * index of the tail of the substring exceeds it, then trim the head part
   *
   *    index
   *      ├──────┼────┐
   *  ────┴──────┼────┴────►
   *           first
   *        unassembled
   */
  if ( seg._idx < _1st_unassembled_idx() && seg._idx + seg.length() - 1 >= _1st_unassembled_idx() ) {
    seg._data = seg._data.substr( _1st_unassembled_idx() - seg._idx );
    seg._idx = _1st_unassembled_idx();
  }

  if ( _buffer.empty() ) {
    _buffer_insert( seg );
    return;
  }

  _handle_overlapping( seg );
}

void Reassembler::_handle_overlapping( Segment& seg )
{
  for ( auto iter = _buffer.begin(); iter != _buffer.end(); ) {
    uint64_t seg_tail = seg._idx + seg.length() - 1;
    uint64_t cache_tail = iter->_idx + iter->length() - 1;

    if ( ( seg._idx >= iter->_idx && seg._idx <= cache_tail )
         || ( iter->_idx >= seg._idx && iter->_idx <= seg_tail ) ) {
      _merge_seg( seg, *iter );
      _buffer_erase( iter++ );
    } else {
      ++iter;
    }
  }

  /**
   * @brief data 与已经缓存的 segments 之间没有重叠，可以存入缓冲区
   *
   *             index     tail
   *     ┌─────┐  ├─────────┤   ┌────────┐
   *   ──┴─────┴──┴─────────┴───┴────────┴────►
   */
  _buffer_insert( seg );
}

void Reassembler::_merge_seg( Segment& seg, const Segment& cache )
{
  uint64_t seg_tail = seg._idx + seg.length() - 1;
  uint64_t cache_tail = cache._idx + cache.length() - 1;

  /**
   * @brief seg 尾部与已缓存的 segment 重合，裁切 seg 尾部再合并
   *
   *   seg           seg
   *   index         tail
   *     ├────────────┤             ┌──────────┐
   *     │////////////│             │//////////│
   *     └──────┬─────┴───┐         └─┬────────┤
   *            │         │           │        │
   *        ────┼─────────┼───────────┴────────┴───►
   *        stored   1   stored           2
   *    segment index   segment tail
   */
  if ( seg._idx < cache._idx && seg_tail <= cache_tail ) {
    seg._data = seg._data.substr( 0, cache._idx - seg._idx ) + cache._data;
  }

  /**
   * @brief seg 头部与已缓存的 segment 重合，裁切 seg 头部再合并
   *
   *               seg        seg
   *               index      tail
   *                 ├──────────┤      ┌─────────────┐
   *                 │//////////│      │/////////////│
   *             ┌───┴─────┬────┘      ├─────────┬───┘
   *             │         │           │         │
   *         ────┼─────────┼───────────┴─────────┴─────────────►
   *         stored   1    stored           2
   *     segment index   segment tail
   */
  else if ( seg._idx >= cache._idx && seg_tail > cache_tail ) {
    seg._data = cache._data + seg._data.substr( cache._idx + cache.length() - seg._idx );
    seg._idx = cache._idx;
  }

  /**
   * @brief seg 所包含的字节内容已经存在于缓冲区中。
   *
   *         seg       seg
   *         index     tail
   *           ├────────┤         ┌─────┐           ┌────┐  ┌───────┐
   *           │////////│         │/////│           │////│  │///////│
   *       ┌───┴────────┴───┐     ├─────┴────┐  ┌───┴────┤  ├───────┤
   *       │                │     │          │  │        │  │       │
   *     ──┼────────────────┼─────┴──────────┴──┴────────┴──┴───────┴───►
   *    stored      1     stored       2            3          4
   *   segment index    segment tail
   */
  else if ( seg._idx >= cache._idx && seg_tail <= cache_tail ) {
    seg._data = cache._data;
    seg._idx = cache._idx;
  }

  /**
   * @brief seg 中部与已经缓存的 segment 重合
   *
   *       seg               seg
   *       index             tail
   *         ├─────────────────┤   ┌─────────────────┐
   *         │/////////////////│   │/////////////////│
   *         └───┬─────────┬───┘   └─┬───┬───┬───┬───┘
   *             │         │         │   │   │   │
   *         ────┼─────────┼─────────┴───┴───┴───┴───►
   *         stored   1   stored           2
   *     segment index   segment tail
   */
  // else if (seg._idx < cache._idx && seg_tail > cache_tail) {
  // do nothing
  // }
}

uint64_t Reassembler::bytes_pending() const
{
  // Your code here.
  return _unassembled_bytes;
}
