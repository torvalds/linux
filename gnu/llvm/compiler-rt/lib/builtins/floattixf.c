//===-- floattixf.c - Implement __floattixf -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __floattixf for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

// Returns: convert a to a long double, rounding toward even.

// Assumption: long double is a IEEE 80 bit floating point type padded to 128
// bits ti_int is a 128 bit integral type

// gggg gggg gggg gggg gggg gggg gggg gggg | gggg gggg gggg gggg seee eeee eeee
// eeee | 1mmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm
// mmmm mmmm mmmm

COMPILER_RT_ABI xf_float __floattixf(ti_int a) {
  if (a == 0)
    return 0.0;
  const unsigned N = sizeof(ti_int) * CHAR_BIT;
  const ti_int s = a >> (N - 1);
  a = (a ^ s) - s;
  int sd = N - __clzti2(a); // number of significant digits
  int e = sd - 1;           // exponent
  if (sd > LDBL_MANT_DIG) {
    //  start:  0000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQxxxxxxxxxxxxxxxxxx
    //  finish: 000000000000000000000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQR
    //                                                12345678901234567890123456
    //  1 = msb 1 bit
    //  P = bit LDBL_MANT_DIG-1 bits to the right of 1
    //  Q = bit LDBL_MANT_DIG bits to the right of 1
    //  R = "or" of all bits to the right of Q
    switch (sd) {
    case LDBL_MANT_DIG + 1:
      a <<= 1;
      break;
    case LDBL_MANT_DIG + 2:
      break;
    default:
      a = ((tu_int)a >> (sd - (LDBL_MANT_DIG + 2))) |
          ((a & ((tu_int)(-1) >> ((N + LDBL_MANT_DIG + 2) - sd))) != 0);
    };
    // finish:
    a |= (a & 4) != 0; // Or P into R
    ++a;               // round - this step may add a significant bit
    a >>= 2;           // dump Q and R
    // a is now rounded to LDBL_MANT_DIG or LDBL_MANT_DIG+1 bits
    if (a & ((tu_int)1 << LDBL_MANT_DIG)) {
      a >>= 1;
      ++e;
    }
    // a is now rounded to LDBL_MANT_DIG bits
  } else {
    a <<= (LDBL_MANT_DIG - sd);
    // a is now rounded to LDBL_MANT_DIG bits
  }
  xf_bits fb;
  fb.u.high.s.low = ((su_int)s & 0x8000) | // sign
                    (e + 16383);           // exponent
  fb.u.low.all = (du_int)a;                // mantissa
  return fb.f;
}

#endif // CRT_HAS_128BIT
