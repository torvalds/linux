// Locale support -*- C++ -*-

// Copyright (C) 2004 Free Software Foundation, Inc.
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

/** @file ctype_noninline.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 22.1  Locales
//
  
// Information as gleaned from /usr/include/ctype.h

  const ctype_base::mask*
  ctype<char>::classic_table() throw()
  {
    const ctype_base::mask* __ret;
    char* __old = strdup(setlocale(LC_CTYPE, NULL));
    setlocale(LC_CTYPE, "C");
    __ret = *__ctype_b_loc();
    setlocale(LC_CTYPE, __old);
    free(__old);
    return __ret;
  }

  ctype<char>::ctype(__c_locale, const mask* __table, bool __del, 
		     size_t __refs) 
  : facet(__refs), _M_del(__table != 0 && __del)
  {
    char* __old=strdup(setlocale(LC_CTYPE, NULL));
    setlocale(LC_CTYPE, "C");
    _M_toupper = *__ctype_toupper_loc();
    _M_tolower = *__ctype_tolower_loc();
    _M_table = __table ? __table : *__ctype_b_loc();
    setlocale(LC_CTYPE, __old);
    free(__old);
    _M_c_locale_ctype = _S_get_c_locale();
  }

  ctype<char>::ctype(const mask* __table, bool __del, size_t __refs)
  : facet(__refs), _M_del(__table != 0 && __del)
  {
    char* __old=strdup(setlocale(LC_CTYPE, NULL));
    setlocale(LC_CTYPE, "C");
    _M_toupper = *__ctype_toupper_loc();
    _M_tolower = *__ctype_tolower_loc();
    _M_table = __table ? __table : *__ctype_b_loc();
    setlocale(LC_CTYPE, __old);
    free(__old);
    _M_c_locale_ctype = _S_get_c_locale();
  }

  char
  ctype<char>::do_toupper(char __c) const
  { return _M_toupper[static_cast<unsigned char>(__c)]; }

  const char*
  ctype<char>::do_toupper(char* __low, const char* __high) const
  {
    while (__low < __high)
      {
	*__low = _M_toupper[static_cast<unsigned char>(*__low)];
	++__low;
      }
    return __high;
  }

  char
  ctype<char>::do_tolower(char __c) const
  { return _M_tolower[static_cast<unsigned char>(__c)]; }

  const char* 
  ctype<char>::do_tolower(char* __low, const char* __high) const
  {
    while (__low < __high)
      {
	*__low = _M_tolower[static_cast<unsigned char>(*__low)];
	++__low;
      }
    return __high;
  }
