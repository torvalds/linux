//===-- divdi3.c - Implement __divdi3 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divdi3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: a / b

#define fixint_t di_int
#define fixuint_t du_int
#define COMPUTE_UDIV(a, b) __udivmoddi4((a), (b), (du_int *)0)
#include "int_div_impl.inc"

COMPILER_RT_ABI di_int __divdi3(di_int a, di_int b) { return __divXi3(a, b); }
