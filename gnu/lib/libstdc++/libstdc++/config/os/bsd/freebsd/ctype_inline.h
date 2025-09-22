// Locale support -*- C++ -*-

// Copyright (C) 2000, 2003 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.1  Locales
//
  
// ctype bits to be inlined go here. Non-inlinable (ie virtual do_*)
// functions go in ctype.cc
  
  bool
  ctype<char>::
  is(mask __m, char __c) const
  { 
    if (_M_table)
      return _M_table[static_cast<unsigned char>(__c)] & __m;
    else
      return __istype(__c, __m);
  }

  const char*
  ctype<char>::
  is(const char* __low, const char* __high, mask* __vec) const
  {
    if (_M_table)
      while (__low < __high)
	*__vec++ = _M_table[static_cast<unsigned char>(*__low++)];
    else
      for (;__low < __high; ++__vec, ++__low)
	{
#if defined (_CTYPE_S) || defined (__istype)
	  *__vec = __maskrune (*__low, upper | lower | alpha | digit | xdigit
			       | space | print | graph | cntrl | punct | alnum);
#else
	  mask __m = 0;
	  if (this->is(upper, *__low)) __m |= upper;
	  if (this->is(lower, *__low)) __m |= lower;
	  if (this->is(alpha, *__low)) __m |= alpha;
	  if (this->is(digit, *__low)) __m |= digit;
	  if (this->is(xdigit, *__low)) __m |= xdigit;
	  if (this->is(space, *__low)) __m |= space;
	  if (this->is(print, *__low)) __m |= print;
	  if (this->is(graph, *__low)) __m |= graph;
	  if (this->is(cntrl, *__low)) __m |= cntrl;
	  if (this->is(punct, *__low)) __m |= punct;
	  // Do not include explicit line for alnum mask since it is a
	  // pure composite of masks on FreeBSD.
	  *__vec = __m;
#endif
	}
    return __high;
  }

  const char*
  ctype<char>::
  scan_is(mask __m, const char* __low, const char* __high) const
  {
    if (_M_table)
      while (__low < __high
	     && !(_M_table[static_cast<unsigned char>(*__low)] & __m))
	++__low;
    else
      while (__low < __high && !this->is(__m, *__low))
	++__low;
    return __low;
  }

  const char*
  ctype<char>::
  scan_not(mask __m, const char* __low, const char* __high) const
  {
    if (_M_table)
      while (__low < __high
	     && (_M_table[static_cast<unsigned char>(*__low)] & __m) != 0)
	++__low;
    else
      while (__low < __high && this->is(__m, *__low) != 0)
	++__low;
    return __low;
  }
