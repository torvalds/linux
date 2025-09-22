//===-- bswapsi2.c - Implement __bswapsi2 ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __bswapsi2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

COMPILER_RT_ABI uint32_t __bswapsi2(uint32_t u) {
  return ((((u)&0xff000000) >> 24) |
          (((u)&0x00ff0000) >> 8)  |
          (((u)&0x0000ff00) << 8)  |
          (((u)&0x000000ff) << 24));
}
