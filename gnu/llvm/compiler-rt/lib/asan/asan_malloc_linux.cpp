//===-- asan_malloc_linux.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Linux-specific malloc interception.
// We simply define functions like malloc, free, realloc, etc.
// They will replace the corresponding libc functions automagically.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_FUCHSIA || SANITIZER_LINUX || \
    SANITIZER_NETBSD || SANITIZER_SOLARIS

#  include "asan_allocator.h"
#  include "asan_interceptors.h"
#  include "asan_internal.h"
#  include "asan_stack.h"
#  include "lsan/lsan_common.h"
#  include "sanitizer_common/sanitizer_allocator_checks.h"
#  include "sanitizer_common/sanitizer_allocator_dlsym.h"
#  include "sanitizer_common/sanitizer_errno.h"
#  include "sanitizer_common/sanitizer_tls_get_addr.h"

// ---------------------- Replacement functions ---------------- {{{1
using namespace __asan;

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !TryAsanInitFromRtl(); }
  static void OnAllocate(const void *ptr, uptr size) {
#  if CAN_SANITIZE_LEAKS
    // Suppress leaks from dlerror(). Previously dlsym hack on global array was
    // used by leak sanitizer as a root region.
    __lsan_register_root_region(ptr, size);
#  endif
  }
  static void OnFree(const void *ptr, uptr size) {
#  if CAN_SANITIZE_LEAKS
    __lsan_unregister_root_region(ptr, size);
#  endif
  }
};

INTERCEPTOR(void, free, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_STACK_TRACE_FREE;
  asan_free(ptr, &stack, FROM_MALLOC);
}

#if SANITIZER_INTERCEPT_CFREE
INTERCEPTOR(void, cfree, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_STACK_TRACE_FREE;
  asan_free(ptr, &stack, FROM_MALLOC);
}
#endif // SANITIZER_INTERCEPT_CFREE

INTERCEPTOR(void*, malloc, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  GET_STACK_TRACE_MALLOC;
  return asan_malloc(size, &stack);
}

INTERCEPTOR(void*, calloc, uptr nmemb, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  GET_STACK_TRACE_MALLOC;
  return asan_calloc(nmemb, size, &stack);
}

INTERCEPTOR(void*, realloc, void *ptr, uptr size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  GET_STACK_TRACE_MALLOC;
  return asan_realloc(ptr, size, &stack);
}

#if SANITIZER_INTERCEPT_REALLOCARRAY
INTERCEPTOR(void*, reallocarray, void *ptr, uptr nmemb, uptr size) {
  AsanInitFromRtl();
  GET_STACK_TRACE_MALLOC;
  return asan_reallocarray(ptr, nmemb, size, &stack);
}
#endif  // SANITIZER_INTERCEPT_REALLOCARRAY

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void*, memalign, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_memalign(boundary, size, &stack, FROM_MALLOC);
}

INTERCEPTOR(void*, __libc_memalign, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  void *res = asan_memalign(boundary, size, &stack, FROM_MALLOC);
  DTLS_on_libc_memalign(res, size);
  return res;
}
#endif // SANITIZER_INTERCEPT_MEMALIGN

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void*, aligned_alloc, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_aligned_alloc(boundary, size, &stack);
}
#endif // SANITIZER_INTERCEPT_ALIGNED_ALLOC

INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  GET_CURRENT_PC_BP_SP;
  (void)sp;
  return asan_malloc_usable_size(ptr, pc, bp);
}

#if SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
// We avoid including malloc.h for portability reasons.
// man mallinfo says the fields are "long", but the implementation uses int.
// It doesn't matter much -- we just need to make sure that the libc's mallinfo
// is not called.
struct fake_mallinfo {
  int x[10];
};

INTERCEPTOR(struct fake_mallinfo, mallinfo, void) {
  struct fake_mallinfo res;
  REAL(memset)(&res, 0, sizeof(res));
  return res;
}

INTERCEPTOR(int, mallopt, int cmd, int value) {
  return 0;
}
#endif // SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_posix_memalign(memptr, alignment, size, &stack);
}

INTERCEPTOR(void*, valloc, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_valloc(size, &stack);
}

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void*, pvalloc, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_pvalloc(size, &stack);
}
#endif // SANITIZER_INTERCEPT_PVALLOC

INTERCEPTOR(void, malloc_stats, void) {
  __asan_print_accumulated_stats();
}

#if SANITIZER_ANDROID
// Format of __libc_malloc_dispatch has changed in Android L.
// While we are moving towards a solution that does not depend on bionic
// internals, here is something to support both K* and L releases.
struct MallocDebugK {
  void *(*malloc)(uptr bytes);
  void (*free)(void *mem);
  void *(*calloc)(uptr n_elements, uptr elem_size);
  void *(*realloc)(void *oldMem, uptr bytes);
  void *(*memalign)(uptr alignment, uptr bytes);
  uptr (*malloc_usable_size)(void *mem);
};

struct MallocDebugL {
  void *(*calloc)(uptr n_elements, uptr elem_size);
  void (*free)(void *mem);
  fake_mallinfo (*mallinfo)(void);
  void *(*malloc)(uptr bytes);
  uptr (*malloc_usable_size)(void *mem);
  void *(*memalign)(uptr alignment, uptr bytes);
  int (*posix_memalign)(void **memptr, uptr alignment, uptr size);
  void* (*pvalloc)(uptr size);
  void *(*realloc)(void *oldMem, uptr bytes);
  void* (*valloc)(uptr size);
};

alignas(32) const MallocDebugK asan_malloc_dispatch_k = {
    WRAP(malloc),  WRAP(free),     WRAP(calloc),
    WRAP(realloc), WRAP(memalign), WRAP(malloc_usable_size)};

alignas(32) const MallocDebugL asan_malloc_dispatch_l = {
    WRAP(calloc),         WRAP(free),               WRAP(mallinfo),
    WRAP(malloc),         WRAP(malloc_usable_size), WRAP(memalign),
    WRAP(posix_memalign), WRAP(pvalloc),            WRAP(realloc),
    WRAP(valloc)};

namespace __asan {
void ReplaceSystemMalloc() {
  void **__libc_malloc_dispatch_p =
      (void **)AsanDlSymNext("__libc_malloc_dispatch");
  if (__libc_malloc_dispatch_p) {
    // Decide on K vs L dispatch format by the presence of
    // __libc_malloc_default_dispatch export in libc.
    void *default_dispatch_p = AsanDlSymNext("__libc_malloc_default_dispatch");
    if (default_dispatch_p)
      *__libc_malloc_dispatch_p = (void *)&asan_malloc_dispatch_k;
    else
      *__libc_malloc_dispatch_p = (void *)&asan_malloc_dispatch_l;
  }
}
}  // namespace __asan

#else  // SANITIZER_ANDROID

namespace __asan {
void ReplaceSystemMalloc() {
}
}  // namespace __asan
#endif  // SANITIZER_ANDROID

#endif  // SANITIZER_FREEBSD || SANITIZER_FUCHSIA || SANITIZER_LINUX ||
        // SANITIZER_NETBSD || SANITIZER_SOLARIS
