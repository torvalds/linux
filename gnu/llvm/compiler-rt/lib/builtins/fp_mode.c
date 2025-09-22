//===----- lib/fp_mode.c - Floaing-point environment mode utilities --C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a default implementation of fp_mode.h for architectures
// that does not support or does not have an implementation of floating point
// environment mode.
//
//===----------------------------------------------------------------------===//

#include "fp_mode.h"

// IEEE-754 default rounding (to nearest, ties to even).
CRT_FE_ROUND_MODE __fe_getround(void) { return CRT_FE_TONEAREST; }

int __fe_raise_inexact(void) {
  return 0;
}
