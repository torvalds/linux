//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_TRIGONOMETRIC_FUNCTIONS_H
#define _LIBCPP___MATH_TRIGONOMETRIC_FUNCTIONS_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// cos

inline _LIBCPP_HIDE_FROM_ABI float cos(float __x) _NOEXCEPT { return __builtin_cosf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double cos(double __x) _NOEXCEPT {
  return __builtin_cos(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double cos(long double __x) _NOEXCEPT { return __builtin_cosl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double cos(_A1 __x) _NOEXCEPT {
  return __builtin_cos((double)__x);
}

// sin

inline _LIBCPP_HIDE_FROM_ABI float sin(float __x) _NOEXCEPT { return __builtin_sinf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double sin(double __x) _NOEXCEPT {
  return __builtin_sin(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double sin(long double __x) _NOEXCEPT { return __builtin_sinl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double sin(_A1 __x) _NOEXCEPT {
  return __builtin_sin((double)__x);
}

// tan

inline _LIBCPP_HIDE_FROM_ABI float tan(float __x) _NOEXCEPT { return __builtin_tanf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double tan(double __x) _NOEXCEPT {
  return __builtin_tan(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double tan(long double __x) _NOEXCEPT { return __builtin_tanl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double tan(_A1 __x) _NOEXCEPT {
  return __builtin_tan((double)__x);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_TRIGONOMETRIC_FUNCTIONS_H
