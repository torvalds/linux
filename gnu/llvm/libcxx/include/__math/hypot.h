//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_HYPOT_H
#define _LIBCPP___MATH_HYPOT_H

#include <__algorithm/max.h>
#include <__config>
#include <__math/abs.h>
#include <__math/exponential_functions.h>
#include <__math/roots.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_same.h>
#include <__type_traits/promote.h>
#include <__utility/pair.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

inline _LIBCPP_HIDE_FROM_ABI float hypot(float __x, float __y) _NOEXCEPT { return __builtin_hypotf(__x, __y); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double hypot(double __x, double __y) _NOEXCEPT {
  return __builtin_hypot(__x, __y);
}

inline _LIBCPP_HIDE_FROM_ABI long double hypot(long double __x, long double __y) _NOEXCEPT {
  return __builtin_hypotl(__x, __y);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type hypot(_A1 __x, _A2 __y) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2>::type;
  static_assert(!(_IsSame<_A1, __result_type>::value && _IsSame<_A2, __result_type>::value), "");
  return __math::hypot((__result_type)__x, (__result_type)__y);
}

#if _LIBCPP_STD_VER >= 17
// Computes the three-dimensional hypotenuse: `std::hypot(x,y,z)`.
// The naive implementation might over-/underflow which is why this implementation is more involved:
//    If the square of an argument might run into issues, we scale the arguments appropriately.
// See https://github.com/llvm/llvm-project/issues/92782 for a detailed discussion and summary.
template <class _Real>
_LIBCPP_HIDE_FROM_ABI _Real __hypot(_Real __x, _Real __y, _Real __z) {
  // Factors needed to determine if over-/underflow might happen
  constexpr int __exp              = std::numeric_limits<_Real>::max_exponent / 2;
  const _Real __overflow_threshold = __math::ldexp(_Real(1), __exp);
  const _Real __overflow_scale     = __math::ldexp(_Real(1), -(__exp + 20));

  // Scale arguments depending on their size
  const _Real __max_abs = std::max(__math::fabs(__x), std::max(__math::fabs(__y), __math::fabs(__z)));
  _Real __scale;
  if (__max_abs > __overflow_threshold) { // x*x + y*y + z*z might overflow
    __scale = __overflow_scale;
  } else if (__max_abs < 1 / __overflow_threshold) { // x*x + y*y + z*z might underflow
    __scale = 1 / __overflow_scale;
  } else {
    __scale = 1;
  }
  __x *= __scale;
  __y *= __scale;
  __z *= __scale;

  // Compute hypot of scaled arguments and undo scaling
  return __math::sqrt(__x * __x + __y * __y + __z * __z) / __scale;
}

inline _LIBCPP_HIDE_FROM_ABI float hypot(float __x, float __y, float __z) { return __math::__hypot(__x, __y, __z); }

inline _LIBCPP_HIDE_FROM_ABI double hypot(double __x, double __y, double __z) { return __math::__hypot(__x, __y, __z); }

inline _LIBCPP_HIDE_FROM_ABI long double hypot(long double __x, long double __y, long double __z) {
  return __math::__hypot(__x, __y, __z);
}

template <class _A1,
          class _A2,
          class _A3,
          std::enable_if_t< is_arithmetic_v<_A1> && is_arithmetic_v<_A2> && is_arithmetic_v<_A3>, int> = 0 >
_LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2, _A3>::type hypot(_A1 __x, _A2 __y, _A3 __z) _NOEXCEPT {
  using __result_type = typename __promote<_A1, _A2, _A3>::type;
  static_assert(!(
      std::is_same_v<_A1, __result_type> && std::is_same_v<_A2, __result_type> && std::is_same_v<_A3, __result_type>));
  return __math::__hypot(
      static_cast<__result_type>(__x), static_cast<__result_type>(__y), static_cast<__result_type>(__z));
}
#endif

} // namespace __math

_LIBCPP_END_NAMESPACE_STD
_LIBCPP_POP_MACROS

#endif // _LIBCPP___MATH_HYPOT_H
