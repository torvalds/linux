// Locale support -*- C++ -*-

// Copyright (C) 2002 Free Software Foundation, Inc.
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
  
// The following definitions are portable, but insanely slow. If one
// cares at all about performance, then specialized ctype
// functionality should be added for the native os in question: see
// the config/os/bits/ctype_*.h files.

  bool
  ctype<char>::
  is(mask __m, char __c) const
  { 
    bool __ret;
    switch (__m)
      {
      case space:
	__ret = isspace(__c);
	break;
      case print:
	__ret = isprint(__c);
	break;
      case cntrl:
	__ret = iscntrl(__c);
	break;
      case upper:
	__ret = isupper(__c);
	break;
      case lower:
	__ret = islower(__c);
	break;
      case alpha:
	__ret = isalpha(__c);
	break;
      case digit:
	__ret = isdigit(__c);
	break;
      case punct:
	__ret = ispunct(__c);
	break;
      case xdigit:
	__ret = isxdigit(__c);
	break;
      case alnum:
	__ret = isalnum(__c);
	break;
      case graph:
	__ret = isgraph(__c);
	break;
      default:
	__ret = false;
	break;
      }
    return __ret;
  }
   
  const char*
  ctype<char>::
  is(const char* __low, const char* __high, mask* __vec) const
  {
    const int __bitmasksize = 11; // Highest bitmask in ctype_base == 10
    for (;__low < __high; ++__vec, ++__low)
      {
	mask __m = 0;
	int __i = 0; // Lowest bitmask in ctype_base == 0
	for (;__i < __bitmasksize; ++__i)
	  {
	    mask __bit = static_cast<mask>(1 << __i);
	    if (this->is(__bit, *__low))
	      __m |= __bit;
	  }
	*__vec = __m;
      }
    return __high;
  }

  const char*
  ctype<char>::
  scan_is(mask __m, const char* __low, const char* __high) const
  {
    while (__low < __high && !this->is(__m, *__low))
      ++__low;
    return __low;
  }

  const char*
  ctype<char>::
  scan_not(mask __m, const char* __low, const char* __high) const
  {
    while (__low < __high && this->is(__m, *__low) != 0)
      ++__low;
    return __low;
  }
