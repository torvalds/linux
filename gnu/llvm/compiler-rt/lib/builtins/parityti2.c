//===-- parityti2.c - Implement __parityti2 -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __parityti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: 1 if number of bits is odd else returns 0

COMPILER_RT_ABI int __parityti2(ti_int a) {
  twords x;
  dwords x2;
  x.all = a;
  x2.all = x.s.high ^ x.s.low;
  su_int x3 = x2.s.high ^ x2.s.low;
  x3 ^= x3 >> 16;
  x3 ^= x3 >> 8;
  x3 ^= x3 >> 4;
  return (0x6996 >> (x3 & 0xF)) & 1;
}

#endif // CRT_HAS_128BIT
