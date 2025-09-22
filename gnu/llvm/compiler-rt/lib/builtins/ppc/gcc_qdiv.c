// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// long double __gcc_qdiv(long double x, long double y);
// This file implements the PowerPC 128-bit double-double division operation.
// This implementation is shamelessly cribbed from Apple's DDRT, circa 1993(!)

#include "DD.h"

long double __gcc_qdiv(long double a, long double b) {
  static const uint32_t infinityHi = UINT32_C(0x7ff00000);
  DD dst = {.ld = a}, src = {.ld = b};

  register double x = dst.s.hi, x1 = dst.s.lo, y = src.s.hi, y1 = src.s.lo;

  double yHi, yLo, qHi, qLo;
  double yq, tmp, q;

  q = x / y;

  // Detect special cases
  if (q == 0.0) {
    dst.s.hi = q;
    dst.s.lo = 0.0;
    return dst.ld;
  }

  const doublebits qBits = {.d = q};
  if (((uint32_t)(qBits.x >> 32) & infinityHi) == infinityHi) {
    dst.s.hi = q;
    dst.s.lo = 0.0;
    return dst.ld;
  }

  yHi = high26bits(y);
  qHi = high26bits(q);

  yq = y * q;
  yLo = y - yHi;
  qLo = q - qHi;

  tmp = LOWORDER(yq, yHi, yLo, qHi, qLo);
  tmp = (x - yq) - tmp;
  tmp = ((tmp + x1) - y1 * q) / y;
  x = q + tmp;

  dst.s.lo = (q - x) + tmp;
  dst.s.hi = x;

  return dst.ld;
}
