// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// long double __floatditf(long long x);
// This file implements the PowerPC long long -> long double conversion

#include "DD.h"

long double __floatditf(int64_t a) {

  static const double twop32 = 0x1.0p32;
  static const double twop52 = 0x1.0p52;

  doublebits low = {.d = twop52};
  low.x |= a & UINT64_C(0x00000000ffffffff); // 0x1.0p52 + low 32 bits of a.

  const double high_addend = (double)((int32_t)(a >> 32)) * twop32 - twop52;

  // At this point, we have two double precision numbers
  // high_addend and low.d, and we wish to return their sum
  // as a canonicalized long double:

  // This implementation sets the inexact flag spuriously.
  // This could be avoided, but at some substantial cost.

  DD result;

  result.s.hi = high_addend + low.d;
  result.s.lo = (high_addend - result.s.hi) + low.d;

  return result.ld;
}
