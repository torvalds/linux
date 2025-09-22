//===-- fixunsdfti.c - Implement __fixunsdfti -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT
#define DOUBLE_PRECISION
#include "fp_lib.h"
typedef tu_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI tu_int __fixunsdfti(fp_t a) { return __fixuint(a); }
#endif // CRT_HAS_128BIT
