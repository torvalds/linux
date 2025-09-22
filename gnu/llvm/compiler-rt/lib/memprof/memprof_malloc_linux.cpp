//===-- memprof_malloc_linux.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Linux-specific malloc interception.
// We simply define functions like malloc, free, realloc, etc.
// They will replace the corresponding libc functions automagically.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if !SANITIZER_LINUX
#error Unsupported OS
#endif

#include "memprof_allocator.h"
#include "memprof_interceptors.h"
#include "memprof_internal.h"
#include "memprof_stack.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

// ---------------------- Replacement functions ---------------- {{{1
using namespace __memprof;

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return memprof_init_is_running; }
};

INTERCEPTOR(void, free, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_STACK_TRACE_FREE;
  memprof_free(ptr, &stack, FROM_MALLOC);
}

#if SANITIZER_INTERCEPT_CFREE
INTERCEPTOR(void, cfree, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_STACK_TRACE_FREE;
  memprof_free(ptr, &stack, FROM_MALLOC);
}
#endif // SANITIZER_INTERCEPT_CFREE

INTERCEPTOR(void *, malloc, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  ENSURE_MEMPROF_INITED();
  GET_STACK_TRACE_MALLOC;
  return memprof_malloc(size, &stack);
}

INTERCEPTOR(void *, calloc, uptr nmemb, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  ENSURE_MEMPROF_INITED();
  GET_STACK_TRACE_MALLOC;
  return memprof_calloc(nmemb, size, &stack);
}

INTERCEPTOR(void *, realloc, void *ptr, uptr size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  ENSURE_MEMPROF_INITED();
  GET_STACK_TRACE_MALLOC;
  return memprof_realloc(ptr, size, &stack);
}

#if SANITIZER_INTERCEPT_REALLOCARRAY
INTERCEPTOR(void *, reallocarray, void *ptr, uptr nmemb, uptr size) {
  ENSURE_MEMPROF_INITED();
  GET_STACK_TRACE_MALLOC;
  return memprof_reallocarray(ptr, nmemb, size, &stack);
}
#endif // SANITIZER_INTERCEPT_REALLOCARRAY

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void *, memalign, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return memprof_memalign(boundary, size, &stack, FROM_MALLOC);
}

INTERCEPTOR(void *, __libc_memalign, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  void *res = memprof_memalign(boundary, size, &stack, FROM_MALLOC);
  DTLS_on_libc_memalign(res, size);
  return res;
}
#endif // SANITIZER_INTERCEPT_MEMALIGN

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void *, aligned_alloc, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return memprof_aligned_alloc(boundary, size, &stack);
}
#endif // SANITIZER_INTERCEPT_ALIGNED_ALLOC

INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  return memprof_malloc_usable_size(ptr);
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

INTERCEPTOR(int, mallopt, int cmd, int value) { return 0; }
#endif // SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return memprof_posix_memalign(memptr, alignment, size, &stack);
}

INTERCEPTOR(void *, valloc, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return memprof_valloc(size, &stack);
}

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void *, pvalloc, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return memprof_pvalloc(size, &stack);
}
#endif // SANITIZER_INTERCEPT_PVALLOC

INTERCEPTOR(void, malloc_stats, void) { __memprof_print_accumulated_stats(); }

namespace __memprof {
void ReplaceSystemMalloc() {}
} // namespace __memprof
