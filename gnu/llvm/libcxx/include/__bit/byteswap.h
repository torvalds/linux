// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___BIT_BYTESWAP_H
#define _LIBCPP___BIT_BYTESWAP_H

#include <__concepts/arithmetic.h>
#include <__config>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <integral _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Tp byteswap(_Tp __val) noexcept {
  if constexpr (sizeof(_Tp) == 1) {
    return __val;
  } else if constexpr (sizeof(_Tp) == 2) {
    return __builtin_bswap16(__val);
  } else if constexpr (sizeof(_Tp) == 4) {
    return __builtin_bswap32(__val);
  } else if constexpr (sizeof(_Tp) == 8) {
    return __builtin_bswap64(__val);
#  ifndef _LIBCPP_HAS_NO_INT128
  } else if constexpr (sizeof(_Tp) == 16) {
#    if __has_builtin(__builtin_bswap128)
    return __builtin_bswap128(__val);
#    else
    return static_cast<_Tp>(byteswap(static_cast<uint64_t>(__val))) << 64 |
           static_cast<_Tp>(byteswap(static_cast<uint64_t>(__val >> 64)));
#    endif // __has_builtin(__builtin_bswap128)
#  endif   // _LIBCPP_HAS_NO_INT128
  } else {
    static_assert(sizeof(_Tp) == 0, "byteswap is unimplemented for integral types of this size");
  }
}

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___BIT_BYTESWAP_H
