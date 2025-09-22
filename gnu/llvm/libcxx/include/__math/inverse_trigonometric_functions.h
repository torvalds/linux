//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_INVERSE_TRIGONOMETRIC_FUNCTIONS_H
#define _LIBCPP___MATH_INVERSE_TRIGONOMETRIC_FUNCTIONS_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_same.h>
#include <__type_traits/promote.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// acos

inline _LIBCPP_HIDE_FROM_ABI float acos(float __x) _NOEXCEPT { return __builtin_acosf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double acos(double __x) _NOEXCEPT {
  return __builtin_acos(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double acos(long double __x) _NOEXCEPT { return __builtin_acosl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double acos(_A1 __x) _NOEXCEPT {
  return __builtin_acos((double)__x);
}

// asin

inline _LIBCPP_HIDE_FROM_ABI float asin(float __x) _NOEXCEPT { return __builtin_asinf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double asin(double __x) _NOEXCEPT {
  return __builtin_asin(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double asin(long double __x) _NOEXCEPT { return __builtin_asinl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double asin(_A1 __x) _NOEXCEPT {
  return __builtin_asin((double)__x);
}

// atan

inline _LIBCPP_HIDE_FROM_ABI float atan(float __x) _NOEXCEPT { return __builtin_atanf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double atan(double __x) _NOEXCEPT {
  return __builtin_atan(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double atan(long double __x) _NOEXCEPT { return __builtin_atanl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double atan(_A1 __x) _NOEXCEPT {
  return __builtin_atan((double)__x);
}

// atan2

inline _LIBCPP_HIDE_FROM_ABI float atan2(float __y, float __x) _NOEXCEPT { return __builtin_atan2f(__y, __x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double atan2(double __x, double __y) _NOEXCEPT {
  return __builtin_atan2(__x, __y);
}

inline _LIBCPP_HIDE_FROM_ABI long double atan2(long double __y, long double __x) _NOEXCEPT {
  return __builtin_atan2l(__y, __x);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type atan2(_A1 __y, _A2 __x) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2>::type;
  static_assert(!(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value), "");
  return __math::atan2((__result_type)__y, (__result_type)__x);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_INVERSE_TRIGONOMETRIC_FUNCTIONS_H
