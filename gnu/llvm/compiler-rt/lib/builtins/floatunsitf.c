//===-- lib/floatunsitf.c - uint -> quad-precision conversion -----*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements unsigned integer to quad-precision conversion for the
// compiler-rt library in the IEEE-754 default round-to-nearest, ties-to-even
// mode.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE)
COMPILER_RT_ABI fp_t __floatunsitf(su_int a) {

  const int aWidth = sizeof a * CHAR_BIT;

  // Handle zero as a special case to protect clz
  if (a == 0)
    return fromRep(0);

  // Exponent of (fp_t)a is the width of abs(a).
  const int exponent = (aWidth - 1) - clzsi(a);
  rep_t result;

  // Shift a into the significand field and clear the implicit bit.
  const int shift = significandBits - exponent;
  result = (rep_t)a << shift ^ implicitBit;

  // Insert the exponent
  result += (rep_t)(exponent + exponentBias) << significandBits;
  return fromRep(result);
}

#endif
