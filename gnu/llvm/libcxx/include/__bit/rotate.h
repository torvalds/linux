//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___BIT_ROTATE_H
#define _LIBCPP___BIT_ROTATE_H

#include <__concepts/arithmetic.h>
#include <__config>
#include <__type_traits/is_unsigned_integer.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Writing two full functions for rotl and rotr makes it easier for the compiler
// to optimize the code. On x86 this function becomes the ROL instruction and
// the rotr function becomes the ROR instruction.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp __rotl(_Tp __x, int __s) _NOEXCEPT {
  static_assert(__libcpp_is_unsigned_integer<_Tp>::value, "__rotl requires an unsigned integer type");
  const int __N = numeric_limits<_Tp>::digits;
  int __r       = __s % __N;

  if (__r == 0)
    return __x;

  if (__r > 0)
    return (__x << __r) | (__x >> (__N - __r));

  return (__x >> -__r) | (__x << (__N + __r));
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp __rotr(_Tp __x, int __s) _NOEXCEPT {
  static_assert(__libcpp_is_unsigned_integer<_Tp>::value, "__rotr requires an unsigned integer type");
  const int __N = numeric_limits<_Tp>::digits;
  int __r       = __s % __N;

  if (__r == 0)
    return __x;

  if (__r > 0)
    return (__x >> __r) | (__x << (__N - __r));

  return (__x << -__r) | (__x >> (__N + __r));
}

#if _LIBCPP_STD_VER >= 20

template <__libcpp_unsigned_integer _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Tp rotl(_Tp __t, int __cnt) noexcept {
  return std::__rotl(__t, __cnt);
}

template <__libcpp_unsigned_integer _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Tp rotr(_Tp __t, int __cnt) noexcept {
  return std::__rotr(__t, __cnt);
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___BIT_ROTATE_H
