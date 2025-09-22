//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___BIT_BLSR_H
#define _LIBCPP___BIT_BLSR_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unsigned __libcpp_blsr(unsigned __x) _NOEXCEPT {
  return __x ^ (__x & -__x);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unsigned long __libcpp_blsr(unsigned long __x) _NOEXCEPT {
  return __x ^ (__x & -__x);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unsigned long long __libcpp_blsr(unsigned long long __x) _NOEXCEPT {
  return __x ^ (__x & -__x);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___BIT_BLSR_H
