//===-- lshrti3.c - Implement __lshrti3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __lshrti3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: logical a >> b

// Precondition:  0 <= b < bits_in_tword

COMPILER_RT_ABI ti_int __lshrti3(ti_int a, int b) {
  const int bits_in_dword = (int)(sizeof(di_int) * CHAR_BIT);
  utwords input;
  utwords result;
  input.all = a;
  if (b & bits_in_dword) /* bits_in_dword <= b < bits_in_tword */ {
    result.s.high = 0;
    result.s.low = input.s.high >> (b - bits_in_dword);
  } else /* 0 <= b < bits_in_dword */ {
    if (b == 0)
      return a;
    result.s.high = input.s.high >> b;
    result.s.low = (input.s.high << (bits_in_dword - b)) | (input.s.low >> b);
  }
  return result.all;
}

#endif // CRT_HAS_128BIT
