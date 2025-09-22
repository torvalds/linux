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

//
// ISO C++ 14882: 22.1  Locales
//
  
// Information as gleaned from /usr/include/ctype.h on FreeBSD 3.4,
// 4.0 and all versions of the CVS managed file at:
// :pserver:anoncvs@anoncvs.freebsd.org:/home/ncvs/src/include/ctype.h
  
_GLIBCXX_BEGIN_NAMESPACE(std)

  /// @brief  Base class for ctype.
  struct ctype_base
  {
    // Non-standard typedefs.
    typedef const int* 		__to_type;

    // NB: Offsets into ctype<char>::_M_table force a particular size
    // on the mask type. Because of this, we don't use an enum.
    typedef unsigned long 	mask;
#ifdef _CTYPE_S
    // FreeBSD 4.0 uses this style of define.
    static const mask upper    	= _CTYPE_U;
    static const mask lower 	= _CTYPE_L;
    static const mask alpha 	= _CTYPE_A;
    static const mask digit 	= _CTYPE_D;
    static const mask xdigit 	= _CTYPE_X;
    static const mask space 	= _CTYPE_S;
    static const mask print 	= _CTYPE_R;
    static const mask graph 	= _CTYPE_A | _CTYPE_D | _CTYPE_P;
    static const mask cntrl 	= _CTYPE_C;
    static const mask punct 	= _CTYPE_P;
    static const mask alnum 	= _CTYPE_A | _CTYPE_D;
#else
    // Older versions, including Free BSD 3.4, use this style of define.
    static const mask upper    	= _U;
    static const mask lower 	= _L;
    static const mask alpha 	= _A;
    static const mask digit 	= _D;
    static const mask xdigit 	= _X;
    static const mask space 	= _S;
    static const mask print 	= _R;
    static const mask graph 	= _A | _D | _P;
    static const mask cntrl 	= _C;
    static const mask punct 	= _P;
    static const mask alnum 	= _A | _D;
#endif
  };

_GLIBCXX_END_NAMESPACE
