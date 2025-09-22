//===-- memprof_interceptors_memintrinsics.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf versions of memcpy, memmove, and memset.
//===---------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "memprof_interceptors_memintrinsics.h"

#include "memprof_interceptors.h"
#include "memprof_stack.h"

using namespace __memprof;

// memcpy is called during __memprof_init() from the internals of printf(...).
// We do not treat memcpy with to==from as a bug.
// See http://llvm.org/bugs/show_bug.cgi?id=11763.
#define MEMPROF_MEMCPY_IMPL(to, from, size)                                    \
  do {                                                                         \
    if (UNLIKELY(!memprof_inited))                                             \
      return internal_memcpy(to, from, size);                                  \
    if (memprof_init_is_running) {                                             \
      return REAL(memcpy)(to, from, size);                                     \
    }                                                                          \
    ENSURE_MEMPROF_INITED();                                                   \
    MEMPROF_READ_RANGE(from, size);                                            \
    MEMPROF_WRITE_RANGE(to, size);                                             \
    return REAL(memcpy)(to, from, size);                                       \
  } while (0)

// memset is called inside Printf.
#define MEMPROF_MEMSET_IMPL(block, c, size)                                    \
  do {                                                                         \
    if (UNLIKELY(!memprof_inited))                                             \
      return internal_memset(block, c, size);                                  \
    if (memprof_init_is_running) {                                             \
      return REAL(memset)(block, c, size);                                     \
    }                                                                          \
    ENSURE_MEMPROF_INITED();                                                   \
    MEMPROF_WRITE_RANGE(block, size);                                          \
    return REAL(memset)(block, c, size);                                       \
  } while (0)

#define MEMPROF_MEMMOVE_IMPL(to, from, size)                                   \
  do {                                                                         \
    if (UNLIKELY(!memprof_inited))                                             \
      return internal_memmove(to, from, size);                                 \
    ENSURE_MEMPROF_INITED();                                                   \
    MEMPROF_READ_RANGE(from, size);                                            \
    MEMPROF_WRITE_RANGE(to, size);                                             \
    return internal_memmove(to, from, size);                                   \
  } while (0)

#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size)                   \
  do {                                                                         \
    MEMPROF_INTERCEPTOR_ENTER(ctx, memmove);                                   \
    MEMPROF_MEMMOVE_IMPL(to, from, size);                                      \
  } while (false)

#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size)                    \
  do {                                                                         \
    MEMPROF_INTERCEPTOR_ENTER(ctx, memcpy);                                    \
    MEMPROF_MEMCPY_IMPL(to, from, size);                                       \
  } while (false)

#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size)                    \
  do {                                                                         \
    MEMPROF_INTERCEPTOR_ENTER(ctx, memset);                                    \
    MEMPROF_MEMSET_IMPL(block, c, size);                                       \
  } while (false)

#include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"

void *__memprof_memcpy(void *to, const void *from, uptr size) {
  MEMPROF_MEMCPY_IMPL(to, from, size);
}

void *__memprof_memset(void *block, int c, uptr size) {
  MEMPROF_MEMSET_IMPL(block, c, size);
}

void *__memprof_memmove(void *to, const void *from, uptr size) {
  MEMPROF_MEMMOVE_IMPL(to, from, size);
}
