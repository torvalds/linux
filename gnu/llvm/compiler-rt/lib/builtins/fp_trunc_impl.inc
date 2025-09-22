//= lib/fp_trunc_impl.inc - high precision -> low precision conversion *-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a fairly generic conversion from a wider to a narrower
// IEEE-754 floating-point type in the default (round to nearest, ties to even)
// rounding mode.  The constants and types defined following the includes below
// parameterize the conversion.
//
// This routine can be trivially adapted to support conversions to
// half-precision or from quad-precision. It does not support types that don't
// use the usual IEEE-754 interchange formats; specifically, some work would be
// needed to adapt it to (for example) the Intel 80-bit format or PowerPC
// double-double format.
//
// Note please, however, that this implementation is only intended to support
// *narrowing* operations; if you need to convert to a *wider* floating-point
// type (e.g. float -> double), then this routine will not do what you want it
// to.
//
// It also requires that integer types at least as large as both formats
// are available on the target platform; this may pose a problem when trying
// to add support for quad on some 32-bit systems, for example.
//
// Finally, the following assumptions are made:
//
// 1. Floating-point types and integer types have the same endianness on the
//    target platform.
//
// 2. Quiet NaNs, if supported, are indicated by the leading bit of the
//    significand field being set.
//
//===----------------------------------------------------------------------===//

#include "fp_trunc.h"

// The destination type may use a usual IEEE-754 interchange format or Intel
// 80-bit format. In particular, for the destination type dstSigFracBits may be
// not equal to dstSigBits. The source type is assumed to be one of IEEE-754
// standard types.
static __inline dst_t __truncXfYf2__(src_t a) {
  // Various constants whose values follow from the type parameters.
  // Any reasonable optimizer will fold and propagate all of these.
  const int srcInfExp = (1 << srcExpBits) - 1;
  const int srcExpBias = srcInfExp >> 1;

  const src_rep_t srcMinNormal = SRC_REP_C(1) << srcSigFracBits;
  const src_rep_t roundMask =
      (SRC_REP_C(1) << (srcSigFracBits - dstSigFracBits)) - 1;
  const src_rep_t halfway = SRC_REP_C(1)
                            << (srcSigFracBits - dstSigFracBits - 1);
  const src_rep_t srcQNaN = SRC_REP_C(1) << (srcSigFracBits - 1);
  const src_rep_t srcNaNCode = srcQNaN - 1;

  const int dstInfExp = (1 << dstExpBits) - 1;
  const int dstExpBias = dstInfExp >> 1;
  const int overflowExponent = srcExpBias + dstInfExp - dstExpBias;

  const dst_rep_t dstQNaN = DST_REP_C(1) << (dstSigFracBits - 1);
  const dst_rep_t dstNaNCode = dstQNaN - 1;

  const src_rep_t aRep = srcToRep(a);
  const src_rep_t srcSign = extract_sign_from_src(aRep);
  const src_rep_t srcExp = extract_exp_from_src(aRep);
  const src_rep_t srcSigFrac = extract_sig_frac_from_src(aRep);

  dst_rep_t dstSign = srcSign;
  dst_rep_t dstExp;
  dst_rep_t dstSigFrac;

  // Same size exponents and a's significand tail is 0.
  // The significand can be truncated and the exponent can be copied over.
  const int sigFracTailBits = srcSigFracBits - dstSigFracBits;
  if (srcExpBits == dstExpBits &&
      ((aRep >> sigFracTailBits) << sigFracTailBits) == aRep) {
    dstExp = srcExp;
    dstSigFrac = (dst_rep_t)(srcSigFrac >> sigFracTailBits);
    return dstFromRep(construct_dst_rep(dstSign, dstExp, dstSigFrac));
  }

  const int dstExpCandidate = ((int)srcExp - srcExpBias) + dstExpBias;
  if (dstExpCandidate >= 1 && dstExpCandidate < dstInfExp) {
    // The exponent of a is within the range of normal numbers in the
    // destination format. We can convert by simply right-shifting with
    // rounding and adjusting the exponent.
    dstExp = dstExpCandidate;
    dstSigFrac = (dst_rep_t)(srcSigFrac >> sigFracTailBits);

    const src_rep_t roundBits = srcSigFrac & roundMask;
    // Round to nearest.
    if (roundBits > halfway)
      dstSigFrac++;
    // Tie to even.
    else if (roundBits == halfway)
      dstSigFrac += dstSigFrac & 1;

    // Rounding has changed the exponent.
    if (dstSigFrac >= (DST_REP_C(1) << dstSigFracBits)) {
      dstExp += 1;
      dstSigFrac ^= (DST_REP_C(1) << dstSigFracBits);
    }
  } else if (srcExp == srcInfExp && srcSigFrac) {
    // a is NaN.
    // Conjure the result by beginning with infinity, setting the qNaN
    // bit and inserting the (truncated) trailing NaN field.
    dstExp = dstInfExp;
    dstSigFrac = dstQNaN;
    dstSigFrac |= ((srcSigFrac & srcNaNCode) >> sigFracTailBits) & dstNaNCode;
  } else if ((int)srcExp >= overflowExponent) {
    dstExp = dstInfExp;
    dstSigFrac = 0;
  } else {
    // a underflows on conversion to the destination type or is an exact
    // zero.  The result may be a denormal or zero.  Extract the exponent
    // to get the shift amount for the denormalization.
    src_rep_t significand = srcSigFrac;
    int shift = srcExpBias - dstExpBias - srcExp;

    if (srcExp) {
      // Set the implicit integer bit if the source is a normal number.
      significand |= srcMinNormal;
      shift += 1;
    }

    // Right shift by the denormalization amount with sticky.
    if (shift > srcSigFracBits) {
      dstExp = 0;
      dstSigFrac = 0;
    } else {
      dstExp = 0;
      const bool sticky = shift && ((significand << (srcBits - shift)) != 0);
      src_rep_t denormalizedSignificand = significand >> shift | sticky;
      dstSigFrac = denormalizedSignificand >> sigFracTailBits;
      const src_rep_t roundBits = denormalizedSignificand & roundMask;
      // Round to nearest
      if (roundBits > halfway)
        dstSigFrac++;
      // Ties to even
      else if (roundBits == halfway)
        dstSigFrac += dstSigFrac & 1;

      // Rounding has changed the exponent.
      if (dstSigFrac >= (DST_REP_C(1) << dstSigFracBits)) {
        dstExp += 1;
        dstSigFrac ^= (DST_REP_C(1) << dstSigFracBits);
      }
    }
  }

  return dstFromRep(construct_dst_rep(dstSign, dstExp, dstSigFrac));
}
