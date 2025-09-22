// -*- C++ -*-
//===-----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// These are reimplementations of some extended locale functions ( *_l ) that
// aren't part of POSIX.  They are widely available though (GLIBC, BSD, maybe
// others).  The unifying aspect in this case is that all of these functions
// convert strings to some numeric type.
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SUPPORT_XLOCALE_STRTONUM_FALLBACK_H
#define _LIBCPP___SUPPORT_XLOCALE_STRTONUM_FALLBACK_H

#include <__config>
#include <stdlib.h>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  include <wchar.h>
#endif

inline _LIBCPP_HIDE_FROM_ABI float strtof_l(const char* __nptr, char** __endptr, locale_t) {
  return ::strtof(__nptr, __endptr);
}

inline _LIBCPP_HIDE_FROM_ABI double strtod_l(const char* __nptr, char** __endptr, locale_t) {
  return ::strtod(__nptr, __endptr);
}

inline _LIBCPP_HIDE_FROM_ABI long double strtold_l(const char* __nptr, char** __endptr, locale_t) {
  return ::strtold(__nptr, __endptr);
}

inline _LIBCPP_HIDE_FROM_ABI long long strtoll_l(const char* __nptr, char** __endptr, int __base, locale_t) {
  return ::strtoll(__nptr, __endptr, __base);
}

inline _LIBCPP_HIDE_FROM_ABI unsigned long long strtoull_l(const char* __nptr, char** __endptr, int __base, locale_t) {
  return ::strtoull(__nptr, __endptr, __base);
}

#endif // _LIBCPP___SUPPORT_XLOCALE_STRTONUM_FALLBACK_H
