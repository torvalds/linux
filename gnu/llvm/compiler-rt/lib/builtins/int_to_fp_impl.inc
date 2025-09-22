//===-- int_to_fp_impl.inc - integer to floating point conversion ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Thsi file implements a generic conversion from an integer type to an
// IEEE-754 floating point type, allowing a common implementation to be hsared
// without copy and paste.
//
//===----------------------------------------------------------------------===//

#include "int_to_fp.h"

static __inline dst_t __floatXiYf__(src_t a) {
  if (a == 0)
    return 0.0;

  enum {
    dstMantDig = dstSigBits + 1,
    srcBits = sizeof(src_t) * CHAR_BIT,
    srcIsSigned = ((src_t)-1) < 0,
  };

  const src_t s = srcIsSigned ? a >> (srcBits - 1) : 0;

  a = (usrc_t)(a ^ s) - s;
  int sd = srcBits - clzSrcT(a);         // number of significant digits
  int e = sd - 1;                        // exponent
  if (sd > dstMantDig) {
    //  start:  0000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQxxxxxxxxxxxxxxxxxx
    //  finish: 000000000000000000000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQR
    //                                                12345678901234567890123456
    //  1 = msb 1 bit
    //  P = bit dstMantDig-1 bits to the right of 1
    //  Q = bit dstMantDig bits to the right of 1
    //  R = "or" of all bits to the right of Q
    if (sd == dstMantDig + 1) {
      a <<= 1;
    } else if (sd == dstMantDig + 2) {
      // Do nothing.
    } else {
      a = ((usrc_t)a >> (sd - (dstMantDig + 2))) |
          ((a & ((usrc_t)(-1) >> ((srcBits + dstMantDig + 2) - sd))) != 0);
    }
    // finish:
    a |= (a & 4) != 0; // Or P into R
    ++a;               // round - this step may add a significant bit
    a >>= 2;           // dump Q and R
    // a is now rounded to dstMantDig or dstMantDig+1 bits
    if (a & ((usrc_t)1 << dstMantDig)) {
      a >>= 1;
      ++e;
    }
    // a is now rounded to dstMantDig bits
  } else {
    a <<= (dstMantDig - sd);
    // a is now rounded to dstMantDig bits
  }
  const int dstBits = sizeof(dst_t) * CHAR_BIT;
  const dst_rep_t dstSignMask = DST_REP_C(1) << (dstBits - 1);
  const int dstExpBits = dstBits - dstSigBits - 1;
  const int dstExpBias = (1 << (dstExpBits - 1)) - 1;
  const dst_rep_t dstSignificandMask = (DST_REP_C(1) << dstSigBits) - 1;
  // Combine sign, exponent, and mantissa.
  const dst_rep_t result = ((dst_rep_t)s & dstSignMask) |
                           ((dst_rep_t)(e + dstExpBias) << dstSigBits) |
                           ((dst_rep_t)(a) & dstSignificandMask);
  return dstFromRep(result);
}
