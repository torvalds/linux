//===-- lib/floatunsisf.c - uint -> single-precision conversion ---*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements unsigned integer to single-precision conversion for the
// compiler-rt library in the IEEE-754 default round-to-nearest, ties-to-even
// mode.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

#include "int_lib.h"

COMPILER_RT_ABI fp_t __floatunsisf(su_int a) {

  const int aWidth = sizeof a * CHAR_BIT;

  // Handle zero as a special case to protect clz
  if (a == 0)
    return fromRep(0);

  // Exponent of (fp_t)a is the width of abs(a).
  const int exponent = (aWidth - 1) - clzsi(a);
  rep_t result;

  // Shift a into the significand field, rounding if it is a right-shift
  if (exponent <= significandBits) {
    const int shift = significandBits - exponent;
    result = (rep_t)a << shift ^ implicitBit;
  } else {
    const int shift = exponent - significandBits;
    result = (rep_t)a >> shift ^ implicitBit;
    rep_t round = (rep_t)a << (typeWidth - shift);
    if (round > signBit)
      result++;
    if (round == signBit)
      result += result & 1;
  }

  // Insert the exponent
  result += (rep_t)(exponent + exponentBias) << significandBits;
  return fromRep(result);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_ui2f(unsigned int a) { return __floatunsisf(a); }
#else
COMPILER_RT_ALIAS(__floatunsisf, __aeabi_ui2f)
#endif
#endif
