//===-- ffsti2.c - Implement __ffsti2 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __ffsti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: the index of the least significant 1-bit in a, or
// the value zero if a is zero. The least significant bit is index one.

COMPILER_RT_ABI int __ffsti2(ti_int a) {
  twords x;
  x.all = a;
  if (x.s.low == 0) {
    if (x.s.high == 0)
      return 0;
    return __builtin_ctzll(x.s.high) + (1 + sizeof(di_int) * CHAR_BIT);
  }
  return __builtin_ctzll(x.s.low) + 1;
}

#endif // CRT_HAS_128BIT
