//===-- memprof_linux.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Linux-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if !SANITIZER_LINUX
#error Unsupported OS
#endif

#include "memprof_interceptors.h"
#include "memprof_internal.h"
#include "memprof_thread.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_procmaps.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <unwind.h>

typedef enum {
  MEMPROF_RT_VERSION_UNDEFINED = 0,
  MEMPROF_RT_VERSION_DYNAMIC,
  MEMPROF_RT_VERSION_STATIC,
} memprof_rt_version_t;

// FIXME: perhaps also store abi version here?
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
memprof_rt_version_t __memprof_rt_version;
}

namespace __memprof {

void InitializePlatformInterceptors() {}
void InitializePlatformExceptionHandlers() {}

uptr FindDynamicShadowStart() {
  uptr shadow_size_bytes = MemToShadowSize(kHighMemEnd);
  return MapDynamicShadow(shadow_size_bytes, SHADOW_SCALE,
                          /*min_shadow_base_alignment*/ 0, kHighMemEnd,
                          GetMmapGranularity());
}

void *MemprofDlSymNext(const char *sym) { return dlsym(RTLD_NEXT, sym); }

} // namespace __memprof
