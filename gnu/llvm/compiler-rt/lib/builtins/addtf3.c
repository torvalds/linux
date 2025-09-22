//===-- lib/addtf3.c - Quad-precision addition --------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements quad-precision soft-float addition.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
#include "fp_add_impl.inc"

COMPILER_RT_ABI fp_t __addtf3(fp_t a, fp_t b) {
  return __addXf3__(a, b);
}

#endif
