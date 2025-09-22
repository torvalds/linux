//===-- lib/extendsfdf2.c - single -> double conversion -----------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define SRC_SINGLE
#define DST_DOUBLE
#include "fp_extend_impl.inc"

COMPILER_RT_ABI double __extendsfdf2(float a) { return __extendXfYf2__(a); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI double __aeabi_f2d(float a) { return __extendsfdf2(a); }
#else
COMPILER_RT_ALIAS(__extendsfdf2, __aeabi_f2d)
#endif
#endif
