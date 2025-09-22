//===-- dfsan_allocator.cpp -------------------------- --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataflowSanitizer.
//
// DataflowSanitizer allocator.
//===----------------------------------------------------------------------===//

#include "dfsan_allocator.h"

#include "dfsan.h"
#include "dfsan_flags.h"
#include "dfsan_thread.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_errno.h"

namespace __dfsan {

struct Metadata {
  uptr requested_size;
};

struct DFsanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const { dfsan_set_label(0, (void *)p, size); }
  void OnMapSecondary(uptr p, uptr size, uptr user_begin,
                      uptr user_size) const {
    OnMap(p, size);
  }
  void OnUnmap(uptr p, uptr size) const { dfsan_set_label(0, (void *)p, size); }
};

// Note: to ensure that the allocator is compatible with the application memory
// layout (especially with high-entropy ASLR), kSpaceBeg and kSpaceSize must be
// duplicated as MappingDesc::ALLOCATOR in dfsan_platform.h.
#if defined(__aarch64__)
const uptr kAllocatorSpace = 0xE00000000000ULL;
#else
const uptr kAllocatorSpace = 0x700000000000ULL;
#endif
const uptr kMaxAllowedMallocSize = 1ULL << 40;

struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = 0x40000000000;  // 4T.
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef DFsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;

typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;

static Allocator allocator;
static AllocatorCache fallback_allocator_cache;
static StaticSpinMutex fallback_mutex;

static uptr max_malloc_size;

void dfsan_allocator_init() {
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
  allocator.Init(common_flags()->allocator_release_to_os_interval_ms);
  if (common_flags()->max_allocation_size_mb)
    max_malloc_size = Min(common_flags()->max_allocation_size_mb << 20,
                          kMaxAllowedMallocSize);
  else
    max_malloc_size = kMaxAllowedMallocSize;
}

AllocatorCache *GetAllocatorCache(DFsanThreadLocalMallocStorage *ms) {
  CHECK(ms);
  CHECK_LE(sizeof(AllocatorCache), sizeof(ms->allocator_cache));
  return reinterpret_cast<AllocatorCache *>(ms->allocator_cache);
}

void DFsanThreadLocalMallocStorage::CommitBack() {
  allocator.SwallowCache(GetAllocatorCache(this));
}

static void *DFsanAllocate(uptr size, uptr alignment, bool zeroise) {
  if (size > max_malloc_size) {
    if (AllocatorMayReturnNull()) {
      Report("WARNING: DataflowSanitizer failed to allocate 0x%zx bytes\n",
             size);
      return nullptr;
    }
    BufferedStackTrace stack;
    ReportAllocationSizeTooBig(size, max_malloc_size, &stack);
  }
  if (UNLIKELY(IsRssLimitExceeded())) {
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportRssLimitExceeded(&stack);
  }
  DFsanThread *t = GetCurrentThread();
  void *allocated;
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocated = allocator.Allocate(cache, size, alignment);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocated = allocator.Allocate(cache, size, alignment);
  }
  if (UNLIKELY(!allocated)) {
    SetAllocatorOutOfMemory();
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportOutOfMemory(size, &stack);
  }
  Metadata *meta =
      reinterpret_cast<Metadata *>(allocator.GetMetaData(allocated));
  meta->requested_size = size;
  if (zeroise) {
    internal_memset(allocated, 0, size);
    dfsan_set_label(0, allocated, size);
  } else if (flags().zero_in_malloc) {
    dfsan_set_label(0, allocated, size);
  }
  return allocated;
}

void dfsan_deallocate(void *p) {
  CHECK(p);
  Metadata *meta = reinterpret_cast<Metadata *>(allocator.GetMetaData(p));
  uptr size = meta->requested_size;
  meta->requested_size = 0;
  if (flags().zero_in_free)
    dfsan_set_label(0, p, size);
  DFsanThread *t = GetCurrentThread();
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocator.Deallocate(cache, p);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocator.Deallocate(cache, p);
  }
}

void *DFsanReallocate(void *old_p, uptr new_size, uptr alignment) {
  Metadata *meta = reinterpret_cast<Metadata *>(allocator.GetMetaData(old_p));
  uptr old_size = meta->requested_size;
  uptr actually_allocated_size = allocator.GetActuallyAllocatedSize(old_p);
  if (new_size <= actually_allocated_size) {
    // We are not reallocating here.
    meta->requested_size = new_size;
    if (new_size > old_size && flags().zero_in_malloc)
      dfsan_set_label(0, (char *)old_p + old_size, new_size - old_size);
    return old_p;
  }
  uptr memcpy_size = Min(new_size, old_size);
  void *new_p = DFsanAllocate(new_size, alignment, false /*zeroise*/);
  if (new_p) {
    dfsan_copy_memory(new_p, old_p, memcpy_size);
    dfsan_deallocate(old_p);
  }
  return new_p;
}

