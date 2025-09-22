//=== lib/builtins/loongarch/fp_mode.c - Floaing-point mode utilities -*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "../fp_mode.h"

#define LOONGARCH_TONEAREST  0x0000
#define LOONGARCH_TOWARDZERO 0x0100
#define LOONGARCH_UPWARD     0x0200
#define LOONGARCH_DOWNWARD   0x0300

#define LOONGARCH_RMODE_MASK (LOONGARCH_TONEAREST | LOONGARCH_TOWARDZERO | \
                              LOONGARCH_UPWARD | LOONGARCH_DOWNWARD)

#define LOONGARCH_INEXACT    0x10000

CRT_FE_ROUND_MODE __fe_getround(void) {
#if __loongarch_frlen != 0
  int fcsr;
#  ifdef __clang__
  __asm__ __volatile__("movfcsr2gr %0, $fcsr0" : "=r" (fcsr));
#  else
  __asm__ __volatile__("movfcsr2gr %0, $r0" : "=r" (fcsr));
#  endif
  fcsr &= LOONGARCH_RMODE_MASK;
  switch (fcsr) {
  case LOONGARCH_TOWARDZERO:
    return CRT_FE_TOWARDZERO;
  case LOONGARCH_DOWNWARD:
    return CRT_FE_DOWNWARD;
  case LOONGARCH_UPWARD:
    return CRT_FE_UPWARD;
  case LOONGARCH_TONEAREST:
  default:
    return CRT_FE_TONEAREST;
  }
#else
  return CRT_FE_TONEAREST;
#endif
}

int __fe_raise_inexact(void) {
#if __loongarch_frlen != 0
  int fcsr;
#  ifdef __clang__
  __asm__ __volatile__("movfcsr2gr %0, $fcsr0" : "=r" (fcsr));
  __asm__ __volatile__(
      "movgr2fcsr $fcsr0, %0" :: "r" (fcsr | LOONGARCH_INEXACT));
#  else
  __asm__ __volatile__("movfcsr2gr %0, $r0" : "=r" (fcsr));
  __asm__ __volatile__(
      "movgr2fcsr $r0, %0" :: "r" (fcsr | LOONGARCH_INEXACT));
#  endif
#endif
  return 0;
}
