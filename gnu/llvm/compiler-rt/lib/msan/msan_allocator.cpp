//===-- msan_allocator.cpp -------------------------- ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// MemorySanitizer allocator.
//===----------------------------------------------------------------------===//

#include "msan_allocator.h"

#include "msan.h"
#include "msan_interface_internal.h"
#include "msan_origin.h"
#include "msan_poisoning.h"
#include "msan_thread.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_errno.h"

namespace __msan {

struct Metadata {
  uptr requested_size;
};

struct MsanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const {}
  void OnMapSecondary(uptr p, uptr size, uptr user_begin,
                      uptr user_size) const {}
  void OnUnmap(uptr p, uptr size) const {
    __msan_unpoison((void *)p, size);

    // We are about to unmap a chunk of user memory.
    // Mark the corresponding shadow memory as not needed.
    uptr shadow_p = MEM_TO_SHADOW(p);
    ReleaseMemoryPagesToOS(shadow_p, shadow_p + size);
    if (__msan_get_track_origins()) {
      uptr origin_p = MEM_TO_ORIGIN(p);
      ReleaseMemoryPagesToOS(origin_p, origin_p + size);
    }
  }
};

// Note: to ensure that the allocator is compatible with the application memory
// layout (especially with high-entropy ASLR), kSpaceBeg and kSpaceSize must be
// duplicated as MappingDesc::ALLOCATOR in msan.h.
#if defined(__mips64)
static const uptr kMaxAllowedMallocSize = 2UL << 30;

struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef __sanitizer::CompactSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = 20;
  using AddressSpaceView = LocalAddressSpaceView;
  typedef MsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
typedef SizeClassAllocator32<AP32> PrimaryAllocator;
#elif defined(__x86_64__)
#if SANITIZER_NETBSD || SANITIZER_LINUX
static const uptr kAllocatorSpace = 0x700000000000ULL;
#else
static const uptr kAllocatorSpace = 0x600000000000ULL;
#endif
static const uptr kMaxAllowedMallocSize = 1ULL << 40;

struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = 0x40000000000;  // 4T.
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef MsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;

#elif defined(__loongarch_lp64)
const uptr kAllocatorSpace = 0x700000000000ULL;
const uptr kMaxAllowedMallocSize = 8UL << 30;

struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = 0x40000000000;  // 4T.
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef MsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;

#elif defined(__powerpc64__)
static const uptr kMaxAllowedMallocSize = 2UL << 30;  // 2G

struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = 0x300000000000;
  static const uptr kSpaceSize = 0x020000000000;  // 2T.
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef MsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;
#elif defined(__s390x__)
static const uptr kMaxAllowedMallocSize = 2UL << 30;  // 2G

struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = 0x440000000000;
  static const uptr kSpaceSize = 0x020000000000;  // 2T.
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef MsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;
#elif defined(__aarch64__)
static const uptr kMaxAllowedMallocSize = 8UL << 30;

struct AP64 {
  static const uptr kSpaceBeg = 0xE00000000000ULL;
  static const uptr kSpaceSize = 0x40000000000;  // 4T.
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef MsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};
typedef SizeClassAllocator64<AP64> PrimaryAllocator;
#endif
typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;

static Allocator allocator;
static AllocatorCache fallback_allocator_cache;
static StaticSpinMutex fallback_mutex;

static uptr max_malloc_size;

void MsanAllocatorInit() {
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
  allocator.Init(common_flags()->allocator_release_to_os_interval_ms);
  if (common_flags()->max_allocation_size_mb)
    max_malloc_size = Min(common_flags()->max_allocation_size_mb << 20,
                          kMaxAllowedMallocSize);
  else
    max_malloc_size = kMaxAllowedMallocSize;
}

void LockAllocator() { allocator.ForceLock(); }

void UnlockAllocator() { allocator.ForceUnlock(); }

AllocatorCache *GetAllocatorCache(MsanThreadLocalMallocStorage *ms) {
  CHECK(ms);
  CHECK_LE(sizeof(AllocatorCache), sizeof(ms->allocator_cache));
  return reinterpret_cast<AllocatorCache *>(ms->allocator_cache);
}

void MsanThreadLocalMallocStorage::Init() {
  allocator.InitCache(GetAllocatorCache(this));
}

void MsanThreadLocalMallocStorage::CommitBack() {
  allocator.SwallowCache(GetAllocatorCache(this));
  allocator.DestroyCache(GetAllocatorCache(this));
}

static void *MsanAllocate(BufferedStackTrace *stack, uptr size, uptr alignment,
                          bool zeroise) {
  if (UNLIKELY(size > max_malloc_size)) {
    if (AllocatorMayReturnNull()) {
      Report("WARNING: MemorySanitizer failed to allocate 0x%zx bytes\n", size);
      return nullptr;
    }
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportAllocationSizeTooBig(size, max_malloc_size, stack);
  }
  if (UNLIKELY(IsRssLimitExceeded())) {
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportRssLimitExceeded(stack);
  }
  MsanThread *t = GetCurrentThread();
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
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportOutOfMemory(size, stack);
  }
  Metadata *meta =
      reinterpret_cast<Metadata *>(allocator.GetMetaData(allocated));
  meta->requested_size = size;
  if (zeroise) {
    if (allocator.FromPrimary(allocated))
      __msan_clear_and_unpoison(allocated, size);
    else
      __msan_unpoison(allocated, size);  // Mem is already zeroed.
  } else if (flags()->poison_in_malloc) {
    __msan_poison(allocated, size);
    if (__msan_get_track_origins()) {
      stack->tag = StackTrace::TAG_ALLOC;
      Origin o = Origin::CreateHeapOrigin(stack);
      __msan_set_origin(allocated, size, o.raw_id());
    }
  }
  UnpoisonParam(2);
  RunMallocHooks(allocated, size);
  return allocated;
}

