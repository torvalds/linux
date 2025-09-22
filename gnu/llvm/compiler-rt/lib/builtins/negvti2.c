//===-- negvti2.c - Implement __negvti2 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __negvti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: -a

// Effects: aborts if -a overflows

COMPILER_RT_ABI ti_int __negvti2(ti_int a) {
  const ti_int MIN = (tu_int)1 << ((int)(sizeof(ti_int) * CHAR_BIT) - 1);
  if (a == MIN)
    compilerrt_abort();
  return -a;
}

#endif // CRT_HAS_128BIT
