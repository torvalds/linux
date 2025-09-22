// ostream classes -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
// 2006, 2007
// Free Software Foundation, Inc.
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

/** @file ostream.tcc
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 27.6.2  Output streams
//

#ifndef _OSTREAM_TCC
#define _OSTREAM_TCC 1

#pragma GCC system_header

#include <locale>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>::sentry::
    sentry(basic_ostream<_CharT, _Traits>& __os)
    : _M_ok(false), _M_os(__os)
    {
      // XXX MT
      if (__os.tie() && __os.good())
	__os.tie()->flush();

      if (__os.good())
	_M_ok = true;
      else
	__os.setstate(ios_base::failbit);
    }

  template<typename _CharT, typename _Traits>
    template<typename _ValueT>
      basic_ostream<_CharT, _Traits>&
      basic_ostream<_CharT, _Traits>::
      _M_insert(_ValueT __v)
      {
	sentry __cerb(*this);
	if (__cerb)
	  {
	    ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	    try
	      {
		const __num_put_type& __np = __check_facet(this->_M_num_put);
		if (__np.put(*this, *this, this->fill(), __v).failed())
		  __err |= ios_base::badbit;
	      }
	    catch(...)
	      { this->_M_setstate(ios_base::badbit); }
	    if (__err)
	      this->setstate(__err);
	  }
	return *this;
      }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    operator<<(short __n)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 117. basic_ostream uses nonexistent num_put member functions.
      const ios_base::fmtflags __fmt = this->flags() & ios_base::basefield;
      if (__fmt == ios_base::oct || __fmt == ios_base::hex)
	return _M_insert(static_cast<long>(static_cast<unsigned short>(__n)));
      else
	return _M_insert(static_cast<long>(__n));
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    operator<<(int __n)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 117. basic_ostream uses nonexistent num_put member functions.
      const ios_base::fmtflags __fmt = this->flags() & ios_base::basefield;
      if (__fmt == ios_base::oct || __fmt == ios_base::hex)
	return _M_insert(static_cast<long>(static_cast<unsigned int>(__n)));
      else
	return _M_insert(static_cast<long>(__n));
    }
  
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    operator<<(__streambuf_type* __sbin)
    {
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      sentry __cerb(*this);
      if (__cerb && __sbin)
	{
	  try
	    {
	      if (!__copy_streambufs(__sbin, this->rdbuf()))
		__err |= ios_base::failbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::failbit); }
	}
      else if (!__sbin)
	__err |= ios_base::badbit;
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    put(char_type __c)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // basic_ostream::put(char_type) is an unformatted output function.
      // DR 63. Exception-handling policy for unformatted output.
      // Unformatted output functions should catch exceptions thrown
      // from streambuf members.
      sentry __cerb(*this);
      if (__cerb)
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      const int_type __put = this->rdbuf()->sputc(__c);
	      if (traits_type::eq_int_type(__put, traits_type::eof()))
		__err |= ios_base::badbit;
	    }
	  catch (...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    write(const _CharT* __s, streamsize __n)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // basic_ostream::write(const char_type*, streamsize) is an
      // unformatted output function.
      // DR 63. Exception-handling policy for unformatted output.
      // Unformatted output functions should catch exceptions thrown
      // from streambuf members.
      sentry __cerb(*this);
      if (__cerb)
	{
	  try
	    { _M_write(__s, __n); }
	  catch (...)
	    { this->_M_setstate(ios_base::badbit); }
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    flush()
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // basic_ostream::flush() is *not* an unformatted output function.
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      try
	{
	  if (this->rdbuf() && this->rdbuf()->pubsync() == -1)
	    __err |= ios_base::badbit;
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    typename basic_ostream<_CharT, _Traits>::pos_type
    basic_ostream<_CharT, _Traits>::
    tellp()
    {
      pos_type __ret = pos_type(-1);
      try
	{
	  if (!this->fail())
	    __ret = this->rdbuf()->pubseekoff(0, ios_base::cur, ios_base::out);
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    seekp(pos_type __pos)
    {
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      try
	{
	  if (!this->fail())
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 136.  seekp, seekg setting wrong streams?
	      const pos_type __p = this->rdbuf()->pubseekpos(__pos,
							     ios_base::out);

	      // 129. Need error indication from seekp() and seekg()
	      if (__p == pos_type(off_type(-1)))
		__err |= ios_base::failbit;
	    }
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    seekp(off_type __off, ios_base::seekdir __dir)
    {
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      try
	{
	  if (!this->fail())
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 136.  seekp, seekg setting wrong streams?
	      const pos_type __p = this->rdbuf()->pubseekoff(__off, __dir,
							     ios_base::out);

	      // 129. Need error indication from seekp() and seekg()
	      if (__p == pos_type(off_type(-1)))
		__err |= ios_base::failbit;
	    }
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, const char* __s)
    {
      if (!__s)
	__out.setstate(ios_base::badbit);
      else
	{
	  // _GLIBCXX_RESOLVE_LIB_DEFECTS
	  // 167.  Improper use of traits_type::length()
	  const size_t __clen = char_traits<char>::length(__s);      
	  _CharT* __ws = 0;
	  try
	    { 
	      __ws = new _CharT[__clen];
	      for (size_t  __i = 0; __i < __clen; ++__i)
		__ws[__i] = __out.widen(__s[__i]);
	    }
	  catch(...)
	    {
	      delete [] __ws;
	      __out._M_setstate(ios_base::badbit);
	      return __out;
	    }

	  try
	    {
	      __ostream_insert(__out, __ws, __clen);
	      delete [] __ws;
	    }
	  catch(...)
	    {
	      delete [] __ws;
	      __throw_exception_again;
	    }
	}
      return __out;
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.
  // NB:  This syntax is a GNU extension.
#if _GLIBCXX_EXTERN_TEMPLATE
  extern template class basic_ostream<char>;
  extern template ostream& endl(ostream&);
  extern template ostream& ends(ostream&);
  extern template ostream& flush(ostream&);
  extern template ostream& operator<<(ostream&, char);
  extern template ostream& operator<<(ostream&, unsigned char);
  extern template ostream& operator<<(ostream&, signed char);
  extern template ostream& operator<<(ostream&, const char*);
  extern template ostream& operator<<(ostream&, const unsigned char*);
  extern template ostream& operator<<(ostream&, const signed char*);

  extern template ostream& ostream::_M_insert(long);
  extern template ostream& ostream::_M_insert(unsigned long);
  extern template ostream& ostream::_M_insert(bool);
#ifdef _GLIBCXX_USE_LONG_LONG
  extern template ostream& ostream::_M_insert(long long);
  extern template ostream& ostream::_M_insert(unsigned long long);
#endif
  extern template ostream& ostream::_M_insert(double);
  extern template ostream& ostream::_M_insert(long double);
  extern template ostream& ostream::_M_insert(const void*);

#ifdef _GLIBCXX_USE_WCHAR_T
  extern template class basic_ostream<wchar_t>;
  extern template wostream& endl(wostream&);
  extern template wostream& ends(wostream&);
  extern template wostream& flush(wostream&);
  extern template wostream& operator<<(wostream&, wchar_t);
  extern template wostream& operator<<(wostream&, char);
  extern template wostream& operator<<(wostream&, const wchar_t*);
  extern template wostream& operator<<(wostream&, const char*);

  extern template wostream& wostream::_M_insert(long);
  extern template wostream& wostream::_M_insert(unsigned long);
  extern template wostream& wostream::_M_insert(bool);
#ifdef _GLIBCXX_USE_LONG_LONG
  extern template wostream& wostream::_M_insert(long long);
  extern template wostream& wostream::_M_insert(unsigned long long);
#endif
  extern template wostream& wostream::_M_insert(double);
  extern template wostream& wostream::_M_insert(long double);
  extern template wostream& wostream::_M_insert(const void*);
#endif
#endif

_GLIBCXX_END_NAMESPACE

#endif
