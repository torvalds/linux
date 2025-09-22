// Wrapper for underlying C-language localization -*- C++ -*-

// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.8  Standard locale categories.
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>
#include <stdexcept>
#include <langinfo.h>
#include <bits/c++locale_internal.h>

namespace std 
{
  template<>
    void
    __convert_to_v(const char* __s, long& __v, ios_base::iostate& __err, 
		   const __c_locale& __cloc, int __base)
    {
      if (!(__err & ios_base::failbit))
      {
	char* __sanity;
	errno = 0;
	long __l = __strtol_l(__s, &__sanity, __base, __cloc);
	if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	  __v = __l;
	else
	  __err |= ios_base::failbit;
      }
    }

  template<>
    void
    __convert_to_v(const char* __s, unsigned long& __v, 
		   ios_base::iostate& __err, const __c_locale& __cloc, 
		   int __base)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  unsigned long __ul = __strtoul_l(__s, &__sanity, __base, __cloc);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __ul;
	  else
	    __err |= ios_base::failbit;
	}
    }

#ifdef _GLIBCPP_USE_LONG_LONG
  template<>
    void
    __convert_to_v(const char* __s, long long& __v, ios_base::iostate& __err, 
		   const __c_locale& __cloc, int __base)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  long long __ll = __strtoll_l(__s, &__sanity, __base, __cloc);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __ll;
	  else
	    __err |= ios_base::failbit;
	}
    }

  template<>
    void
    __convert_to_v(const char* __s, unsigned long long& __v, 
		   ios_base::iostate& __err, const __c_locale& __cloc, 
		   int __base)
    {
      if (!(__err & ios_base::failbit))
	{      
	  char* __sanity;
	  errno = 0;
	  unsigned long long __ull = __strtoull_l(__s, &__sanity, __base, 
						  __cloc);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __ull;
	  else
	    __err |= ios_base::failbit;
	}  
    }
#endif

  template<>
    void
    __convert_to_v(const char* __s, float& __v, ios_base::iostate& __err, 
		   const __c_locale& __cloc, int)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  float __f = __strtof_l(__s, &__sanity, __cloc);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __f;
	  else
	    __err |= ios_base::failbit;
	}
    }

  template<>
    void
    __convert_to_v(const char* __s, double& __v, ios_base::iostate& __err, 
		   const __c_locale& __cloc, int)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  double __d = __strtod_l(__s, &__sanity, __cloc);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __d;
	  else
	    __err |= ios_base::failbit;
	}
    }

  template<>
    void
    __convert_to_v(const char* __s, long double& __v, ios_base::iostate& __err,
		   const __c_locale& __cloc, int)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  long double __ld = __strtold_l(__s, &__sanity, __cloc);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __ld;
	  else
	    __err |= ios_base::failbit;
	}
    }

  void
  locale::facet::_S_create_c_locale(__c_locale& __cloc, const char* __s, 
				    __c_locale __old)
  {
    __cloc = __newlocale(1 << LC_ALL, __s, __old);
    if (!__cloc)
      {
	// This named locale is not supported by the underlying OS.
	__throw_runtime_error("attempt to create locale from unknown name");
      }
  }
  
  void
  locale::facet::_S_destroy_c_locale(__c_locale& __cloc)
  {
    if (_S_c_locale != __cloc)
      __freelocale(__cloc); 
  }

  __c_locale
  locale::facet::_S_clone_c_locale(__c_locale& __cloc)
  { return __duplocale(__cloc); }

  const char* locale::_S_categories[_S_categories_size 
				    + _S_extra_categories_size] =
    {
      "LC_CTYPE", 
      "LC_NUMERIC",
      "LC_TIME", 
      "LC_COLLATE", 
      "LC_MONETARY",
      "LC_MESSAGES", 
      "LC_PAPER", 
      "LC_NAME", 
      "LC_ADDRESS",
      "LC_TELEPHONE", 
      "LC_MEASUREMENT", 
      "LC_IDENTIFICATION" 
    };
}  // namespace std
