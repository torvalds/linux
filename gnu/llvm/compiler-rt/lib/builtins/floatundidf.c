//===-- floatundidf.c - Implement __floatundidf ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __floatundidf for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

// Returns: convert a to a double, rounding toward even.

// Assumption: double is a IEEE 64 bit floating point type
//             du_int is a 64 bit integral type

// seee eeee eeee mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm mmmm mmmm
// mmmm

#include "int_lib.h"

#ifndef __SOFTFP__
// Support for systems that have hardware floating-point; we'll set the inexact
// flag as a side-effect of this computation.

COMPILER_RT_ABI double __floatundidf(du_int a) {
  static const double twop52 = 4503599627370496.0;           // 0x1.0p52
  static const double twop84 = 19342813113834066795298816.0; // 0x1.0p84
  static const double twop84_plus_twop52 =
      19342813118337666422669312.0; // 0x1.00000001p84

  union {
    uint64_t x;
    double d;
  } high = {.d = twop84};
  union {
    uint64_t x;
    double d;
  } low = {.d = twop52};

  high.x |= a >> 32;
  low.x |= a & UINT64_C(0x00000000ffffffff);

  const double result = (high.d - twop84_plus_twop52) + low.d;
  return result;
}

#else
// Support for systems that don't have hardware floating-point; there are no
// flags to set, and we don't want to code-gen to an unknown soft-float
// implementation.

#define SRC_U64
#define DST_DOUBLE
#include "int_to_fp_impl.inc"

COMPILER_RT_ABI double __floatundidf(du_int a) { return __floatXiYf__(a); }
#endif

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI double __aeabi_ul2d(du_int a) { return __floatundidf(a); }
#else
COMPILER_RT_ALIAS(__floatundidf, __aeabi_ul2d)
#endif
#endif

#if defined(__MINGW32__) && defined(__arm__)
COMPILER_RT_ALIAS(__floatundidf, __u64tod)
#endif
