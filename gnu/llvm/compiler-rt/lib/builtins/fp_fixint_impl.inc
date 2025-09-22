//===-- lib/fixdfsi.c - Double-precision -> integer conversion ----*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements float to integer conversion for the
// compiler-rt library.
//
//===----------------------------------------------------------------------===//

#include "fp_lib.h"

static __inline fixint_t __fixint(fp_t a) {
  const fixint_t fixint_max = (fixint_t)((~(fixuint_t)0) / 2);
  const fixint_t fixint_min = -fixint_max - 1;
  // Break a into sign, exponent, significand parts.
  const rep_t aRep = toRep(a);
  const rep_t aAbs = aRep & absMask;
  const fixint_t sign = aRep & signBit ? -1 : 1;
  const int exponent = (aAbs >> significandBits) - exponentBias;
  const rep_t significand = (aAbs & significandMask) | implicitBit;

  // If exponent is negative, the result is zero.
  if (exponent < 0)
    return 0;

  // If the value is too large for the integer type, saturate.
  if ((unsigned)exponent >= sizeof(fixint_t) * CHAR_BIT)
    return sign == 1 ? fixint_max : fixint_min;

  // If 0 <= exponent < significandBits, right shift to get the result.
  // Otherwise, shift left.
  if (exponent < significandBits)
    return (fixint_t)(sign * (significand >> (significandBits - exponent)));
  else
    return (fixint_t)(sign * ((fixuint_t)significand << (exponent - significandBits)));
}
