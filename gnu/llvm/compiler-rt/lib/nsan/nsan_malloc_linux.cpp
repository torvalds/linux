//===- nsan_malloc_linux.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interceptors for memory allocation functions on ELF OSes.
//
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "nsan/nsan.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

#if !SANITIZER_APPLE && !SANITIZER_WINDOWS
using namespace __sanitizer;
using __nsan::nsan_initialized;

namespace {
struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !nsan_initialized; }
};
} // namespace

INTERCEPTOR(void *, aligned_alloc, uptr align, uptr size) {
  void *res = REAL(aligned_alloc)(align, size);
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), size);
  return res;
}

INTERCEPTOR(void *, calloc, uptr nmemb, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);

  void *res = REAL(calloc)(nmemb, size);
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), nmemb * size);
  return res;
}

INTERCEPTOR(void, free, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  REAL(free)(ptr);
}

INTERCEPTOR(void *, malloc, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  void *res = REAL(malloc)(size);
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), size);
  return res;
}

INTERCEPTOR(void *, realloc, void *ptr, uptr size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  void *res = REAL(realloc)(ptr, size);
  // TODO: We might want to copy the types from the original allocation
  // (although that would require that we know its size).
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), size);
  return res;
}

#if SANITIZER_INTERCEPT_REALLOCARRAY
INTERCEPTOR(void *, reallocarray, void *ptr, uptr nmemb, uptr size) {
  void *res = REAL(reallocarray)(ptr, nmemb, size);
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), nmemb * size);
  return res;
}
#endif // SANITIZER_INTERCEPT_REALLOCARRAY

INTERCEPTOR(int, posix_memalign, void **memptr, uptr align, uptr size) {
  int res = REAL(posix_memalign)(memptr, align, size);
  if (res == 0 && *memptr)
    __nsan_set_value_unknown(static_cast<u8 *>(*memptr), size);
  return res;
}

// Deprecated allocation functions (memalign, etc).
#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void *, memalign, uptr align, uptr size) {
  void *const res = REAL(memalign)(align, size);
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), size);
  return res;
}

INTERCEPTOR(void *, __libc_memalign, uptr align, uptr size) {
  void *const res = REAL(__libc_memalign)(align, size);
  if (res)
    __nsan_set_value_unknown(static_cast<u8 *>(res), size);
  return res;
}
#endif

void __nsan::InitializeMallocInterceptors() {
  INTERCEPT_FUNCTION(aligned_alloc);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(posix_memalign);
  INTERCEPT_FUNCTION(realloc);
#if SANITIZER_INTERCEPT_REALLOCARRAY
  INTERCEPT_FUNCTION(reallocarray);
#endif

#if SANITIZER_INTERCEPT_MEMALIGN
  INTERCEPT_FUNCTION(memalign);
  INTERCEPT_FUNCTION(__libc_memalign);
#endif
}

#endif
