// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
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

#include <clocale>
#include <cstring>
#include <cctype>
#include <cwctype>     // For towupper, etc.
#include <locale>
#include <bits/atomicity.h>

namespace __gnu_cxx
{
  // Defined in globals.cc.
  extern std::locale 		c_locale;
  extern std::locale::_Impl 	c_locale_impl;
} // namespace __gnu_cxx

namespace std 
{
  using namespace __gnu_cxx;

  // Definitions for static const data members of locale.
  const locale::category 	locale::none;
  const locale::category 	locale::ctype;
  const locale::category 	locale::numeric;
  const locale::category 	locale::collate;
  const locale::category 	locale::time;
  const locale::category 	locale::monetary;
  const locale::category 	locale::messages;
  const locale::category 	locale::all;

  // In the future, GLIBCXX_ABI > 5 should remove all uses of
  // _GLIBCPP_ASM_SYMVER in this file, and remove exports of any
  // static data members of locale.
  locale::_Impl* 		locale::_S_classic;
  locale::_Impl* 		locale::_S_global; 
  const size_t 			locale::_S_categories_size;
  _GLIBCPP_ASM_SYMVER(_ZNSt6locale18_S_categories_sizeE, _ZNSt6locale17_S_num_categoriesE, GLIBCPP_3.2)
  const size_t 			locale::_S_extra_categories_size;

  // Definitions for static const data members of locale::id
  _Atomic_word locale::id::_S_highwater;  // init'd to 0 by linker

  // Definitions for static const data members of locale::_Impl
  const locale::id* const
  locale::_Impl::_S_id_ctype[] =
  {
    &std::ctype<char>::id, 
    &codecvt<char, char, mbstate_t>::id,
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    &std::ctype<wchar_t>::id,
    &codecvt<wchar_t, char, mbstate_t>::id,
#endif
    0
  };

  const locale::id* const
  locale::_Impl::_S_id_numeric[] =
  {
    &num_get<char>::id,  
    &num_put<char>::id,  
    &numpunct<char>::id, 
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    &num_get<wchar_t>::id,
    &num_put<wchar_t>::id,
    &numpunct<wchar_t>::id,
#endif
    0
  };
  
  const locale::id* const
  locale::_Impl::_S_id_collate[] =
  {
    &std::collate<char>::id,
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    &std::collate<wchar_t>::id,
#endif
    0
  };

  const locale::id* const
  locale::_Impl::_S_id_time[] =
  {
    &__timepunct<char>::id, 
    &time_get<char>::id, 
    &time_put<char>::id, 
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    &__timepunct<wchar_t>::id, 
    &time_get<wchar_t>::id,
    &time_put<wchar_t>::id,
#endif
    0
  };
  
  const locale::id* const
  locale::_Impl::_S_id_monetary[] =
  {
    &money_get<char>::id,        
    &money_put<char>::id,        
    &moneypunct<char, false>::id, 
    &moneypunct<char, true >::id, 
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    &money_get<wchar_t>::id,
    &money_put<wchar_t>::id,
    &moneypunct<wchar_t, false>::id,
    &moneypunct<wchar_t, true >::id,
#endif
    0
  };

  const locale::id* const
  locale::_Impl::_S_id_messages[] =
  {
    &std::messages<char>::id, 
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    &std::messages<wchar_t>::id,
#endif
    0
  };
  
  const locale::id* const* const
  locale::_Impl::_S_facet_categories[] =
  {
    // Order must match the decl order in class locale.
    locale::_Impl::_S_id_ctype,
    locale::_Impl::_S_id_numeric,
    locale::_Impl::_S_id_collate,
    locale::_Impl::_S_id_time,
    locale::_Impl::_S_id_monetary,
    locale::_Impl::_S_id_messages,
    0
  };

  locale::locale() throw()
  { 
    _S_initialize(); 
    (_M_impl = _S_global)->_M_add_reference(); 
  }

  locale::locale(const locale& __other) throw()
  { (_M_impl = __other._M_impl)->_M_add_reference(); }

  // This is used to initialize global and classic locales, and
  // assumes that the _Impl objects are constructed correctly.
  // The lack of a reference increment is intentional.
  locale::locale(_Impl* __ip) throw() : _M_impl(__ip)
  { }

