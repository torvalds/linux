// Wrapper for underlying C-language localization -*- C++ -*-

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

#include <locale>

namespace std 
{
  void
  locale::facet::_S_create_c_locale(__c_locale&, const char*, __c_locale*)
  { }

  void
  locale::facet::_S_destroy_c_locale(__c_locale&)
  { }

  __c_locale
  locale::facet::_S_clone_c_locale(__c_locale&)
  { return __c_locale(); }

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
      
#ifdef _GLIBCXX_USE_WCHAR_T
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
#endif

  template<> 
    void
    moneypunct<char>::_M_initialize_moneypunct(__c_locale)
    {
      // "C" locale
      _M_decimal_point = '.';
      _M_thousands_sep = ',';
      _M_grouping = "";
      _M_curr_symbol = string_type();
      _M_positive_sign = string_type();
      _M_negative_sign = string_type();
      _M_frac_digits = 0;
      _M_pos_format = money_base::_S_default_pattern;
      _M_neg_format = money_base::_S_default_pattern;
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<> 
    void
    moneypunct<wchar_t>::_M_initialize_moneypunct(__c_locale)
    {
      // "C" locale
      _M_decimal_point = L'.';
      _M_thousands_sep = L',';
      _M_grouping = "";
      _M_curr_symbol = string_type();
      _M_positive_sign = string_type();
      _M_negative_sign = string_type();
      _M_frac_digits = 0;
      _M_pos_format = money_base::_S_default_pattern;
      _M_neg_format = money_base::_S_default_pattern;
    }
#endif
}  // namespace std

