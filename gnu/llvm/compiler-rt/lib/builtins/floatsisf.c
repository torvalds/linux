//===-- lib/floatsisf.c - integer -> single-precision conversion --*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements integer to single-precision conversion for the
// compiler-rt library in the IEEE-754 default round-to-nearest, ties-to-even
// mode.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

#include "int_lib.h"

COMPILER_RT_ABI fp_t __floatsisf(si_int a) {

  const int aWidth = sizeof a * CHAR_BIT;

  // Handle zero as a special case to protect clz
  if (a == 0)
    return fromRep(0);

  // All other cases begin by extracting the sign and absolute value of a
  rep_t sign = 0;
  su_int aAbs = (su_int)a;
  if (a < 0) {
    sign = signBit;
    aAbs = -aAbs;
  }

  // Exponent of (fp_t)a is the width of abs(a).
  const int exponent = (aWidth - 1) - clzsi(aAbs);
  rep_t result;

  // Shift a into the significand field, rounding if it is a right-shift
  if (exponent <= significandBits) {
    const int shift = significandBits - exponent;
    result = (rep_t)aAbs << shift ^ implicitBit;
  } else {
    const int shift = exponent - significandBits;
    result = (rep_t)aAbs >> shift ^ implicitBit;
    rep_t round = (rep_t)aAbs << (typeWidth - shift);
    if (round > signBit)
      result++;
    if (round == signBit)
      result += result & 1;
  }

  // Insert the exponent
  result += (rep_t)(exponent + exponentBias) << significandBits;
  // Insert the sign bit and return
  return fromRep(result | sign);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_i2f(int a) { return __floatsisf(a); }
#else
COMPILER_RT_ALIAS(__floatsisf, __aeabi_i2f)
#endif
#endif
