//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MATH_ERROR_FUNCTIONS_H
#define _LIBCPP___MATH_ERROR_FUNCTIONS_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// erf

inline _LIBCPP_HIDE_FROM_ABI float erf(float __x) _NOEXCEPT { return __builtin_erff(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double erf(double __x) _NOEXCEPT {
  return __builtin_erf(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double erf(long double __x) _NOEXCEPT { return __builtin_erfl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double erf(_A1 __x) _NOEXCEPT {
  return __builtin_erf((double)__x);
}

// erfc

inline _LIBCPP_HIDE_FROM_ABI float erfc(float __x) _NOEXCEPT { return __builtin_erfcf(__x); }

template <class = int>
_LIBCPP_HIDE_FROM_ABI double erfc(double __x) _NOEXCEPT {
  return __builtin_erfc(__x);
}

inline _LIBCPP_HIDE_FROM_ABI long double erfc(long double __x) _NOEXCEPT { return __builtin_erfcl(__x); }

template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI double erfc(_A1 __x) _NOEXCEPT {
  return __builtin_erfc((double)__x);
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MATH_ERROR_FUNCTIONS_H
