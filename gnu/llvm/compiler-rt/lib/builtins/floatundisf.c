//===-- floatundisf.c - Implement __floatundisf ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __floatundisf for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

// Returns: convert a to a float, rounding toward even.

// Assumption: float is a IEEE 32 bit floating point type
//            du_int is a 64 bit integral type

// seee eeee emmm mmmm mmmm mmmm mmmm mmmm

#include "int_lib.h"

#define SRC_U64
#define DST_SINGLE
#include "int_to_fp_impl.inc"

COMPILER_RT_ABI float __floatundisf(du_int a) { return __floatXiYf__(a); }

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI float __aeabi_ul2f(du_int a) { return __floatundisf(a); }
#else
COMPILER_RT_ALIAS(__floatundisf, __aeabi_ul2f)
#endif
#endif

#if defined(__MINGW32__) && defined(__arm__)
COMPILER_RT_ALIAS(__floatundisf, __u64tos)
#endif
