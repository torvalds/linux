// -*- C++ -*- forwarding header.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

/** @file include/cwctype
 *  This is a Standard C++ Library file.  You should @c #include this file
 *  in your programs, rather than any of the "*.h" implementation files.
 *
 *  This is the C++ version of the Standard C Library header @c wctype.h,
 *  and its contents are (mostly) the same as that header, but are all
 *  contained in the namespace @c std (except for names which are defined
 *  as macros in C).
 */

//
// ISO C++ 14882: <cwctype>
//

#ifndef _GLIBCXX_CWCTYPE
#define _GLIBCXX_CWCTYPE 1

#pragma GCC system_header

#include <bits/c++config.h>

#if _GLIBCXX_HAVE_WCTYPE_H
#include <wctype.h>
#endif

// Get rid of those macros defined in <wctype.h> in lieu of real functions.
#undef iswalnum
#undef iswalpha
#if _GLIBCXX_HAVE_ISWBLANK
# undef iswblank
#endif
#undef iswcntrl
#undef iswctype
#undef iswdigit
#undef iswgraph
#undef iswlower
#undef iswprint
#undef iswpunct
#undef iswspace
#undef iswupper
#undef iswxdigit
#undef towctrans
#undef towlower
#undef towupper
#undef wctrans
#undef wctype

#if _GLIBCXX_USE_WCHAR_T

_GLIBCXX_BEGIN_NAMESPACE(std)

  using ::wint_t;	  // cwchar

  using ::wctype_t;
  using ::wctrans_t;

  using ::iswalnum;
  using ::iswalpha;
#if _GLIBCXX_HAVE_ISWBLANK
  using ::iswblank;
#endif
  using ::iswcntrl;
  using ::iswctype;
  using ::iswdigit;
  using ::iswgraph;
  using ::iswlower;
  using ::iswprint;
  using ::iswpunct;
  using ::iswspace;
  using ::iswupper;
  using ::iswxdigit;
  using ::towctrans;
  using ::towlower;
  using ::towupper;
  using ::wctrans;
  using ::wctype;

_GLIBCXX_END_NAMESPACE

#endif //_GLIBCXX_USE_WCHAR_T

#endif
