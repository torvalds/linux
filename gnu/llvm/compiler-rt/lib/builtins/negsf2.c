//===-- lib/negsf2.c - single-precision negation ------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements single-precision soft-float negation.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI fp_t __negsf2(fp_t a) { return fromRep(toRep(a) ^ signBit); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_fneg(fp_t a) { return __negsf2(a); }
#else
COMPILER_RT_ALIAS(__negsf2, __aeabi_fneg)
#endif
#endif
