//===- coff_platform.per_jd.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code that will be loaded per each JITDylib.
//
//===----------------------------------------------------------------------===//
#include "compiler.h"

ORC_RT_INTERFACE void __orc_rt_coff_per_jd_marker() {}

typedef int (*OnExitFunction)(void);
typedef void (*AtExitFunction)(void);

extern "C" void *__ImageBase;
ORC_RT_INTERFACE OnExitFunction __orc_rt_coff_onexit(void *Header,
                                                     OnExitFunction Func);
ORC_RT_INTERFACE int __orc_rt_coff_atexit(void *Header, AtExitFunction Func);

ORC_RT_INTERFACE OnExitFunction
__orc_rt_coff_onexit_per_jd(OnExitFunction Func) {
  return __orc_rt_coff_onexit(&__ImageBase, Func);
}

ORC_RT_INTERFACE int __orc_rt_coff_atexit_per_jd(AtExitFunction Func) {
  return __orc_rt_coff_atexit(&__ImageBase, Func);
}
