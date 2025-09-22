//===-- absvsi2.c - Implement __absvsi2 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __absvsi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: absolute value

// Effects: aborts if abs(x) < 0

COMPILER_RT_ABI si_int __absvsi2(si_int a) {
  const int N = (int)(sizeof(si_int) * CHAR_BIT);
  if (a == ((si_int)((su_int)1 << (N - 1))))
    compilerrt_abort();
  const si_int t = a >> (N - 1);
  return (a ^ t) - t;
}
