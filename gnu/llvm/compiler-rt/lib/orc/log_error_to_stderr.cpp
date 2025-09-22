//===-- log_error_to_stderr.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#include "compiler.h"

#include <stdio.h>

ORC_RT_INTERFACE void __orc_rt_log_error_to_stderr(const char *ErrMsg) {
  fprintf(stderr, "orc runtime error: %s\n", ErrMsg);
}