void *DFsanCalloc(uptr nmemb, uptr size) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportCallocOverflow(nmemb, size, &stack);
  }
  return DFsanAllocate(nmemb * size, sizeof(u64), true /*zeroise*/);
}

static const void *AllocationBegin(const void *p) {
  if (!p)
    return nullptr;
  void *beg = allocator.GetBlockBegin(p);
  if (!beg)
    return nullptr;
  Metadata *b = (Metadata *)allocator.GetMetaData(beg);
  if (!b)
    return nullptr;
  if (b->requested_size == 0)
    return nullptr;
  return (const void *)beg;
}

static uptr AllocationSize(const void *p) {
  if (!p)
    return 0;
  const void *beg = allocator.GetBlockBegin(p);
  if (beg != p)
    return 0;
  Metadata *b = (Metadata *)allocator.GetMetaData(p);
  return b->requested_size;
}

static uptr AllocationSizeFast(const void *p) {
  return reinterpret_cast<Metadata *>(allocator.GetMetaData(p))->requested_size;
}

void *dfsan_malloc(uptr size) {
  return SetErrnoOnNull(DFsanAllocate(size, sizeof(u64), false /*zeroise*/));
}

void *dfsan_calloc(uptr nmemb, uptr size) {
  return SetErrnoOnNull(DFsanCalloc(nmemb, size));
}

void *dfsan_realloc(void *ptr, uptr size) {
  if (!ptr)
    return SetErrnoOnNull(DFsanAllocate(size, sizeof(u64), false /*zeroise*/));
  if (size == 0) {
    dfsan_deallocate(ptr);
    return nullptr;
  }
  return SetErrnoOnNull(DFsanReallocate(ptr, size, sizeof(u64)));
}

void *dfsan_reallocarray(void *ptr, uptr nmemb, uptr size) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportReallocArrayOverflow(nmemb, size, &stack);
  }
  return dfsan_realloc(ptr, nmemb * size);
}

void *dfsan_valloc(uptr size) {
  return SetErrnoOnNull(
      DFsanAllocate(size, GetPageSizeCached(), false /*zeroise*/));
}

void *dfsan_pvalloc(uptr size) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(size, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportPvallocOverflow(size, &stack);
  }
  // pvalloc(0) should allocate one page.
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return SetErrnoOnNull(DFsanAllocate(size, PageSize, false /*zeroise*/));
}

void *dfsan_aligned_alloc(uptr alignment, uptr size) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportInvalidAlignedAllocAlignment(size, alignment, &stack);
  }
  return SetErrnoOnNull(DFsanAllocate(size, alignment, false /*zeroise*/));
}

void *dfsan_memalign(uptr alignment, uptr size) {
  if (UNLIKELY(!IsPowerOfTwo(alignment))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    BufferedStackTrace stack;
    ReportInvalidAllocationAlignment(alignment, &stack);
  }
  return SetErrnoOnNull(DFsanAllocate(size, alignment, false /*zeroise*/));
}

int dfsan_posix_memalign(void **memptr, uptr alignment, uptr size) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    BufferedStackTrace stack;
    ReportInvalidPosixMemalignAlignment(alignment, &stack);
  }
  void *ptr = DFsanAllocate(size, alignment, false /*zeroise*/);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by DFsanAllocate.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

}  // namespace __dfsan

using namespace __dfsan;

uptr __sanitizer_get_current_allocated_bytes() {
  uptr stats[AllocatorStatCount];
  allocator.GetStats(stats);
  return stats[AllocatorStatAllocated];
}

uptr __sanitizer_get_heap_size() {
  uptr stats[AllocatorStatCount];
  allocator.GetStats(stats);
  return stats[AllocatorStatMapped];
}

uptr __sanitizer_get_free_bytes() { return 1; }

uptr __sanitizer_get_unmapped_bytes() { return 1; }

uptr __sanitizer_get_estimated_allocated_size(uptr size) { return size; }

int __sanitizer_get_ownership(const void *p) { return AllocationSize(p) != 0; }

const void *__sanitizer_get_allocated_begin(const void *p) {
  return AllocationBegin(p);
}

uptr __sanitizer_get_allocated_size(const void *p) { return AllocationSize(p); }

uptr __sanitizer_get_allocated_size_fast(const void *p) {
  DCHECK_EQ(p, __sanitizer_get_allocated_begin(p));
  uptr ret = AllocationSizeFast(p);
  DCHECK_EQ(ret, __sanitizer_get_allocated_size(p));
  return ret;
}
