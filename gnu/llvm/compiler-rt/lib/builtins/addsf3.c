//===-- lib/addsf3.c - Single-precision addition ------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements single-precision soft-float addition.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_add_impl.inc"

COMPILER_RT_ABI float __addsf3(float a, float b) { return __addXf3__(a, b); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI float __aeabi_fadd(float a, float b) { return __addsf3(a, b); }
#else
COMPILER_RT_ALIAS(__addsf3, __aeabi_fadd)
#endif
#endif
