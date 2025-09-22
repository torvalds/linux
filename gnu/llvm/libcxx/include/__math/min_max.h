//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_MIN_MAX_H
#define _LIBCPP___MATH_MIN_MAX_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_same.h>
#include <__type_traits/promote.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// fmax

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI float fmax(float __x, float __y) _NOEXCEPT {
  return __builtin_fmaxf(__x, __y);
}

template <class = int>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI double fmax(double __x, double __y) _NOEXCEPT {
  return __builtin_fmax(__x, __y);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long double fmax(long double __x, long double __y) _NOEXCEPT {
  return __builtin_fmaxl(__x, __y);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type fmax(_A1 __x, _A2 __y) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2>::type;
  static_assert(!(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value), "");
  return __math::fmax((__result_type)__x, (__result_type)__y);
}

// fmin

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI float fmin(float __x, float __y) _NOEXCEPT {
  return __builtin_fminf(__x, __y);
}

template <class = int>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI double fmin(double __x, double __y) _NOEXCEPT {
  return __builtin_fmin(__x, __y);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long double fmin(long double __x, long double __y) _NOEXCEPT {
  return __builtin_fminl(__x, __y);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type fmin(_A1 __x, _A2 __y) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2>::type;
  static_assert(!(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value), "");
  return __math::fmin((__result_type)__x, (__result_type)__y);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_MIN_MAX_H
