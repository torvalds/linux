// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// The BSDs have lots of *_l functions.  This file provides reimplementations
// of those functions for non-BSD platforms.
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___LOCALE_LOCALE_BASE_API_BSD_LOCALE_FALLBACKS_H
#define _LIBCPP___LOCALE_LOCALE_BASE_API_BSD_LOCALE_FALLBACKS_H

#include <__locale_dir/locale_base_api/locale_guard.h>
#include <cstdio>
#include <stdarg.h>
#include <stdlib.h>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  include <cwchar>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

inline _LIBCPP_HIDE_FROM_ABI decltype(MB_CUR_MAX) __libcpp_mb_cur_max_l(locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return MB_CUR_MAX;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
inline _LIBCPP_HIDE_FROM_ABI wint_t __libcpp_btowc_l(int __c, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return btowc(__c);
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_wctob_l(wint_t __c, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return wctob(__c);
}

inline _LIBCPP_HIDE_FROM_ABI size_t
__libcpp_wcsnrtombs_l(char* __dest, const wchar_t** __src, size_t __nwc, size_t __len, mbstate_t* __ps, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return wcsnrtombs(__dest, __src, __nwc, __len, __ps);
}

inline _LIBCPP_HIDE_FROM_ABI size_t __libcpp_wcrtomb_l(char* __s, wchar_t __wc, mbstate_t* __ps, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return wcrtomb(__s, __wc, __ps);
}

inline _LIBCPP_HIDE_FROM_ABI size_t
__libcpp_mbsnrtowcs_l(wchar_t* __dest, const char** __src, size_t __nms, size_t __len, mbstate_t* __ps, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return mbsnrtowcs(__dest, __src, __nms, __len, __ps);
}

inline _LIBCPP_HIDE_FROM_ABI size_t
__libcpp_mbrtowc_l(wchar_t* __pwc, const char* __s, size_t __n, mbstate_t* __ps, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return mbrtowc(__pwc, __s, __n, __ps);
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_mbtowc_l(wchar_t* __pwc, const char* __pmb, size_t __max, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return mbtowc(__pwc, __pmb, __max);
}

inline _LIBCPP_HIDE_FROM_ABI size_t __libcpp_mbrlen_l(const char* __s, size_t __n, mbstate_t* __ps, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return mbrlen(__s, __n, __ps);
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

inline _LIBCPP_HIDE_FROM_ABI lconv* __libcpp_localeconv_l(locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return localeconv();
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
inline _LIBCPP_HIDE_FROM_ABI size_t
__libcpp_mbsrtowcs_l(wchar_t* __dest, const char** __src, size_t __len, mbstate_t* __ps, locale_t __l) {
  __libcpp_locale_guard __current(__l);
  return mbsrtowcs(__dest, __src, __len, __ps);
}
#endif

inline _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 4, 5) int __libcpp_snprintf_l(
    char* __s, size_t __n, locale_t __l, const char* __format, ...) {
  va_list __va;
  va_start(__va, __format);
  __libcpp_locale_guard __current(__l);
  int __res = vsnprintf(__s, __n, __format, __va);
  va_end(__va);
  return __res;
}

inline _LIBCPP_ATTRIBUTE_FORMAT(__printf__, 3, 4) int __libcpp_asprintf_l(
    char** __s, locale_t __l, const char* __format, ...) {
  va_list __va;
  va_start(__va, __format);
  __libcpp_locale_guard __current(__l);
  int __res = vasprintf(__s, __format, __va);
  va_end(__va);
  return __res;
}

inline _LIBCPP_ATTRIBUTE_FORMAT(__scanf__, 3, 4) int __libcpp_sscanf_l(
    const char* __s, locale_t __l, const char* __format, ...) {
  va_list __va;
  va_start(__va, __format);
  __libcpp_locale_guard __current(__l);
  int __res = vsscanf(__s, __format, __va);
  va_end(__va);
  return __res;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___LOCALE_LOCALE_BASE_API_BSD_LOCALE_FALLBACKS_H
