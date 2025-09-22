// Stream iterators

// Copyright (C) 2001, 2004, 2005 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file stream_iterator.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _STREAM_ITERATOR_H
#define _STREAM_ITERATOR_H 1

#pragma GCC system_header

#include <debug/debug.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  /// Provides input iterator semantics for streams.
  template<typename _Tp, typename _CharT = char,
           typename _Traits = char_traits<_CharT>, typename _Dist = ptrdiff_t>
    class istream_iterator
    : public iterator<input_iterator_tag, _Tp, _Dist, const _Tp*, const _Tp&>
    {
    public:
      typedef _CharT                         char_type;
      typedef _Traits                        traits_type;
      typedef basic_istream<_CharT, _Traits> istream_type;

    private:
      istream_type*	_M_stream;
      _Tp		_M_value;
      bool		_M_ok;

    public:
      ///  Construct end of input stream iterator.
      istream_iterator()
      : _M_stream(0), _M_value(), _M_ok(false) {}

      ///  Construct start of input stream iterator.
      istream_iterator(istream_type& __s)
      : _M_stream(&__s)
      { _M_read(); }

      istream_iterator(const istream_iterator& __obj)
      : _M_stream(__obj._M_stream), _M_value(__obj._M_value),
        _M_ok(__obj._M_ok)
      { }

      const _Tp&
      operator*() const
      {
	__glibcxx_requires_cond(_M_ok,
				_M_message(__gnu_debug::__msg_deref_istream)
				._M_iterator(*this));
	return _M_value;
      }

      const _Tp*
      operator->() const { return &(operator*()); }

      istream_iterator&
      operator++()
      {
	__glibcxx_requires_cond(_M_ok,
				_M_message(__gnu_debug::__msg_inc_istream)
				._M_iterator(*this));
	_M_read();
	return *this;
      }

      istream_iterator
      operator++(int)
      {
	__glibcxx_requires_cond(_M_ok,
				_M_message(__gnu_debug::__msg_inc_istream)
				._M_iterator(*this));
	istream_iterator __tmp = *this;
	_M_read();
	return __tmp;
      }

      bool
      _M_equal(const istream_iterator& __x) const
      { return (_M_ok == __x._M_ok) && (!_M_ok || _M_stream == __x._M_stream); }

    private:
      void
      _M_read()
      {
	_M_ok = (_M_stream && *_M_stream) ? true : false;
	if (_M_ok)
	  {
	    *_M_stream >> _M_value;
	    _M_ok = *_M_stream ? true : false;
	  }
      }
    };

  ///  Return true if x and y are both end or not end, or x and y are the same.
  template<typename _Tp, typename _CharT, typename _Traits, typename _Dist>
    inline bool
    operator==(const istream_iterator<_Tp, _CharT, _Traits, _Dist>& __x,
	       const istream_iterator<_Tp, _CharT, _Traits, _Dist>& __y)
    { return __x._M_equal(__y); }

  ///  Return false if x and y are both end or not end, or x and y are the same.
  template <class _Tp, class _CharT, class _Traits, class _Dist>
    inline bool
    operator!=(const istream_iterator<_Tp, _CharT, _Traits, _Dist>& __x,
	       const istream_iterator<_Tp, _CharT, _Traits, _Dist>& __y)
    { return !__x._M_equal(__y); }

  /**
   *  @brief  Provides output iterator semantics for streams.
   *
   *  This class provides an iterator to write to an ostream.  The type Tp is
   *  the only type written by this iterator and there must be an
   *  operator<<(Tp) defined.
   *
   *  @param  Tp  The type to write to the ostream.
   *  @param  CharT  The ostream char_type.
   *  @param  Traits  The ostream char_traits.
  */
  template<typename _Tp, typename _CharT = char,
           typename _Traits = char_traits<_CharT> >
    class ostream_iterator
    : public iterator<output_iterator_tag, void, void, void, void>
    {
    public:
      //@{
      /// Public typedef
      typedef _CharT                         char_type;
      typedef _Traits                        traits_type;
      typedef basic_ostream<_CharT, _Traits> ostream_type;
      //@}

    private:
      ostream_type*	_M_stream;
      const _CharT*	_M_string;

    public:
      /// Construct from an ostream.
      ostream_iterator(ostream_type& __s) : _M_stream(&__s), _M_string(0) {}

      /**
       *  Construct from an ostream.
       *
       *  The delimiter string @a c is written to the stream after every Tp
       *  written to the stream.  The delimiter is not copied, and thus must
       *  not be destroyed while this iterator is in use.
       *
       *  @param  s  Underlying ostream to write to.
       *  @param  c  CharT delimiter string to insert.
      */
      ostream_iterator(ostream_type& __s, const _CharT* __c)
      : _M_stream(&__s), _M_string(__c)  { }

      /// Copy constructor.
      ostream_iterator(const ostream_iterator& __obj)
      : _M_stream(__obj._M_stream), _M_string(__obj._M_string)  { }

      /// Writes @a value to underlying ostream using operator<<.  If
      /// constructed with delimiter string, writes delimiter to ostream.
      ostream_iterator&
      operator=(const _Tp& __value)
      {
	__glibcxx_requires_cond(_M_stream != 0,
				_M_message(__gnu_debug::__msg_output_ostream)
				._M_iterator(*this));
	*_M_stream << __value;
	if (_M_string) *_M_stream << _M_string;
	return *this;
      }

      ostream_iterator&
      operator*()
      { return *this; }

      ostream_iterator&
      operator++()
      { return *this; }

      ostream_iterator&
      operator++(int)
      { return *this; }
    };

_GLIBCXX_END_NAMESPACE

#endif
