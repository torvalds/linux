//===-- sanitizer_common_interceptors_memintrinsics.inc ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Memintrinsic function interceptors for tools like AddressSanitizer,
// ThreadSanitizer, MemorySanitizer, etc.
//
// These interceptors are part of the common interceptors, but separated out so
// that implementations may add them, if necessary, to a separate source file
// that should define SANITIZER_COMMON_NO_REDEFINE_BUILTINS at the top.
//
// This file should be included into the tool's memintrinsic interceptor file,
// which has to define its own macros:
//   COMMON_INTERCEPTOR_ENTER
//   COMMON_INTERCEPTOR_READ_RANGE
//   COMMON_INTERCEPTOR_WRITE_RANGE
//   COMMON_INTERCEPTOR_MEMSET_IMPL
//   COMMON_INTERCEPTOR_MEMMOVE_IMPL
//   COMMON_INTERCEPTOR_MEMCPY_IMPL
//   COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED
//===----------------------------------------------------------------------===//

#ifdef SANITIZER_REDEFINE_BUILTINS_H
#error "Define SANITIZER_COMMON_NO_REDEFINE_BUILTINS in .cpp file"
#endif

#include "interception/interception.h"
#include "sanitizer_platform_interceptors.h"

// Platform-specific options.
#if SANITIZER_APPLE
#define PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE 0
#elif SANITIZER_WINDOWS64
#define PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE 0
#else
#define PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE 1
#endif  // SANITIZER_APPLE

#ifndef COMMON_INTERCEPTOR_MEMSET_IMPL
#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, v, size) \
  {                                                       \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)        \
      return internal_memset(dst, v, size);               \
    COMMON_INTERCEPTOR_ENTER(ctx, memset, dst, v, size);  \
    if (common_flags()->intercept_intrin)                 \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);     \
    return REAL(memset)(dst, v, size);                    \
  }
#endif

#ifndef COMMON_INTERCEPTOR_MEMMOVE_IMPL
#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size) \
  {                                                          \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)           \
      return internal_memmove(dst, src, size);               \
    COMMON_INTERCEPTOR_ENTER(ctx, memmove, dst, src, size);  \
    if (common_flags()->intercept_intrin) {                  \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);        \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, src, size);         \
    }                                                        \
    return REAL(memmove)(dst, src, size);                    \
  }
#endif

#ifndef COMMON_INTERCEPTOR_MEMCPY_IMPL
#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size) \
  {                                                         \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED) {        \
      return internal_memmove(dst, src, size);              \
    }                                                       \
    COMMON_INTERCEPTOR_ENTER(ctx, memcpy, dst, src, size);  \
    if (common_flags()->intercept_intrin) {                 \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);       \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, src, size);        \
    }                                                       \
    return REAL(memcpy)(dst, src, size);                    \
  }
#endif

#if SANITIZER_INTERCEPT_MEMSET
INTERCEPTOR(void *, memset, void *dst, int v, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, v, size);
}

#define INIT_MEMSET COMMON_INTERCEPT_FUNCTION(memset)
#else
#define INIT_MEMSET
#endif

#if SANITIZER_INTERCEPT_MEMMOVE
INTERCEPTOR(void *, memmove, void *dst, const void *src, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
}

#define INIT_MEMMOVE COMMON_INTERCEPT_FUNCTION(memmove)
#else
#define INIT_MEMMOVE
#endif

#if SANITIZER_INTERCEPT_MEMCPY
INTERCEPTOR(void *, memcpy, void *dst, const void *src, uptr size) {
  // On OS X, calling internal_memcpy here will cause memory corruptions,
  // because memcpy and memmove are actually aliases of the same
  // implementation.  We need to use internal_memmove here.
  // N.B.: If we switch this to internal_ we'll have to use internal_memmove
  // due to memcpy being an alias of memmove on OS X.
  void *ctx;
#if PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE
    COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size);
#else
    COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
#endif
}

#define INIT_MEMCPY                                  \
  do {                                               \
    if (PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE) { \
      COMMON_INTERCEPT_FUNCTION(memcpy);             \
    } else {                                         \
      ASSIGN_REAL(memcpy, memmove);                  \
    }                                                \
    CHECK(REAL(memcpy));                             \
  } while (false)

#else
#define INIT_MEMCPY
#endif

#if SANITIZER_INTERCEPT_AEABI_MEM
INTERCEPTOR(void *, __aeabi_memmove, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memmove4, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memmove8, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memcpy, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memcpy4, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size);
}

INTERCEPTOR(void *, __aeabi_memcpy8, void *to, const void *from, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size);
}

// Note the argument order.
INTERCEPTOR(void *, __aeabi_memset, void *block, uptr size, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size);
}

INTERCEPTOR(void *, __aeabi_memset4, void *block, uptr size, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size);
}

INTERCEPTOR(void *, __aeabi_memset8, void *block, uptr size, int c) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size);
}

INTERCEPTOR(void *, __aeabi_memclr, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

INTERCEPTOR(void *, __aeabi_memclr4, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

INTERCEPTOR(void *, __aeabi_memclr8, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}

#define INIT_AEABI_MEM                         \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memmove);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memmove4); \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memmove8); \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memcpy);   \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memcpy4);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memcpy8);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memset);   \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memset4);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memset8);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memclr);   \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memclr4);  \
  COMMON_INTERCEPT_FUNCTION(__aeabi_memclr8);
#else
#define INIT_AEABI_MEM
#endif  // SANITIZER_INTERCEPT_AEABI_MEM

#if SANITIZER_INTERCEPT___BZERO
INTERCEPTOR(void *, __bzero, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}
#define INIT___BZERO COMMON_INTERCEPT_FUNCTION(__bzero);
#else
#define INIT___BZERO
#endif  // SANITIZER_INTERCEPT___BZERO

#if SANITIZER_INTERCEPT_BZERO
INTERCEPTOR(void *, bzero, void *block, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, 0, size);
}
#define INIT_BZERO COMMON_INTERCEPT_FUNCTION(bzero);
#else
#define INIT_BZERO
#endif  // SANITIZER_INTERCEPT_BZERO

namespace __sanitizer {
// This does not need to be called if InitializeCommonInterceptors() is called.
void InitializeMemintrinsicInterceptors() {
  INIT_MEMSET;
  INIT_MEMMOVE;
  INIT_MEMCPY;
  INIT_AEABI_MEM;
  INIT___BZERO;
  INIT_BZERO;
}
}  // namespace __sanitizer
