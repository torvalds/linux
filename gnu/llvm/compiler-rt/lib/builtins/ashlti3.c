//===-- ashlti3.c - Implement __ashlti3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __ashlti3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: a << b

// Precondition:  0 <= b < bits_in_tword

COMPILER_RT_ABI ti_int __ashlti3(ti_int a, int b) {
  const int bits_in_dword = (int)(sizeof(di_int) * CHAR_BIT);
  twords input;
  twords result;
  input.all = a;
  if (b & bits_in_dword) /* bits_in_dword <= b < bits_in_tword */ {
    result.s.low = 0;
    result.s.high = input.s.low << (b - bits_in_dword);
  } else /* 0 <= b < bits_in_dword */ {
    if (b == 0)
      return a;
    result.s.low = input.s.low << b;
    result.s.high =
        ((du_int)input.s.high << b) | (input.s.low >> (bits_in_dword - b));
  }
  return result.all;
}

#endif // CRT_HAS_128BIT
