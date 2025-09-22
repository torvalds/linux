//===-- lib/comparedf2.c - Double-precision comparisons -----------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// // This file implements the following soft-float comparison routines:
//
//   __eqdf2   __gedf2   __unorddf2
//   __ledf2   __gtdf2
//   __ltdf2
//   __nedf2
//
// The semantics of the routines grouped in each column are identical, so there
// is a single implementation for each, and wrappers to provide the other names.
//
// The main routines behave as follows:
//
//   __ledf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                         1 if either a or b is NaN
//
//   __gedf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                        -1 if either a or b is NaN
//
//   __unorddf2(a,b) returns 0 if both a and b are numbers
//                           1 if either a or b is NaN
//
// Note that __ledf2( ) and __gedf2( ) are identical except in their handling of
// NaN values.
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

#include "fp_compare_impl.inc"

COMPILER_RT_ABI CMP_RESULT __ledf2(fp_t a, fp_t b) { return __leXf2__(a, b); }

#if defined(__ELF__)
// Alias for libgcc compatibility
COMPILER_RT_ALIAS(__ledf2, __cmpdf2)
#endif
COMPILER_RT_ALIAS(__ledf2, __eqdf2)
COMPILER_RT_ALIAS(__ledf2, __ltdf2)
COMPILER_RT_ALIAS(__ledf2, __nedf2)

COMPILER_RT_ABI CMP_RESULT __gedf2(fp_t a, fp_t b) { return __geXf2__(a, b); }

COMPILER_RT_ALIAS(__gedf2, __gtdf2)

COMPILER_RT_ABI CMP_RESULT __unorddf2(fp_t a, fp_t b) {
  return __unordXf2__(a, b);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI int __aeabi_dcmpun(fp_t a, fp_t b) { return __unorddf2(a, b); }
#else
COMPILER_RT_ALIAS(__unorddf2, __aeabi_dcmpun)
#endif
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
// The alias mechanism doesn't work on Windows except for MinGW, so emit
// wrapper functions.
int __eqdf2(fp_t a, fp_t b) { return __ledf2(a, b); }
int __ltdf2(fp_t a, fp_t b) { return __ledf2(a, b); }
int __nedf2(fp_t a, fp_t b) { return __ledf2(a, b); }
int __gtdf2(fp_t a, fp_t b) { return __gedf2(a, b); }
#endif
