//===-- dfsan_interceptors.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// Interceptors for standard library functions.
//===----------------------------------------------------------------------===//

#include <sys/syscall.h>
#include <unistd.h>

#include "dfsan/dfsan.h"
#include "dfsan/dfsan_thread.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "sanitizer_common/sanitizer_posix.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

using namespace __sanitizer;

static bool interceptors_initialized;

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !__dfsan::dfsan_inited; }
};

INTERCEPTOR(void *, reallocarray, void *ptr, SIZE_T nmemb, SIZE_T size) {
  return __dfsan::dfsan_reallocarray(ptr, nmemb, size);
}

INTERCEPTOR(void *, __libc_memalign, SIZE_T alignment, SIZE_T size) {
  void *ptr = __dfsan::dfsan_memalign(alignment, size);
  if (ptr)
    DTLS_on_libc_memalign(ptr, size);
  return ptr;
}

INTERCEPTOR(void *, aligned_alloc, SIZE_T alignment, SIZE_T size) {
  return __dfsan::dfsan_aligned_alloc(alignment, size);
}

INTERCEPTOR(void *, calloc, SIZE_T nmemb, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  return __dfsan::dfsan_calloc(nmemb, size);
}

INTERCEPTOR(void *, realloc, void *ptr, SIZE_T size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  return __dfsan::dfsan_realloc(ptr, size);
}

INTERCEPTOR(void *, malloc, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  return __dfsan::dfsan_malloc(size);
}

INTERCEPTOR(void, free, void *ptr) {
  if (!ptr)
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  return __dfsan::dfsan_deallocate(ptr);
}

INTERCEPTOR(void, cfree, void *ptr) {
  if (!ptr)
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  return __dfsan::dfsan_deallocate(ptr);
}

INTERCEPTOR(int, posix_memalign, void **memptr, SIZE_T alignment, SIZE_T size) {
  CHECK_NE(memptr, 0);
  int res = __dfsan::dfsan_posix_memalign(memptr, alignment, size);
  if (!res)
    dfsan_set_label(0, memptr, sizeof(*memptr));
  return res;
}

INTERCEPTOR(void *, memalign, SIZE_T alignment, SIZE_T size) {
  return __dfsan::dfsan_memalign(alignment, size);
}

INTERCEPTOR(void *, valloc, SIZE_T size) { return __dfsan::dfsan_valloc(size); }

INTERCEPTOR(void *, pvalloc, SIZE_T size) {
  return __dfsan::dfsan_pvalloc(size);
}

INTERCEPTOR(void, mallinfo, __sanitizer_struct_mallinfo *sret) {
  internal_memset(sret, 0, sizeof(*sret));
  dfsan_set_label(0, sret, sizeof(*sret));
}

INTERCEPTOR(int, mallopt, int cmd, int value) { return 0; }

INTERCEPTOR(void, malloc_stats, void) {
  // FIXME: implement, but don't call REAL(malloc_stats)!
}

INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  return __sanitizer_get_allocated_size(ptr);
}

#define ENSURE_DFSAN_INITED()               \
  do {                                      \
    CHECK(!__dfsan::dfsan_init_is_running); \
    if (!__dfsan::dfsan_inited) {           \
      __dfsan::dfsan_init();                \
    }                                       \
  } while (0)

#define COMMON_INTERCEPTOR_ENTER(func, ...) \
  if (__dfsan::dfsan_init_is_running)       \
    return REAL(func)(__VA_ARGS__);         \
  ENSURE_DFSAN_INITED();                    \
  dfsan_set_label(0, __errno_location(), sizeof(int));

INTERCEPTOR(void *, mmap, void *addr, SIZE_T length, int prot, int flags,
            int fd, OFF_T offset) {
  if (common_flags()->detect_write_exec)
    ReportMmapWriteExec(prot, flags);
  if (!__dfsan::dfsan_inited)
    return (void *)internal_mmap(addr, length, prot, flags, fd, offset);
  COMMON_INTERCEPTOR_ENTER(mmap, addr, length, prot, flags, fd, offset);
  void *res = REAL(mmap)(addr, length, prot, flags, fd, offset);
  if (res != (void *)-1) {
    dfsan_set_label(0, res, RoundUpTo(length, GetPageSizeCached()));
  }
  return res;
}

INTERCEPTOR(void *, mmap64, void *addr, SIZE_T length, int prot, int flags,
            int fd, OFF64_T offset) {
  if (common_flags()->detect_write_exec)
    ReportMmapWriteExec(prot, flags);
  if (!__dfsan::dfsan_inited)
    return (void *)internal_mmap(addr, length, prot, flags, fd, offset);
  COMMON_INTERCEPTOR_ENTER(mmap64, addr, length, prot, flags, fd, offset);
  void *res = REAL(mmap64)(addr, length, prot, flags, fd, offset);
  if (res != (void *)-1) {
    dfsan_set_label(0, res, RoundUpTo(length, GetPageSizeCached()));
  }
  return res;
}

INTERCEPTOR(int, munmap, void *addr, SIZE_T length) {
  if (!__dfsan::dfsan_inited)
    return internal_munmap(addr, length);
  COMMON_INTERCEPTOR_ENTER(munmap, addr, length);
  int res = REAL(munmap)(addr, length);
  if (res != -1)
    dfsan_set_label(0, addr, RoundUpTo(length, GetPageSizeCached()));
  return res;
}

#define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end)           \
  if (__dfsan::DFsanThread *t = __dfsan::GetCurrentThread()) { \
    *begin = t->tls_begin();                                   \
    *end = t->tls_end();                                       \
  } else {                                                     \
    *begin = *end = 0;                                         \
  }
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(ptr, size) \
  dfsan_set_label(0, ptr, size)

INTERCEPTOR(void *, __tls_get_addr, void *arg) {
  COMMON_INTERCEPTOR_ENTER(__tls_get_addr, arg);
  void *res = REAL(__tls_get_addr)(arg);
  uptr tls_begin, tls_end;
  COMMON_INTERCEPTOR_GET_TLS_RANGE(&tls_begin, &tls_end);
  DTLS::DTV *dtv = DTLS_on_tls_get_addr(arg, res, tls_begin, tls_end);
  if (dtv) {
    // New DTLS block has been allocated.
    COMMON_INTERCEPTOR_INITIALIZE_RANGE((void *)dtv->beg, dtv->size);
  }
  return res;
}

namespace __dfsan {
void initialize_interceptors() {
  CHECK(!interceptors_initialized);

  INTERCEPT_FUNCTION(aligned_alloc);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(cfree);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(mallinfo);
  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(malloc_stats);
  INTERCEPT_FUNCTION(malloc_usable_size);
  INTERCEPT_FUNCTION(mallopt);
  INTERCEPT_FUNCTION(memalign);
  INTERCEPT_FUNCTION(mmap);
  INTERCEPT_FUNCTION(mmap64);
  INTERCEPT_FUNCTION(munmap);
  INTERCEPT_FUNCTION(posix_memalign);
  INTERCEPT_FUNCTION(pvalloc);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(reallocarray);
  INTERCEPT_FUNCTION(valloc);
  INTERCEPT_FUNCTION(__tls_get_addr);
  INTERCEPT_FUNCTION(__libc_memalign);

  interceptors_initialized = true;
}
}  // namespace __dfsan
