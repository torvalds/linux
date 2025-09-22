//===--- lib/builtins/ppc/fixtfti.c - Convert long double->int128 *-C -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements converting the 128bit IBM/PowerPC long double (double-
// double) data type to a signed 128 bit integer.
//
//===----------------------------------------------------------------------===//

#include "../int_math.h"

// Convert long double into a signed 128-bit integer.
__int128_t __fixtfti(long double input) {

  // If we are trying to convert a NaN, return the NaN bit pattern.
  if (crt_isnan(input)) {
    return ((__uint128_t)0x7FF8000000000000ll) << 64 |
           (__uint128_t)0x0000000000000000ll;
  }

  // Note: overflow is an undefined behavior for this conversion.
  // For this reason, overflow is not checked here.

  // If the long double is negative, use unsigned conversion from its absolute
  // value.
  if (input < 0.0) {
    __uint128_t result = (__uint128_t)(-input);
    return -((__int128_t)result);
  }

  // Otherwise, use unsigned conversion from the input value.
  __uint128_t result = (__uint128_t)input;
  return result;
}
