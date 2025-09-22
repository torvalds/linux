//===-- lib/multf3.c - Quad-precision multiplication --------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements quad-precision soft-float multiplication
// with the IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
#include "fp_mul_impl.inc"

COMPILER_RT_ABI fp_t __multf3(fp_t a, fp_t b) { return __mulXf3__(a, b); }

#endif
