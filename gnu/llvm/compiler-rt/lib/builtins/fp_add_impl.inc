//===----- lib/fp_add_impl.inc - floaing point addition -----------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements soft-float addition with the IEEE-754 default rounding
// (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#include "fp_lib.h"
#include "fp_mode.h"

static __inline fp_t __addXf3__(fp_t a, fp_t b) {
  rep_t aRep = toRep(a);
  rep_t bRep = toRep(b);
  const rep_t aAbs = aRep & absMask;
  const rep_t bAbs = bRep & absMask;

  // Detect if a or b is zero, infinity, or NaN.
  if (aAbs - REP_C(1) >= infRep - REP_C(1) ||
      bAbs - REP_C(1) >= infRep - REP_C(1)) {
    // NaN + anything = qNaN
    if (aAbs > infRep)
      return fromRep(toRep(a) | quietBit);
    // anything + NaN = qNaN
    if (bAbs > infRep)
      return fromRep(toRep(b) | quietBit);

    if (aAbs == infRep) {
      // +/-infinity + -/+infinity = qNaN
      if ((toRep(a) ^ toRep(b)) == signBit)
        return fromRep(qnanRep);
      // +/-infinity + anything remaining = +/- infinity
      else
        return a;
    }

    // anything remaining + +/-infinity = +/-infinity
    if (bAbs == infRep)
      return b;

    // zero + anything = anything
    if (!aAbs) {
      // We need to get the sign right for zero + zero.
      if (!bAbs)
        return fromRep(toRep(a) & toRep(b));
      else
        return b;
    }

    // anything + zero = anything
    if (!bAbs)
      return a;
  }

  // Swap a and b if necessary so that a has the larger absolute value.
  if (bAbs > aAbs) {
    const rep_t temp = aRep;
    aRep = bRep;
    bRep = temp;
  }

  // Extract the exponent and significand from the (possibly swapped) a and b.
  int aExponent = aRep >> significandBits & maxExponent;
  int bExponent = bRep >> significandBits & maxExponent;
  rep_t aSignificand = aRep & significandMask;
  rep_t bSignificand = bRep & significandMask;

  // Normalize any denormals, and adjust the exponent accordingly.
  if (aExponent == 0)
    aExponent = normalize(&aSignificand);
  if (bExponent == 0)
    bExponent = normalize(&bSignificand);

  // The sign of the result is the sign of the larger operand, a.  If they
  // have opposite signs, we are performing a subtraction.  Otherwise, we
  // perform addition.
  const rep_t resultSign = aRep & signBit;
  const bool subtraction = (aRep ^ bRep) & signBit;

  // Shift the significands to give us round, guard and sticky, and set the
  // implicit significand bit.  If we fell through from the denormal path it
  // was already set by normalize( ), but setting it twice won't hurt
  // anything.
  aSignificand = (aSignificand | implicitBit) << 3;
  bSignificand = (bSignificand | implicitBit) << 3;

  // Shift the significand of b by the difference in exponents, with a sticky
  // bottom bit to get rounding correct.
  const unsigned int align = (unsigned int)(aExponent - bExponent);
  if (align) {
    if (align < typeWidth) {
      const bool sticky = (bSignificand << (typeWidth - align)) != 0;
      bSignificand = bSignificand >> align | sticky;
    } else {
      bSignificand = 1; // Set the sticky bit.  b is known to be non-zero.
    }
  }
  if (subtraction) {
    aSignificand -= bSignificand;
    // If a == -b, return +zero.
    if (aSignificand == 0)
      return fromRep(0);

    // If partial cancellation occured, we need to left-shift the result
    // and adjust the exponent.
    if (aSignificand < implicitBit << 3) {
      const int shift = rep_clz(aSignificand) - rep_clz(implicitBit << 3);
      aSignificand <<= shift;
      aExponent -= shift;
    }
  } else /* addition */ {
    aSignificand += bSignificand;

    // If the addition carried up, we need to right-shift the result and
    // adjust the exponent.
    if (aSignificand & implicitBit << 4) {
      const bool sticky = aSignificand & 1;
      aSignificand = aSignificand >> 1 | sticky;
      aExponent += 1;
    }
  }

  // If we have overflowed the type, return +/- infinity.
  if (aExponent >= maxExponent)
    return fromRep(infRep | resultSign);

  if (aExponent <= 0) {
    // The result is denormal before rounding.  The exponent is zero and we
    // need to shift the significand.
    const int shift = 1 - aExponent;
    const bool sticky = (aSignificand << (typeWidth - shift)) != 0;
    aSignificand = aSignificand >> shift | sticky;
    aExponent = 0;
  }

  // Low three bits are round, guard, and sticky.
  const int roundGuardSticky = aSignificand & 0x7;

  // Shift the significand into place, and mask off the implicit bit.
  rep_t result = aSignificand >> 3 & significandMask;

  // Insert the exponent and sign.
  result |= (rep_t)aExponent << significandBits;
  result |= resultSign;

  // Perform the final rounding.  The result may overflow to infinity, but
  // that is the correct result in that case.
  switch (__fe_getround()) {
  case CRT_FE_TONEAREST:
    if (roundGuardSticky > 0x4)
      result++;
    if (roundGuardSticky == 0x4)
      result += result & 1;
    break;
  case CRT_FE_DOWNWARD:
    if (resultSign && roundGuardSticky) result++;
    break;
  case CRT_FE_UPWARD:
    if (!resultSign && roundGuardSticky) result++;
    break;
  case CRT_FE_TOWARDZERO:
    break;
  }
  if (roundGuardSticky)
    __fe_raise_inexact();
  return fromRep(result);
}
