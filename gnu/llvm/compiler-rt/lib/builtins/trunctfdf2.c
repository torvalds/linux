//===-- lib/truncdfsf2.c - quad -> double conversion --------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
#define SRC_QUAD
#define DST_DOUBLE
#include "fp_trunc_impl.inc"

COMPILER_RT_ABI dst_t __trunctfdf2(src_t a) { return __truncXfYf2__(a); }

#endif
