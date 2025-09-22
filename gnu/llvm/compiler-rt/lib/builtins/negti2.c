//===-- negti2.c - Implement __negti2 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __negti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: -a

COMPILER_RT_ABI ti_int __negti2(ti_int a) {
  // Note: this routine is here for API compatibility; any sane compiler
  // should expand it inline.
  return -(tu_int)a;
}

#endif // CRT_HAS_128BIT
