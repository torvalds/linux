//===-- fixunssfsi.c - Implement __fixunssfsi -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __fixunssfsi for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"
typedef su_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI su_int __fixunssfsi(fp_t a) { return __fixuint(a); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI su_int __aeabi_f2uiz(fp_t a) { return __fixunssfsi(a); }
#else
COMPILER_RT_ALIAS(__fixunssfsi, __aeabi_f2uiz)
#endif
#endif
