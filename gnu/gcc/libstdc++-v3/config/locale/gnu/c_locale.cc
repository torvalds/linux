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

//
// ISO C++ 14882: 22.8  Standard locale categories.
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <cerrno>  // For errno
#include <locale>
#include <stdexcept>
#include <langinfo.h>
#include <bits/c++locale_internal.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<>
    void
    __convert_to_v(const char* __s, float& __v, ios_base::iostate& __err, 
		   const __c_locale& __cloc)
    {
      char* __sanity;
      errno = 0;
      float __f = __strtof_l(__s, &__sanity, __cloc);
      if (__sanity != __s && errno != ERANGE)
	__v = __f;
      else
	__err |= ios_base::failbit;
    }

  template<>
    void
    __convert_to_v(const char* __s, double& __v, ios_base::iostate& __err, 
		   const __c_locale& __cloc)
    {
      char* __sanity;
      errno = 0;
      double __d = __strtod_l(__s, &__sanity, __cloc);
      if (__sanity != __s && errno != ERANGE)
	__v = __d;
      else
	__err |= ios_base::failbit;
    }

  template<>
    void
    __convert_to_v(const char* __s, long double& __v, ios_base::iostate& __err,
		   const __c_locale& __cloc)
    {
      char* __sanity;
      errno = 0;
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
      // Prefer strtold_l, as __strtold_l isn't prototyped in more recent
      // glibc versions.
      long double __ld = strtold_l(__s, &__sanity, __cloc);
#else
      long double __ld = __strtold_l(__s, &__sanity, __cloc);
#endif
      if (__sanity != __s && errno != ERANGE)
	__v = __ld;
      else
	__err |= ios_base::failbit;
    }

  void
  locale::facet::_S_create_c_locale(__c_locale& __cloc, const char* __s, 
				    __c_locale __old)
  {
    __cloc = __newlocale(1 << LC_ALL, __s, __old);
    if (!__cloc)
      {
	// This named locale is not supported by the underlying OS.
	__throw_runtime_error(__N("locale::facet::_S_create_c_locale "
			      "name not valid"));
      }
  }
  
  void
  locale::facet::_S_destroy_c_locale(__c_locale& __cloc)
  {
    if (__cloc && _S_get_c_locale() != __cloc)
      __freelocale(__cloc); 
  }

  __c_locale
  locale::facet::_S_clone_c_locale(__c_locale& __cloc)
  { return __duplocale(__cloc); }

_GLIBCXX_END_NAMESPACE

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  const char* const category_names[6 + _GLIBCXX_NUM_CATEGORIES] =
    {
      "LC_CTYPE", 
      "LC_NUMERIC",
      "LC_TIME", 
      "LC_COLLATE", 
      "LC_MONETARY",
      "LC_MESSAGES", 
      "LC_PAPER", 
      "LC_NAME", 
      "LC_ADDRESS",
      "LC_TELEPHONE", 
      "LC_MEASUREMENT", 
      "LC_IDENTIFICATION" 
    };

_GLIBCXX_END_NAMESPACE

_GLIBCXX_BEGIN_NAMESPACE(std)

  const char* const* const locale::_S_categories = __gnu_cxx::category_names;

_GLIBCXX_END_NAMESPACE

// XXX GLIBCXX_ABI Deprecated
#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT
#define _GLIBCXX_LDBL_COMPAT(dbl, ldbl) \
  extern "C" void ldbl (void) __attribute__ ((alias (#dbl)))
_GLIBCXX_LDBL_COMPAT(_ZSt14__convert_to_vIdEvPKcRT_RSt12_Ios_IostateRKP15__locale_struct, _ZSt14__convert_to_vIeEvPKcRT_RSt12_Ios_IostateRKP15__locale_struct);
#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
