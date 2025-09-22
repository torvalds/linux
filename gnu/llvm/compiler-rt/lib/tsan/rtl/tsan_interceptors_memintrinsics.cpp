//===-- tsan_interceptors_posix.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "tsan_interceptors.h"
#include "tsan_interface.h"

using namespace __tsan;

#include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"

extern "C" {

void *__tsan_memcpy(void *dst, const void *src, uptr size) {
  void *ctx;
#if PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size);
#else
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
#endif
}

void *__tsan_memset(void *dst, int c, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, c, size);
}

void *__tsan_memmove(void *dst, const void *src, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
}

}  // extern "C"
