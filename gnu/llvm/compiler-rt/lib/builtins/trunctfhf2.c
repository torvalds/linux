//===-- lib/trunctfhf2.c - quad -> half conversion ----------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE) && defined(COMPILER_RT_HAS_FLOAT16)
#define SRC_QUAD
#define DST_HALF
#include "fp_trunc_impl.inc"

COMPILER_RT_ABI dst_t __trunctfhf2(src_t a) { return __truncXfYf2__(a); }

#endif
