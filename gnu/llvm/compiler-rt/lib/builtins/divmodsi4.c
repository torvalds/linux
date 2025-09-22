//===-- divmodsi4.c - Implement __divmodsi4
//--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divmodsi4 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: a / b, *rem = a % b

COMPILER_RT_ABI si_int __divmodsi4(si_int a, si_int b, si_int *rem) {
  const int bits_in_word_m1 = (int)(sizeof(si_int) * CHAR_BIT) - 1;
  si_int s_a = a >> bits_in_word_m1;                    // s_a = a < 0 ? -1 : 0
  si_int s_b = b >> bits_in_word_m1;                    // s_b = b < 0 ? -1 : 0
  a = (su_int)(a ^ s_a) - s_a;                          // negate if s_a == -1
  b = (su_int)(b ^ s_b) - s_b;                          // negate if s_b == -1
  s_b ^= s_a;                                           // sign of quotient
  su_int r;
  si_int q = (__udivmodsi4(a, b, &r) ^ s_b) - s_b;      // negate if s_b == -1
  *rem = (r ^ s_a) - s_a;                               // negate if s_a == -1
  return q;
}
