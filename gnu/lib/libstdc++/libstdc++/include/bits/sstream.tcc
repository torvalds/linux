// String based streams -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2001, 2002
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

//
// ISO C++ 14882: 27.7  String-based streams
//

#ifndef _CPP_BITS_SSTREAM_TCC
#define _CPP_BITS_SSTREAM_TCC	1

#pragma GCC system_header

#include <sstream>

namespace std
{
  template <class _CharT, class _Traits, class _Alloc>
    typename basic_stringbuf<_CharT, _Traits, _Alloc>::int_type 
    basic_stringbuf<_CharT, _Traits, _Alloc>::
    pbackfail(int_type __c)
    {
      int_type __ret = traits_type::eof();
      bool __testeof = traits_type::eq_int_type(__c, traits_type::eof());
      bool __testpos = _M_in_cur && _M_in_beg < _M_in_cur; 
      
      // Try to put back __c into input sequence in one of three ways.
      // Order these tests done in is unspecified by the standard.
      if (__testpos)
	{
	  if (traits_type::eq(traits_type::to_char_type(__c), this->gptr()[-1])
	      && !__testeof)
	    {
	      --_M_in_cur;
	      __ret = __c;
	    }
	  else if (!__testeof)
	    {
	      --_M_in_cur;
	      *_M_in_cur = traits_type::to_char_type(__c);
	      __ret = __c;
	    }
	  else if (__testeof)
	    {
	      --_M_in_cur;
	      __ret = traits_type::not_eof(__c);
	    }
	}
      return __ret;
    }
  
  template <class _CharT, class _Traits, class _Alloc>
    typename basic_stringbuf<_CharT, _Traits, _Alloc>::int_type 
    basic_stringbuf<_CharT, _Traits, _Alloc>::
    overflow(int_type __c)
    {
      int_type __ret = traits_type::eof();
      bool __testeof = traits_type::eq_int_type(__c, __ret);
      bool __testwrite = _M_out_cur < _M_buf + _M_buf_size;
      bool __testout = _M_mode & ios_base::out;

      // Try to append __c into output sequence in one of two ways.
      // Order these tests done in is unspecified by the standard.
      if (__testout)
	{
	  if (!__testeof)
	    {
	      __size_type __len = max(_M_buf_size, _M_buf_size_opt);
	      __len *= 2;

	      if (__testwrite)
		__ret = this->sputc(traits_type::to_char_type(__c));
	      else if (__len <= _M_string.max_size())
		{
		  // Force-allocate, re-sync.
		  _M_string = this->str();
		  _M_string.reserve(__len);
		  _M_buf_size = __len;
		  _M_really_sync(_M_in_cur - _M_in_beg, 
				 _M_out_cur - _M_out_beg);
		  *_M_out_cur = traits_type::to_char_type(__c);
		  _M_out_cur_move(1);
		  __ret = __c;
		}
	    }
	  else
	    __ret = traits_type::not_eof(__c);
	}
      return __ret;
    }

  template <class _CharT, class _Traits, class _Alloc>
    typename basic_stringbuf<_CharT, _Traits, _Alloc>::pos_type
    basic_stringbuf<_CharT, _Traits, _Alloc>::
    seekoff(off_type __off, ios_base::seekdir __way, ios_base::openmode __mode)
    {
      pos_type __ret =  pos_type(off_type(-1)); 
      bool __testin = (ios_base::in & _M_mode & __mode) != 0;
      bool __testout = (ios_base::out & _M_mode & __mode) != 0;
      bool __testboth = __testin && __testout && __way != ios_base::cur;
      __testin &= !(__mode & ios_base::out);
      __testout &= !(__mode & ios_base::in);

      if (_M_buf_size && (__testin || __testout || __testboth))
	{
	  char_type* __beg = _M_buf;
	  char_type* __curi = NULL;
	  char_type* __curo = NULL;
	  char_type* __endi = NULL;
	  char_type* __endo = NULL;

	  if (__testin || __testboth)
	    {
	      __curi = this->gptr();
	      __endi = this->egptr();
	    }
	  if (__testout || __testboth)
	    {
	      __curo = this->pptr();
	      __endo = this->epptr();
	    }

	  off_type __newoffi = 0;
	  off_type __newoffo = 0;
	  if (__way == ios_base::cur)
	    {
	      __newoffi = __curi - __beg;
	      __newoffo = __curo - __beg;
	    }
	  else if (__way == ios_base::end)
	    {
	      __newoffi = __endi - __beg;
	      __newoffo = __endo - __beg;
	    }

	  if ((__testin || __testboth)
	      && __newoffi + __off >= 0 && __endi - __beg >= __newoffi + __off)
	    {
	      _M_in_cur = __beg + __newoffi + __off;
	      __ret = pos_type(__newoffi);
	    }
	  if ((__testout || __testboth)
	      && __newoffo + __off >= 0 && __endo - __beg >= __newoffo + __off)
	    {
	      _M_out_cur_move(__newoffo + __off - (_M_out_cur - __beg));
	      __ret = pos_type(__newoffo);
	    }
	}
      return __ret;
    }

  template <class _CharT, class _Traits, class _Alloc>
    typename basic_stringbuf<_CharT, _Traits, _Alloc>::pos_type
    basic_stringbuf<_CharT, _Traits, _Alloc>::
    seekpos(pos_type __sp, ios_base::openmode __mode)
    {
      pos_type __ret =  pos_type(off_type(-1)); 
      
      if (_M_buf_size)
	{
	  off_type __pos = __sp; // Use streamoff operator to do conversion.
	  char_type* __beg = NULL;
	  char_type* __end = NULL;
	  bool __testin = (ios_base::in & _M_mode & __mode) != 0;
	  bool __testout = (ios_base::out & _M_mode & __mode) != 0;
	  bool __testboth = __testin && __testout;
	  __testin &= !(__mode & ios_base::out);
	  __testout &= !(__mode & ios_base::in);
	  
	  // NB: Ordered.
	  bool __testposi = false;
	  bool __testposo = false;
	  if (__testin || __testboth)
	    {
	      __beg = this->eback();
	      __end = this->egptr();
	      if (0 <= __pos && __pos <= __end - __beg)
		__testposi = true;
	    }
	  if (__testout || __testboth)
	    {
	      __beg = this->pbase();
	      __end = _M_buf + _M_buf_size;
	      if (0 <= __pos && __pos <= __end - __beg)
		__testposo = true;
	    }
	  if (__testposi || __testposo)
	    {
	      if (__testposi)
		_M_in_cur = _M_in_beg + __pos;
	      if (__testposo)
		_M_out_cur_move((__pos) - (_M_out_cur - __beg));
	      __ret = pos_type(off_type(__pos));
	    }
	}
      return __ret;
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.  
  // NB:  This syntax is a GNU extension.
#if _GLIBCXX_EXTERN_TEMPLATE
  extern template class basic_stringbuf<char>;
  extern template class basic_istringstream<char>;
  extern template class basic_ostringstream<char>;
  extern template class basic_stringstream<char>;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern template class basic_stringbuf<wchar_t>;
  extern template class basic_istringstream<wchar_t>;
  extern template class basic_ostringstream<wchar_t>;
  extern template class basic_stringstream<wchar_t>;
#endif
#endif
} // namespace std

#endif
