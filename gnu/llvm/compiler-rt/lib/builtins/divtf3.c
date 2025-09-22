//===-- lib/divtf3.c - Quad-precision division --------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements quad-precision soft-float division
// with the IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)

#define NUMBER_OF_HALF_ITERATIONS 4
#define NUMBER_OF_FULL_ITERATIONS 1

#include "fp_div_impl.inc"

COMPILER_RT_ABI fp_t __divtf3(fp_t a, fp_t b) { return __divXf3__(a, b); }

#endif
