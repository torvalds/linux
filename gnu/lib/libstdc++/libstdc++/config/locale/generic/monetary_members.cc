// std::moneypunct implementation details, generic version -*- C++ -*-

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
// ISO C++ 14882: 22.2.6.3.2  moneypunct virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

namespace std
{
  // Construct and return valid pattern consisting of some combination of:
  // space none symbol sign value
  money_base::pattern
  money_base::_S_construct_pattern(char, char, char)
  { return _S_default_pattern; }

  template<> 
    void
    moneypunct<char, true>::_M_initialize_moneypunct(__c_locale, const char*)
    {
      // "C" locale
      _M_decimal_point = '.';
      _M_thousands_sep = ',';
      _M_grouping = "";
      _M_curr_symbol = "";
      _M_positive_sign = "";
      _M_negative_sign = "";
      _M_frac_digits = 0;
      _M_pos_format = money_base::_S_default_pattern;
      _M_neg_format = money_base::_S_default_pattern;
    }

  template<> 
    void
    moneypunct<char, false>::_M_initialize_moneypunct(__c_locale, const char*)
    {
      // "C" locale
      _M_decimal_point = '.';
      _M_thousands_sep = ',';
      _M_grouping = "";
      _M_curr_symbol = "";
      _M_positive_sign = "";
      _M_negative_sign = "";
      _M_frac_digits = 0;
      _M_pos_format = money_base::_S_default_pattern;
      _M_neg_format = money_base::_S_default_pattern;
    }

  template<> 
    moneypunct<char, true>::~moneypunct()
    { }

  template<> 
    moneypunct<char, false>::~moneypunct()
    { }

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<> 
    void
    moneypunct<wchar_t, true>::_M_initialize_moneypunct(__c_locale, 
							const char*)
    {
      // "C" locale
      _M_decimal_point = L'.';
      _M_thousands_sep = L',';
      _M_grouping = "";
      _M_curr_symbol = L"";
      _M_positive_sign = L"";
      _M_negative_sign = L"";
      _M_frac_digits = 0;
      _M_pos_format = money_base::_S_default_pattern;
      _M_neg_format = money_base::_S_default_pattern;
    }

  template<> 
    void
    moneypunct<wchar_t, false>::_M_initialize_moneypunct(__c_locale, 
							 const char*)
    {
      // "C" locale
      _M_decimal_point = L'.';
      _M_thousands_sep = L',';
      _M_grouping = "";
      _M_curr_symbol = L"";
      _M_positive_sign = L"";
      _M_negative_sign = L"";
      _M_frac_digits = 0;
      _M_pos_format = money_base::_S_default_pattern;
      _M_neg_format = money_base::_S_default_pattern;
    }

  template<> 
    moneypunct<wchar_t, true>::~moneypunct()
    { }

  template<> 
    moneypunct<wchar_t, false>::~moneypunct()
    { }
#endif
}
