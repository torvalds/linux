//===-- lib/truncdfsf2.c - double -> single conversion ------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define SRC_DOUBLE
#define DST_SINGLE
#include "fp_trunc_impl.inc"

COMPILER_RT_ABI float __truncdfsf2(double a) { return __truncXfYf2__(a); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI float __aeabi_d2f(double a) { return __truncdfsf2(a); }
#else
COMPILER_RT_ALIAS(__truncdfsf2, __aeabi_d2f)
#endif
#endif
