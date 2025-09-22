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

#include <locale>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Definitions for static const data members of time_base.
  template<> 
    const char*
    __timepunct_cache<char>::_S_timezones[14] =
    { 
      "GMT", "HST", "AKST", "PST", "MST", "CST", "EST", "AST", "NST", "CET", 
      "IST", "EET", "CST", "JST"  
    };
 
#ifdef _GLIBCXX_USE_WCHAR_T
  template<> 
    const wchar_t*
    __timepunct_cache<wchar_t>::_S_timezones[14] =
    { 
      L"GMT", L"HST", L"AKST", L"PST", L"MST", L"CST", L"EST", L"AST", 
      L"NST", L"CET", L"IST", L"EET", L"CST", L"JST"  
    };
#endif

  // Definitions for static const data members of money_base.
  const money_base::pattern 
  money_base::_S_default_pattern =  { {symbol, sign, none, value} };

  const char* money_base::_S_atoms = "-0123456789";

  const char* __num_base::_S_atoms_in = "-+xX0123456789abcdefABCDEF";
  const char* __num_base::_S_atoms_out ="-+xX0123456789abcdef0123456789ABCDEF";

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // According to the resolution of DR 231, about 22.2.2.2.2, p11,
  // "str.precision() is specified in the conversion specification".
  void
  __num_base::_S_format_float(const ios_base& __io, char* __fptr, char __mod)
  {
    ios_base::fmtflags __flags = __io.flags();
    *__fptr++ = '%';
    // [22.2.2.2.2] Table 60
    if (__flags & ios_base::showpos)
      *__fptr++ = '+';
    if (__flags & ios_base::showpoint)
      *__fptr++ = '#';

    // As per DR 231: _always_, not only when 
    // __flags & ios_base::fixed || __prec > 0
    *__fptr++ = '.';
    *__fptr++ = '*';

    if (__mod)
      *__fptr++ = __mod;
    ios_base::fmtflags __fltfield = __flags & ios_base::floatfield;
    // [22.2.2.2.2] Table 58
    if (__fltfield == ios_base::fixed)
      *__fptr++ = 'f';
    else if (__fltfield == ios_base::scientific)
      *__fptr++ = (__flags & ios_base::uppercase) ? 'E' : 'e';
    else
      *__fptr++ = (__flags & ios_base::uppercase) ? 'G' : 'g';
    *__fptr = '\0';
  }

_GLIBCXX_END_NAMESPACE

