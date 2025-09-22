//===-- divsi3.c - Implement __divsi3 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divsi3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Returns: a / b

#define fixint_t si_int
#define fixuint_t su_int
// On CPUs without unsigned hardware division support,
//  this calls __udivsi3 (notice the cast to su_int).
// On CPUs with unsigned hardware division support,
//  this uses the unsigned division instruction.
#define COMPUTE_UDIV(a, b) ((su_int)(a) / (su_int)(b))
#include "int_div_impl.inc"

COMPILER_RT_ABI si_int __divsi3(si_int a, si_int b) { return __divXi3(a, b); }

#if defined(__ARM_EABI__)
COMPILER_RT_ALIAS(__divsi3, __aeabi_idiv)
#endif
