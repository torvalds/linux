// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_CTYPE_H
#define _LIBCPP_CTYPE_H

/*
    ctype.h synopsis

int isalnum(int c);
int isalpha(int c);
int isblank(int c);  // C99
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);
*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if __has_include_next(<ctype.h>)
#  include_next <ctype.h>
#endif

#ifdef __cplusplus

#  undef isalnum
#  undef isalpha
#  undef isblank
#  undef iscntrl
#  undef isdigit
#  undef isgraph
#  undef islower
#  undef isprint
#  undef ispunct
#  undef isspace
#  undef isupper
#  undef isxdigit
#  undef tolower
#  undef toupper

#endif

#endif // _LIBCPP_CTYPE_H
