//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_FMA_H
#define _LIBCPP___MATH_FMA_H

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

inline _LIBCPP_HIDE_FROM_ABI float fma(float __x, float __y, float __z) _NOEXCEPT {
  return __builtin_fmaf(__x, __y, __z);
}

template <class = int>
_LIBCPP_HIDE_FROM_ABI double fma(double __x, double __y, double __z) _NOEXCEPT {
  return __builtin_fma(__x, __y, __z);
}

inline _LIBCPP_HIDE_FROM_ABI long double fma(long double __x, long double __y, long double __z) _NOEXCEPT {
  return __builtin_fmal(__x, __y, __z);
}

template <class _A1,
          class _A2,
          class _A3,
          __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value && is_arithmetic<_A3>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2, _A3>::type fma(_A1 __x, _A2 __y, _A3 __z) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2, _A3>::type;
  static_assert(
      !(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value && _IsSame<_A3, __result_type>::value),
      "");
  return __builtin_fma((__result_type)__x, (__result_type)__y, (__result_type)__z);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_FMA_H
