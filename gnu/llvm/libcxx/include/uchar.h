// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_UCHAR_H
#define _LIBCPP_UCHAR_H

/*
    uchar.h synopsis // since C++11

Macros:

    __STDC_UTF_16__
    __STDC_UTF_32__

Types:

  mbstate_t
  size_t

size_t mbrtoc8(char8_t* pc8, const char* s, size_t n, mbstate_t* ps);     // since C++20
size_t c8rtomb(char* s, char8_t c8, mbstate_t* ps);                       // since C++20
size_t mbrtoc16(char16_t* pc16, const char* s, size_t n, mbstate_t* ps);
size_t c16rtomb(char* s, char16_t c16, mbstate_t* ps);
size_t mbrtoc32(char32_t* pc32, const char* s, size_t n, mbstate_t* ps);
size_t c32rtomb(char* s, char32_t c32, mbstate_t* ps);

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if !defined(_LIBCPP_CXX03_LANG)

// Some platforms don't implement <uchar.h> and we don't want to give a hard
// error on those platforms. When the platform doesn't provide <uchar.h>, at
// least include <stddef.h> so we get the declaration for size_t, and try to
// get the declaration of mbstate_t too.
#  if __has_include_next(<uchar.h>)
#    include_next <uchar.h>
#  else
#    include <__mbstate_t.h>
#    include <stddef.h>
#  endif

#endif // _LIBCPP_CXX03_LANG

#endif // _LIBCPP_UCHAR_H
