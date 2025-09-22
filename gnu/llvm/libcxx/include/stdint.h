// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_STDINT_H
// AIX system headers need stdint.h to be re-enterable while _STD_TYPES_T
// is defined until an inclusion of it without _STD_TYPES_T occurs, in which
// case the header guard macro is defined.
#if !defined(_AIX) || !defined(_STD_TYPES_T)
#  define _LIBCPP_STDINT_H
#endif // _STD_TYPES_T

/*
    stdint.h synopsis

Macros:

    INT8_MIN
    INT16_MIN
    INT32_MIN
    INT64_MIN

    INT8_MAX
    INT16_MAX
    INT32_MAX
    INT64_MAX

    UINT8_MAX
    UINT16_MAX
    UINT32_MAX
    UINT64_MAX

    INT_LEAST8_MIN
    INT_LEAST16_MIN
    INT_LEAST32_MIN
    INT_LEAST64_MIN

    INT_LEAST8_MAX
    INT_LEAST16_MAX
    INT_LEAST32_MAX
    INT_LEAST64_MAX

    UINT_LEAST8_MAX
    UINT_LEAST16_MAX
    UINT_LEAST32_MAX
    UINT_LEAST64_MAX

    INT_FAST8_MIN
    INT_FAST16_MIN
    INT_FAST32_MIN
    INT_FAST64_MIN

    INT_FAST8_MAX
    INT_FAST16_MAX
    INT_FAST32_MAX
    INT_FAST64_MAX

    UINT_FAST8_MAX
    UINT_FAST16_MAX
    UINT_FAST32_MAX
    UINT_FAST64_MAX

    INTPTR_MIN
    INTPTR_MAX
    UINTPTR_MAX

    INTMAX_MIN
    INTMAX_MAX

    UINTMAX_MAX

    PTRDIFF_MIN
    PTRDIFF_MAX

    SIG_ATOMIC_MIN
    SIG_ATOMIC_MAX

    SIZE_MAX

    WCHAR_MIN
    WCHAR_MAX

    WINT_MIN
    WINT_MAX

    INT8_C(value)
    INT16_C(value)
    INT32_C(value)
    INT64_C(value)

    UINT8_C(value)
    UINT16_C(value)
    UINT32_C(value)
    UINT64_C(value)

    INTMAX_C(value)
    UINTMAX_C(value)

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

/* C99 stdlib (e.g. glibc < 2.18) does not provide macros needed
   for C++11 unless __STDC_LIMIT_MACROS and __STDC_CONSTANT_MACROS
   are defined
*/
#if defined(__cplusplus) && !defined(__STDC_LIMIT_MACROS)
#  define __STDC_LIMIT_MACROS
#endif
#if defined(__cplusplus) && !defined(__STDC_CONSTANT_MACROS)
#  define __STDC_CONSTANT_MACROS
#endif

#if __has_include_next(<stdint.h>)
#  include_next <stdint.h>
#endif

#endif // _LIBCPP_STDINT_H
