//===-- lib/comparesf2.c - Single-precision comparisons -----------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the following soft-fp_t comparison routines:
//
//   __eqsf2   __gesf2   __unordsf2
//   __lesf2   __gtsf2
//   __ltsf2
//   __nesf2
//
// The semantics of the routines grouped in each column are identical, so there
// is a single implementation for each, and wrappers to provide the other names.
//
// The main routines behave as follows:
//
//   __lesf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                         1 if either a or b is NaN
//
//   __gesf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                        -1 if either a or b is NaN
//
//   __unordsf2(a,b) returns 0 if both a and b are numbers
//                           1 if either a or b is NaN
//
// Note that __lesf2( ) and __gesf2( ) are identical except in their handling of
// NaN values.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

#include "fp_compare_impl.inc"

COMPILER_RT_ABI CMP_RESULT __lesf2(fp_t a, fp_t b) { return __leXf2__(a, b); }

#if defined(__ELF__)
// Alias for libgcc compatibility
COMPILER_RT_ALIAS(__lesf2, __cmpsf2)
#endif
COMPILER_RT_ALIAS(__lesf2, __eqsf2)
COMPILER_RT_ALIAS(__lesf2, __ltsf2)
COMPILER_RT_ALIAS(__lesf2, __nesf2)

COMPILER_RT_ABI CMP_RESULT __gesf2(fp_t a, fp_t b) { return __geXf2__(a, b); }

COMPILER_RT_ALIAS(__gesf2, __gtsf2)

COMPILER_RT_ABI CMP_RESULT __unordsf2(fp_t a, fp_t b) {
  return __unordXf2__(a, b);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI int __aeabi_fcmpun(fp_t a, fp_t b) { return __unordsf2(a, b); }
#else
COMPILER_RT_ALIAS(__unordsf2, __aeabi_fcmpun)
#endif
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
// The alias mechanism doesn't work on Windows except for MinGW, so emit
// wrapper functions.
int __eqsf2(fp_t a, fp_t b) { return __lesf2(a, b); }
int __ltsf2(fp_t a, fp_t b) { return __lesf2(a, b); }
int __nesf2(fp_t a, fp_t b) { return __lesf2(a, b); }
int __gtsf2(fp_t a, fp_t b) { return __gesf2(a, b); }
#endif
