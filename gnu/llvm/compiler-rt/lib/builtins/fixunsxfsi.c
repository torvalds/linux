//===-- fixunsxfsi.c - Implement __fixunsxfsi -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __fixunsxfsi for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#if !_ARCH_PPC

#include "int_lib.h"

// Returns: convert a to a unsigned int, rounding toward zero.
//          Negative values all become zero.

// Assumption: long double is an intel 80 bit floating point type padded with 6
// bytes su_int is a 32 bit integral type value in long double is representable
// in su_int or is negative

// gggg gggg gggg gggg gggg gggg gggg gggg | gggg gggg gggg gggg seee eeee eeee
// eeee | 1mmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm
// mmmm mmmm mmmm

#if defined(_MSC_VER) && !defined(__clang__)
// MSVC throws a warning about 'uninitialized variable use' here,
// disable it for builds that warn-as-error
#pragma warning(push)
#pragma warning(disable : 4700)
#endif

COMPILER_RT_ABI su_int __fixunsxfsi(xf_float a) {
  xf_bits fb;
  fb.f = a;
  int e = (fb.u.high.s.low & 0x00007FFF) - 16383;
  if (e < 0 || (fb.u.high.s.low & 0x00008000))
    return 0;
  if ((unsigned)e > sizeof(su_int) * CHAR_BIT)
    return ~(su_int)0;
  return fb.u.low.s.high >> (31 - e);
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

#endif // !_ARCH_PPC
