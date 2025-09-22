// -*- C++ -*- forwarding header.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
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
// ISO C++ 14882: 20.4.6  C library
//

/** @file cstdlib
 *  This is a Standard C++ Library file.  You should @c #include this file
 *  in your programs, rather than any of the "*.h" implementation files.
 *
 *  This is the C++ version of the Standard C Library header @c stdlib.h,
 *  and its contents are (mostly) the same as that header, but are all
 *  contained in the namespace @c std.
 */

#ifndef _CPP_CSTDLIB
#define _CPP_CSTDLIB 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <cstddef>

#include <stdlib.h>

// Get rid of those macros defined in <stdlib.h> in lieu of real functions.
#undef abort
#undef abs
#undef atexit
#undef atof
#undef atoi
#undef atol
#undef bsearch
#undef calloc
#undef div
#undef exit
#undef free
#undef getenv
#undef labs
#undef ldiv
#undef malloc
#undef mblen
#undef mbstowcs
#undef mbtowc
#undef qsort
#undef rand
#undef realloc
#undef srand
#undef strtod
#undef strtol
#undef strtoul
#undef system
#undef wcstombs
#undef wctomb

namespace std 
{
  using ::div_t;
  using ::ldiv_t;

  using ::abort;
  using ::abs;
  using ::atexit;
  using ::atof;
  using ::atoi;
  using ::atol;
  using ::bsearch;
  using ::calloc;
  using ::div;
  using ::exit;
  using ::free;
  using ::getenv;
  using ::labs;
  using ::ldiv;
  using ::malloc;
  using ::mblen;
  using ::mbstowcs;
  using ::mbtowc;
  using ::qsort;
  using ::rand;
  using ::realloc;
  using ::srand;
  using ::strtod;
  using ::strtol;
  using ::strtoul;
  using ::system;
  using ::wcstombs;
  using ::wctomb;

  inline long 
  abs(long __i) { return labs(__i); }

  inline ldiv_t
  div(long __i, long __j) { return ldiv(__i, __j); }
} 

#if defined(_GLIBCPP_USE_C99)

#undef _Exit
#undef llabs
#undef lldiv
#undef atoll
#undef strtoll
#undef strtoull
#undef strtof
#undef strtold

namespace __gnu_cxx
{
#if !defined(_GLIBCPP_USE_C99_LONG_LONG_DYNAMIC)
  using ::lldiv_t;
#endif
#if defined(_GLIBCPP_USE_C99_CHECK) || defined(_GLIBCPP_USE_C99_DYNAMIC)
  extern "C" void (_Exit)(int);
#endif
#if !defined(_GLIBCPP_USE_C99_DYNAMIC)
  using ::_Exit;
#endif

  inline long long 
  abs(long long __x) { return __x >= 0 ? __x : -__x; }

  inline long long 
  llabs(long long __x) { return __x >= 0 ? __x : -__x; }

#if !defined(_GLIBCPP_USE_C99_LONG_LONG_DYNAMIC)
  inline lldiv_t 
  div(long long __n, long long __d)
  { lldiv_t __q; __q.quot = __n / __d; __q.rem = __n % __d; return __q; }

  inline lldiv_t 
  lldiv(long long __n, long long __d)
  { lldiv_t __q; __q.quot = __n / __d; __q.rem = __n % __d; return __q; }
#endif

#if defined(_GLIBCPP_USE_C99_LONG_LONG_CHECK) || defined(_GLIBCPP_USE_C99_LONG_LONG_DYNAMIC)
  extern "C" long long int (atoll)(const char *);
  extern "C" long long int
    (strtoll)(const char * restrict, char ** restrict, int);
  extern "C" unsigned long long int
    (strtoull)(const char * restrict, char ** restrict, int);
#endif
#if !defined(_GLIBCPP_USE_C99_LONG_LONG_DYNAMIC)
  using ::atoll;
  using ::strtoll;
  using ::strtoull;
#endif
  using ::strtof;
  using ::strtold; 
} 

namespace std
{
#if !defined(_GLIBCPP_USE_C99_LONG_LONG_DYNAMIC)
  using __gnu_cxx::lldiv_t;
#endif
  using __gnu_cxx::_Exit;
  using __gnu_cxx::abs;
  using __gnu_cxx::llabs; 
#if !defined(_GLIBCPP_USE_C99_LONG_LONG_DYNAMIC)
  using __gnu_cxx::div;
  using __gnu_cxx::lldiv;
#endif
  using __gnu_cxx::atoll;
  using __gnu_cxx::strtof;
  using __gnu_cxx::strtoll;
  using __gnu_cxx::strtoull;
  using __gnu_cxx::strtold;
}
#endif

#endif 
