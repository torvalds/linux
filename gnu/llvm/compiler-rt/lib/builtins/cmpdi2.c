//===-- cmpdi2.c - Implement __cmpdi2 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __cmpdi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: if (a <  b) returns 0
//           if (a == b) returns 1
//           if (a >  b) returns 2

COMPILER_RT_ABI si_int __cmpdi2(di_int a, di_int b) {
  dwords x;
  x.all = a;
  dwords y;
  y.all = b;
  if (x.s.high < y.s.high)
    return 0;
  if (x.s.high > y.s.high)
    return 2;
  if (x.s.low < y.s.low)
    return 0;
  if (x.s.low > y.s.low)
    return 2;
  return 1;
}

#ifdef __ARM_EABI__
// Returns: if (a <  b) returns -1
//           if (a == b) returns  0
//           if (a >  b) returns  1
COMPILER_RT_ABI si_int __aeabi_lcmp(di_int a, di_int b) {
  return __cmpdi2(a, b) - 1;
}
#endif
