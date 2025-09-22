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

#ifdef _GLIBCPP_HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

namespace std 
{
  // Specializations for all types used in num_get.
  template<>
    void
    __convert_to_v(const char* __s, long& __v, ios_base::iostate& __err, 
		   const __c_locale&, int __base)
    {
      if (!(__err & ios_base::failbit))
      {
	char* __sanity;
	errno = 0;
	long __l = strtol(__s, &__sanity, __base);
	if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	  __v = __l;
	else
	  __err |= ios_base::failbit;
      }
    }

  template<>
    void
    __convert_to_v(const char* __s, unsigned long& __v, 
		   ios_base::iostate& __err, const __c_locale&, int __base)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  unsigned long __ul = strtoul(__s, &__sanity, __base);
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
		   const __c_locale&, int __base)
    {
      if (!(__err & ios_base::failbit))
	{
	  char* __sanity;
	  errno = 0;
	  long long __ll = strtoll(__s, &__sanity, __base);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __ll;
	  else
	    __err |= ios_base::failbit;
	}
    }

  template<>
    void
    __convert_to_v(const char* __s, unsigned long long& __v, 
		   ios_base::iostate& __err, const __c_locale&, int __base)
    {
      if (!(__err & ios_base::failbit))
	{      
	  char* __sanity;
	  errno = 0;
	  unsigned long long __ull = strtoull(__s, &__sanity, __base);
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
		   const __c_locale&, int) 	      
    {
      if (!(__err & ios_base::failbit))
	{
	  // Assumes __s formatted for "C" locale.
	  char* __old = strdup(setlocale(LC_ALL, NULL));
	  setlocale(LC_ALL, "C");
	  char* __sanity;
	  errno = 0;
#if defined(_GLIBCPP_USE_C99)
	  float __f = strtof(__s, &__sanity);
#else
	  double __d = strtod(__s, &__sanity);
	  float __f = static_cast<float>(__d);
#ifdef _GLIBCPP_HAVE_FINITEF
	  if (!finitef (__f))
	    errno = ERANGE;
#elif defined (_GLIBCPP_HAVE_FINITE)
	  if (!finite (static_cast<double> (__f)))
	    errno = ERANGE;
#elif defined (_GLIBCPP_HAVE_ISINF)
	  if (isinf (static_cast<double> (__f)))
	    errno = ERANGE;
#else
	  if (fabs(__d) > numeric_limits<float>::max())
	    errno = ERANGE;
#endif
#endif
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __f;
	  else
	    __err |= ios_base::failbit;
	  setlocale(LC_ALL, __old);
	  free(__old);
	}
    }

  template<>
    void
    __convert_to_v(const char* __s, double& __v, ios_base::iostate& __err, 
		   const __c_locale&, int) 
    {
      if (!(__err & ios_base::failbit))
	{
	  // Assumes __s formatted for "C" locale.
	  char* __old = strdup(setlocale(LC_ALL, NULL));
	  setlocale(LC_ALL, "C");
	  char* __sanity;
	  errno = 0;
	  double __d = strtod(__s, &__sanity);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __d;
	  else
	    __err |= ios_base::failbit;
	  setlocale(LC_ALL, __old);
	  free(__old);
	}
    }

  template<>
    void
    __convert_to_v(const char* __s, long double& __v, 
		   ios_base::iostate& __err, const __c_locale&, int) 
    {
      if (!(__err & ios_base::failbit))
	{
	  // Assumes __s formatted for "C" locale.
	  char* __old = strdup(setlocale(LC_ALL, NULL));
	  setlocale(LC_ALL, "C");
#if defined(_GLIBCPP_USE_C99)
	  char* __sanity;
	  errno = 0;
	  long double __ld = strtold(__s, &__sanity);
          if (__sanity != __s && *__sanity == '\0' && errno != ERANGE)
	    __v = __ld;
#else
	  typedef char_traits<char>::int_type int_type;
	  long double __ld;
	  errno = 0;
	  int __p = sscanf(__s, "%Lf", &__ld);
	  if (errno == ERANGE)
	    __p = 0;
#ifdef _GLIBCPP_HAVE_FINITEL
	  if ((__p == 1) && !finitel (__ld))
	    __p = 0;
#endif
	  if (__p && static_cast<int_type>(__p) != char_traits<char>::eof())
	    __v = __ld;
#endif
	  else
	    __err |= ios_base::failbit;
	  setlocale(LC_ALL, __old);
	  free(__old);
	}
    }

  void
  locale::facet::_S_create_c_locale(__c_locale& __cloc, const char*, 
				    __c_locale)
  { __cloc = NULL; }

  void
  locale::facet::_S_destroy_c_locale(__c_locale& __cloc)
  { __cloc = NULL; }

  __c_locale
  locale::facet::_S_clone_c_locale(__c_locale&)
  { return __c_locale(); }

  const char* locale::_S_categories[_S_categories_size 
				    + _S_extra_categories_size] =
    {
      "LC_CTYPE", 
      "LC_NUMERIC",
      "LC_TIME",   
      "LC_COLLATE", 
      "LC_MONETARY",
      "LC_MESSAGES"
    };
}  // namespace std
