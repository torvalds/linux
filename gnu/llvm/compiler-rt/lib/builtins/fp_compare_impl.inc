//===-- lib/fp_compare_impl.inc - Floating-point comparison -------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "fp_lib.h"

// GCC uses long (at least for x86_64) as the return type of the comparison
// functions. We need to ensure that the return value is sign-extended in the
// same way as GCC expects (since otherwise GCC-generated __builtin_isinf
// returns true for finite 128-bit floating-point numbers).
#ifdef __aarch64__
// AArch64 GCC overrides libgcc_cmp_return to use int instead of long.
typedef int CMP_RESULT;
#elif __SIZEOF_POINTER__ == 8 && __SIZEOF_LONG__ == 4
// LLP64 ABIs use long long instead of long.
typedef long long CMP_RESULT;
#elif __AVR__
// AVR uses a single byte for the return value.
typedef char CMP_RESULT;
#else
// Otherwise the comparison functions return long.
typedef long CMP_RESULT;
#endif

#if !defined(__clang__) && defined(__GNUC__)
// GCC uses a special __libgcc_cmp_return__ mode to define the return type, so
// check that we are ABI-compatible when compiling the builtins with GCC.
typedef int GCC_CMP_RESULT __attribute__((__mode__(__libgcc_cmp_return__)));
_Static_assert(sizeof(GCC_CMP_RESULT) == sizeof(CMP_RESULT),
               "SOFTFP ABI not compatible with GCC");
#endif

enum {
  LE_LESS = -1,
  LE_EQUAL = 0,
  LE_GREATER = 1,
  LE_UNORDERED = 1,
};

static inline CMP_RESULT __leXf2__(fp_t a, fp_t b) {
  const srep_t aInt = toRep(a);
  const srep_t bInt = toRep(b);
  const rep_t aAbs = aInt & absMask;
  const rep_t bAbs = bInt & absMask;

  // If either a or b is NaN, they are unordered.
  if (aAbs > infRep || bAbs > infRep)
    return LE_UNORDERED;

  // If a and b are both zeros, they are equal.
  if ((aAbs | bAbs) == 0)
    return LE_EQUAL;

  // If at least one of a and b is positive, we get the same result comparing
  // a and b as signed integers as we would with a floating-point compare.
  if ((aInt & bInt) >= 0) {
    if (aInt < bInt)
      return LE_LESS;
    else if (aInt == bInt)
      return LE_EQUAL;
    else
      return LE_GREATER;
  } else {
    // Otherwise, both are negative, so we need to flip the sense of the
    // comparison to get the correct result.  (This assumes a twos- or ones-
    // complement integer representation; if integers are represented in a
    // sign-magnitude representation, then this flip is incorrect).
    if (aInt > bInt)
      return LE_LESS;
    else if (aInt == bInt)
      return LE_EQUAL;
    else
      return LE_GREATER;
  }
}

enum {
  GE_LESS = -1,
  GE_EQUAL = 0,
  GE_GREATER = 1,
  GE_UNORDERED = -1 // Note: different from LE_UNORDERED
};

static inline CMP_RESULT __geXf2__(fp_t a, fp_t b) {
  const srep_t aInt = toRep(a);
  const srep_t bInt = toRep(b);
  const rep_t aAbs = aInt & absMask;
  const rep_t bAbs = bInt & absMask;

  if (aAbs > infRep || bAbs > infRep)
    return GE_UNORDERED;
  if ((aAbs | bAbs) == 0)
    return GE_EQUAL;
  if ((aInt & bInt) >= 0) {
    if (aInt < bInt)
      return GE_LESS;
    else if (aInt == bInt)
      return GE_EQUAL;
    else
      return GE_GREATER;
  } else {
    if (aInt > bInt)
      return GE_LESS;
    else if (aInt == bInt)
      return GE_EQUAL;
    else
      return GE_GREATER;
  }
}

static inline CMP_RESULT __unordXf2__(fp_t a, fp_t b) {
  const rep_t aAbs = toRep(a) & absMask;
  const rep_t bAbs = toRep(b) & absMask;
  return aAbs > infRep || bAbs > infRep;
}
