//===-- fixunstfsi.c - Implement __fixunstfsi -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
typedef tu_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI tu_int __fixunstfti(fp_t a) { return __fixuint(a); }
#endif
