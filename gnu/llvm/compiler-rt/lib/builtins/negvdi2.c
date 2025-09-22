//===-- negvdi2.c - Implement __negvdi2 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __negvdi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: -a

// Effects: aborts if -a overflows

COMPILER_RT_ABI di_int __negvdi2(di_int a) {
  const di_int MIN =
      (di_int)((du_int)1 << ((int)(sizeof(di_int) * CHAR_BIT) - 1));
  if (a == MIN)
    compilerrt_abort();
  return -a;
}
