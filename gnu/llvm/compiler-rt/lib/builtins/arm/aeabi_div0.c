//===-- aeabi_div0.c - ARM Runtime ABI support routines for compiler-rt ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the division by zero helper routines as specified by the
// Run-time ABI for the ARM Architecture.
//
//===----------------------------------------------------------------------===//

// RTABI 4.3.2 - Division by zero
//
// The *div0 functions:
// - Return the value passed to them as a parameter
// - Or, return a fixed value defined by the execution environment (such as 0)
// - Or, raise a signal (often SIGFPE) or throw an exception, and do not return
//
// An application may provide its own implementations of the *div0 functions to
// for a particular behaviour from the *div and *divmod functions called out of
// line.

#include "../int_lib.h"

// provide an unused declaration to pacify pendantic compilation
extern unsigned char declaration;

#if defined(__ARM_EABI__)
AEABI_RTABI int __attribute__((weak)) __attribute__((visibility("hidden")))
__aeabi_idiv0(int return_value) {
  return return_value;
}

AEABI_RTABI long long __attribute__((weak))
__attribute__((visibility("hidden"))) __aeabi_ldiv0(long long return_value) {
  return return_value;
}
#endif
