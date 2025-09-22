//===-- lib/adddf3.c - Double-precision subtraction ---------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements double-precision soft-float subtraction.
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

// Subtraction; flip the sign bit of b and add.
COMPILER_RT_ABI fp_t __subdf3(fp_t a, fp_t b) {
  return __adddf3(a, fromRep(toRep(b) ^ signBit));
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_dsub(fp_t a, fp_t b) { return __subdf3(a, b); }
#else
COMPILER_RT_ALIAS(__subdf3, __aeabi_dsub)
#endif
#endif
