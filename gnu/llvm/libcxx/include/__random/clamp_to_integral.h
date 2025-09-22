//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_CLAMP_TO_INTEGRAL_H
#define _LIBCPP___RANDOM_CLAMP_TO_INTEGRAL_H

#include <__config>
#include <cmath>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _IntT,
          class _FloatT,
          bool _FloatBigger = (numeric_limits<_FloatT>::digits > numeric_limits<_IntT>::digits),
          int _Bits         = (numeric_limits<_IntT>::digits - numeric_limits<_FloatT>::digits)>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _IntT __max_representable_int_for_float() _NOEXCEPT {
  static_assert(is_floating_point<_FloatT>::value, "must be a floating point type");
  static_assert(is_integral<_IntT>::value, "must be an integral type");
  static_assert(numeric_limits<_FloatT>::radix == 2, "FloatT has incorrect radix");
  static_assert(
      (_IsSame<_FloatT, float>::value || _IsSame<_FloatT, double>::value || _IsSame<_FloatT, long double>::value),
      "unsupported floating point type");
  return _FloatBigger ? numeric_limits<_IntT>::max() : (numeric_limits<_IntT>::max() >> _Bits << _Bits);
}

// Convert a floating point number to the specified integral type after
// clamping to the integral type's representable range.
//
// The behavior is undefined if `__r` is NaN.
template <class _IntT, class _RealT>
_LIBCPP_HIDE_FROM_ABI _IntT __clamp_to_integral(_RealT __r) _NOEXCEPT {
  using _Lim            = numeric_limits<_IntT>;
  const _IntT __max_val = __max_representable_int_for_float<_IntT, _RealT>();
  if (__r >= ::nextafter(static_cast<_RealT>(__max_val), INFINITY)) {
    return _Lim::max();
  } else if (__r <= _Lim::lowest()) {
    return _Lim::min();
  }
  return static_cast<_IntT>(__r);
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_CLAMP_TO_INTEGRAL_H
