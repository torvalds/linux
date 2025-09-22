//===-- fixunsxfti.c - Implement __fixunsxfti -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __fixunsxfti for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: convert a to a unsigned long long, rounding toward zero.
//          Negative values all become zero.

// Assumption: long double is an intel 80 bit floating point type padded with 6
// bytes tu_int is a 128 bit integral type value in long double is representable
// in tu_int or is negative

// gggg gggg gggg gggg gggg gggg gggg gggg | gggg gggg gggg gggg seee eeee eeee
// eeee | 1mmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm
// mmmm mmmm mmmm

COMPILER_RT_ABI tu_int __fixunsxfti(xf_float a) {
  xf_bits fb;
  fb.f = a;
  int e = (fb.u.high.s.low & 0x00007FFF) - 16383;
  if (e < 0 || (fb.u.high.s.low & 0x00008000))
    return 0;
  if ((unsigned)e > sizeof(tu_int) * CHAR_BIT)
    return ~(tu_int)0;
  tu_int r = fb.u.low.all;
  if (e > 63)
    r <<= (e - 63);
  else
    r >>= (63 - e);
  return r;
}

#endif // CRT_HAS_128BIT
