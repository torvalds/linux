// Wrapper for underlying C-language localization -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
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

/** @file c++locale.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 22.8  Standard locale categories.
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#ifndef _C_LOCALE_H
#define _C_LOCALE_H 1

#pragma GCC system_header

#include <cstring>              // get std::strlen
#include <cstdio>               // get std::vsnprintf or std::vsprintf
#include <clocale>
#include <langinfo.h>		// For codecvt
#include <iconv.h>		// For codecvt using iconv, iconv_t
#include <libintl.h> 		// For messages
#include <cstdarg>

#define _GLIBCXX_C_LOCALE_GNU 1

#define _GLIBCXX_NUM_CATEGORIES 6

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  extern "C" __typeof(uselocale) __uselocale;

_GLIBCXX_END_NAMESPACE
#endif

_GLIBCXX_BEGIN_NAMESPACE(std)

  typedef __locale_t		__c_locale;

  // Convert numeric value of type double and long double to string and
  // return length of string.  If vsnprintf is available use it, otherwise
  // fall back to the unsafe vsprintf which, in general, can be dangerous
  // and should be avoided.
  inline int
  __convert_from_v(const __c_locale& __cloc __attribute__ ((__unused__)),
		   char* __out,
		   const int __size __attribute__ ((__unused__)),
		   const char* __fmt, ...)
  {
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __c_locale __old = __gnu_cxx::__uselocale(__cloc);
#else
    char* __old = std::setlocale(LC_ALL, NULL);
    size_t __sz = std::strlen(__old) + 1;
    char* __sav = new char[__sz];
    std::memcpy(__sav, __old, __sz);
    std::setlocale(LC_ALL, "C");
#endif

    va_list __args;
    va_start(__args, __fmt);

#ifdef _GLIBCXX_USE_C99
    const int __ret = std::vsnprintf(__out, __size, __fmt, __args);
#else
    const int __ret = std::vsprintf(__out, __fmt, __args);
#endif

    va_end(__args);

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __gnu_cxx::__uselocale(__old);
#else
    std::setlocale(LC_ALL, __sav);
    delete [] __sav;
#endif
    return __ret;
  }

_GLIBCXX_END_NAMESPACE

#endif
