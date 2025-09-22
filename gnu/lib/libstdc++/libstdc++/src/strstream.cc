// strstream definitions -*- C++ -*-

// Copyright (C) 2001, 2002, 2003 Free Software Foundation
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
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/*
 * Copyright (c) 1998
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

// Implementation of the classes in header <strstream>.
// WARNING: The classes defined in <strstream> are DEPRECATED.  This
// header is defined in section D.7.1 of the C++ standard, and it
// MAY BE REMOVED in a future standard revision.  You should use the
// header <sstream> instead.

#include <strstream>
#include <algorithm>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

namespace std
{
  strstreambuf::strstreambuf(streamsize initial_capacity)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(true), 
    _M_frozen(false), _M_constant(false)
  {
    streamsize n = max(initial_capacity, streamsize(16));
    
    char* buf = _M_alloc(n);
    if (buf) 
      {
	setp(buf, buf + n);
	setg(buf, buf, buf);
      }
  }

  strstreambuf::strstreambuf(void* (*alloc_f)(size_t), void (*free_f)(void*))
  : _Base(), _M_alloc_fun(alloc_f), _M_free_fun(free_f), _M_dynamic(true), 
    _M_frozen(false), _M_constant(false)
  {
    streamsize n = 16;

    char* buf = _M_alloc(n);
    if (buf) 
      {
	setp(buf, buf + n);
	setg(buf, buf, buf);
      }
  }

  strstreambuf::strstreambuf(char* get, streamsize n, char* put)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(false), 
    _M_frozen(false), _M_constant(false)
  { _M_setup(get, put, n); }

  strstreambuf::strstreambuf(signed char* get, streamsize n, signed char* put)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(false), 
  _M_frozen(false), _M_constant(false)
  { _M_setup(reinterpret_cast<char*>(get), reinterpret_cast<char*>(put), n); }

  strstreambuf::strstreambuf(unsigned char* get, streamsize n, 
			     unsigned char* put)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(false), 
    _M_frozen(false), _M_constant(false)
  { _M_setup(reinterpret_cast<char*>(get), reinterpret_cast<char*>(put), n); }

  strstreambuf::strstreambuf(const char* get, streamsize n)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(false), 
    _M_frozen(false), _M_constant(true)
  { _M_setup(const_cast<char*>(get), 0, n); }

  strstreambuf::strstreambuf(const signed char* get, streamsize n)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(false), 
    _M_frozen(false), _M_constant(true)
  { _M_setup(reinterpret_cast<char*>(const_cast<signed char*>(get)), 0, n); }

  strstreambuf::strstreambuf(const unsigned char* get, streamsize n)
  : _Base(), _M_alloc_fun(0), _M_free_fun(0), _M_dynamic(false), 
    _M_frozen(false), _M_constant(true)
  { _M_setup(reinterpret_cast<char*>(const_cast<unsigned char*>(get)), 0, n); }

  strstreambuf::~strstreambuf()
  {
    if (_M_dynamic && !_M_frozen)
      _M_free(eback());
  }

  void 
  strstreambuf::freeze(bool frozenflag)
  {
    if (_M_dynamic)
      _M_frozen = frozenflag;
  }

  char* 
  strstreambuf::str()
  {
    freeze(true);
    return eback();
  }

  int 
  strstreambuf::pcount() const
  { return pptr() ? pptr() - pbase() : 0; }

  strstreambuf::int_type 
  strstreambuf::overflow(int_type c) 
  {
    if (c == traits_type::eof())
      return traits_type::not_eof(c);
    
    // Try to expand the buffer.
    if (pptr() == epptr() && _M_dynamic && !_M_frozen && !_M_constant) 
      {
	ptrdiff_t old_size = epptr() - pbase();
	ptrdiff_t new_size = max(ptrdiff_t(2 * old_size), ptrdiff_t(1));
	
	char* buf = _M_alloc(new_size);
	if (buf) 
	  {
	    memcpy(buf, pbase(), old_size);
	    char* old_buffer = pbase();
	    bool reposition_get = false;
	    ptrdiff_t old_get_offset;
	    if (gptr() != 0) 
	      {
		reposition_get = true;
		old_get_offset = gptr() - eback();
	      }
	    
	    setp(buf, buf + new_size);
	    pbump(old_size);

	    if (reposition_get)
	      setg(buf, buf + old_get_offset, buf + 
		   max(old_get_offset, old_size));

	    _M_free(old_buffer);
	  }
      }
    
    if (pptr() != epptr()) 
      {
	*pptr() = c;
	pbump(1);
	return c;
      }
    else
      return traits_type::eof();
  }

  strstreambuf::int_type 
  strstreambuf::pbackfail(int_type c)
  {
    if (gptr() != eback()) 
      {
      if (c == _Traits::eof()) 
	{
	  gbump(-1);
	  return _Traits::not_eof(c);
	}
      else if (c == static_cast<int_type>(gptr()[-1])) 
	{  // KLUDGE
	  gbump(-1);
	  return c;
	}
      else if (!_M_constant) 
	{
	  gbump(-1);
	  *gptr() = c;
	  return c;
	}
    }
    return _Traits::eof();
  }

  strstreambuf::int_type 
  strstreambuf::underflow()
  {
    if (gptr() == egptr() && pptr() && pptr() > egptr())
      setg(eback(), gptr(), pptr());
    
    if (gptr() != egptr())
      return (unsigned char) *gptr();
    else
      return _Traits::eof();
  }

  basic_streambuf<char, char_traits<char> >*
  strstreambuf::setbuf(char*, streamsize)
  { return this; }

  strstreambuf::pos_type
  strstreambuf::seekoff(off_type off, ios_base::seekdir dir, 
			ios_base::openmode mode)
  {
    bool do_get = false;
    bool do_put = false;

    if ((mode & (ios_base::in | ios_base::out)) 
	== (ios_base::in | ios_base::out) &&
	(dir == ios_base::beg || dir == ios_base::end))
      do_get = do_put = true;
    else if (mode & ios_base::in)
      do_get = true;
    else if (mode & ios_base::out)
      do_put = true;

    // !gptr() is here because, according to D.7.1 paragraph 4, the seekable
    // area is undefined if there is no get area.
    if ((!do_get && !do_put) || (do_put && !pptr()) || !gptr())
      return pos_type(off_type(-1));

    char* seeklow  = eback();
    char* seekhigh = epptr() ? epptr() : egptr();

    off_type newoff;
    switch (dir) 
      {
      case ios_base::beg:
	newoff = 0;
	break;
      case ios_base::end:
	newoff = seekhigh - seeklow;
	break;
      case ios_base::cur:
	newoff = do_put ? pptr() - seeklow : gptr() - seeklow;
	break;
      default:
	return pos_type(off_type(-1));
      }
    
    off += newoff;
    if (off < 0 || off > seekhigh - seeklow)
      return pos_type(off_type(-1));

    if (do_put) 
      {
	if (seeklow + off < pbase()) 
	  {
	    setp(seeklow, epptr());
	    pbump(off);
	  }
	else 
	  {
	    setp(pbase(), epptr());
	    pbump(off - (pbase() - seeklow));
	  }
      }
    if (do_get) 
      {
	if (off <= egptr() - seeklow)
	  setg(seeklow, seeklow + off, egptr());
	else if (off <= pptr() - seeklow)
	  setg(seeklow, seeklow + off, pptr());
	else
	  setg(seeklow, seeklow + off, epptr());
      }
    return pos_type(newoff);
  }

  strstreambuf::pos_type
  strstreambuf::seekpos(pos_type pos, ios_base::openmode mode)
  { return seekoff(pos - pos_type(off_type(0)), ios_base::beg, mode); }

  char* 
  strstreambuf::_M_alloc(size_t n)
  {
    if (_M_alloc_fun)
      return static_cast<char*>(_M_alloc_fun(n));
    else
      return new char[n];
  }

  void 
  strstreambuf::_M_free(char* p)
  {
    if (p)
      if (_M_free_fun)
	_M_free_fun(p);
      else
	delete[] p;
  }

  void 
  strstreambuf::_M_setup(char* get, char* put, streamsize n)
  {
    if (get) 
      {
	size_t N = n > 0 ? size_t(n) : n == 0 ? strlen(get) : size_t(INT_MAX);
	
	if (put) 
	  {
	    setg(get, get, put);
	    setp(put, put + N);
	  }
	else 
	  setg(get, get, get + N);
      }
  }

  istrstream::istrstream(char* s)
  : basic_ios<char>(), basic_istream<char>(0), _M_buf(s, 0)
  { basic_ios<char>::init(&_M_buf); }

  istrstream::istrstream(const char* s)
  : basic_ios<char>(), basic_istream<char>(0), _M_buf(s, 0)
  { basic_ios<char>::init(&_M_buf); }

  istrstream::istrstream(char* s, streamsize n)
  : basic_ios<char>(), basic_istream<char>(0), _M_buf(s, n)
  { basic_ios<char>::init(&_M_buf); }

  istrstream::istrstream(const char* s, streamsize n)
  : basic_ios<char>(), basic_istream<char>(0), _M_buf(s, n)
  { basic_ios<char>::init(&_M_buf); }

  istrstream::~istrstream() { }

  strstreambuf* 
  istrstream::rdbuf() const 
  { return const_cast<strstreambuf*>(&_M_buf); }

  char* 
  istrstream::str() 
  { return _M_buf.str(); }

  ostrstream::ostrstream()
  : basic_ios<char>(), basic_ostream<char>(0), _M_buf()
  { basic_ios<char>::init(&_M_buf); }

  ostrstream::ostrstream(char* s, int n, ios_base::openmode mode)
  : basic_ios<char>(), basic_ostream<char>(0), 
    _M_buf(s, n, mode & ios_base::app ? s + strlen(s) : s)
  { basic_ios<char>::init(&_M_buf); }

  ostrstream::~ostrstream() {}

  strstreambuf* 
  ostrstream::rdbuf() const
  { return const_cast<strstreambuf*>(&_M_buf); }

  void 
  ostrstream::freeze(bool freezeflag)
  { _M_buf.freeze(freezeflag); }

  char* 
  ostrstream::str()
  { return _M_buf.str(); }

  int 
  ostrstream::pcount() const
  { return _M_buf.pcount(); }

  strstream::strstream()
  : basic_ios<char>(), basic_iostream<char>(0), _M_buf()
  { basic_ios<char>::init(&_M_buf); }

  strstream::strstream(char* s, int n, ios_base::openmode mode)
  : basic_ios<char>(), basic_iostream<char>(0), 
    _M_buf(s, n, mode & ios_base::app ? s + strlen(s) : s)
  { basic_ios<char>::init(&_M_buf); }

  strstream::~strstream() { }

  strstreambuf* 
  strstream::rdbuf() const
  { return const_cast<strstreambuf*>(&_M_buf); }

  void 
  strstream::freeze(bool freezeflag)
  { _M_buf.freeze(freezeflag); }

  int 
  strstream::pcount() const
  { return _M_buf.pcount(); }

  char* 
  strstream::str()
  { return _M_buf.str(); }
} // namespace std
