// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NUMERIC_SATURATION_ARITHMETIC_H
#define _LIBCPP___NUMERIC_SATURATION_ARITHMETIC_H

#include <__assert>
#include <__concepts/arithmetic.h>
#include <__config>
#include <__utility/cmp.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp __add_sat(_Tp __x, _Tp __y) noexcept {
  if (_Tp __sum; !__builtin_add_overflow(__x, __y, &__sum))
    return __sum;
  // Handle overflow
  if constexpr (__libcpp_unsigned_integer<_Tp>) {
    return std::numeric_limits<_Tp>::max();
  } else {
    // Signed addition overflow
    if (__x > 0)
      // Overflows if (x > 0 && y > 0)
      return std::numeric_limits<_Tp>::max();
    else
      // Overflows if  (x < 0 && y < 0)
      return std::numeric_limits<_Tp>::min();
  }
}

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp __sub_sat(_Tp __x, _Tp __y) noexcept {
  if (_Tp __sub; !__builtin_sub_overflow(__x, __y, &__sub))
    return __sub;
  // Handle overflow
  if constexpr (__libcpp_unsigned_integer<_Tp>) {
    // Overflows if (x < y)
    return std::numeric_limits<_Tp>::min();
  } else {
    // Signed subtration overflow
    if (__x >= 0)
      // Overflows if (x >= 0 && y < 0)
      return std::numeric_limits<_Tp>::max();
    else
      // Overflows if (x < 0 && y > 0)
      return std::numeric_limits<_Tp>::min();
  }
}

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp __mul_sat(_Tp __x, _Tp __y) noexcept {
  if (_Tp __mul; !__builtin_mul_overflow(__x, __y, &__mul))
    return __mul;
  // Handle overflow
  if constexpr (__libcpp_unsigned_integer<_Tp>) {
    return std::numeric_limits<_Tp>::max();
  } else {
    // Signed multiplication overflow
    if ((__x > 0 && __y > 0) || (__x < 0 && __y < 0))
      return std::numeric_limits<_Tp>::max();
    // Overflows if (x < 0 && y > 0) || (x > 0 && y < 0)
    return std::numeric_limits<_Tp>::min();
  }
}

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp __div_sat(_Tp __x, _Tp __y) noexcept {
  _LIBCPP_ASSERT_UNCATEGORIZED(__y != 0, "Division by 0 is undefined");
  if constexpr (__libcpp_unsigned_integer<_Tp>) {
    return __x / __y;
  } else {
    // Handle signed division overflow
    if (__x == std::numeric_limits<_Tp>::min() && __y == _Tp{-1})
      return std::numeric_limits<_Tp>::max();
    return __x / __y;
  }
}

template <__libcpp_integer _Rp, __libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Rp __saturate_cast(_Tp __x) noexcept {
  // Saturation is impossible edge case when ((min _Rp) < (min _Tp) && (max _Rp) > (max _Tp)) and it is expected to be
  // optimized out by the compiler.

  // Handle overflow
  if (std::cmp_less(__x, std::numeric_limits<_Rp>::min()))
    return std::numeric_limits<_Rp>::min();
  if (std::cmp_greater(__x, std::numeric_limits<_Rp>::max()))
    return std::numeric_limits<_Rp>::max();
  // No overflow
  return static_cast<_Rp>(__x);
}

#endif // _LIBCPP_STD_VER >= 20

#if _LIBCPP_STD_VER >= 26

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp add_sat(_Tp __x, _Tp __y) noexcept {
  return std::__add_sat(__x, __y);
}

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp sub_sat(_Tp __x, _Tp __y) noexcept {
  return std::__sub_sat(__x, __y);
}

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp mul_sat(_Tp __x, _Tp __y) noexcept {
  return std::__mul_sat(__x, __y);
}

template <__libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp div_sat(_Tp __x, _Tp __y) noexcept {
  return std::__div_sat(__x, __y);
}

template <__libcpp_integer _Rp, __libcpp_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Rp saturate_cast(_Tp __x) noexcept {
  return std::__saturate_cast<_Rp>(__x);
}

#endif // _LIBCPP_STD_VER >= 26

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___NUMERIC_SATURATION_ARITHMETIC_H
