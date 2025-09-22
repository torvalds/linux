// TR1 functional -*- C++ -*-

// Copyright (C) 2007 Free Software Foundation, Inc.
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

/** @file tr1/functional_hash.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _TR1_FUNCTIONAL_HASH_H
#define _TR1_FUNCTIONAL_HASH_H 1

#include <string>
#include <cmath>  // for std::frexp

namespace std
{
_GLIBCXX_BEGIN_NAMESPACE(tr1)

  // Definition of default hash function std::tr1::hash<>.  The types for
  // which std::tr1::hash<T> is defined is in clause 6.3.3. of the PDTR.
  template<typename T>
    struct hash;

#define _TR1_hashtable_define_trivial_hash(_Tp)         \
  template<>                                            \
    struct hash<_Tp>                                    \
    : public std::unary_function<_Tp, std::size_t>      \
    {                                                   \
      std::size_t                                       \
      operator()(_Tp __val) const                       \
      { return static_cast<std::size_t>(__val); }       \
    }                                                     

  _TR1_hashtable_define_trivial_hash(bool);
  _TR1_hashtable_define_trivial_hash(char);
  _TR1_hashtable_define_trivial_hash(signed char);
  _TR1_hashtable_define_trivial_hash(unsigned char);
  _TR1_hashtable_define_trivial_hash(wchar_t);
  _TR1_hashtable_define_trivial_hash(short);
  _TR1_hashtable_define_trivial_hash(int);
  _TR1_hashtable_define_trivial_hash(long);
  _TR1_hashtable_define_trivial_hash(long long);
  _TR1_hashtable_define_trivial_hash(unsigned short);
  _TR1_hashtable_define_trivial_hash(unsigned int);
  _TR1_hashtable_define_trivial_hash(unsigned long);
  _TR1_hashtable_define_trivial_hash(unsigned long long);

#undef _TR1_hashtable_define_trivial_hash

  template<typename _Tp>
    struct hash<_Tp*>
    : public std::unary_function<_Tp*, std::size_t>
    {
      std::size_t
      operator()(_Tp* __p) const
      { return reinterpret_cast<std::size_t>(__p); }
    };

  // Fowler / Noll / Vo (FNV) Hash (type FNV-1a)
  // (used by the next specializations of std::tr1::hash<>)

  // Dummy generic implementation (for sizeof(size_t) != 4, 8).
  template<std::size_t = sizeof(std::size_t)>
    struct _Fnv_hash
    {
      static std::size_t
      hash(const char* __first, std::size_t __length)
      {
	std::size_t __result = 0;
	for (; __length > 0; --__length)
	  __result = (__result * 131) + *__first++;
	return __result;
      }
    };

  template<>
    struct _Fnv_hash<4>
    {
      static std::size_t
      hash(const char* __first, std::size_t __length)
      {
	std::size_t __result = static_cast<std::size_t>(2166136261UL);
	for (; __length > 0; --__length)
	  {
	    __result ^= static_cast<std::size_t>(*__first++);
	    __result *= static_cast<std::size_t>(16777619UL);
	  }
	return __result;
      }
    };
  
  template<>
    struct _Fnv_hash<8>
    {
      static std::size_t
      hash(const char* __first, std::size_t __length)
      {
	std::size_t __result =
	  static_cast<std::size_t>(14695981039346656037ULL);
	for (; __length > 0; --__length)
	  {
	    __result ^= static_cast<std::size_t>(*__first++);
	    __result *= static_cast<std::size_t>(1099511628211ULL);
	  }
	return __result;
      }
    };

  // XXX String and floating point hashes probably shouldn't be inline
  // member functions, since are nontrivial.  Once we have the framework
  // for TR1 .cc files, these should go in one.
  template<>
    struct hash<std::string>
    : public std::unary_function<std::string, std::size_t>
    {      
      std::size_t
      operator()(const std::string& __s) const
      { return _Fnv_hash<>::hash(__s.data(), __s.length()); }
    };

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    struct hash<std::wstring>
    : public std::unary_function<std::wstring, std::size_t>
    {
      std::size_t
      operator()(const std::wstring& __s) const
      {
	return _Fnv_hash<>::hash(reinterpret_cast<const char*>(__s.data()),
				 __s.length() * sizeof(wchar_t));
      }
    };
#endif

  template<>
    struct hash<float>
    : public std::unary_function<float, std::size_t>
    {
      std::size_t
      operator()(float __fval) const
      {
	std::size_t __result = 0;

	// 0 and -0 both hash to zero.
	if (__fval != 0.0f)
	  __result = _Fnv_hash<>::hash(reinterpret_cast<const char*>(&__fval),
				       sizeof(__fval));
	return __result;
      }
    };

  template<>
    struct hash<double>
    : public std::unary_function<double, std::size_t>
    {
      std::size_t
      operator()(double __dval) const
      {
	std::size_t __result = 0;

	// 0 and -0 both hash to zero.
	if (__dval != 0.0)
	  __result = _Fnv_hash<>::hash(reinterpret_cast<const char*>(&__dval),
				       sizeof(__dval));
	return __result;
      }
    };

  // For long double, careful with random padding bits (e.g., on x86,
  // 10 bytes -> 12 bytes) and resort to frexp.
  template<>
    struct hash<long double>
    : public std::unary_function<long double, std::size_t>
    {
      std::size_t
      operator()(long double __ldval) const
      {
	std::size_t __result = 0;

	int __exponent;
	__ldval = std::frexp(__ldval, &__exponent);
	__ldval = __ldval < 0.0l ? -(__ldval + 0.5l) : __ldval;

	const long double __mult =
	  std::numeric_limits<std::size_t>::max() + 1.0l;
	__ldval *= __mult;

	// Try to use all the bits of the mantissa (really necessary only
	// on 32-bit targets, at least for 80-bit floating point formats).
	const std::size_t __hibits = (std::size_t)__ldval;
	__ldval = (__ldval - (long double)__hibits) * __mult;

	const std::size_t __coeff =
	  (std::numeric_limits<std::size_t>::max()
	   / std::numeric_limits<long double>::max_exponent);

	__result = __hibits + (std::size_t)__ldval + __coeff * __exponent;

	return __result;
      }
    };

_GLIBCXX_END_NAMESPACE
}

#endif
