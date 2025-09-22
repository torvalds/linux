//=-- lsan_allocator.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// See lsan_allocator.h for details.
//
//===----------------------------------------------------------------------===//

#include "lsan_allocator.h"

#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "lsan_common.h"

extern "C" void *memset(void *ptr, int value, uptr num);

namespace __lsan {
#if defined(__i386__) || defined(__arm__)
static const uptr kMaxAllowedMallocSize = 1ULL << 30;
#elif defined(__mips64) || defined(__aarch64__)
static const uptr kMaxAllowedMallocSize = 4ULL << 30;
#else
static const uptr kMaxAllowedMallocSize = 1ULL << 40;
#endif

static Allocator allocator;

static uptr max_malloc_size;

void InitializeAllocator() {
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
  allocator.InitLinkerInitialized(
      common_flags()->allocator_release_to_os_interval_ms);
  if (common_flags()->max_allocation_size_mb)
    max_malloc_size = Min(common_flags()->max_allocation_size_mb << 20,
                          kMaxAllowedMallocSize);
  else
    max_malloc_size = kMaxAllowedMallocSize;
}

void AllocatorThreadStart() { allocator.InitCache(GetAllocatorCache()); }

void AllocatorThreadFinish() {
  allocator.SwallowCache(GetAllocatorCache());
  allocator.DestroyCache(GetAllocatorCache());
}

static ChunkMetadata *Metadata(const void *p) {
  return reinterpret_cast<ChunkMetadata *>(allocator.GetMetaData(p));
}

static void RegisterAllocation(const StackTrace &stack, void *p, uptr size) {
  if (!p) return;
  ChunkMetadata *m = Metadata(p);
  CHECK(m);
  m->tag = DisabledInThisThread() ? kIgnored : kDirectlyLeaked;
  m->stack_trace_id = StackDepotPut(stack);
  m->requested_size = size;
  atomic_store(reinterpret_cast<atomic_uint8_t *>(m), 1, memory_order_relaxed);
  RunMallocHooks(p, size);
}

static void RegisterDeallocation(void *p) {
  if (!p) return;
  ChunkMetadata *m = Metadata(p);
  CHECK(m);
  RunFreeHooks(p);
  atomic_store(reinterpret_cast<atomic_uint8_t *>(m), 0, memory_order_relaxed);
}

static void *ReportAllocationSizeTooBig(uptr size, const StackTrace &stack) {
  if (AllocatorMayReturnNull()) {
    Report("WARNING: LeakSanitizer failed to allocate 0x%zx bytes\n", size);
    return nullptr;
  }
  ReportAllocationSizeTooBig(size, max_malloc_size, &stack);
}

void *Allocate(const StackTrace &stack, uptr size, uptr alignment,
               bool cleared) {
  if (size == 0)
    size = 1;
  if (size > max_malloc_size)
    return ReportAllocationSizeTooBig(size, stack);
  if (UNLIKELY(IsRssLimitExceeded())) {
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportRssLimitExceeded(&stack);
  }
  void *p = allocator.Allocate(GetAllocatorCache(), size, alignment);
  if (UNLIKELY(!p)) {
    SetAllocatorOutOfMemory();
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportOutOfMemory(size, &stack);
  }
  // Do not rely on the allocator to clear the memory (it's slow).
  if (cleared && allocator.FromPrimary(p))
    memset(p, 0, size);
  RegisterAllocation(stack, p, size);
  return p;
}

static void *Calloc(uptr nmemb, uptr size, const StackTrace &stack) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportCallocOverflow(nmemb, size, &stack);
  }
  size *= nmemb;
  return Allocate(stack, size, 1, true);
}

void Deallocate(void *p) {
  RegisterDeallocation(p);
  allocator.Deallocate(GetAllocatorCache(), p);
}

void *Reallocate(const StackTrace &stack, void *p, uptr new_size,
                 uptr alignment) {
  if (new_size > max_malloc_size) {
    ReportAllocationSizeTooBig(new_size, stack);
    return nullptr;
  }
  RegisterDeallocation(p);
  void *new_p =
      allocator.Reallocate(GetAllocatorCache(), p, new_size, alignment);
  if (new_p)
    RegisterAllocation(stack, new_p, new_size);
  else if (new_size != 0)
    RegisterAllocation(stack, p, new_size);
  return new_p;
}

void GetAllocatorCacheRange(uptr *begin, uptr *end) {
  *begin = (uptr)GetAllocatorCache();
  *end = *begin + sizeof(AllocatorCache);
}

static const void *GetMallocBegin(const void *p) {
  if (!p)
    return nullptr;
  void *beg = allocator.GetBlockBegin(p);
  if (!beg)
    return nullptr;
  ChunkMetadata *m = Metadata(beg);
  if (!m)
    return nullptr;
  if (!m->allocated)
    return nullptr;
  if (m->requested_size == 0)
    return nullptr;
  return (const void *)beg;
}

uptr GetMallocUsableSize(const void *p) {
  if (!p)
    return 0;
  ChunkMetadata *m = Metadata(p);
  if (!m) return 0;
  return m->requested_size;
}

uptr GetMallocUsableSizeFast(const void *p) {
  return Metadata(p)->requested_size;
}

int lsan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        const StackTrace &stack) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    ReportInvalidPosixMemalignAlignment(alignment, &stack);
  }
  void *ptr = Allocate(stack, size, alignment, kAlwaysClearMemory);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by Allocate.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

void *lsan_aligned_alloc(uptr alignment, uptr size, const StackTrace &stack) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAlignedAllocAlignment(size, alignment, &stack);
  }
  return SetErrnoOnNull(Allocate(stack, size, alignment, kAlwaysClearMemory));
}

