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

#include <locale>

namespace std 
{
  // XXX At some point, just rename this file to ctype_members_char.cc
  // and compile it as a separate file instead of including it here.
  // Platform-specific initialization code for ctype tables.
  #include <bits/ctype_noninline.h>

  // Definitions for locale::id of standard facets that are specialized.
  locale::id ctype<char>::id;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  locale::id ctype<wchar_t>::id;
#endif

  template<>
    const ctype<char>&
    use_facet<ctype<char> >(const locale& __loc)
    {
      size_t __i = ctype<char>::id._M_id();
      const locale::_Impl* __tmp = __loc._M_impl;
      return static_cast<const ctype<char>&>(*(__tmp->_M_facets[__i]));
    }

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    const ctype<wchar_t>&
    use_facet<ctype<wchar_t> >(const locale& __loc)
    {
      size_t __i = ctype<wchar_t>::id._M_id();
      const locale::_Impl* __tmp = __loc._M_impl;
      return static_cast<const ctype<wchar_t>&>(*(__tmp->_M_facets[__i]));
    }
#endif

  // Definitions for static const data members of ctype_base.
  const ctype_base::mask ctype_base::space;
  const ctype_base::mask ctype_base::print;
  const ctype_base::mask ctype_base::cntrl;
  const ctype_base::mask ctype_base::upper;
  const ctype_base::mask ctype_base::lower;
  const ctype_base::mask ctype_base::alpha;
  const ctype_base::mask ctype_base::digit;
  const ctype_base::mask ctype_base::punct;
  const ctype_base::mask ctype_base::xdigit;
  const ctype_base::mask ctype_base::alnum;
  const ctype_base::mask ctype_base::graph;

  const size_t ctype<char>::table_size;

  ctype<char>::~ctype()
  { 
    _S_destroy_c_locale(_M_c_locale_ctype);
    if (_M_del) 
      delete[] this->table(); 
  }

  // These are dummy placeholders as these virtual functions are never called.
  bool 
  ctype<char>::do_is(mask, char_type) const 
  { return false; }
  
  const char*
  ctype<char>::do_is(const char_type* __c, const char_type*, mask*) const 
  { return __c; }
  
  const char*
  ctype<char>::do_scan_is(mask, const char_type* __c, const char_type*) const 
  { return __c; }

  const char* 
  ctype<char>::do_scan_not(mask, const char_type* __c, const char_type*) const
  { return __c; }

  char
  ctype<char>::do_widen(char __c) const
  { return __c; }
  
  const char* 
  ctype<char>::do_widen(const char* __lo, const char* __hi, char* __dest) const
  {
    memcpy(__dest, __lo, __hi - __lo);
    return __hi;
  }
  
  char
  ctype<char>::do_narrow(char __c, char /*__dfault*/) const
  { return __c; }
  
  const char* 
  ctype<char>::do_narrow(const char* __lo, const char* __hi, 
			 char /*__dfault*/, char* __dest) const
  {
    memcpy(__dest, __lo, __hi - __lo);
    return __hi;
  }

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  ctype<wchar_t>::ctype(size_t __refs) 
  : __ctype_abstract_base<wchar_t>(__refs)
  { _M_c_locale_ctype = _S_c_locale; }

  ctype<wchar_t>::ctype(__c_locale __cloc, size_t __refs) 
  : __ctype_abstract_base<wchar_t>(__refs) 
  { _M_c_locale_ctype = _S_clone_c_locale(__cloc); }

  ctype<wchar_t>::~ctype() 
  { _S_destroy_c_locale(_M_c_locale_ctype); }

  template<>
    ctype_byname<wchar_t>::ctype_byname(const char* __s, size_t __refs)
    : ctype<wchar_t>(__refs) 
    { 	
      _S_destroy_c_locale(_M_c_locale_ctype);
      _S_create_c_locale(_M_c_locale_ctype, __s); 
    }
#endif
} // namespace std

