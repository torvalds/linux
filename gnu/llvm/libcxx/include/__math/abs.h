//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_ABS_H
#define _LIBCPP___MATH_ABS_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// fabs

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI float fabs(float __x) _NOEXCEPT { return __builtin_fabsf(__x); }

template <class = int>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI double fabs(double __x) _NOEXCEPT {
  return __builtin_fabs(__x);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long double fabs(long double __x) _NOEXCEPT {
  return __builtin_fabsl(__x);
}

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI double fabs(_A1 __x) _NOEXCEPT {
  return __builtin_fabs((double)__x);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_ABS_H
