//===-- asan_interceptors_memintrinsics.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan versions of memcpy, memmove, and memset.
//===---------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "asan_interceptors_memintrinsics.h"

#include "asan_interceptors.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_suppressions.h"

using namespace __asan;

// memcpy is called during __asan_init() from the internals of printf(...).
// We do not treat memcpy with to==from as a bug.
// See http://llvm.org/bugs/show_bug.cgi?id=11763.
#define ASAN_MEMCPY_IMPL(ctx, to, from, size)                 \
  do {                                                        \
    if (LIKELY(replace_intrin_cached)) {                      \
      if (LIKELY(to != from)) {                               \
        CHECK_RANGES_OVERLAP("memcpy", to, size, from, size); \
      }                                                       \
      ASAN_READ_RANGE(ctx, from, size);                       \
      ASAN_WRITE_RANGE(ctx, to, size);                        \
    } else if (UNLIKELY(!AsanInited())) {                     \
      return internal_memcpy(to, from, size);                 \
    }                                                         \
    return REAL(memcpy)(to, from, size);                      \
  } while (0)

// memset is called inside Printf.
#define ASAN_MEMSET_IMPL(ctx, block, c, size) \
  do {                                        \
    if (LIKELY(replace_intrin_cached)) {      \
      ASAN_WRITE_RANGE(ctx, block, size);     \
    } else if (UNLIKELY(!AsanInited())) {     \
      return internal_memset(block, c, size); \
    }                                         \
    return REAL(memset)(block, c, size);      \
  } while (0)

#define ASAN_MEMMOVE_IMPL(ctx, to, from, size) \
  do {                                         \
    if (LIKELY(replace_intrin_cached)) {       \
      ASAN_READ_RANGE(ctx, from, size);        \
      ASAN_WRITE_RANGE(ctx, to, size);         \
    }                                          \
    return internal_memmove(to, from, size);   \
  } while (0)

void *__asan_memcpy(void *to, const void *from, uptr size) {
  ASAN_MEMCPY_IMPL(nullptr, to, from, size);
}

void *__asan_memset(void *block, int c, uptr size) {
  ASAN_MEMSET_IMPL(nullptr, block, c, size);
}

void *__asan_memmove(void *to, const void *from, uptr size) {
  ASAN_MEMMOVE_IMPL(nullptr, to, from, size);
}

#if SANITIZER_FUCHSIA

// Fuchsia doesn't use sanitizer_common_interceptors.inc, but
// the only things there it wants are these three.  Just define them
// as aliases here rather than repeating the contents.

extern "C" decltype(__asan_memcpy) memcpy[[gnu::alias("__asan_memcpy")]];
extern "C" decltype(__asan_memmove) memmove[[gnu::alias("__asan_memmove")]];
extern "C" decltype(__asan_memset) memset[[gnu::alias("__asan_memset")]];

#else  // SANITIZER_FUCHSIA

#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size) \
  do {                                                       \
    ASAN_INTERCEPTOR_ENTER(ctx, memmove);                    \
    ASAN_MEMMOVE_IMPL(ctx, to, from, size);                  \
  } while (false)

#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size) \
  do {                                                      \
    ASAN_INTERCEPTOR_ENTER(ctx, memcpy);                    \
    ASAN_MEMCPY_IMPL(ctx, to, from, size);                  \
  } while (false)

#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size) \
  do {                                                      \
    ASAN_INTERCEPTOR_ENTER(ctx, memset);                    \
    ASAN_MEMSET_IMPL(ctx, block, c, size);                  \
  } while (false)

#include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"

#endif  // SANITIZER_FUCHSIA
