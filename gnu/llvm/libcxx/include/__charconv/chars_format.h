// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHARCONV_CHARS_FORMAT_H
#define _LIBCPP___CHARCONV_CHARS_FORMAT_H

#include <__config>
#include <__utility/to_underlying.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

enum class chars_format { scientific = 0x1, fixed = 0x2, hex = 0x4, general = fixed | scientific };

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format operator~(chars_format __x) {
  return chars_format(~std::__to_underlying(__x));
}

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format operator&(chars_format __x, chars_format __y) {
  return chars_format(std::__to_underlying(__x) & std::__to_underlying(__y));
}

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format operator|(chars_format __x, chars_format __y) {
  return chars_format(std::__to_underlying(__x) | std::__to_underlying(__y));
}

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format operator^(chars_format __x, chars_format __y) {
  return chars_format(std::__to_underlying(__x) ^ std::__to_underlying(__y));
}

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format& operator&=(chars_format& __x, chars_format __y) {
  __x = __x & __y;
  return __x;
}

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format& operator|=(chars_format& __x, chars_format __y) {
  __x = __x | __y;
  return __x;
}

inline _LIBCPP_HIDE_FROM_ABI constexpr chars_format& operator^=(chars_format& __x, chars_format __y) {
  __x = __x ^ __y;
  return __x;
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CHARCONV_CHARS_FORMAT_H
