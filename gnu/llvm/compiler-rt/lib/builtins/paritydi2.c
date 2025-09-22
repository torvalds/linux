//===-- paritydi2.c - Implement __paritydi2 -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __paritydi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: 1 if number of bits is odd else returns 0

COMPILER_RT_ABI int __paritydi2(di_int a) {
  dwords x;
  x.all = a;
  su_int x2 = x.s.high ^ x.s.low;
  x2 ^= x2 >> 16;
  x2 ^= x2 >> 8;
  x2 ^= x2 >> 4;
  return (0x6996 >> (x2 & 0xF)) & 1;
}
