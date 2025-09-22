//===-- popcountdi2.c - Implement __popcountdi2 ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __popcountdi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: count of 1 bits

COMPILER_RT_ABI int __popcountdi2(di_int a) {
  du_int x2 = (du_int)a;
  x2 = x2 - ((x2 >> 1) & 0x5555555555555555uLL);
  // Every 2 bits holds the sum of every pair of bits (32)
  x2 = ((x2 >> 2) & 0x3333333333333333uLL) + (x2 & 0x3333333333333333uLL);
  // Every 4 bits holds the sum of every 4-set of bits (3 significant bits) (16)
  x2 = (x2 + (x2 >> 4)) & 0x0F0F0F0F0F0F0F0FuLL;
  // Every 8 bits holds the sum of every 8-set of bits (4 significant bits) (8)
  su_int x = (su_int)(x2 + (x2 >> 32));
  // The lower 32 bits hold four 16 bit sums (5 significant bits).
  //   Upper 32 bits are garbage
  x = x + (x >> 16);
  // The lower 16 bits hold two 32 bit sums (6 significant bits).
  //   Upper 16 bits are garbage
  return (x + (x >> 8)) & 0x0000007F; // (7 significant bits)
}
