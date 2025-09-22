//===---- lib/fp_mul_impl.inc - floating point multiplication -----*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements soft-float multiplication with the IEEE-754 default
// rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#include "fp_lib.h"

static __inline fp_t __mulXf3__(fp_t a, fp_t b) {
  const unsigned int aExponent = toRep(a) >> significandBits & maxExponent;
  const unsigned int bExponent = toRep(b) >> significandBits & maxExponent;
  const rep_t productSign = (toRep(a) ^ toRep(b)) & signBit;

  rep_t aSignificand = toRep(a) & significandMask;
  rep_t bSignificand = toRep(b) & significandMask;
  int scale = 0;

  // Detect if a or b is zero, denormal, infinity, or NaN.
  if (aExponent - 1U >= maxExponent - 1U ||
      bExponent - 1U >= maxExponent - 1U) {

    const rep_t aAbs = toRep(a) & absMask;
    const rep_t bAbs = toRep(b) & absMask;

    // NaN * anything = qNaN
    if (aAbs > infRep)
      return fromRep(toRep(a) | quietBit);
    // anything * NaN = qNaN
    if (bAbs > infRep)
      return fromRep(toRep(b) | quietBit);

    if (aAbs == infRep) {
      // infinity * non-zero = +/- infinity
      if (bAbs)
        return fromRep(aAbs | productSign);
      // infinity * zero = NaN
      else
        return fromRep(qnanRep);
    }

    if (bAbs == infRep) {
      // non-zero * infinity = +/- infinity
      if (aAbs)
        return fromRep(bAbs | productSign);
      // zero * infinity = NaN
      else
        return fromRep(qnanRep);
    }

    // zero * anything = +/- zero
    if (!aAbs)
      return fromRep(productSign);
    // anything * zero = +/- zero
    if (!bAbs)
      return fromRep(productSign);

    // One or both of a or b is denormal.  The other (if applicable) is a
    // normal number.  Renormalize one or both of a and b, and set scale to
    // include the necessary exponent adjustment.
    if (aAbs < implicitBit)
      scale += normalize(&aSignificand);
    if (bAbs < implicitBit)
      scale += normalize(&bSignificand);
  }

  // Set the implicit significand bit.  If we fell through from the
  // denormal path it was already set by normalize( ), but setting it twice
  // won't hurt anything.
  aSignificand |= implicitBit;
  bSignificand |= implicitBit;

  // Perform a basic multiplication on the significands.  One of them must be
  // shifted beforehand to be aligned with the exponent.
  rep_t productHi, productLo;
  wideMultiply(aSignificand, bSignificand << exponentBits, &productHi,
               &productLo);

  int productExponent = aExponent + bExponent - exponentBias + scale;

  // Normalize the significand and adjust the exponent if needed.
  if (productHi & implicitBit)
    productExponent++;
  else
    wideLeftShift(&productHi, &productLo, 1);

  // If we have overflowed the type, return +/- infinity.
  if (productExponent >= maxExponent)
    return fromRep(infRep | productSign);

  if (productExponent <= 0) {
    // The result is denormal before rounding.
    //
    // If the result is so small that it just underflows to zero, return
    // zero with the appropriate sign.  Mathematically, there is no need to
    // handle this case separately, but we make it a special case to
    // simplify the shift logic.
    const unsigned int shift = REP_C(1) - (unsigned int)productExponent;
    if (shift >= typeWidth)
      return fromRep(productSign);

    // Otherwise, shift the significand of the result so that the round
    // bit is the high bit of productLo.
    wideRightShiftWithSticky(&productHi, &productLo, shift);
  } else {
    // The result is normal before rounding.  Insert the exponent.
    productHi &= significandMask;
    productHi |= (rep_t)productExponent << significandBits;
  }

  // Insert the sign of the result.
  productHi |= productSign;

  // Perform the final rounding.  The final result may overflow to infinity,
  // or underflow to zero, but those are the correct results in those cases.
  // We use the default IEEE-754 round-to-nearest, ties-to-even rounding mode.
  if (productLo > signBit)
    productHi++;
  if (productLo == signBit)
    productHi += productHi & 1;
  return fromRep(productHi);
}
