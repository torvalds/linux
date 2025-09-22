//===-- lib/comparetf2.c - Quad-precision comparisons -------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// // This file implements the following soft-float comparison routines:
//
//   __eqtf2   __getf2   __unordtf2
//   __letf2   __gttf2
//   __lttf2
//   __netf2
//
// The semantics of the routines grouped in each column are identical, so there
// is a single implementation for each, and wrappers to provide the other names.
//
// The main routines behave as follows:
//
//   __letf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                         1 if either a or b is NaN
//
//   __getf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                        -1 if either a or b is NaN
//
//   __unordtf2(a,b) returns 0 if both a and b are numbers
//                           1 if either a or b is NaN
//
// Note that __letf2( ) and __getf2( ) are identical except in their handling of
// NaN values.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
#include "fp_compare_impl.inc"

COMPILER_RT_ABI CMP_RESULT __letf2(fp_t a, fp_t b) { return __leXf2__(a, b); }

#if defined(__ELF__)
// Alias for libgcc compatibility
COMPILER_RT_ALIAS(__letf2, __cmptf2)
#endif
COMPILER_RT_ALIAS(__letf2, __eqtf2)
COMPILER_RT_ALIAS(__letf2, __lttf2)
COMPILER_RT_ALIAS(__letf2, __netf2)

COMPILER_RT_ABI CMP_RESULT __getf2(fp_t a, fp_t b) { return __geXf2__(a, b); }

COMPILER_RT_ALIAS(__getf2, __gttf2)

COMPILER_RT_ABI CMP_RESULT __unordtf2(fp_t a, fp_t b) {
  return __unordXf2__(a, b);
}

#endif
