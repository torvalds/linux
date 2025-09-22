//===----- lib/fp_mode.h - Floaing-point environment mode utilities --C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is not part of the interface of this library.
//
// This file defines an interface for accessing hardware floating point
// environment mode.
//
//===----------------------------------------------------------------------===//

#ifndef FP_MODE_H
#define FP_MODE_H

typedef enum {
  CRT_FE_TONEAREST,
  CRT_FE_DOWNWARD,
  CRT_FE_UPWARD,
  CRT_FE_TOWARDZERO
} CRT_FE_ROUND_MODE;

CRT_FE_ROUND_MODE __fe_getround(void);
int __fe_raise_inexact(void);

#endif // FP_MODE_H
