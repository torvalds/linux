//===-- lib/muldf3.c - Double-precision multiplication ------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements double-precision soft-float multiplication
// with the IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_mul_impl.inc"

COMPILER_RT_ABI fp_t __muldf3(fp_t a, fp_t b) { return __mulXf3__(a, b); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_dmul(fp_t a, fp_t b) { return __muldf3(a, b); }
#else
COMPILER_RT_ALIAS(__muldf3, __aeabi_dmul)
#endif
#endif
