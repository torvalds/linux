//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_COPYSIGN_H
#define _LIBCPP___MATH_COPYSIGN_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/promote.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// copysign

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI float copysign(float __x, float __y) _NOEXCEPT {
  return ::__builtin_copysignf(__x, __y);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long double copysign(long double __x, long double __y) _NOEXCEPT {
  return ::__builtin_copysignl(__x, __y);
}

template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI typename __promote<_A1, _A2>::type copysign(_A1 __x, _A2 __y) _NOEXCEPT {
  return ::__builtin_copysign(__x, __y);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_COPYSIGN_H
