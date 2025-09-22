//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_HALF_POSITIVE_H
#define _LIBCPP___ALGORITHM_HALF_POSITIVE_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/make_unsigned.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Perform division by two quickly for positive integers (llvm.org/PR39129)

template <typename _Integral, __enable_if_t<is_integral<_Integral>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Integral __half_positive(_Integral __value) {
  return static_cast<_Integral>(static_cast<__make_unsigned_t<_Integral> >(__value) / 2);
}

template <typename _Tp, __enable_if_t<!is_integral<_Tp>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Tp __half_positive(_Tp __value) {
  return __value / 2;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_HALF_POSITIVE_H
