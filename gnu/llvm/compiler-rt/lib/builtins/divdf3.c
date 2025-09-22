//===-- lib/divdf3.c - Double-precision division ------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements double-precision soft-float division
// with the IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION

#define NUMBER_OF_HALF_ITERATIONS 3
#define NUMBER_OF_FULL_ITERATIONS 1

#include "fp_div_impl.inc"

COMPILER_RT_ABI fp_t __divdf3(fp_t a, fp_t b) { return __divXf3__(a, b); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_ddiv(fp_t a, fp_t b) { return __divdf3(a, b); }
#else
COMPILER_RT_ALIAS(__divdf3, __aeabi_ddiv)
#endif
#endif
