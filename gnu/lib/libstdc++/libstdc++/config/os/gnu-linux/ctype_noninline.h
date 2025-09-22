// Locale support -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002
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
// ISO C++ 14882: 22.1  Locales
//
  
// Information as gleaned from /usr/include/ctype.h

#if _GLIBCPP_C_LOCALE_GNU
  const ctype_base::mask*
  ctype<char>::classic_table() throw()
  {
    locale::classic();
    return _S_c_locale->__ctype_b;
  }
#else
  const ctype_base::mask*
  ctype<char>::classic_table() throw()
  {
    const ctype_base::mask* __ret;
    char* __old = strdup(setlocale(LC_CTYPE, NULL));
    setlocale(LC_CTYPE, "C");
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __ret = *__ctype_b_loc();
#else
    __ret = __ctype_b;
#endif
    setlocale(LC_CTYPE, __old);
    free(__old);
    return __ret;
  }
#endif

#if _GLIBCPP_C_LOCALE_GNU
  ctype<char>::ctype(__c_locale __cloc, const mask* __table, bool __del, 
		     size_t __refs) 
  : __ctype_abstract_base<char>(__refs), _M_del(__table != 0 && __del)
  {
    _M_c_locale_ctype = _S_clone_c_locale(__cloc);
    _M_toupper = _M_c_locale_ctype->__ctype_toupper;
    _M_tolower = _M_c_locale_ctype->__ctype_tolower;
    _M_table = __table ? __table : _M_c_locale_ctype->__ctype_b;
  }
#else
  ctype<char>::ctype(__c_locale, const mask* __table, bool __del, 
		     size_t __refs) 
  : __ctype_abstract_base<char>(__refs), _M_del(__table != 0 && __del)
  {
    char* __old=strdup(setlocale(LC_CTYPE, NULL));
    setlocale(LC_CTYPE, "C");
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    _M_toupper = *__ctype_toupper_loc();
    _M_tolower = *__ctype_tolower_loc();
    _M_table = __table ? __table : *__ctype_b_loc();
#else
    _M_toupper = __ctype_toupper;
    _M_tolower = __ctype_tolower;
    _M_table = __table ? __table : __ctype_b;
#endif
    setlocale(LC_CTYPE, __old);
    free(__old);
    _M_c_locale_ctype = _S_c_locale;
  }
#endif

#if _GLIBCPP_C_LOCALE_GNU
  ctype<char>::ctype(const mask* __table, bool __del, size_t __refs) : 
  __ctype_abstract_base<char>(__refs), _M_del(__table != 0 && __del)
  {
    _M_c_locale_ctype = _S_c_locale; 
    _M_toupper = _M_c_locale_ctype->__ctype_toupper;
    _M_tolower = _M_c_locale_ctype->__ctype_tolower;
    _M_table = __table ? __table : _M_c_locale_ctype->__ctype_b;
  }
#else
  ctype<char>::ctype(const mask* __table, bool __del, size_t __refs) : 
  __ctype_abstract_base<char>(__refs), _M_del(__table != 0 && __del)
  {
    char* __old=strdup(setlocale(LC_CTYPE, NULL));
    setlocale(LC_CTYPE, "C");
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    _M_toupper = *__ctype_toupper_loc();
    _M_tolower = *__ctype_tolower_loc();
    _M_table = __table ? __table : *__ctype_b_loc();
#else
    _M_toupper = __ctype_toupper;
    _M_tolower = __ctype_tolower;
    _M_table = __table ? __table : __ctype_b;
#endif
    setlocale(LC_CTYPE, __old);
    free(__old);
    _M_c_locale_ctype = _S_c_locale;
  }
#endif

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
