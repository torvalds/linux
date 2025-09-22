// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NUMERIC_GCD_LCM_H
#define _LIBCPP___NUMERIC_GCD_LCM_H

#include <__algorithm/min.h>
#include <__assert>
#include <__bit/countr.h>
#include <__config>
#include <__type_traits/common_type.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/make_unsigned.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <typename _Result, typename _Source, bool _IsSigned = is_signed<_Source>::value>
struct __ct_abs;

template <typename _Result, typename _Source>
struct __ct_abs<_Result, _Source, true> {
  constexpr _LIBCPP_HIDE_FROM_ABI _Result operator()(_Source __t) const noexcept {
    if (__t >= 0)
      return __t;
    if (__t == numeric_limits<_Source>::min())
      return -static_cast<_Result>(__t);
    return -__t;
  }
};

template <typename _Result, typename _Source>
struct __ct_abs<_Result, _Source, false> {
  constexpr _LIBCPP_HIDE_FROM_ABI _Result operator()(_Source __t) const noexcept { return __t; }
};

template <class _Tp>
constexpr _LIBCPP_HIDDEN _Tp __gcd(_Tp __a, _Tp __b) {
  static_assert(!is_signed<_Tp>::value, "");

  // From: https://lemire.me/blog/2013/12/26/fastest-way-to-compute-the-greatest-common-divisor
  //
  // If power of two divides both numbers, we can push it out.
  // - gcd( 2^x * a, 2^x * b) = 2^x * gcd(a, b)
  //
  // If and only if exactly one number is even, we can divide that number by that power.
  // - if a, b are odd, then gcd(2^x * a, b) = gcd(a, b)
  //
  // And standard gcd algorithm where instead of modulo, minus is used.

  if (__a < __b) {
    _Tp __tmp = __b;
    __b       = __a;
    __a       = __tmp;
  }
  if (__b == 0)
    return __a;
  __a %= __b; // Make both argument of the same size, and early result in the easy case.
  if (__a == 0)
    return __b;

  int __az    = std::__countr_zero(__a);
  int __bz    = std::__countr_zero(__b);
  int __shift = std::min(__az, __bz);
  __a >>= __az;
  __b >>= __bz;
  do {
    _Tp __diff = __a - __b;
    if (__a > __b) {
      __a = __b;
      __b = __diff;
    } else {
      __b = __b - __a;
    }
    if (__diff != 0)
      __b >>= std::__countr_zero(__diff);
  } while (__b != 0);
  return __a << __shift;
}

template <class _Tp, class _Up>
constexpr _LIBCPP_HIDE_FROM_ABI common_type_t<_Tp, _Up> gcd(_Tp __m, _Up __n) {
  static_assert(is_integral<_Tp>::value && is_integral<_Up>::value, "Arguments to gcd must be integer types");
  static_assert(!is_same<__remove_cv_t<_Tp>, bool>::value, "First argument to gcd cannot be bool");
  static_assert(!is_same<__remove_cv_t<_Up>, bool>::value, "Second argument to gcd cannot be bool");
  using _Rp = common_type_t<_Tp, _Up>;
  using _Wp = make_unsigned_t<_Rp>;
  return static_cast<_Rp>(
      std::__gcd(static_cast<_Wp>(__ct_abs<_Rp, _Tp>()(__m)), static_cast<_Wp>(__ct_abs<_Rp, _Up>()(__n))));
}

template <class _Tp, class _Up>
constexpr _LIBCPP_HIDE_FROM_ABI common_type_t<_Tp, _Up> lcm(_Tp __m, _Up __n) {
  static_assert(is_integral<_Tp>::value && is_integral<_Up>::value, "Arguments to lcm must be integer types");
  static_assert(!is_same<__remove_cv_t<_Tp>, bool>::value, "First argument to lcm cannot be bool");
  static_assert(!is_same<__remove_cv_t<_Up>, bool>::value, "Second argument to lcm cannot be bool");
  if (__m == 0 || __n == 0)
    return 0;

  using _Rp  = common_type_t<_Tp, _Up>;
  _Rp __val1 = __ct_abs<_Rp, _Tp>()(__m) / std::gcd(__m, __n);
  _Rp __val2 = __ct_abs<_Rp, _Up>()(__n);
  _Rp __res;
  [[maybe_unused]] bool __overflow = __builtin_mul_overflow(__val1, __val2, &__res);
  _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(!__overflow, "Overflow in lcm");
  return __res;
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___NUMERIC_GCD_LCM_H
