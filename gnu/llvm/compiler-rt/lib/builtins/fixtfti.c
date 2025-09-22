//===-- fixtfti.c - Implement __fixtfti -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
typedef ti_int fixint_t;
typedef tu_int fixuint_t;
#include "fp_fixint_impl.inc"

COMPILER_RT_ABI ti_int __fixtfti(fp_t a) { return __fixint(a); }
#endif
