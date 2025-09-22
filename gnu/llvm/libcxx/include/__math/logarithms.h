//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_LOGARITHMS_H
#define _LIBCPP___MATH_LOGARITHMS_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// log

inline _LIBCPP_HIDE_FROM_ABI float log(float __x) _NOEXCEPT { return __builtin_logf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double log(double __x) _NOEXCEPT {
  return __builtin_log(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double log(long double __x) _NOEXCEPT { return __builtin_logl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double log(_A1 __x) _NOEXCEPT {
  return __builtin_log((double)__x);
}

// log10

inline _LIBCPP_HIDE_FROM_ABI float log10(float __x) _NOEXCEPT { return __builtin_log10f(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double log10(double __x) _NOEXCEPT {
  return __builtin_log10(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double log10(long double __x) _NOEXCEPT { return __builtin_log10l(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double log10(_A1 __x) _NOEXCEPT {
  return __builtin_log10((double)__x);
}

// ilogb

inline _LIBCPP_HIDE_FROM_ABI int ilogb(float __x) _NOEXCEPT { return __builtin_ilogbf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double ilogb(double __x) _NOEXCEPT {
  return __builtin_ilogb(__x);
}

inline _LIBCPP_HIDE_FROM_ABI int ilogb(long double __x) _NOEXCEPT { return __builtin_ilogbl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI int ilogb(_A1 __x) _NOEXCEPT {
  return __builtin_ilogb((double)__x);
}

// log1p

inline _LIBCPP_HIDE_FROM_ABI float log1p(float __x) _NOEXCEPT { return __builtin_log1pf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double log1p(double __x) _NOEXCEPT {
  return __builtin_log1p(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double log1p(long double __x) _NOEXCEPT { return __builtin_log1pl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double log1p(_A1 __x) _NOEXCEPT {
  return __builtin_log1p((double)__x);
}

// log2

inline _LIBCPP_HIDE_FROM_ABI float log2(float __x) _NOEXCEPT { return __builtin_log2f(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double log2(double __x) _NOEXCEPT {
  return __builtin_log2(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double log2(long double __x) _NOEXCEPT { return __builtin_log2l(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double log2(_A1 __x) _NOEXCEPT {
  return __builtin_log2((double)__x);
}

// logb

inline _LIBCPP_HIDE_FROM_ABI float logb(float __x) _NOEXCEPT { return __builtin_logbf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double logb(double __x) _NOEXCEPT {
  return __builtin_logb(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double logb(long double __x) _NOEXCEPT { return __builtin_logbl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double logb(_A1 __x) _NOEXCEPT {
  return __builtin_logb((double)__x);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_LOGARITHMS_H
