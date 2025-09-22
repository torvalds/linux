//===-- hwasan_allocation_functions.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Definitions for __sanitizer allocation functions.
//
//===----------------------------------------------------------------------===//

#include "hwasan.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_mallinfo.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

using namespace __hwasan;

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !hwasan_inited; }
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

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_posix_memalign(void **memptr, uptr alignment, uptr size) {
  GET_MALLOC_STACK_TRACE;
  CHECK_NE(memptr, 0);
  int res = hwasan_posix_memalign(memptr, alignment, size, &stack);
  return res;
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_memalign(uptr alignment, uptr size) {
  GET_MALLOC_STACK_TRACE;
  return hwasan_memalign(alignment, size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_aligned_alloc(uptr alignment, uptr size) {
  GET_MALLOC_STACK_TRACE;
  return hwasan_aligned_alloc(alignment, size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer___libc_memalign(uptr alignment, uptr size) {
  GET_MALLOC_STACK_TRACE;
  void *ptr = hwasan_memalign(alignment, size, &stack);
  if (ptr)
    DTLS_on_libc_memalign(ptr, size);
  return ptr;
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_valloc(uptr size) {
  GET_MALLOC_STACK_TRACE;
  return hwasan_valloc(size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_pvalloc(uptr size) {
  GET_MALLOC_STACK_TRACE;
  return hwasan_pvalloc(size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_free(void *ptr) {
  if (!ptr)
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_MALLOC_STACK_TRACE;
  hwasan_free(ptr, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_cfree(void *ptr) {
  if (!ptr)
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_MALLOC_STACK_TRACE;
  hwasan_free(ptr, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_malloc_usable_size(const void *ptr) {
  return __sanitizer_get_allocated_size(ptr);
}

SANITIZER_INTERFACE_ATTRIBUTE
struct __sanitizer_struct_mallinfo __sanitizer_mallinfo() {
  __sanitizer_struct_mallinfo sret;
  internal_memset(&sret, 0, sizeof(sret));
  return sret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_mallopt(int cmd, int value) { return 0; }

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_malloc_stats(void) {
  // FIXME: implement, but don't call REAL(malloc_stats)!
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_calloc(uptr nmemb, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  GET_MALLOC_STACK_TRACE;
  return hwasan_calloc(nmemb, size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_realloc(void *ptr, uptr size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  GET_MALLOC_STACK_TRACE;
  return hwasan_realloc(ptr, size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_reallocarray(void *ptr, uptr nmemb, uptr size) {
  GET_MALLOC_STACK_TRACE;
  return hwasan_reallocarray(ptr, nmemb, size, &stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_malloc(uptr size) {
  if (UNLIKELY(!hwasan_init_is_running))
    ENSURE_HWASAN_INITED();
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  GET_MALLOC_STACK_TRACE;
  return hwasan_malloc(size, &stack);
}

}  // extern "C"

#if HWASAN_WITH_INTERCEPTORS || SANITIZER_FUCHSIA
#if SANITIZER_FUCHSIA
// Fuchsia does not use WRAP/wrappers used for the interceptor infrastructure.
#  define INTERCEPTOR_ALIAS(RET, FN, ARGS...)                                 \
    extern "C" SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE RET FN( \
        ARGS) ALIAS(__sanitizer_##FN)
#else
#  define INTERCEPTOR_ALIAS(RET, FN, ARGS...)                                 \
    extern "C" SANITIZER_INTERFACE_ATTRIBUTE RET WRAP(FN)(ARGS)               \
        ALIAS(__sanitizer_##FN);                                              \
    extern "C" SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE RET FN( \
        ARGS) ALIAS(__sanitizer_##FN)
#endif

INTERCEPTOR_ALIAS(int, posix_memalign, void **memptr, SIZE_T alignment,
                  SIZE_T size);
INTERCEPTOR_ALIAS(void *, aligned_alloc, SIZE_T alignment, SIZE_T size);
INTERCEPTOR_ALIAS(void *, __libc_memalign, SIZE_T alignment, SIZE_T size);
INTERCEPTOR_ALIAS(void *, valloc, SIZE_T size);
INTERCEPTOR_ALIAS(void, free, void *ptr);
INTERCEPTOR_ALIAS(uptr, malloc_usable_size, const void *ptr);
INTERCEPTOR_ALIAS(void *, calloc, SIZE_T nmemb, SIZE_T size);
INTERCEPTOR_ALIAS(void *, realloc, void *ptr, SIZE_T size);
INTERCEPTOR_ALIAS(void *, reallocarray, void *ptr, SIZE_T nmemb, SIZE_T size);
INTERCEPTOR_ALIAS(void *, malloc, SIZE_T size);

#  if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR_ALIAS(void *, memalign, SIZE_T alignment, SIZE_T size);
INTERCEPTOR_ALIAS(void *, pvalloc, SIZE_T size);
INTERCEPTOR_ALIAS(void, cfree, void *ptr);
INTERCEPTOR_ALIAS(__sanitizer_struct_mallinfo, mallinfo,);
INTERCEPTOR_ALIAS(int, mallopt, int cmd, int value);
INTERCEPTOR_ALIAS(void, malloc_stats, void);
#  endif
#endif  // #if HWASAN_WITH_INTERCEPTORS
