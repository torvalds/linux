//===- macho_platform.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC Runtime support for Darwin dynamic loading features.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_MACHO_PLATFORM_H
#define ORC_RT_MACHO_PLATFORM_H

#include "common.h"
#include "executor_address.h"

// Atexit functions.
ORC_RT_INTERFACE int __orc_rt_macho_cxa_atexit(void (*func)(void *), void *arg,
                                               void *dso_handle);
ORC_RT_INTERFACE void __orc_rt_macho_cxa_finalize(void *dso_handle);

// dlfcn functions.
ORC_RT_INTERFACE const char *__orc_rt_macho_jit_dlerror();
ORC_RT_INTERFACE void *__orc_rt_macho_jit_dlopen(const char *path, int mode);
ORC_RT_INTERFACE int __orc_rt_macho_jit_dlclose(void *dso_handle);
ORC_RT_INTERFACE void *__orc_rt_macho_jit_dlsym(void *dso_handle,
                                                const char *symbol);

namespace __orc_rt {
namespace macho {

enum dlopen_mode : int {
  ORC_RT_RTLD_LAZY = 0x1,
  ORC_RT_RTLD_NOW = 0x2,
  ORC_RT_RTLD_LOCAL = 0x4,
  ORC_RT_RTLD_GLOBAL = 0x8
};

} // end namespace macho
} // end namespace __orc_rt

#endif // ORC_RT_MACHO_PLATFORM_H
