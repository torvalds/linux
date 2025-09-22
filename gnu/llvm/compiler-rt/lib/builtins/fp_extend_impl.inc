//=-lib/fp_extend_impl.inc - low precision -> high precision conversion -*-- -//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a fairly generic conversion from a narrower to a wider
// IEEE-754 floating-point type.  The constants and types defined following the
// includes below parameterize the conversion.
//
// It does not support types that don't use the usual IEEE-754 interchange
// formats; specifically, some work would be needed to adapt it to
// (for example) the Intel 80-bit format or PowerPC double-double format.
//
// Note please, however, that this implementation is only intended to support
// *widening* operations; if you need to convert to a *narrower* floating-point
// type (e.g. double -> float), then this routine will not do what you want it
// to.
//
// It also requires that integer types at least as large as both formats
// are available on the target platform; this may pose a problem when trying
// to add support for quad on some 32-bit systems, for example.  You also may
// run into trouble finding an appropriate CLZ function for wide source types;
// you will likely need to roll your own on some platforms.
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

#include "fp_extend.h"

// The source type may use a usual IEEE-754 interchange format or Intel 80-bit
// format. In particular, for the source type srcSigFracBits may be not equal to
// srcSigBits. The destination type is assumed to be one of IEEE-754 standard
// types.
static __inline dst_t __extendXfYf2__(src_t a) {
  // Various constants whose values follow from the type parameters.
  // Any reasonable optimizer will fold and propagate all of these.
  const int srcInfExp = (1 << srcExpBits) - 1;
  const int srcExpBias = srcInfExp >> 1;

  const int dstInfExp = (1 << dstExpBits) - 1;
  const int dstExpBias = dstInfExp >> 1;

  // Break a into a sign and representation of the absolute value.
  const src_rep_t aRep = srcToRep(a);
  const src_rep_t srcSign = extract_sign_from_src(aRep);
  const src_rep_t srcExp = extract_exp_from_src(aRep);
  const src_rep_t srcSigFrac = extract_sig_frac_from_src(aRep);

  dst_rep_t dstSign = srcSign;
  dst_rep_t dstExp;
  dst_rep_t dstSigFrac;

  if (srcExp >= 1 && srcExp < (src_rep_t)srcInfExp) {
    // a is a normal number.
    dstExp = (dst_rep_t)srcExp + (dst_rep_t)(dstExpBias - srcExpBias);
    dstSigFrac = (dst_rep_t)srcSigFrac << (dstSigFracBits - srcSigFracBits);
  }

  else if (srcExp == srcInfExp) {
    // a is NaN or infinity.
    dstExp = dstInfExp;
    dstSigFrac = (dst_rep_t)srcSigFrac << (dstSigFracBits - srcSigFracBits);
  }

  else if (srcSigFrac) {
    // a is denormal.
    if (srcExpBits == dstExpBits) {
      // The exponent fields are identical and this is a denormal number, so all
      // the non-significand bits are zero. In particular, this branch is always
      // taken when we extend a denormal F80 to F128.
      dstExp = 0;
      dstSigFrac = ((dst_rep_t)srcSigFrac) << (dstSigFracBits - srcSigFracBits);
    } else {
#ifndef src_rep_t_clz
      // If src_rep_t_clz is not defined this branch must be unreachable.
      __builtin_unreachable();
#else
      // Renormalize the significand and clear the leading bit.
      // For F80 -> F128 this codepath is unused.
      const int scale = clz_in_sig_frac(srcSigFrac) + 1;
      dstExp = dstExpBias - srcExpBias - scale + 1;
      dstSigFrac = (dst_rep_t)srcSigFrac
                   << (dstSigFracBits - srcSigFracBits + scale);
      const dst_rep_t dstMinNormal = DST_REP_C(1) << (dstBits - 1 - dstExpBits);
      dstSigFrac ^= dstMinNormal;
#endif
    }
  }

  else {
    // a is zero.
    dstExp = 0;
    dstSigFrac = 0;
  }

  const dst_rep_t result = construct_dst_rep(dstSign, dstExp, dstSigFrac);
  return dstFromRep(result);
}
