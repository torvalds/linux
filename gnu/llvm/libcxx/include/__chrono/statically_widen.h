// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_STATICALLY_WIDEN_H
#define _LIBCPP___CHRONO_STATICALLY_WIDEN_H

// Implements the STATICALLY-WIDEN exposition-only function. ([time.general]/2)

#include <__concepts/same_as.h>
#include <__config>
#include <__format/concepts.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <__fmt_char_type _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr const _CharT* __statically_widen(const char* __str, const wchar_t* __wstr) {
  if constexpr (same_as<_CharT, char>)
    return __str;
  else
    return __wstr;
}
#    define _LIBCPP_STATICALLY_WIDEN(_CharT, __str) ::std::__statically_widen<_CharT>(__str, L##__str)
#  else // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// Without this indirection the unit test test/libcxx/modules_include.sh.cpp
// fails for the CI build "No wide characters". This seems like a bug.
// TODO FMT investigate why this is needed.
template <__fmt_char_type _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr const _CharT* __statically_widen(const char* __str) {
  return __str;
}
#    define _LIBCPP_STATICALLY_WIDEN(_CharT, __str) ::std::__statically_widen<_CharT>(__str)
#  endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CHRONO_STATICALLY_WIDEN_H
