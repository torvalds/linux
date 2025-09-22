//===-- subvdi3.c - Implement __subvdi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __subvdi3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: a - b

// Effects: aborts if a - b overflows

COMPILER_RT_ABI di_int __subvdi3(di_int a, di_int b) {
  di_int s = (du_int)a - (du_int)b;
  if (b >= 0) {
    if (s > a)
      compilerrt_abort();
  } else {
    if (s <= a)
      compilerrt_abort();
  }
  return s;
}
