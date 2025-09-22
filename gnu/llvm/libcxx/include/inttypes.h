// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_INTTYPES_H
// AIX system headers need inttypes.h to be re-enterable while _STD_TYPES_T
// is defined until an inclusion of it without _STD_TYPES_T occurs, in which
// case the header guard macro is defined.
#if !defined(_AIX) || !defined(_STD_TYPES_T)
#  define _LIBCPP_INTTYPES_H
#endif // _STD_TYPES_T

/*
    inttypes.h synopsis

This entire header is C99 / C++0X

#include <stdint.h>  // <cinttypes> includes <cstdint>

Macros:

    PRId8
    PRId16
    PRId32
    PRId64

    PRIdLEAST8
    PRIdLEAST16
    PRIdLEAST32
    PRIdLEAST64

    PRIdFAST8
    PRIdFAST16
    PRIdFAST32
    PRIdFAST64

    PRIdMAX
    PRIdPTR

    PRIi8
    PRIi16
    PRIi32
    PRIi64

    PRIiLEAST8
    PRIiLEAST16
    PRIiLEAST32
    PRIiLEAST64

    PRIiFAST8
    PRIiFAST16
    PRIiFAST32
    PRIiFAST64

    PRIiMAX
    PRIiPTR

    PRIo8
    PRIo16
    PRIo32
    PRIo64

    PRIoLEAST8
    PRIoLEAST16
    PRIoLEAST32
    PRIoLEAST64

    PRIoFAST8
    PRIoFAST16
    PRIoFAST32
    PRIoFAST64

    PRIoMAX
    PRIoPTR

    PRIu8
    PRIu16
    PRIu32
    PRIu64

    PRIuLEAST8
    PRIuLEAST16
    PRIuLEAST32
    PRIuLEAST64

    PRIuFAST8
    PRIuFAST16
    PRIuFAST32
    PRIuFAST64

    PRIuMAX
    PRIuPTR

    PRIx8
    PRIx16
    PRIx32
    PRIx64

    PRIxLEAST8
    PRIxLEAST16
    PRIxLEAST32
    PRIxLEAST64

    PRIxFAST8
    PRIxFAST16
    PRIxFAST32
    PRIxFAST64

    PRIxMAX
    PRIxPTR

    PRIX8
    PRIX16
    PRIX32
    PRIX64

    PRIXLEAST8
    PRIXLEAST16
    PRIXLEAST32
    PRIXLEAST64

    PRIXFAST8
    PRIXFAST16
    PRIXFAST32
    PRIXFAST64

    PRIXMAX
    PRIXPTR

    SCNd8
    SCNd16
    SCNd32
    SCNd64

    SCNdLEAST8
    SCNdLEAST16
    SCNdLEAST32
    SCNdLEAST64

    SCNdFAST8
    SCNdFAST16
    SCNdFAST32
    SCNdFAST64

    SCNdMAX
    SCNdPTR

    SCNi8
    SCNi16
    SCNi32
    SCNi64

    SCNiLEAST8
    SCNiLEAST16
    SCNiLEAST32
    SCNiLEAST64

    SCNiFAST8
    SCNiFAST16
    SCNiFAST32
    SCNiFAST64

    SCNiMAX
    SCNiPTR

    SCNo8
    SCNo16
    SCNo32
    SCNo64

    SCNoLEAST8
    SCNoLEAST16
    SCNoLEAST32
    SCNoLEAST64

    SCNoFAST8
    SCNoFAST16
    SCNoFAST32
    SCNoFAST64

    SCNoMAX
    SCNoPTR

    SCNu8
    SCNu16
    SCNu32
    SCNu64

    SCNuLEAST8
    SCNuLEAST16
    SCNuLEAST32
    SCNuLEAST64

    SCNuFAST8
    SCNuFAST16
    SCNuFAST32
    SCNuFAST64

    SCNuMAX
    SCNuPTR

    SCNx8
    SCNx16
    SCNx32
    SCNx64

    SCNxLEAST8
    SCNxLEAST16
    SCNxLEAST32
    SCNxLEAST64

    SCNxFAST8
    SCNxFAST16
    SCNxFAST32
    SCNxFAST64

    SCNxMAX
    SCNxPTR

Types:

    imaxdiv_t

intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char* restrict nptr, char** restrict endptr, int base);
uintmax_t strtoumax(const char* restrict nptr, char** restrict endptr, int base);
intmax_t  wcstoimax(const wchar_t* restrict nptr, wchar_t** restrict endptr, int base);
uintmax_t wcstoumax(const wchar_t* restrict nptr, wchar_t** restrict endptr, int base);

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

/* C99 stdlib (e.g. glibc < 2.18) does not provide format macros needed
   for C++11 unless __STDC_FORMAT_MACROS is defined
*/
#if defined(__cplusplus) && !defined(__STDC_FORMAT_MACROS)
#  define __STDC_FORMAT_MACROS
#endif

#if __has_include_next(<inttypes.h>)
#  include_next <inttypes.h>
#endif

#ifdef __cplusplus

#  include <stdint.h>

#  undef imaxabs
#  undef imaxdiv

#endif // __cplusplus

#endif // _LIBCPP_INTTYPES_H
