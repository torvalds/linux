// Locale support -*- C++ -*-

// Copyright (C) 1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
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

#include <cstdlib>
#include <clocale>
#include <cstring>
#include <locale>

namespace std
{
  // moneypunct, money_get, and money_put
  template class moneypunct<char, false>;
  template class moneypunct<char, true>;
  template class moneypunct_byname<char, false>;
  template class moneypunct_byname<char, true>;
  template class money_get<char, istreambuf_iterator<char> >;
  template class money_put<char, ostreambuf_iterator<char> >;
  template class __locale_cache<numpunct<char> >;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template class moneypunct<wchar_t, false>;
  template class moneypunct<wchar_t, true>;
  template class moneypunct_byname<wchar_t, false>;
  template class moneypunct_byname<wchar_t, true>;
  template class money_get<wchar_t, istreambuf_iterator<wchar_t> >;
  template class money_put<wchar_t, ostreambuf_iterator<wchar_t> >;
  template class __locale_cache<numpunct<wchar_t> >;
#endif

  // numpunct, numpunct_byname, num_get, and num_put
  template class numpunct<char>;
  template class numpunct_byname<char>;
  template class num_get<char, istreambuf_iterator<char> >;
  template class num_put<char, ostreambuf_iterator<char> >; 
  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, 
		   long) const;

  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, 
		   unsigned long) const;

#ifdef _GLIBCPP_USE_LONG_LONG
  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, 
		   long long) const;

  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, 
		   unsigned long long) const;
#endif

  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_float(ostreambuf_iterator<char>, ios_base&, char, char, 
		     double) const;

  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_float(ostreambuf_iterator<char>, ios_base&, char, char, 
		     long double) const;
  
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template class numpunct<wchar_t>;
  template class numpunct_byname<wchar_t>;
  template class num_get<wchar_t, istreambuf_iterator<wchar_t> >;
  template class num_put<wchar_t, ostreambuf_iterator<wchar_t> >;

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, 
		   long) const;

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, 
		   unsigned long) const;

#ifdef _GLIBCPP_USE_LONG_LONG
  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t,
		   long long) const;

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t,
		   unsigned long long) const;
#endif

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_float(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, char, 
		     double) const;

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_float(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, char, 
		     long double) const;
#endif

#if 1
      // XXX GLIBCXX_ABI Deprecated, compatibility only.
  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, char, char, 
		   long) const;

  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, char, char, 
		   unsigned long) const;

#ifdef _GLIBCPP_USE_LONG_LONG
  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, char, char, 
		   long long) const;

  template
    ostreambuf_iterator<char>
    num_put<char, ostreambuf_iterator<char> >::
    _M_convert_int(ostreambuf_iterator<char>, ios_base&, char, char, char,
		   unsigned long long) const;
#endif

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, char, 
		   char, long) const;

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, char, 
		   char, unsigned long) const;

#ifdef _GLIBCPP_USE_LONG_LONG
  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, char, 
		   char, long long) const;

  template
    ostreambuf_iterator<wchar_t>
    num_put<wchar_t, ostreambuf_iterator<wchar_t> >::
    _M_convert_int(ostreambuf_iterator<wchar_t>, ios_base&, wchar_t, char, 
		   char, unsigned long long) const;
#endif
#endif

#endif

  // time_get and time_put
  template class __timepunct<char>;
  template class time_put<char, ostreambuf_iterator<char> >;
  template class time_put_byname<char, ostreambuf_iterator<char> >;
  template class time_get<char, istreambuf_iterator<char> >;
  template class time_get_byname<char, istreambuf_iterator<char> >;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template class __timepunct<wchar_t>;
  template class time_put<wchar_t, ostreambuf_iterator<wchar_t> >;
  template class time_put_byname<wchar_t, ostreambuf_iterator<wchar_t> >;
  template class time_get<wchar_t, istreambuf_iterator<wchar_t> >;
  template class time_get_byname<wchar_t, istreambuf_iterator<wchar_t> >;
