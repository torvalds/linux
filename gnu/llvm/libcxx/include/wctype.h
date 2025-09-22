// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_WCTYPE_H
#define _LIBCPP_WCTYPE_H

/*
    wctype.h synopsis

Macros:

    WEOF

Types:

    wint_t
    wctrans_t
    wctype_t

int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);  // C99
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);
int iswctype(wint_t wc, wctype_t desc);
wctype_t wctype(const char* property);
wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);
wint_t towctrans(wint_t wc, wctrans_t desc);
wctrans_t wctrans(const char* property);

*/

#include <__config>

#if defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)
#  error                                                                                                               \
      "The <wctype.h> header is not supported since libc++ has been configured with LIBCXX_ENABLE_WIDE_CHARACTERS disabled"
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// TODO:
// In the future, we should unconditionally include_next <wctype.h> here and instead
// have a mode under which the library does not need libc++'s <wctype.h> or <cwctype>
// at all (i.e. a mode without wchar_t). As it stands, we need to do that to completely
// bypass the using declarations in <cwctype> when we did not include <wctype.h>.
// Otherwise, a using declaration like `using ::wint_t` in <cwctype> will refer to
// nothing (with using_if_exists), and if we include another header that defines one
// of these declarations (e.g. <wchar.h>), the second `using ::wint_t` with using_if_exists
// will fail because it does not refer to the same declaration.
#if __has_include_next(<wctype.h>)
#  include_next <wctype.h>
#  define _LIBCPP_INCLUDED_C_LIBRARY_WCTYPE_H
#endif

#ifdef __cplusplus

#  undef iswalnum
#  undef iswalpha
#  undef iswblank
#  undef iswcntrl
#  undef iswdigit
#  undef iswgraph
#  undef iswlower
#  undef iswprint
#  undef iswpunct
#  undef iswspace
#  undef iswupper
#  undef iswxdigit
#  undef iswctype
#  undef wctype
#  undef towlower
#  undef towupper
#  undef towctrans
#  undef wctrans

#endif // __cplusplus

#endif // _LIBCPP_WCTYPE_H