void *lsan_memalign(uptr alignment, uptr size, const StackTrace &stack) {
  if (UNLIKELY(!IsPowerOfTwo(alignment))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAllocationAlignment(alignment, &stack);
  }
  return SetErrnoOnNull(Allocate(stack, size, alignment, kAlwaysClearMemory));
}

void *lsan_malloc(uptr size, const StackTrace &stack) {
  return SetErrnoOnNull(Allocate(stack, size, 1, kAlwaysClearMemory));
}

void lsan_free(void *p) {
  Deallocate(p);
}

void *lsan_realloc(void *p, uptr size, const StackTrace &stack) {
  return SetErrnoOnNull(Reallocate(stack, p, size, 1));
}

void *lsan_reallocarray(void *ptr, uptr nmemb, uptr size,
                        const StackTrace &stack) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportReallocArrayOverflow(nmemb, size, &stack);
  }
  return lsan_realloc(ptr, nmemb * size, stack);
}

void *lsan_calloc(uptr nmemb, uptr size, const StackTrace &stack) {
  return SetErrnoOnNull(Calloc(nmemb, size, stack));
}

void *lsan_valloc(uptr size, const StackTrace &stack) {
  return SetErrnoOnNull(
      Allocate(stack, size, GetPageSizeCached(), kAlwaysClearMemory));
}

void *lsan_pvalloc(uptr size, const StackTrace &stack) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(size, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportPvallocOverflow(size, &stack);
  }
  // pvalloc(0) should allocate one page.
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return SetErrnoOnNull(Allocate(stack, size, PageSize, kAlwaysClearMemory));
}

uptr lsan_mz_size(const void *p) {
  return GetMallocUsableSize(p);
}

///// Interface to the common LSan module. /////

void LockAllocator() {
  allocator.ForceLock();
}

void UnlockAllocator() {
  allocator.ForceUnlock();
}

void GetAllocatorGlobalRange(uptr *begin, uptr *end) {
  *begin = (uptr)&allocator;
  *end = *begin + sizeof(allocator);
}

uptr PointsIntoChunk(void* p) {
  uptr addr = reinterpret_cast<uptr>(p);
  uptr chunk = reinterpret_cast<uptr>(allocator.GetBlockBeginFastLocked(p));
  if (!chunk) return 0;
  // LargeMmapAllocator considers pointers to the meta-region of a chunk to be
  // valid, but we don't want that.
  if (addr < chunk) return 0;
  ChunkMetadata *m = Metadata(reinterpret_cast<void *>(chunk));
  CHECK(m);
  if (!m->allocated)
    return 0;
  if (addr < chunk + m->requested_size)
    return chunk;
  if (IsSpecialCaseOfOperatorNew0(chunk, m->requested_size, addr))
    return chunk;
  return 0;
}

uptr GetUserBegin(uptr chunk) {
  return chunk;
}

uptr GetUserAddr(uptr chunk) {
  return chunk;
}

LsanMetadata::LsanMetadata(uptr chunk) {
  metadata_ = Metadata(reinterpret_cast<void *>(chunk));
  CHECK(metadata_);
}

bool LsanMetadata::allocated() const {
  return reinterpret_cast<ChunkMetadata *>(metadata_)->allocated;
}

ChunkTag LsanMetadata::tag() const {
  return reinterpret_cast<ChunkMetadata *>(metadata_)->tag;
}

void LsanMetadata::set_tag(ChunkTag value) {
  reinterpret_cast<ChunkMetadata *>(metadata_)->tag = value;
}

uptr LsanMetadata::requested_size() const {
  return reinterpret_cast<ChunkMetadata *>(metadata_)->requested_size;
}

u32 LsanMetadata::stack_trace_id() const {
  return reinterpret_cast<ChunkMetadata *>(metadata_)->stack_trace_id;
}

void ForEachChunk(ForEachChunkCallback callback, void *arg) {
  allocator.ForEachChunk(callback, arg);
}

IgnoreObjectResult IgnoreObject(const void *p) {
  void *chunk = allocator.GetBlockBegin(p);
  if (!chunk || p < chunk) return kIgnoreObjectInvalid;
  ChunkMetadata *m = Metadata(chunk);
  CHECK(m);
  if (m->allocated && (uptr)p < (uptr)chunk + m->requested_size) {
    if (m->tag == kIgnored)
      return kIgnoreObjectAlreadyIgnored;
    m->tag = kIgnored;
    return kIgnoreObjectSuccess;
  } else {
    return kIgnoreObjectInvalid;
  }
}

} // namespace __lsan

using namespace __lsan;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_current_allocated_bytes() {
  uptr stats[AllocatorStatCount];
  allocator.GetStats(stats);
  return stats[AllocatorStatAllocated];
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_heap_size() {
  uptr stats[AllocatorStatCount];
  allocator.GetStats(stats);
  return stats[AllocatorStatMapped];
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_free_bytes() { return 1; }

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_unmapped_bytes() { return 0; }

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_estimated_allocated_size(uptr size) { return size; }

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_get_ownership(const void *p) {
  return GetMallocBegin(p) != nullptr;
}

SANITIZER_INTERFACE_ATTRIBUTE
const void * __sanitizer_get_allocated_begin(const void *p) {
  return GetMallocBegin(p);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_allocated_size(const void *p) {
  return GetMallocUsableSize(p);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_allocated_size_fast(const void *p) {
  DCHECK_EQ(p, __sanitizer_get_allocated_begin(p));
  uptr ret = GetMallocUsableSizeFast(p);
  DCHECK_EQ(ret, __sanitizer_get_allocated_size(p));
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_purge_allocator() { allocator.ForceReleaseToOS(); }

} // extern "C"
