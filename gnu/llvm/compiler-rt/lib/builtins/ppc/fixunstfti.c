//===-- lib/builtins/ppc/fixunstfti.c - Convert long double->int128 *-C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements converting the 128bit IBM/PowerPC long double (double-
// double) data type to an unsigned 128 bit integer.
//
//===----------------------------------------------------------------------===//

#include "../int_math.h"
#define BIAS 1023

// Convert long double into an unsigned 128-bit integer.
__uint128_t __fixunstfti(long double input) {

  // If we are trying to convert a NaN, return the NaN bit pattern.
  if (crt_isnan(input)) {
    return ((__uint128_t)0x7FF8000000000000ll) << 64 |
           (__uint128_t)0x0000000000000000ll;
  }

  __uint128_t result, hiResult, loResult;
  int hiExponent, loExponent, shift;
  // The long double representation, with the high and low portions of
  // the long double, and the corresponding bit patterns of each double.
  union {
    long double ld;
    double d[2];               // [0] is the high double, [1] is the low double.
    unsigned long long ull[2]; // High and low doubles as 64-bit integers.
  } ldUnion;

  // If the long double is less than 1.0 or negative,
  // return 0.
  if (input < 1.0)
    return 0;

  // Retrieve the 64-bit patterns of high and low doubles.
  // Compute the unbiased exponent of both high and low doubles by
  // removing the signs, isolating the exponent, and subtracting
  // the bias from it.
  ldUnion.ld = input;
  hiExponent = ((ldUnion.ull[0] & 0x7FFFFFFFFFFFFFFFll) >> 52) - BIAS;
  loExponent = ((ldUnion.ull[1] & 0x7FFFFFFFFFFFFFFFll) >> 52) - BIAS;

  // Convert each double into int64; they will be added to the int128 result.
  // CASE 1: High or low double fits in int64
  //      - Convert the each double normally into int64.
  //
  // CASE 2: High or low double does not fit in int64
  //      - Scale the double to fit within a 64-bit integer
  //      - Calculate the shift (amount to scale the double by in the int128)
  //      - Clear all the bits of the exponent (with 0x800FFFFFFFFFFFFF)
  //      - Add BIAS+53 (0x4350000000000000) to exponent to correct the value
  //      - Scale (move) the double to the correct place in the int128
  //        (Move it by 2^53 places)
  //
  // Note: If the high double is assumed to be positive, an unsigned conversion
  // from long double to 64-bit integer is needed. The low double can be either
  // positive or negative, so a signed conversion is needed to retain the result
  // of the low double and to ensure it does not simply get converted to 0.

  // CASE 1 - High double fits in int64.
  if (hiExponent < 63) {
    hiResult = (unsigned long long)ldUnion.d[0];
  } else if (hiExponent < 128) {
    // CASE 2 - High double does not fit in int64, scale and convert it.
    shift = hiExponent - 54;
    ldUnion.ull[0] &= 0x800FFFFFFFFFFFFFll;
    ldUnion.ull[0] |= 0x4350000000000000ll;
    hiResult = (unsigned long long)ldUnion.d[0];
    hiResult <<= shift;
  } else {
    // Detect cases for overflow. When the exponent of the high
    // double is greater than 128 bits and when the long double
    // input is positive, return the max 128-bit integer.
    // For negative inputs with exponents > 128, return 1, like gcc.
    if (ldUnion.d[0] > 0) {
      return ((__uint128_t)0xFFFFFFFFFFFFFFFFll) << 64 |
             (__uint128_t)0xFFFFFFFFFFFFFFFFll;
    } else {
      return ((__uint128_t)0x0000000000000000ll) << 64 |
             (__uint128_t)0x0000000000000001ll;
    }
  }

  // CASE 1 - Low double fits in int64.
  if (loExponent < 63) {
    loResult = (long long)ldUnion.d[1];
  } else {
    // CASE 2 - Low double does not fit in int64, scale and convert it.
    shift = loExponent - 54;
    ldUnion.ull[1] &= 0x800FFFFFFFFFFFFFll;
    ldUnion.ull[1] |= 0x4350000000000000ll;
    loResult = (long long)ldUnion.d[1];
    loResult <<= shift;
  }

  // If the low double is negative, it may change the integer value of the
  // whole number if the absolute value of its fractional part is bigger than
  // the fractional part of the high double. Because both doubles cannot
  // overlap, this situation only occurs when the high double has no
  // fractional part.
  ldUnion.ld = input;
  if ((ldUnion.d[0] == (double)hiResult) &&
      (ldUnion.d[1] < (double)((__int128_t)loResult)))
    loResult--;

  // Add the high and low doublewords together to form a 128 bit integer.
  result = loResult + hiResult;
  return result;
}
