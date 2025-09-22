//===-- mulvdi3.c - Implement __mulvdi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __mulvdi3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#define fixint_t di_int
#define fixuint_t du_int
#include "int_mulv_impl.inc"

// Returns: a * b

// Effects: aborts if a * b overflows

COMPILER_RT_ABI di_int __mulvdi3(di_int a, di_int b) { return __mulvXi3(a, b); }
