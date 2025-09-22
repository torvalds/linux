//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_INVERSE_HYPERBOLIC_FUNCTIONS_H
#define _LIBCPP___MATH_INVERSE_HYPERBOLIC_FUNCTIONS_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// acosh

inline _LIBCPP_HIDE_FROM_ABI float acosh(float __x) _NOEXCEPT { return __builtin_acoshf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double acosh(double __x) _NOEXCEPT {
  return __builtin_acosh(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double acosh(long double __x) _NOEXCEPT { return __builtin_acoshl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double acosh(_A1 __x) _NOEXCEPT {
  return __builtin_acosh((double)__x);
}

// asinh

inline _LIBCPP_HIDE_FROM_ABI float asinh(float __x) _NOEXCEPT { return __builtin_asinhf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double asinh(double __x) _NOEXCEPT {
  return __builtin_asinh(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double asinh(long double __x) _NOEXCEPT { return __builtin_asinhl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double asinh(_A1 __x) _NOEXCEPT {
  return __builtin_asinh((double)__x);
}

// atanh

inline _LIBCPP_HIDE_FROM_ABI float atanh(float __x) _NOEXCEPT { return __builtin_atanhf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double atanh(double __x) _NOEXCEPT {
  return __builtin_atanh(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double atanh(long double __x) _NOEXCEPT { return __builtin_atanhl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double atanh(_A1 __x) _NOEXCEPT {
  return __builtin_atanh((double)__x);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_INVERSE_HYPERBOLIC_FUNCTIONS_H
