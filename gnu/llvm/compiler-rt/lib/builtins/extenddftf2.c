//===-- lib/extenddftf2.c - double -> quad conversion -------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
#define SRC_DOUBLE
#define DST_QUAD
#include "fp_extend_impl.inc"

COMPILER_RT_ABI dst_t __extenddftf2(src_t a) { return __extendXfYf2__(a); }

#endif
