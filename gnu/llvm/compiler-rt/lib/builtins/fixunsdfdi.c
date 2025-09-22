//===-- fixunsdfdi.c - Implement __fixunsdfdi -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

#ifndef __SOFTFP__
// Support for systems that have hardware floating-point; can set the invalid
// flag as a side-effect of computation.

COMPILER_RT_ABI du_int __fixunsdfdi(double a) {
  if (a <= 0.0)
    return 0;
  su_int high = a / 4294967296.f;               // a / 0x1p32f;
  su_int low = a - (double)high * 4294967296.f; // high * 0x1p32f;
  return ((du_int)high << 32) | low;
}

#else
// Support for systems that don't have hardware floating-point; there are no
// flags to set, and we don't want to code-gen to an unknown soft-float
// implementation.

typedef du_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI du_int __fixunsdfdi(fp_t a) { return __fixuint(a); }

#endif

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI du_int __aeabi_d2ulz(fp_t a) { return __fixunsdfdi(a); }
#else
COMPILER_RT_ALIAS(__fixunsdfdi, __aeabi_d2ulz)
#endif
#endif

#if defined(__MINGW32__) && defined(__arm__)
COMPILER_RT_ALIAS(__fixunsdfdi, __dtou64)
#endif
