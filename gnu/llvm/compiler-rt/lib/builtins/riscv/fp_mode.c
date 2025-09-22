//=== lib/builtins/riscv/fp_mode.c - Floaing-point mode utilities -*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "../fp_mode.h"

#define RISCV_TONEAREST  0x0
#define RISCV_TOWARDZERO 0x1
#define RISCV_DOWNWARD   0x2
#define RISCV_UPWARD     0x3

#define RISCV_INEXACT    0x1

CRT_FE_ROUND_MODE __fe_getround(void) {
#if defined(__riscv_f) || defined(__riscv_zfinx)
  int frm;
  __asm__ __volatile__("frrm %0" : "=r" (frm));
  switch (frm) {
    case RISCV_TOWARDZERO:
      return CRT_FE_TOWARDZERO;
    case RISCV_DOWNWARD:
      return CRT_FE_DOWNWARD;
    case RISCV_UPWARD:
      return CRT_FE_UPWARD;
    case RISCV_TONEAREST:
    default:
      return CRT_FE_TONEAREST;
  }
#else
  return CRT_FE_TONEAREST;
#endif
}

int __fe_raise_inexact(void) {
#if defined(__riscv_f) || defined(__riscv_zfinx)
  __asm__ __volatile__("csrsi fflags, %0" :: "i" (RISCV_INEXACT));
#endif
  return 0;
}
