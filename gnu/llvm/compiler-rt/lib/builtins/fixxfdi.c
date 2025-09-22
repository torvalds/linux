//===-- fixxfdi.c - Implement __fixxfdi -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __fixxfdi for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#if !_ARCH_PPC

#include "int_lib.h"

// Returns: convert a to a signed long long, rounding toward zero.

// Assumption: long double is an intel 80 bit floating point type padded with 6
// bytes di_int is a 64 bit integral type value in long double is representable
// in di_int (no range checking performed)

// gggg gggg gggg gggg gggg gggg gggg gggg | gggg gggg gggg gggg seee eeee eeee
// eeee | 1mmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm
// mmmm mmmm mmmm

#if defined(_MSC_VER) && !defined(__clang__)
// MSVC throws a warning about 'uninitialized variable use' here,
// disable it for builds that warn-as-error
#pragma warning(push)
#pragma warning(disable : 4700)
#endif

COMPILER_RT_ABI di_int __fixxfdi(xf_float a) {
  const di_int di_max = (di_int)((~(du_int)0) / 2);
  const di_int di_min = -di_max - 1;
  xf_bits fb;
  fb.f = a;
  int e = (fb.u.high.s.low & 0x00007FFF) - 16383;
  if (e < 0)
    return 0;
  if ((unsigned)e >= sizeof(di_int) * CHAR_BIT)
    return a > 0 ? di_max : di_min;
  di_int s = -(si_int)((fb.u.high.s.low & 0x00008000) >> 15);
  di_int r = fb.u.low.all;
  r = (du_int)r >> (63 - e);
  return (r ^ s) - s;
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

#endif // !_ARCH_PPC
