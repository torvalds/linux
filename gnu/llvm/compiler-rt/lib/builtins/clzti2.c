//===-- clzti2.c - Implement __clzti2 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __clzti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: the number of leading 0-bits

// Precondition: a != 0

COMPILER_RT_ABI int __clzti2(ti_int a) {
  twords x;
  x.all = a;
  const di_int f = -(x.s.high == 0);
  return __builtin_clzll((x.s.high & ~f) | (x.s.low & f)) +
         ((si_int)f & ((si_int)(sizeof(di_int) * CHAR_BIT)));
}

#endif // CRT_HAS_128BIT
