//===-- udivsi3.c - Implement __udivsi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __udivsi3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

typedef su_int fixuint_t;
typedef si_int fixint_t;
#include "int_div_impl.inc"

// Returns: a / b

COMPILER_RT_ABI su_int __udivsi3(su_int a, su_int b) {
  return __udivXi3(a, b);
}

#if defined(__ARM_EABI__)
COMPILER_RT_ALIAS(__udivsi3, __aeabi_uidiv)
#endif
