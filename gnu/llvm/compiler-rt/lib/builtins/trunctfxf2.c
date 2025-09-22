//===-- lib/trunctfsf2.c - long double -> quad conversion ---------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Assumption: long double is a IEEE 80 bit floating point type padded to 128
// bits.

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE) && __LDBL_MANT_DIG__ == 64 && defined(__x86_64__)

#define SRC_QUAD
#define DST_80
#include "fp_trunc_impl.inc"

COMPILER_RT_ABI xf_float __trunctfxf2(tf_float a) { return __truncXfYf2__(a); }

#endif
