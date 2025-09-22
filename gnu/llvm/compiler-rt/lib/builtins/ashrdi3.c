//===-- ashrdi3.c - Implement __ashrdi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __ashrdi3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: arithmetic a >> b

// Precondition:  0 <= b < bits_in_dword

COMPILER_RT_ABI di_int __ashrdi3(di_int a, int b) {
  const int bits_in_word = (int)(sizeof(si_int) * CHAR_BIT);
  dwords input;
  dwords result;
  input.all = a;
  if (b & bits_in_word) /* bits_in_word <= b < bits_in_dword */ {
    // result.s.high = input.s.high < 0 ? -1 : 0
    result.s.high = input.s.high >> (bits_in_word - 1);
    result.s.low = input.s.high >> (b - bits_in_word);
  } else /* 0 <= b < bits_in_word */ {
    if (b == 0)
      return a;
    result.s.high = input.s.high >> b;
    result.s.low =
        ((su_int)input.s.high << (bits_in_word - b)) | (input.s.low >> b);
  }
  return result.all;
}

#if defined(__ARM_EABI__)
COMPILER_RT_ALIAS(__ashrdi3, __aeabi_lasr)
#endif
