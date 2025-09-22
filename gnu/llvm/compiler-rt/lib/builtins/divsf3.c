//===-- lib/divsf3.c - Single-precision division ------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements single-precision soft-float division
// with the IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION

#define NUMBER_OF_HALF_ITERATIONS 0
#define NUMBER_OF_FULL_ITERATIONS 3
#define USE_NATIVE_FULL_ITERATIONS

#include "fp_div_impl.inc"

COMPILER_RT_ABI fp_t __divsf3(fp_t a, fp_t b) { return __divXf3__(a, b); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_fdiv(fp_t a, fp_t b) { return __divsf3(a, b); }
#else
COMPILER_RT_ALIAS(__divsf3, __aeabi_fdiv)
#endif
#endif
