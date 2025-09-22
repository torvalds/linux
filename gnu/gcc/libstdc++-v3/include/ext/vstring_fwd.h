// Versatile string forward -*- C++ -*-

// Copyright (C) 2005 Free Software Foundation, Inc.
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

/** @file ext/vstring_fwd.h
 *  This file is a GNU extension to the Standard C++ Library.
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _VSTRING_FWD_H
#define _VSTRING_FWD_H 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <bits/char_traits.h>
#include <memory> 	// For allocator.

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  template<typename _CharT, typename _Traits, typename _Alloc>
    class __sso_string_base;

  template<typename _CharT, typename _Traits, typename _Alloc>
    class __rc_string_base;

  template<typename _CharT, typename _Traits = std::char_traits<_CharT>,
           typename _Alloc = std::allocator<_CharT>,
	   template
	   <typename, typename, typename> class _Base = __sso_string_base>
    class __versa_string;

  typedef __versa_string<char>                              __vstring;
  typedef __vstring                                         __sso_string;
  typedef 
  __versa_string<char, std::char_traits<char>,
		 std::allocator<char>, __rc_string_base>    __rc_string;

#ifdef _GLIBCXX_USE_WCHAR_T
  typedef __versa_string<wchar_t>                           __wvstring;
  typedef __wvstring                                        __wsso_string;
  typedef
  __versa_string<wchar_t, std::char_traits<wchar_t>,
		 std::allocator<wchar_t>, __rc_string_base> __wrc_string;
#endif  

_GLIBCXX_END_NAMESPACE

#endif /* _VSTRING_FWD_H */
