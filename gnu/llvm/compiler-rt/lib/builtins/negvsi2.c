//===-- negvsi2.c - Implement __negvsi2 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __negvsi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: -a

// Effects: aborts if -a overflows

COMPILER_RT_ABI si_int __negvsi2(si_int a) {
  const si_int MIN =
      (si_int)((su_int)1 << ((int)(sizeof(si_int) * CHAR_BIT) - 1));
  if (a == MIN)
    compilerrt_abort();
  return -a;
}
