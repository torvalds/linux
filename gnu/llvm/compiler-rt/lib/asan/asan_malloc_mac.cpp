//===-- asan_malloc_mac.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Mac-specific malloc interception.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_APPLE

#include "asan_interceptors.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_stats.h"
#include "lsan/lsan_common.h"

using namespace __asan;
#define COMMON_MALLOC_ZONE_NAME "asan"
#  define COMMON_MALLOC_ENTER() \
    do {                        \
      AsanInitFromRtl();        \
    } while (false)
#  define COMMON_MALLOC_SANITIZER_INITIALIZED AsanInited()
#  define COMMON_MALLOC_FORCE_LOCK() asan_mz_force_lock()
#  define COMMON_MALLOC_FORCE_UNLOCK() asan_mz_force_unlock()
#  define COMMON_MALLOC_MEMALIGN(alignment, size) \
    GET_STACK_TRACE_MALLOC;                       \
    void *p = asan_memalign(alignment, size, &stack, FROM_MALLOC)
#  define COMMON_MALLOC_MALLOC(size) \
    GET_STACK_TRACE_MALLOC;          \
    void *p = asan_malloc(size, &stack)
#  define COMMON_MALLOC_REALLOC(ptr, size) \
    GET_STACK_TRACE_MALLOC;                \
    void *p = asan_realloc(ptr, size, &stack);
#  define COMMON_MALLOC_CALLOC(count, size) \
    GET_STACK_TRACE_MALLOC;                 \
    void *p = asan_calloc(count, size, &stack);
#  define COMMON_MALLOC_POSIX_MEMALIGN(memptr, alignment, size) \
    GET_STACK_TRACE_MALLOC;                                     \
    int res = asan_posix_memalign(memptr, alignment, size, &stack);
#  define COMMON_MALLOC_VALLOC(size) \
    GET_STACK_TRACE_MALLOC;          \
    void *p = asan_memalign(GetPageSizeCached(), size, &stack, FROM_MALLOC);
#  define COMMON_MALLOC_FREE(ptr) \
    GET_STACK_TRACE_FREE;         \
    asan_free(ptr, &stack, FROM_MALLOC);
#  define COMMON_MALLOC_SIZE(ptr) uptr size = asan_mz_size(ptr);
#  define COMMON_MALLOC_FILL_STATS(zone, stats)                    \
    AsanMallocStats malloc_stats;                                  \
    FillMallocStatistics(&malloc_stats);                           \
    CHECK(sizeof(malloc_statistics_t) == sizeof(AsanMallocStats)); \
    internal_memcpy(stats, &malloc_stats, sizeof(malloc_statistics_t));
#  define COMMON_MALLOC_REPORT_UNKNOWN_REALLOC(ptr, zone_ptr, zone_name) \
    GET_STACK_TRACE_FREE;                                                \
    ReportMacMzReallocUnknown((uptr)ptr, (uptr)zone_ptr, zone_name, &stack);
#  define COMMON_MALLOC_NAMESPACE __asan
#  define COMMON_MALLOC_HAS_ZONE_ENUMERATOR 0
#  define COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT 1

#  include "sanitizer_common/sanitizer_malloc_mac.inc"

namespace COMMON_MALLOC_NAMESPACE {

bool HandleDlopenInit() {
  static_assert(SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be true");
  // We have no reliable way of knowing how we are being loaded
  // so make it a requirement on Apple platforms to set this environment
  // variable to indicate that we want to perform initialization via
  // dlopen().
  auto init_str = GetEnv("APPLE_ASAN_INIT_FOR_DLOPEN");
  if (!init_str)
    return false;
  if (internal_strncmp(init_str, "1", 1) != 0)
    return false;
  // When we are loaded via `dlopen()` path we still initialize the malloc zone
  // so Symbolication clients (e.g. `leaks`) that load the ASan allocator can
  // find an initialized malloc zone.
  InitMallocZoneFields();
  return true;
}
}  // namespace COMMON_MALLOC_NAMESPACE

namespace {

void mi_extra_init(sanitizer_malloc_introspection_t *mi) {
  uptr last_byte_plus_one = 0;
  mi->allocator_ptr = 0;
  // Range is [begin_ptr, end_ptr)
  __lsan::GetAllocatorGlobalRange(&(mi->allocator_ptr), &last_byte_plus_one);
  CHECK_NE(mi->allocator_ptr, 0);
  CHECK_GT(last_byte_plus_one, mi->allocator_ptr);
  mi->allocator_size = last_byte_plus_one - (mi->allocator_ptr);
  CHECK_GT(mi->allocator_size, 0);
}
}  // namespace

#endif
