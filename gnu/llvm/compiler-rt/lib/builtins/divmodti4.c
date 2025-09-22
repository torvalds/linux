//===-- divmodti4.c - Implement __divmodti4 -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divmodti4 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: a / b, *rem = a % b

COMPILER_RT_ABI ti_int __divmodti4(ti_int a, ti_int b, ti_int *rem) {
  const int bits_in_tword_m1 = (int)(sizeof(ti_int) * CHAR_BIT) - 1;
  ti_int s_a = a >> bits_in_tword_m1;                   // s_a = a < 0 ? -1 : 0
  ti_int s_b = b >> bits_in_tword_m1;                   // s_b = b < 0 ? -1 : 0
  a = (tu_int)(a ^ s_a) - s_a;                          // negate if s_a == -1
  b = (tu_int)(b ^ s_b) - s_b;                          // negate if s_b == -1
  s_b ^= s_a;                                           // sign of quotient
  tu_int r;
  ti_int q = (__udivmodti4(a, b, &r) ^ s_b) - s_b;      // negate if s_b == -1
  *rem = (r ^ s_a) - s_a;                               // negate if s_a == -1
  return q;
}

#endif // CRT_HAS_128BIT
