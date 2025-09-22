//===- InstrProfilingRuntime.cpp - PGO runtime initialization -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

extern "C" {

#include "InstrProfiling.h"

static int RegisterRuntime() {
  __llvm_profile_initialize();
#ifdef _AIX
  extern COMPILER_RT_VISIBILITY void *__llvm_profile_keep[];
  (void)*(void *volatile *)__llvm_profile_keep;
#endif
  return 0;
}

/* int __llvm_profile_runtime  */
COMPILER_RT_VISIBILITY int INSTR_PROF_PROFILE_RUNTIME_VAR = RegisterRuntime();
}
