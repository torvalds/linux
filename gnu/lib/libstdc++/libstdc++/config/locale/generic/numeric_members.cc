// std::numpunct implementation details, generic version -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.2.3.1.2  numpunct virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

namespace std
{
  template<> 
    void
    numpunct<char>::_M_initialize_numpunct(__c_locale)
    {
      // "C" locale
      _M_decimal_point = '.';
      _M_thousands_sep = ',';
      _M_grouping = "";
      _M_truename = "true";
      _M_falsename = "false";
    }

  template<> 
    numpunct<char>::~numpunct()
    { }
      
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<> 
    void
    numpunct<wchar_t>::_M_initialize_numpunct(__c_locale)
    {
      // "C" locale
      _M_decimal_point = L'.';
      _M_thousands_sep = L',';
      _M_grouping = "";
      _M_truename = L"true";
      _M_falsename = L"false";
    }

  template<> 
    numpunct<wchar_t>::~numpunct()
    { }
#endif
}
