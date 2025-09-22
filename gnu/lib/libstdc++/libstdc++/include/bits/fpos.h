// File position object and stream types

// Copyright (C) 1997, 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
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
// ISO C++ 14882: 27 Input/output library
//

/** @file fpos.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_FPOS_H
#define _CPP_BITS_FPOS_H 1

#pragma GCC system_header

#include <bits/c++io.h>
#include <cwchar> 	// For mbstate_t.

namespace std
{
  // 27.4.1  Types

  // [27.4.3] template class fpos
  /**
   *  @doctodo
  */
  template<typename _StateT>
    class fpos
    {
    public:
      // Types:
      typedef _StateT __state_type;

    private:
      streamoff 	_M_off;
      __state_type 	_M_st;

    public:
      __state_type
      state() const  { return _M_st; }

      void 
      state(__state_type __st)  { _M_st = __st; }

      // NB: The standard defines only the implicit copy ctor and the
      // previous two members.  The rest is a "conforming extension".
      fpos(): _M_off(streamoff()), _M_st(__state_type()) { }

      fpos(streamoff __off, __state_type __st = __state_type())
      :  _M_off(__off), _M_st(__st) { }

      operator streamoff() const { return _M_off; }

      fpos& 
      operator+=(streamoff __off) { _M_off += __off; return *this; }

      fpos& 
      operator-=(streamoff __off) { _M_off -= __off; return *this; }

      fpos 
      operator+(streamoff __off) 
      { 
	fpos __t(*this); 
	__t += __off;
	return __t;
      }

      fpos      
      operator-(streamoff __off) 
      { 
	fpos __t(*this); 
	__t -= __off; 
	return __t;
      }

      bool  
      operator==(const fpos& __pos) const
      { return _M_off == __pos._M_off; }

      bool  
      operator!=(const fpos& __pos) const
      { return _M_off != __pos._M_off; }

      streamoff 
      _M_position() const { return _M_off; }

      void
      _M_position(streamoff __off)  { _M_off = __off; }
    };

  /// 27.2, paragraph 10 about fpos/char_traits circularity
  typedef fpos<mbstate_t> 		streampos;
#  if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  /// 27.2, paragraph 10 about fpos/char_traits circularity
  typedef fpos<mbstate_t> 		wstreampos;
#  endif
}  // namespace std

#endif 
