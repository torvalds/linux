//===-- divmoddi4.c - Implement __divmoddi4 -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divmoddi4 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: a / b, *rem = a % b

COMPILER_RT_ABI di_int __divmoddi4(di_int a, di_int b, di_int *rem) {
  const int bits_in_dword_m1 = (int)(sizeof(di_int) * CHAR_BIT) - 1;
  di_int s_a = a >> bits_in_dword_m1;                   // s_a = a < 0 ? -1 : 0
  di_int s_b = b >> bits_in_dword_m1;                   // s_b = b < 0 ? -1 : 0
  a = (du_int)(a ^ s_a) - s_a;                          // negate if s_a == -1
  b = (du_int)(b ^ s_b) - s_b;                          // negate if s_b == -1
  s_b ^= s_a;                                           // sign of quotient
  du_int r;
  di_int q = (__udivmoddi4(a, b, &r) ^ s_b) - s_b;      // negate if s_b == -1
  *rem = (r ^ s_a) - s_a;                               // negate if s_a == -1
  return q;
}
