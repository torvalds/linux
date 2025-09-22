//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// TODO: __builtin_popcountg is available since Clang 19 and GCC 14. When support for older versions is dropped, we can
//  refactor this code to exclusively use __builtin_popcountg.

#ifndef _LIBCPP___BIT_POPCOUNT_H
#define _LIBCPP___BIT_POPCOUNT_H

#include <__bit/rotate.h>
#include <__concepts/arithmetic.h>
#include <__config>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_popcount(unsigned __x) _NOEXCEPT {
  return __builtin_popcount(__x);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_popcount(unsigned long __x) _NOEXCEPT {
  return __builtin_popcountl(__x);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_popcount(unsigned long long __x) _NOEXCEPT {
  return __builtin_popcountll(__x);
}

#if _LIBCPP_STD_VER >= 20

template <__libcpp_unsigned_integer _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr int popcount(_Tp __t) noexcept {
#  if __has_builtin(__builtin_popcountg)
  return __builtin_popcountg(__t);
#  else  // __has_builtin(__builtin_popcountg)
  if (sizeof(_Tp) <= sizeof(unsigned int))
    return std::__libcpp_popcount(static_cast<unsigned int>(__t));
  else if (sizeof(_Tp) <= sizeof(unsigned long))
    return std::__libcpp_popcount(static_cast<unsigned long>(__t));
  else if (sizeof(_Tp) <= sizeof(unsigned long long))
    return std::__libcpp_popcount(static_cast<unsigned long long>(__t));
  else {
    int __ret = 0;
    while (__t != 0) {
      __ret += std::__libcpp_popcount(static_cast<unsigned long long>(__t));
      __t >>= numeric_limits<unsigned long long>::digits;
    }
    return __ret;
  }
#  endif // __has_builtin(__builtin_popcountg)
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___BIT_POPCOUNT_H
