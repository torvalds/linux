//===-- floattidf.c - Implement __floattidf -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __floattidf for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

#define SRC_I128
#define DST_DOUBLE
#include "int_to_fp_impl.inc"

// Returns: convert a to a double, rounding toward even.

// Assumption: double is a IEEE 64 bit floating point type
//            ti_int is a 128 bit integral type

// seee eeee eeee mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm mmmm mmmm
// mmmm

COMPILER_RT_ABI double __floattidf(ti_int a) { return __floatXiYf__(a); }

#endif // CRT_HAS_128BIT
