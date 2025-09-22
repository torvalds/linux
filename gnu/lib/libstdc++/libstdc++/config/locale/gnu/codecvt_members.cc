// std::codecvt implementation details, GNU version -*- C++ -*-

// Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.2.1.5 - Template class codecvt
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>
#include <bits/c++locale_internal.h>

namespace std
{
  // Specializations.
#ifdef _GLIBCPP_USE_WCHAR_T
  codecvt_base::result
  codecvt<wchar_t, char, mbstate_t>::
  do_out(state_type& __state, const intern_type* __from, 
	 const intern_type* __from_end, const intern_type*& __from_next,
	 extern_type* __to, extern_type* __to_end,
	 extern_type*& __to_next) const
  {
    result __ret = error;
    size_t __len = min(__from_end - __from, __to_end - __to);
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __c_locale __old = __uselocale(_S_c_locale);
#endif
    size_t __conv = wcsrtombs(__to, &__from, __len, &__state);
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __uselocale(__old);
#endif

    if (__conv == __len)
      {
	__from_next = __from;
	__to_next = __to + __conv;
	__ret = ok;
      }
    else if (__conv > 0 && __conv < __len)
      {
	__from_next = __from;
	__to_next = __to + __conv;
	__ret = partial;
      }
    else
      __ret = error;
	
    return __ret; 
  }
  
  codecvt_base::result
  codecvt<wchar_t, char, mbstate_t>::
  do_in(state_type& __state, const extern_type* __from, 
	const extern_type* __from_end, const extern_type*& __from_next,
	intern_type* __to, intern_type* __to_end,
	intern_type*& __to_next) const
  {
    result __ret = error;
    size_t __len = min(__from_end - __from, __to_end - __to);
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __c_locale __old = __uselocale(_S_c_locale);
#endif
    size_t __conv = mbsrtowcs(__to, &__from, __len, &__state);
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
    __uselocale(__old);
#endif

    if (__conv == __len)
      {
	__from_next = __from;
	__to_next = __to + __conv;
	__ret = ok;
      }
    else if (__conv > 0 && __conv < __len)
      {
	__from_next = __from;
	__to_next = __to + __conv;
	__ret = partial;
      }
    else
      __ret = error;
	
    return __ret; 
  }
#endif
}