  locale::locale(const char* __s)
  {
    if (__s)
      {
	_S_initialize(); 
	if (strcmp(__s, "C") == 0 || strcmp(__s, "POSIX") == 0)
	  (_M_impl = _S_classic)->_M_add_reference();
	else if (strcmp(__s, "") != 0)
	  _M_impl = new _Impl(__s, 1);
	else
	  {
	    // Get it from the environment.
	    char* __env = getenv("LC_ALL");
	    // If LC_ALL is set we are done.
	    if (__env && strcmp(__env, "") != 0)
	      {
		if (strcmp(__env, "C") == 0 || strcmp(__env, "POSIX") == 0)
		  (_M_impl = _S_classic)->_M_add_reference();
		else
		  _M_impl = new _Impl(__env, 1);
	      }
	    else
	      {
		string __res;
		// LANG may set a default different from "C".
		char* __env = getenv("LANG");
		if (!__env || strcmp(__env, "") == 0 || strcmp(__env, "C") == 0
		    || strcmp(__env, "POSIX") == 0)
		  __res = "C";
		else 
		  __res = __env;
		
		// Scan the categories looking for the first one
		// different from LANG.
		size_t __i = 0;
		if (__res == "C")
		  for (; __i < _S_categories_size
			 + _S_extra_categories_size; ++__i)
		    {
		      __env = getenv(_S_categories[__i]);
		      if (__env && strcmp(__env, "") != 0 
			  && strcmp(__env, "C") != 0 
			  && strcmp(__env, "POSIX") != 0) 
			break;
		    }
		else
		  for (; __i < _S_categories_size
			 + _S_extra_categories_size; ++__i)
		    {
		      __env = getenv(_S_categories[__i]);
		      if (__env && strcmp(__env, "") != 0 
			  && __res != __env) 
			break;
		    }
	
		// If one is found, build the complete string of
		// the form LC_CTYPE=xxx;LC_NUMERIC=yyy; and so on...
		if (__i < _S_categories_size + _S_extra_categories_size)
		  {
		    string __str;
		    for (size_t __j = 0; __j < __i; ++__j)
		      {
			__str += _S_categories[__j];
			__str += '=';
			__str += __res;
			__str += ';';
		      }
		    __str += _S_categories[__i];
		    __str += '=';
		    __str += __env;
		    __str += ';';
		    __i++;
		    for (; __i < _S_categories_size
			   + _S_extra_categories_size; ++__i)
		      {
			__env = getenv(_S_categories[__i]);
			if (!__env || strcmp(__env, "") == 0)
			  {
			    __str += _S_categories[__i];
			    __str += '=';
			    __str += __res;
			    __str += ';';
			  }
			else if (strcmp(__env, "C") == 0
				 || strcmp(__env, "POSIX") == 0)
			  {
			    __str += _S_categories[__i];
			    __str += "=C;";
			  }
			else
			  {
			    __str += _S_categories[__i];
			    __str += '=';
			    __str += __env;
			    __str += ';';
			  }
		      }
		    __str.erase(__str.end() - 1);
		    _M_impl = new _Impl(__str.c_str(), 1);
		  }
		// ... otherwise either an additional instance of
		// the "C" locale or LANG.
		else if (__res == "C")
		  (_M_impl = _S_classic)->_M_add_reference();
		else
		  _M_impl = new _Impl(__res.c_str(), 1);
	      }
	  }
      }
    else
      __throw_runtime_error("attempt to create locale from NULL name");
  }

  locale::locale(const locale& __base, const char* __s, category __cat)
  { 
    // NB: There are complicated, yet more efficient ways to do
    // this. Building up locales on a per-category way is tedious, so
    // let's do it this way until people complain.
    locale __add(__s);
    _M_coalesce(__base, __add, __cat);
  }

  locale::locale(const locale& __base, const locale& __add, category __cat)
  { _M_coalesce(__base, __add, __cat); }

  locale::~locale() throw()
  { _M_impl->_M_remove_reference(); }

  bool
  locale::operator==(const locale& __rhs) const throw()
  {
    string __name = this->name();
    return (_M_impl == __rhs._M_impl 
	    || (__name != "*" && __name == __rhs.name()));
  }

  const locale&
  locale::operator=(const locale& __other) throw()
  {
    __other._M_impl->_M_add_reference();
    _M_impl->_M_remove_reference();
    _M_impl = __other._M_impl;
    return *this;
  }

  locale
  locale::global(const locale& __other)
  {
    // XXX MT
    _S_initialize();
    _Impl* __old = _S_global;
    __other._M_impl->_M_add_reference();
    _S_global = __other._M_impl; 
    if (_S_global->_M_check_same_name() 
	&& (strcmp(_S_global->_M_names[0], "*") != 0))
      setlocale(LC_ALL, __other.name().c_str());

    // Reference count sanity check: one reference removed for the
    // subsition of __other locale, one added by return-by-value. Net
    // difference: zero. When the returned locale object's destrutor
    // is called, then the reference count is decremented and possibly
    // destroyed.
    return locale(__old);
  }

  string
  locale::name() const
  {
    string __ret;
    if (_M_impl->_M_check_same_name())
      __ret = _M_impl->_M_names[0];
    else
      {
	__ret += _S_categories[0];
	__ret += '=';
	__ret += _M_impl->_M_names[0]; 
	for (size_t __i = 1; 
	     __i < _S_categories_size + _S_extra_categories_size; 
	     ++__i)
	  {
	    __ret += ';';
	    __ret += _S_categories[__i];
	    __ret += '=';
	    __ret += _M_impl->_M_names[__i];
	  }
      }
    return __ret;
  }