#endif

  // messages
  template class messages<char>;
  template class messages_byname<char>;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template class messages<wchar_t>;
  template class messages_byname<wchar_t>;
#endif
  
  // ctype
  inline template class __ctype_abstract_base<char>;
  template class ctype_byname<char>;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  inline template class __ctype_abstract_base<wchar_t>;
  template class ctype_byname<wchar_t>;
#endif
  
  // codecvt
  inline template class __codecvt_abstract_base<char, char, mbstate_t>;
  template class codecvt_byname<char, char, mbstate_t>;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  inline template class __codecvt_abstract_base<wchar_t, char, mbstate_t>;
  template class codecvt_byname<wchar_t, char, mbstate_t>;
#endif

  // collate
  template class collate<char>;
  template class collate_byname<char>;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template class collate<wchar_t>;
  template class collate_byname<wchar_t>;
#endif
    
  // use_facet
  // NB: use_facet<ctype> is specialized
  template
    const codecvt<char, char, mbstate_t>& 
    use_facet<codecvt<char, char, mbstate_t> >(const locale&);

  template
    const collate<char>& 
    use_facet<collate<char> >(const locale&);

  template
    const numpunct<char>& 
    use_facet<numpunct<char> >(const locale&);

  template 
    const num_put<char>& 
    use_facet<num_put<char> >(const locale&);

  template 
    const num_get<char>& 
    use_facet<num_get<char> >(const locale&);

  template
    const moneypunct<char, true>& 
    use_facet<moneypunct<char, true> >(const locale&);

  template
    const moneypunct<char, false>& 
    use_facet<moneypunct<char, false> >(const locale&);

  template 
    const money_put<char>& 
    use_facet<money_put<char> >(const locale&);

  template 
    const money_get<char>& 
    use_facet<money_get<char> >(const locale&);

  template
    const __timepunct<char>& 
    use_facet<__timepunct<char> >(const locale&);

  template 
    const time_put<char>& 
    use_facet<time_put<char> >(const locale&);

  template 
    const time_get<char>& 
    use_facet<time_get<char> >(const locale&);

  template 
    const messages<char>& 
    use_facet<messages<char> >(const locale&);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template
    const codecvt<wchar_t, char, mbstate_t>& 
    use_facet<codecvt<wchar_t, char, mbstate_t> >(locale const&);

  template
    const collate<wchar_t>& 
    use_facet<collate<wchar_t> >(const locale&);

  template
    const numpunct<wchar_t>& 
    use_facet<numpunct<wchar_t> >(const locale&);

  template 
    const num_put<wchar_t>& 
    use_facet<num_put<wchar_t> >(const locale&);

  template 
    const num_get<wchar_t>& 
    use_facet<num_get<wchar_t> >(const locale&);

  template
    const moneypunct<wchar_t, true>& 
    use_facet<moneypunct<wchar_t, true> >(const locale&);

  template
    const moneypunct<wchar_t, false>& 
    use_facet<moneypunct<wchar_t, false> >(const locale&);
 
  template 
    const money_put<wchar_t>& 
    use_facet<money_put<wchar_t> >(const locale&);

  template 
    const money_get<wchar_t>& 
    use_facet<money_get<wchar_t> >(const locale&);

  template
    const __timepunct<wchar_t>& 
    use_facet<__timepunct<wchar_t> >(const locale&);

  template 
    const time_put<wchar_t>& 
    use_facet<time_put<wchar_t> >(const locale&);

  template 
    const time_get<wchar_t>& 
    use_facet<time_get<wchar_t> >(const locale&);

  template 
    const messages<wchar_t>& 
    use_facet<messages<wchar_t> >(const locale&);
