//===--- muldi3.S - Integer multiplication routines -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#if __riscv_xlen == 64
#define __mulxi3 __muldi3
#include "int_mul_impl.inc"
#endif