  const locale&
  locale::classic()
  {
    // Locking protocol: singleton-called-before-threading-starts
    if (!_S_classic)
      {
	try 
	  {
	    // 26 Standard facets, 2 references.
	    // One reference for _S_classic, one for _S_global
	    _S_classic = new (&c_locale_impl) _Impl(0, 2, true);
	    _S_global = _S_classic; 	    
	    new (&c_locale) locale(_S_classic);
	  }
	catch(...) 
	  {
	    // Just call destructor, so that locale_impl_c's memory is
	    // not deallocated via a call to delete.
	    if (_S_classic)
	      _S_classic->~_Impl();
	    _S_classic = _S_global = 0;
	    __throw_exception_again;
	  }
      }
    return c_locale;
  }

  void
  locale::_M_coalesce(const locale& __base, const locale& __add, 
		      category __cat)
  {
    __cat = _S_normalize_category(__cat);  
    _M_impl = new _Impl(*__base._M_impl, 1);  

    try 
      { _M_impl->_M_replace_categories(__add._M_impl, __cat); }
    catch (...) 
      { 
	_M_impl->_M_remove_reference(); 
	__throw_exception_again;
      }
  }

  locale::category
  locale::_S_normalize_category(category __cat) 
  {
    int __ret = 0;
    if (__cat == none || (__cat & all) && !(__cat & ~all))
      __ret = __cat;
    else
      {
	// NB: May be a C-style "LC_ALL" category; convert.
	switch (__cat)
	  {
	  case LC_COLLATE:  
	    __ret = collate; 
	    break;
	  case LC_CTYPE:    
	    __ret = ctype;
	    break;
	  case LC_MONETARY: 
	    __ret = monetary;
	    break;
	  case LC_NUMERIC:  
	    __ret = numeric;
	    break;
	  case LC_TIME:     
	    __ret = time; 
	    break;
#ifdef _GLIBCPP_HAVE_LC_MESSAGES
	  case LC_MESSAGES: 
	    __ret = messages;
	    break;
#endif	
	  case LC_ALL:      
	    __ret = all;
	    break;
	  default:
	    __throw_runtime_error("bad locale category");
	  }
      }
    return __ret;
  }

  __c_locale
  locale::facet::_S_c_locale;
  
  char locale::facet::_S_c_name[2];

  locale::facet::
  ~facet() { }

  locale::facet::
  facet(size_t __refs) throw() : _M_references(__refs ? 1 : 0) 
  { }

  void  
  locale::facet::
  _M_add_reference() throw()
  { __atomic_add(&_M_references, 1); }

  void  
  locale::facet::
  _M_remove_reference() throw()
  {
    if (__exchange_and_add(&_M_references, -1) == 1)
      {
        try 
	  { delete this; }  
	catch (...) 
	  { }
      }
  }
  
  locale::id::id() 
  { }

  // Definitions for static const data members of time_base
  template<> 
    const char*
    __timepunct<char>::_S_timezones[14] =
    { 
      "GMT", "HST", "AKST", "PST", "MST", "CST", "EST", "AST", "NST", "CET", 
      "IST", "EET", "CST", "JST"  
    };
 
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<> 
    const wchar_t*
    __timepunct<wchar_t>::_S_timezones[14] =
    { 
      L"GMT", L"HST", L"AKST", L"PST", L"MST", L"CST", L"EST", L"AST", 
      L"NST", L"CET", L"IST", L"EET", L"CST", L"JST"  
    };
#endif

  // Definitions for static const data members of money_base
  const money_base::pattern 
  money_base::_S_default_pattern =  { {symbol, sign, none, value} };

  const char* __num_base::_S_atoms_in = "0123456789eEabcdfABCDF";
  const char* __num_base::_S_atoms_out ="-+xX0123456789abcdef0123456789ABCDEF";

  // _GLIBCPP_RESOLVE_LIB_DEFECTS
  // According to the resolution of DR 231, about 22.2.2.2.2, p11,
  // "str.precision() is specified in the conversion specification".
  void
  __num_base::_S_format_float(const ios_base& __io, char* __fptr,
			      char __mod, streamsize/* unused post DR 231 */)
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
  
  void
  __num_base::_S_format_int(const ios_base& __io, char* __fptr, char __mod, 
			    char __modl)
  {
    ios_base::fmtflags __flags = __io.flags();
    *__fptr++ = '%';
    // [22.2.2.2.2] Table 60
    if (__flags & ios_base::showpos)
      *__fptr++ = '+';
    if (__flags & ios_base::showbase)
      *__fptr++ = '#';
    *__fptr++ = 'l';

    // For long long types.
    if (__modl)
      *__fptr++ = __modl;

    ios_base::fmtflags __bsefield = __flags & ios_base::basefield;
    if (__bsefield == ios_base::hex)
      *__fptr++ = (__flags & ios_base::uppercase) ? 'X' : 'x';
    else if (__bsefield == ios_base::oct)
      *__fptr++ = 'o';
    else
      *__fptr++ = __mod;
    *__fptr = '\0';
  }
} // namespace std

