//===-- ctzdi2.c - Implement __ctzdi2 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __ctzdi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: the number of trailing 0-bits

#if !defined(__clang__) &&                                                     \
    ((defined(__sparc__) && defined(__arch64__)) || defined(__mips64) ||       \
     (defined(__riscv) && __SIZEOF_POINTER__ >= 8))
// On 64-bit architectures with neither a native clz instruction nor a native
// ctz instruction, gcc resolves __builtin_ctz to __ctzdi2 rather than
// __ctzsi2, leading to infinite recursion.
#define __builtin_ctz(a) __ctzsi2(a)
extern int __ctzsi2(si_int);
#endif

// Precondition: a != 0

COMPILER_RT_ABI int __ctzdi2(di_int a) {
  dwords x;
  x.all = a;
  const si_int f = -(x.s.low == 0);
  return ctzsi((x.s.high & f) | (x.s.low & ~f)) +
         (f & ((si_int)(sizeof(si_int) * CHAR_BIT)));
}
