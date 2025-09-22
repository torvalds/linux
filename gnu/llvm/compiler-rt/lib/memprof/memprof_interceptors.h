//===-- memprof_interceptors.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf-private header for memprof_interceptors.cpp
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_INTERCEPTORS_H
#define MEMPROF_INTERCEPTORS_H

#include "interception/interception.h"
#include "memprof_interceptors_memintrinsics.h"
#include "memprof_internal.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

namespace __memprof {

void InitializeMemprofInterceptors();
void InitializePlatformInterceptors();

#define ENSURE_MEMPROF_INITED()                                                \
  do {                                                                         \
    CHECK(!memprof_init_is_running);                                           \
    if (UNLIKELY(!memprof_inited)) {                                           \
      MemprofInitFromRtl();                                                    \
    }                                                                          \
  } while (0)

} // namespace __memprof

DECLARE_REAL(int, memcmp, const void *a1, const void *a2, uptr size)
DECLARE_REAL(char *, strchr, const char *str, int c)
DECLARE_REAL(SIZE_T, strlen, const char *s)
DECLARE_REAL(char *, strncpy, char *to, const char *from, uptr size)
DECLARE_REAL(uptr, strnlen, const char *s, uptr maxlen)
DECLARE_REAL(char *, strstr, const char *s1, const char *s2)

#define MEMPROF_INTERCEPT_FUNC(name)                                           \
  do {                                                                         \
    if (!INTERCEPT_FUNCTION(name))                                             \
      VReport(1, "MemProfiler: failed to intercept '%s'\n'", #name);           \
  } while (0)
#define MEMPROF_INTERCEPT_FUNC_VER(name, ver)                                  \
  do {                                                                         \
    if (!INTERCEPT_FUNCTION_VER(name, ver))                                    \
      VReport(1, "MemProfiler: failed to intercept '%s@@%s'\n", #name, ver);   \
  } while (0)
#define MEMPROF_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)             \
  do {                                                                         \
    if (!INTERCEPT_FUNCTION_VER(name, ver) && !INTERCEPT_FUNCTION(name))       \
      VReport(1, "MemProfiler: failed to intercept '%s@@%s' or '%s'\n", #name, \
              ver, #name);                                                     \
  } while (0)

#define MEMPROF_INTERCEPTOR_ENTER(ctx, func)                                   \
  ctx = 0;                                                                     \
  (void)ctx;

#define COMMON_INTERCEPT_FUNCTION(name) MEMPROF_INTERCEPT_FUNC(name)

#endif // MEMPROF_INTERCEPTORS_H
