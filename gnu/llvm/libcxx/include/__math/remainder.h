//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_REMAINDER_H
#define _LIBCPP___MATH_REMAINDER_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_same.h>
#include <__type_traits/promote.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// remainder

inline _LIBCPP_HIDE_FROM_ABI float remainder(float __x, float __y) _NOEXCEPT { return __builtin_remainderf(__x, __y); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double remainder(double __x, double __y) _NOEXCEPT {
  return __builtin_remainder(__x, __y);
}

inline _LIBCPP_HIDE_FROM_ABI long double remainder(long double __x, long double __y) _NOEXCEPT {
  return __builtin_remainderl(__x, __y);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type remainder(_A1 __x, _A2 __y) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2>::type;
  static_assert(!(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value), "");
  return __math::remainder((__result_type)__x, (__result_type)__y);
}

// remquo

inline _LIBCPP_HIDE_FROM_ABI float remquo(float __x, float __y, int* __z) _NOEXCEPT {
  return __builtin_remquof(__x, __y, __z);
}

template <class = int>
_LIBCPP_HIDE_FROM_ABI double remquo(double __x, double __y, int* __z) _NOEXCEPT {
  return __builtin_remquo(__x, __y, __z);
}

inline _LIBCPP_HIDE_FROM_ABI long double remquo(long double __x, long double __y, int* __z) _NOEXCEPT {
  return __builtin_remquol(__x, __y, __z);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type remquo(_A1 __x, _A2 __y, int* __z) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2>::type;
  static_assert(!(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value), "");
  return __math::remquo((__result_type)__x, (__result_type)__y, __z);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_REMAINDER_H
