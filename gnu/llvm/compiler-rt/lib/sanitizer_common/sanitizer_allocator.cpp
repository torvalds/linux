//===-- sanitizer_allocator.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
// This allocator is used inside run-times.
//===----------------------------------------------------------------------===//

#include "sanitizer_allocator.h"

#include "sanitizer_allocator_checks.h"
#include "sanitizer_allocator_internal.h"
#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_platform.h"

namespace __sanitizer {

// Default allocator names.
const char *PrimaryAllocatorName = "SizeClassAllocator";
const char *SecondaryAllocatorName = "LargeMmapAllocator";

alignas(64) static char internal_alloc_placeholder[sizeof(InternalAllocator)];
static atomic_uint8_t internal_allocator_initialized;
static StaticSpinMutex internal_alloc_init_mu;

static InternalAllocatorCache internal_allocator_cache;
static StaticSpinMutex internal_allocator_cache_mu;

InternalAllocator *internal_allocator() {
  InternalAllocator *internal_allocator_instance =
      reinterpret_cast<InternalAllocator *>(&internal_alloc_placeholder);
  if (atomic_load(&internal_allocator_initialized, memory_order_acquire) == 0) {
    SpinMutexLock l(&internal_alloc_init_mu);
    if (atomic_load(&internal_allocator_initialized, memory_order_relaxed) ==
        0) {
      internal_allocator_instance->Init(kReleaseToOSIntervalNever);
      atomic_store(&internal_allocator_initialized, 1, memory_order_release);
    }
  }
  return internal_allocator_instance;
}

static void *RawInternalAlloc(uptr size, InternalAllocatorCache *cache,
                              uptr alignment) {
  if (alignment == 0) alignment = 8;
  if (cache == 0) {
    SpinMutexLock l(&internal_allocator_cache_mu);
    return internal_allocator()->Allocate(&internal_allocator_cache, size,
                                          alignment);
  }
  return internal_allocator()->Allocate(cache, size, alignment);
}

static void *RawInternalRealloc(void *ptr, uptr size,
                                InternalAllocatorCache *cache) {
  uptr alignment = 8;
  if (cache == 0) {
    SpinMutexLock l(&internal_allocator_cache_mu);
    return internal_allocator()->Reallocate(&internal_allocator_cache, ptr,
                                            size, alignment);
  }
  return internal_allocator()->Reallocate(cache, ptr, size, alignment);
}

static void RawInternalFree(void *ptr, InternalAllocatorCache *cache) {
  if (!cache) {
    SpinMutexLock l(&internal_allocator_cache_mu);
    return internal_allocator()->Deallocate(&internal_allocator_cache, ptr);
  }
  internal_allocator()->Deallocate(cache, ptr);
}

static void NORETURN ReportInternalAllocatorOutOfMemory(uptr requested_size) {
  SetAllocatorOutOfMemory();
  Report("FATAL: %s: internal allocator is out of memory trying to allocate "
         "0x%zx bytes\n", SanitizerToolName, requested_size);
  Die();
}

void *InternalAlloc(uptr size, InternalAllocatorCache *cache, uptr alignment) {
  void *p = RawInternalAlloc(size, cache, alignment);
  if (UNLIKELY(!p))
    ReportInternalAllocatorOutOfMemory(size);
  return p;
}

void *InternalRealloc(void *addr, uptr size, InternalAllocatorCache *cache) {
  void *p = RawInternalRealloc(addr, size, cache);
  if (UNLIKELY(!p))
    ReportInternalAllocatorOutOfMemory(size);
  return p;
}

void *InternalReallocArray(void *addr, uptr count, uptr size,
                           InternalAllocatorCache *cache) {
  if (UNLIKELY(CheckForCallocOverflow(count, size))) {
    Report(
        "FATAL: %s: reallocarray parameters overflow: count * size (%zd * %zd) "
        "cannot be represented in type size_t\n",
        SanitizerToolName, count, size);
    Die();
  }
  return InternalRealloc(addr, count * size, cache);
}

void *InternalCalloc(uptr count, uptr size, InternalAllocatorCache *cache) {
  if (UNLIKELY(CheckForCallocOverflow(count, size))) {
    Report("FATAL: %s: calloc parameters overflow: count * size (%zd * %zd) "
           "cannot be represented in type size_t\n", SanitizerToolName, count,
           size);
    Die();
  }
  void *p = InternalAlloc(count * size, cache);
  if (LIKELY(p))
    internal_memset(p, 0, count * size);
  return p;
}

void InternalFree(void *addr, InternalAllocatorCache *cache) {
  RawInternalFree(addr, cache);
}

void InternalAllocatorLock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  internal_allocator_cache_mu.Lock();
  internal_allocator()->ForceLock();
}

void InternalAllocatorUnlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  internal_allocator()->ForceUnlock();
  internal_allocator_cache_mu.Unlock();
}

// LowLevelAllocator
constexpr uptr kLowLevelAllocatorDefaultAlignment = 8;
constexpr uptr kMinNumPagesRounded = 16;
constexpr uptr kMinRoundedSize = 65536;
static uptr low_level_alloc_min_alignment = kLowLevelAllocatorDefaultAlignment;
static LowLevelAllocateCallback low_level_alloc_callback;

static LowLevelAllocator Alloc;
LowLevelAllocator &GetGlobalLowLevelAllocator() { return Alloc; }

void *LowLevelAllocator::Allocate(uptr size) {
  // Align allocation size.
  size = RoundUpTo(size, low_level_alloc_min_alignment);
  if (allocated_end_ - allocated_current_ < (sptr)size) {
    uptr size_to_allocate = RoundUpTo(
        size, Min(GetPageSizeCached() * kMinNumPagesRounded, kMinRoundedSize));
    allocated_current_ = (char *)MmapOrDie(size_to_allocate, __func__);
    allocated_end_ = allocated_current_ + size_to_allocate;
    if (low_level_alloc_callback) {
      low_level_alloc_callback((uptr)allocated_current_, size_to_allocate);
    }
  }
  CHECK(allocated_end_ - allocated_current_ >= (sptr)size);
  void *res = allocated_current_;
  allocated_current_ += size;
  return res;
}

void SetLowLevelAllocateMinAlignment(uptr alignment) {
  CHECK(IsPowerOfTwo(alignment));
  low_level_alloc_min_alignment = Max(alignment, low_level_alloc_min_alignment);
}

void SetLowLevelAllocateCallback(LowLevelAllocateCallback callback) {
  low_level_alloc_callback = callback;
}

// Allocator's OOM and other errors handling support.

static atomic_uint8_t allocator_out_of_memory = {0};
static atomic_uint8_t allocator_may_return_null = {0};

bool IsAllocatorOutOfMemory() {
  return atomic_load_relaxed(&allocator_out_of_memory);
}

void SetAllocatorOutOfMemory() {
  atomic_store_relaxed(&allocator_out_of_memory, 1);
}

bool AllocatorMayReturnNull() {
  return atomic_load(&allocator_may_return_null, memory_order_relaxed);
}

void SetAllocatorMayReturnNull(bool may_return_null) {
  atomic_store(&allocator_may_return_null, may_return_null,
               memory_order_relaxed);
}

void PrintHintAllocatorCannotReturnNull() {
  Report("HINT: if you don't care about these errors you may set "
         "allocator_may_return_null=1\n");
}

static atomic_uint8_t rss_limit_exceeded;

bool IsRssLimitExceeded() {
  return atomic_load(&rss_limit_exceeded, memory_order_relaxed);
}

void SetRssLimitExceeded(bool limit_exceeded) {
  atomic_store(&rss_limit_exceeded, limit_exceeded, memory_order_relaxed);
}

} // namespace __sanitizer
