//===-- mulodi4.c - Implement __mulodi4 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __mulodi4 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#define fixint_t di_int
#define fixuint_t du_int
#include "int_mulo_impl.inc"

// Returns: a * b

// Effects: sets *overflow to 1  if a * b overflows

COMPILER_RT_ABI di_int __mulodi4(di_int a, di_int b, int *overflow) {
  return __muloXi4(a, b, overflow);
}
