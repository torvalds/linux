//===- coff_platform.h -------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC Runtime support for dynamic loading features on COFF-based platforms.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_COFF_PLATFORM_H
#define ORC_RT_COFF_PLATFORM_H

#include "common.h"
#include "executor_address.h"

// dlfcn functions.
ORC_RT_INTERFACE const char *__orc_rt_coff_jit_dlerror();
ORC_RT_INTERFACE void *__orc_rt_coff_jit_dlopen(const char *path, int mode);
ORC_RT_INTERFACE int __orc_rt_coff_jit_dlclose(void *header);
ORC_RT_INTERFACE void *__orc_rt_coff_jit_dlsym(void *header,
                                               const char *symbol);

namespace __orc_rt {
namespace coff {

enum dlopen_mode : int {
  ORC_RT_RTLD_LAZY = 0x1,
  ORC_RT_RTLD_NOW = 0x2,
  ORC_RT_RTLD_LOCAL = 0x4,
  ORC_RT_RTLD_GLOBAL = 0x8
};

} // end namespace coff
} // end namespace __orc_rt

#endif
