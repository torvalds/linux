// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_SPECIAL_FUNCTIONS_H
#define _LIBCPP___MATH_SPECIAL_FUNCTIONS_H

#include <__config>
#include <__math/copysign.h>
#include <__math/traits.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _Real>
_LIBCPP_HIDE_FROM_ABI _Real __hermite(unsigned __n, _Real __x) {
  // The Hermite polynomial H_n(x).
  // The implementation is based on the recurrence formula: H_{n+1}(x) = 2x H_n(x) - 2n H_{n-1}.
  // Press, William H., et al. Numerical recipes 3rd edition: The art of scientific computing.
  // Cambridge university press, 2007, p. 183.

  // NOLINTBEGIN(readability-identifier-naming)
  if (__math::isnan(__x))
    return __x;

  _Real __H_0{1};
  if (__n == 0)
    return __H_0;

  _Real __H_n_prev = __H_0;
  _Real __H_n      = 2 * __x;
  for (unsigned __i = 1; __i < __n; ++__i) {
    _Real __H_n_next = 2 * (__x * __H_n - __i * __H_n_prev);
    __H_n_prev       = __H_n;
    __H_n            = __H_n_next;
  }

  if (!__math::isfinite(__H_n)) {
    // Overflow occured. Two possible cases:
    //    n is odd:  return infinity of the same sign as x.
    //    n is even: return +Inf
    _Real __inf = std::numeric_limits<_Real>::infinity();
    return (__n & 1) ? __math::copysign(__inf, __x) : __inf;
  }
  return __H_n;
  // NOLINTEND(readability-identifier-naming)
}

inline _LIBCPP_HIDE_FROM_ABI double hermite(unsigned __n, double __x) { return std::__hermite(__n, __x); }

inline _LIBCPP_HIDE_FROM_ABI float hermite(unsigned __n, float __x) {
  // use double internally -- float is too prone to overflow!
  return static_cast<float>(std::hermite(__n, static_cast<double>(__x)));
}

inline _LIBCPP_HIDE_FROM_ABI long double hermite(unsigned __n, long double __x) { return std::__hermite(__n, __x); }

inline _LIBCPP_HIDE_FROM_ABI float hermitef(unsigned __n, float __x) { return std::hermite(__n, __x); }

inline _LIBCPP_HIDE_FROM_ABI long double hermitel(unsigned __n, long double __x) { return std::hermite(__n, __x); }

template <class _Integer, std::enable_if_t<std::is_integral_v<_Integer>, int> = 0>
_LIBCPP_HIDE_FROM_ABI double hermite(unsigned __n, _Integer __x) {
  return std::hermite(__n, static_cast<double>(__x));
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_SPECIAL_FUNCTIONS_H