void MsanDeallocate(BufferedStackTrace *stack, void *p) {
  CHECK(p);
  UnpoisonParam(1);
  RunFreeHooks(p);

  Metadata *meta = reinterpret_cast<Metadata *>(allocator.GetMetaData(p));
  uptr size = meta->requested_size;
  meta->requested_size = 0;
  // This memory will not be reused by anyone else, so we are free to keep it
  // poisoned. The secondary allocator will unmap and unpoison by
  // MsanMapUnmapCallback, no need to poison it here.
  if (flags()->poison_in_free && allocator.FromPrimary(p)) {
    __msan_poison(p, size);
    if (__msan_get_track_origins()) {
      stack->tag = StackTrace::TAG_DEALLOC;
      Origin o = Origin::CreateHeapOrigin(stack);
      __msan_set_origin(p, size, o.raw_id());
    }
  }
  MsanThread *t = GetCurrentThread();
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocator.Deallocate(cache, p);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocator.Deallocate(cache, p);
  }
}

static void *MsanReallocate(BufferedStackTrace *stack, void *old_p,
                            uptr new_size, uptr alignment) {
  Metadata *meta = reinterpret_cast<Metadata*>(allocator.GetMetaData(old_p));
  uptr old_size = meta->requested_size;
  uptr actually_allocated_size = allocator.GetActuallyAllocatedSize(old_p);
  if (new_size <= actually_allocated_size) {
    // We are not reallocating here.
    meta->requested_size = new_size;
    if (new_size > old_size) {
      if (flags()->poison_in_malloc) {
        stack->tag = StackTrace::TAG_ALLOC;
        PoisonMemory((char *)old_p + old_size, new_size - old_size, stack);
      }
    }
    return old_p;
  }
  uptr memcpy_size = Min(new_size, old_size);
  void *new_p = MsanAllocate(stack, new_size, alignment, false /*zeroise*/);
  if (new_p) {
    CopyMemory(new_p, old_p, memcpy_size, stack);
    MsanDeallocate(stack, old_p);
  }
  return new_p;
}

static void *MsanCalloc(BufferedStackTrace *stack, uptr nmemb, uptr size) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportCallocOverflow(nmemb, size, stack);
  }
  return MsanAllocate(stack, nmemb * size, sizeof(u64), true);
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
  if (!p) return 0;
  const void *beg = allocator.GetBlockBegin(p);
  if (beg != p) return 0;
  Metadata *b = (Metadata *)allocator.GetMetaData(p);
  return b->requested_size;
}

static uptr AllocationSizeFast(const void *p) {
  return reinterpret_cast<Metadata *>(allocator.GetMetaData(p))->requested_size;
}

void *msan_malloc(uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(MsanAllocate(stack, size, sizeof(u64), false));
}

void *msan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(MsanCalloc(stack, nmemb, size));
}

void *msan_realloc(void *ptr, uptr size, BufferedStackTrace *stack) {
  if (!ptr)
    return SetErrnoOnNull(MsanAllocate(stack, size, sizeof(u64), false));
  if (size == 0) {
    MsanDeallocate(stack, ptr);
    return nullptr;
  }
  return SetErrnoOnNull(MsanReallocate(stack, ptr, size, sizeof(u64)));
}

void *msan_reallocarray(void *ptr, uptr nmemb, uptr size,
                        BufferedStackTrace *stack) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportReallocArrayOverflow(nmemb, size, stack);
  }
  return msan_realloc(ptr, nmemb * size, stack);
}

void *msan_valloc(uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(MsanAllocate(stack, size, GetPageSizeCached(), false));
}

void *msan_pvalloc(uptr size, BufferedStackTrace *stack) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(size, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportPvallocOverflow(size, stack);
  }
  // pvalloc(0) should allocate one page.
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return SetErrnoOnNull(MsanAllocate(stack, size, PageSize, false));
}

void *msan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportInvalidAlignedAllocAlignment(size, alignment, stack);
  }
  return SetErrnoOnNull(MsanAllocate(stack, size, alignment, false));
}

void *msan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack) {
  if (UNLIKELY(!IsPowerOfTwo(alignment))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportInvalidAllocationAlignment(alignment, stack);
  }
  return SetErrnoOnNull(MsanAllocate(stack, size, alignment, false));
}

int msan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    GET_FATAL_STACK_TRACE_IF_EMPTY(stack);
    ReportInvalidPosixMemalignAlignment(alignment, stack);
  }
  void *ptr = MsanAllocate(stack, size, alignment, false);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by MsanAllocate.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

} // namespace __msan

using namespace __msan;

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

void __sanitizer_purge_allocator() { allocator.ForceReleaseToOS(); }
