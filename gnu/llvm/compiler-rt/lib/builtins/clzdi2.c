//===-- clzdi2.c - Implement __clzdi2 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __clzdi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: the number of leading 0-bits

#if !defined(__clang__) &&                                                     \
    ((defined(__sparc__) && defined(__arch64__)) || defined(__mips64) ||       \
     (defined(__riscv) && __SIZEOF_POINTER__ >= 8))
// On 64-bit architectures with neither a native clz instruction nor a native
// ctz instruction, gcc resolves __builtin_clz to __clzdi2 rather than
// __clzsi2, leading to infinite recursion.
#define __builtin_clz(a) __clzsi2(a)
extern int __clzsi2(si_int);
#endif

// Precondition: a != 0

COMPILER_RT_ABI int __clzdi2(di_int a) {
  dwords x;
  x.all = a;
  const si_int f = -(x.s.high == 0);
  return clzsi((x.s.high & ~f) | (x.s.low & f)) +
         (f & ((si_int)(sizeof(si_int) * CHAR_BIT)));
}
