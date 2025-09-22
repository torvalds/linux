// -*- C++ -*-
//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_STRING_VIEW_H
#define _LIBCPP___FWD_STRING_VIEW_H

#include <__config>
#include <__fwd/string.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _CharT, class _Traits = char_traits<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_string_view;

typedef basic_string_view<char> string_view;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
typedef basic_string_view<char8_t> u8string_view;
#endif
typedef basic_string_view<char16_t> u16string_view;
typedef basic_string_view<char32_t> u32string_view;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
typedef basic_string_view<wchar_t> wstring_view;
#endif

// clang-format off
template <class _CharT, class _Traits>
class _LIBCPP_PREFERRED_NAME(string_view)
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
      _LIBCPP_PREFERRED_NAME(wstring_view)
#endif
#ifndef _LIBCPP_HAS_NO_CHAR8_T
      _LIBCPP_PREFERRED_NAME(u8string_view)
#endif
      _LIBCPP_PREFERRED_NAME(u16string_view)
      _LIBCPP_PREFERRED_NAME(u32string_view)
      basic_string_view;
// clang-format on
_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_STRING_VIEW_H
