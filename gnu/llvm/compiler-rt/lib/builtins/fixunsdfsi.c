//===-- fixunsdfsi.c - Implement __fixunsdfsi -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"
typedef su_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI su_int __fixunsdfsi(fp_t a) { return __fixuint(a); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI su_int __aeabi_d2uiz(fp_t a) { return __fixunsdfsi(a); }
#else
COMPILER_RT_ALIAS(__fixunsdfsi, __aeabi_d2uiz)
#endif
#endif
