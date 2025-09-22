//===----- lib/arm/fp_mode.c - Floaing-point mode utilities -------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

#include "../fp_mode.h"

#define ARM_TONEAREST  0x0
#define ARM_UPWARD     0x1
#define ARM_DOWNWARD   0x2
#define ARM_TOWARDZERO 0x3
#define ARM_RMODE_MASK (ARM_TONEAREST | ARM_UPWARD | \
                        ARM_DOWNWARD | ARM_TOWARDZERO)
#define ARM_RMODE_SHIFT 22

#define ARM_INEXACT     0x10

#ifndef __ARM_FP
// For soft float targets, allow changing rounding mode by overriding the weak
// __arm_fe_default_rmode symbol.
CRT_FE_ROUND_MODE __attribute__((weak)) __arm_fe_default_rmode =
    CRT_FE_TONEAREST;
#endif

CRT_FE_ROUND_MODE __fe_getround(void) {
#ifdef __ARM_FP
  uint32_t fpscr;
  __asm__ __volatile__("vmrs  %0, fpscr" : "=r" (fpscr));
  fpscr = fpscr >> ARM_RMODE_SHIFT & ARM_RMODE_MASK;
  switch (fpscr) {
    case ARM_UPWARD:
      return CRT_FE_UPWARD;
    case ARM_DOWNWARD:
      return CRT_FE_DOWNWARD;
    case ARM_TOWARDZERO:
      return CRT_FE_TOWARDZERO;
    case ARM_TONEAREST:
    default:
      return CRT_FE_TONEAREST;
  }
#else
  return __arm_fe_default_rmode;
#endif
}

int __fe_raise_inexact(void) {
#ifdef __ARM_FP
  uint32_t fpscr;
  __asm__ __volatile__("vmrs  %0, fpscr" : "=r" (fpscr));
  __asm__ __volatile__("vmsr  fpscr, %0" : : "ri" (fpscr | ARM_INEXACT));
  return 0;
#else
  return 0;
#endif
}
