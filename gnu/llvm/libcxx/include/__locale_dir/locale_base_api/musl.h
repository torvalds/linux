// -*- C++ -*-
//===-----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This adds support for the extended locale functions that are currently
// missing from the Musl C library.
//
// This only works when the specified locale is "C" or "POSIX", but that's
// about as good as we can do without implementing full xlocale support
// in Musl.
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___LOCALE_LOCALE_BASE_API_MUSL_H
#define _LIBCPP___LOCALE_LOCALE_BASE_API_MUSL_H

#include <cstdlib>
#include <cwchar>

inline _LIBCPP_HIDE_FROM_ABI long long strtoll_l(const char* __nptr, char** __endptr, int __base, locale_t) {
  return ::strtoll(__nptr, __endptr, __base);
}

inline _LIBCPP_HIDE_FROM_ABI unsigned long long strtoull_l(const char* __nptr, char** __endptr, int __base, locale_t) {
  return ::strtoull(__nptr, __endptr, __base);
}

#endif // _LIBCPP___LOCALE_LOCALE_BASE_API_MUSL_H
