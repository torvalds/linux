//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_LOG2_H
#define _LIBCPP___RANDOM_LOG2_H

#include <__config>
#include <__type_traits/conditional.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _UIntType, _UIntType _Xp, size_t _Rp>
struct __log2_imp;

template <unsigned long long _Xp, size_t _Rp>
struct __log2_imp<unsigned long long, _Xp, _Rp> {
  static const size_t value =
      _Xp & ((unsigned long long)(1) << _Rp) ? _Rp : __log2_imp<unsigned long long, _Xp, _Rp - 1>::value;
};

template <unsigned long long _Xp>
struct __log2_imp<unsigned long long, _Xp, 0> {
  static const size_t value = 0;
};

template <size_t _Rp>
struct __log2_imp<unsigned long long, 0, _Rp> {
  static const size_t value = _Rp + 1;
};

#ifndef _LIBCPP_HAS_NO_INT128

template <__uint128_t _Xp, size_t _Rp>
struct __log2_imp<__uint128_t, _Xp, _Rp> {
  static const size_t value =
      (_Xp >> 64) ? (64 + __log2_imp<unsigned long long, (_Xp >> 64), 63>::value)
                  : __log2_imp<unsigned long long, _Xp, 63>::value;
};

#endif // _LIBCPP_HAS_NO_INT128

template <class _UIntType, _UIntType _Xp>
struct __log2 {
  static const size_t value = __log2_imp<
#ifndef _LIBCPP_HAS_NO_INT128
      __conditional_t<sizeof(_UIntType) <= sizeof(unsigned long long), unsigned long long, __uint128_t>,
#else
      unsigned long long,
#endif // _LIBCPP_HAS_NO_INT128
      _Xp,
      sizeof(_UIntType) * __CHAR_BIT__ - 1>::value;
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANDOM_LOG2_H
