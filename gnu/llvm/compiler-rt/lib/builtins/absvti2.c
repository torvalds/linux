//===-- absvti2.c - Implement __absvdi2 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __absvti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: absolute value

// Effects: aborts if abs(x) < 0

COMPILER_RT_ABI ti_int __absvti2(ti_int a) {
  const int N = (int)(sizeof(ti_int) * CHAR_BIT);
  if (a == (ti_int)((tu_int)1 << (N - 1)))
    compilerrt_abort();
  const ti_int s = a >> (N - 1);
  return (a ^ s) - s;
}

#endif // CRT_HAS_128BIT