#endif

  // has_facet
  template 
    bool
    has_facet<ctype<char> >(const locale&);

  template 
    bool
    has_facet<codecvt<char, char, mbstate_t> >(const locale&);

  template 
    bool
    has_facet<collate<char> >(const locale&);

  template 
    bool
    has_facet<numpunct<char> >(const locale&);

  template 
    bool
    has_facet<num_put<char> >(const locale&);

  template 
    bool
    has_facet<num_get<char> >(const locale&);

  template 
    bool
    has_facet<moneypunct<char> >(const locale&);

  template 
    bool
    has_facet<money_put<char> >(const locale&);

  template 
    bool
    has_facet<money_get<char> >(const locale&);

  template 
    bool
    has_facet<__timepunct<char> >(const locale&);

  template 
    bool
    has_facet<time_put<char> >(const locale&);

  template 
    bool
    has_facet<time_get<char> >(const locale&);

  template 
    bool
    has_facet<messages<char> >(const locale&);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
 template 
    bool
    has_facet<ctype<wchar_t> >(const locale&);

  template 
    bool
    has_facet<codecvt<wchar_t, char, mbstate_t> >(const locale&);

  template 
    bool
    has_facet<collate<wchar_t> >(const locale&);

  template 
    bool
    has_facet<numpunct<wchar_t> >(const locale&);

  template 
    bool
    has_facet<num_put<wchar_t> >(const locale&);

  template 
    bool
    has_facet<num_get<wchar_t> >(const locale&);

  template 
    bool
    has_facet<moneypunct<wchar_t> >(const locale&);

  template 
    bool
    has_facet<money_put<wchar_t> >(const locale&);

  template 
    bool
    has_facet<money_get<wchar_t> >(const locale&);

  template 
    bool
    has_facet<__timepunct<wchar_t> >(const locale&);

  template 
    bool
    has_facet<time_put<wchar_t> >(const locale&);

  template 
    bool
    has_facet<time_get<wchar_t> >(const locale&);

  template 
    bool
    has_facet<messages<wchar_t> >(const locale&);
#endif

  // __use_cache
  template
    const __locale_cache<numpunct<char> >&
    __use_cache<numpunct<char> >(const locale& __loc);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
   template
    const __locale_cache<numpunct<wchar_t> >&
    __use_cache<numpunct<wchar_t> >(const locale& __loc);
#endif

  // locale
  template
    char*
    __add_grouping<char>(char*, char, char const*, char const*, 
			 char const*, char const*);

  template
    bool
    __verify_grouping<char>(const basic_string<char>&, basic_string<char>&);

  template class __pad<char, char_traits<char> >;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template
    wchar_t*
    __add_grouping<wchar_t>(wchar_t*, wchar_t, char const*, char const*, 
			    wchar_t const*, wchar_t const*);
  template
    bool
    __verify_grouping<wchar_t>(const basic_string<wchar_t>&, 
			       basic_string<wchar_t>&);

  template class __pad<wchar_t, char_traits<wchar_t> >;
#endif 

  template
    int
    __convert_from_v(char*, const int, const char*, double, 
		     const __c_locale&, int);

  template
    int
    __convert_from_v(char*, const int, const char*, long double, 
		     const __c_locale&, int);

  template
    int
    __convert_from_v(char*, const int, const char*, long, 
		     const __c_locale&, int);

  template
    int
    __convert_from_v(char*, const int, const char*, unsigned long, 
		     const __c_locale&, int);

#ifdef _GLIBCPP_USE_LONG_LONG
  template
    int
    __convert_from_v(char*, const int, const char*, long long, 
		     const __c_locale&, int);

  template
    int
    __convert_from_v(char*, const int, const char*, unsigned long long, 
		     const __c_locale&, int);
#endif

  template
    int
    __int_to_char(char*, const int, unsigned long, const char*, 
		  ios_base::fmtflags, bool);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template
    int
    __int_to_char(wchar_t*, const int, unsigned long, const wchar_t*, 
		  ios_base::fmtflags, bool);
#endif

#ifdef _GLIBCPP_USE_LONG_LONG
  template
    int
    __int_to_char(char*, const int, unsigned long long, const char*, 
		  ios_base::fmtflags, bool);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template
    int
    __int_to_char(wchar_t*, const int, unsigned long long, const wchar_t*,
		  ios_base::fmtflags, bool);
#endif
#endif
} // namespace std
