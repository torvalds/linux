//===-- fixsfdi.c - Implement __fixsfdi -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

#ifndef __SOFTFP__
// Support for systems that have hardware floating-point; can set the invalid
// flag as a side-effect of computation.

COMPILER_RT_ABI du_int __fixunssfdi(float a);

COMPILER_RT_ABI di_int __fixsfdi(float a) {
  if (a < 0.0f) {
    return -__fixunssfdi(-a);
  }
  return __fixunssfdi(a);
}

#else
// Support for systems that don't have hardware floating-point; there are no
// flags to set, and we don't want to code-gen to an unknown soft-float
// implementation.

typedef di_int fixint_t;
typedef du_int fixuint_t;
#include "fp_fixint_impl.inc"

COMPILER_RT_ABI di_int __fixsfdi(fp_t a) { return __fixint(a); }

#endif

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI di_int __aeabi_f2lz(fp_t a) { return __fixsfdi(a); }
#else
COMPILER_RT_ALIAS(__fixsfdi, __aeabi_f2lz)
#endif
#endif

#if defined(__MINGW32__) && defined(__arm__)
COMPILER_RT_ALIAS(__fixsfdi, __stoi64)
#endif
