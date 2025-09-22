//===-- lsan_malloc_mac.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer (LSan), a memory leak detector.
//
// Mac-specific malloc interception.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_APPLE

#include "lsan.h"
#include "lsan_allocator.h"
#include "lsan_thread.h"

using namespace __lsan;
#define COMMON_MALLOC_ZONE_NAME "lsan"
#define COMMON_MALLOC_ENTER() ENSURE_LSAN_INITED
#define COMMON_MALLOC_SANITIZER_INITIALIZED lsan_inited
#define COMMON_MALLOC_FORCE_LOCK()
#define COMMON_MALLOC_FORCE_UNLOCK()
#define COMMON_MALLOC_MEMALIGN(alignment, size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = lsan_memalign(alignment, size, stack)
#define COMMON_MALLOC_MALLOC(size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = lsan_malloc(size, stack)
#define COMMON_MALLOC_REALLOC(ptr, size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = lsan_realloc(ptr, size, stack)
#define COMMON_MALLOC_CALLOC(count, size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = lsan_calloc(count, size, stack)
#define COMMON_MALLOC_POSIX_MEMALIGN(memptr, alignment, size) \
  GET_STACK_TRACE_MALLOC; \
  int res = lsan_posix_memalign(memptr, alignment, size, stack)
#define COMMON_MALLOC_VALLOC(size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = lsan_valloc(size, stack)
#define COMMON_MALLOC_FREE(ptr) \
  lsan_free(ptr)
#define COMMON_MALLOC_SIZE(ptr) \
  uptr size = lsan_mz_size(ptr)
#define COMMON_MALLOC_FILL_STATS(zone, stats)
#define COMMON_MALLOC_REPORT_UNKNOWN_REALLOC(ptr, zone_ptr, zone_name) \
  (void)zone_name; \
  Report("mz_realloc(%p) -- attempting to realloc unallocated memory.\n", ptr);
#define COMMON_MALLOC_NAMESPACE __lsan
#define COMMON_MALLOC_HAS_ZONE_ENUMERATOR 0
#define COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT 0

#include "sanitizer_common/sanitizer_malloc_mac.inc"

#endif // SANITIZER_APPLE
