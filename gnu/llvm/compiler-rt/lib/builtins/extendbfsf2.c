//===-- lib/extendbfsf2.c - bfloat -> single conversion -----------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define SRC_BFLOAT16
#define DST_SINGLE
#include "fp_extend_impl.inc"

COMPILER_RT_ABI float __extendbfsf2(src_t a) { return __extendXfYf2__(a); }
