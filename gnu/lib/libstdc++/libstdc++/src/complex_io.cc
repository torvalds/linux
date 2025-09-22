// The template and inlines for the -*- C++ -*- complex number classes.

// Copyright (C) 2000, 2001 Free Software Foundation, Inc.
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

#include <complex>

namespace std
{
  template
    basic_istream<char, char_traits<char> >&
    operator>>(basic_istream<char, char_traits<char> >&, complex<float>&);

  template
    basic_ostream<char, char_traits<char> >&
    operator<<(basic_ostream<char, char_traits<char> >&, 
	       const complex<float>&);

  template
    basic_istream<char, char_traits<char> >&
    operator>>(basic_istream<char, char_traits<char> >&, complex<double>&);

  template
    basic_ostream<char, char_traits<char> >&
    operator<<(basic_ostream<char, char_traits<char> >&, 
	       const complex<double>&);

  template
    basic_istream<char, char_traits<char> >&
    operator>>(basic_istream<char, char_traits<char> >&, 
	       complex<long double>&);

  template
    basic_ostream<char, char_traits<char> >&
    operator<<(basic_ostream<char, char_traits<char> >&,
               const complex<long double>&);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template
    basic_istream<wchar_t, char_traits<wchar_t> >&
    operator>>(basic_istream<wchar_t, char_traits<wchar_t> >&,
               complex<float>&);

  template
    basic_ostream<wchar_t, char_traits<wchar_t> >&
    operator<<(basic_ostream<wchar_t, char_traits<wchar_t> >&,
               const complex<float>&);

  template
    basic_istream<wchar_t, char_traits<wchar_t> >&
    operator>>(basic_istream<wchar_t, char_traits<wchar_t> >&,
               complex<double>&);

  template
    basic_ostream<wchar_t, char_traits<wchar_t> >&
    operator<<(basic_ostream<wchar_t, char_traits<wchar_t> >&,
               const complex<double>&);

  template
    basic_istream<wchar_t, char_traits<wchar_t> >&
    operator>>(basic_istream<wchar_t, char_traits<wchar_t> >&,
               complex<long double>&);

  template
    basic_ostream<wchar_t, char_traits<wchar_t> >&
    operator<<(basic_ostream<wchar_t, char_traits<wchar_t> >&,
               const complex<long double>&);
#endif //_GLIBCPP_USE_WCHAR_T
} // namespace std
