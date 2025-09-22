//===-- int_mulv_impl.inc - Implement __mulv[sdt]i3 ---------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper used by __mulvsi3, __mulvdi3 and __mulvti3.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: a * b

// Effects: aborts if a * b overflows

static __inline fixint_t __mulvXi3(fixint_t a, fixint_t b) {
  const int N = (int)(sizeof(fixint_t) * CHAR_BIT);
  const fixint_t MIN = (fixint_t)((fixuint_t)1 << (N - 1));
  const fixint_t MAX = ~MIN;
  if (a == MIN) {
    if (b == 0 || b == 1)
      return a * b;
    compilerrt_abort();
  }
  if (b == MIN) {
    if (a == 0 || a == 1)
      return a * b;
    compilerrt_abort();
  }
  fixint_t sa = a >> (N - 1);
  fixint_t abs_a = (a ^ sa) - sa;
  fixint_t sb = b >> (N - 1);
  fixint_t abs_b = (b ^ sb) - sb;
  if (abs_a < 2 || abs_b < 2)
    return a * b;
  if (sa == sb) {
    if (abs_a > MAX / abs_b)
      compilerrt_abort();
  } else {
    if (abs_a > MIN / -abs_b)
      compilerrt_abort();
  }
  return a * b;
}
