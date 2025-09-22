//===-- asan_allocator.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Implementation of ASan's memory allocator, 2-nd version.
// This variant uses the allocator from sanitizer_common, i.e. the one shared
// with ThreadSanitizer and MemorySanitizer.
//
//===----------------------------------------------------------------------===//

#include "asan_allocator.h"

#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_thread.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_list.h"
#include "sanitizer_common/sanitizer_quarantine.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __asan {

// Valid redzone sizes are 16, 32, 64, ... 2048, so we encode them in 3 bits.
// We use adaptive redzones: for larger allocation larger redzones are used.
static u32 RZLog2Size(u32 rz_log) {
  CHECK_LT(rz_log, 8);
  return 16 << rz_log;
}

static u32 RZSize2Log(u32 rz_size) {
  CHECK_GE(rz_size, 16);
  CHECK_LE(rz_size, 2048);
  CHECK(IsPowerOfTwo(rz_size));
  u32 res = Log2(rz_size) - 4;
  CHECK_EQ(rz_size, RZLog2Size(res));
  return res;
}

static AsanAllocator &get_allocator();

static void AtomicContextStore(volatile atomic_uint64_t *atomic_context,
                               u32 tid, u32 stack) {
  u64 context = tid;
  context <<= 32;
  context += stack;
  atomic_store(atomic_context, context, memory_order_relaxed);
}

static void AtomicContextLoad(const volatile atomic_uint64_t *atomic_context,
                              u32 &tid, u32 &stack) {
  u64 context = atomic_load(atomic_context, memory_order_relaxed);
  stack = context;
  context >>= 32;
  tid = context;
}

// The memory chunk allocated from the underlying allocator looks like this:
// L L L L L L H H U U U U U U R R
//   L -- left redzone words (0 or more bytes)
//   H -- ChunkHeader (16 bytes), which is also a part of the left redzone.
//   U -- user memory.
//   R -- right redzone (0 or more bytes)
// ChunkBase consists of ChunkHeader and other bytes that overlap with user
// memory.

// If the left redzone is greater than the ChunkHeader size we store a magic
// value in the first uptr word of the memory block and store the address of
// ChunkBase in the next uptr.
// M B L L L L L L L L L  H H U U U U U U
//   |                    ^
//   ---------------------|
//   M -- magic value kAllocBegMagic
//   B -- address of ChunkHeader pointing to the first 'H'

class ChunkHeader {
 public:
  atomic_uint8_t chunk_state;
  u8 alloc_type : 2;
  u8 lsan_tag : 2;

  // align < 8 -> 0
  // else      -> log2(min(align, 512)) - 2
  u8 user_requested_alignment_log : 3;

 private:
  u16 user_requested_size_hi;
  u32 user_requested_size_lo;
  atomic_uint64_t alloc_context_id;

 public:
  uptr UsedSize() const {
    static_assert(sizeof(user_requested_size_lo) == 4,
                  "Expression below requires this");
    return FIRST_32_SECOND_64(0, ((uptr)user_requested_size_hi << 32)) +
           user_requested_size_lo;
  }

  void SetUsedSize(uptr size) {
    user_requested_size_lo = size;
    static_assert(sizeof(user_requested_size_lo) == 4,
                  "Expression below requires this");
    user_requested_size_hi = FIRST_32_SECOND_64(0, size >> 32);
    CHECK_EQ(UsedSize(), size);
  }

  void SetAllocContext(u32 tid, u32 stack) {
    AtomicContextStore(&alloc_context_id, tid, stack);
  }

  void GetAllocContext(u32 &tid, u32 &stack) const {
    AtomicContextLoad(&alloc_context_id, tid, stack);
  }
};

class ChunkBase : public ChunkHeader {
  atomic_uint64_t free_context_id;

 public:
  void SetFreeContext(u32 tid, u32 stack) {
    AtomicContextStore(&free_context_id, tid, stack);
  }

  void GetFreeContext(u32 &tid, u32 &stack) const {
    AtomicContextLoad(&free_context_id, tid, stack);
  }
};

static const uptr kChunkHeaderSize = sizeof(ChunkHeader);
static const uptr kChunkHeader2Size = sizeof(ChunkBase) - kChunkHeaderSize;
COMPILER_CHECK(kChunkHeaderSize == 16);
COMPILER_CHECK(kChunkHeader2Size <= 16);

enum {
  // Either just allocated by underlying allocator, but AsanChunk is not yet
  // ready, or almost returned to undelying allocator and AsanChunk is already
  // meaningless.
  CHUNK_INVALID = 0,
  // The chunk is allocated and not yet freed.
  CHUNK_ALLOCATED = 2,
  // The chunk was freed and put into quarantine zone.
  CHUNK_QUARANTINE = 3,
};

class AsanChunk : public ChunkBase {
 public:
  uptr Beg() { return reinterpret_cast<uptr>(this) + kChunkHeaderSize; }
  bool AddrIsInside(uptr addr) {
    return (addr >= Beg()) && (addr < Beg() + UsedSize());
  }
};

class LargeChunkHeader {
  static constexpr uptr kAllocBegMagic =
      FIRST_32_SECOND_64(0xCC6E96B9, 0xCC6E96B9CC6E96B9ULL);
  atomic_uintptr_t magic;
  AsanChunk *chunk_header;

 public:
  AsanChunk *Get() const {
    return atomic_load(&magic, memory_order_acquire) == kAllocBegMagic
               ? chunk_header
               : nullptr;
  }

  void Set(AsanChunk *p) {
    if (p) {
      chunk_header = p;
      atomic_store(&magic, kAllocBegMagic, memory_order_release);
      return;
    }

    uptr old = kAllocBegMagic;
    if (!atomic_compare_exchange_strong(&magic, &old, 0,
                                        memory_order_release)) {
      CHECK_EQ(old, kAllocBegMagic);
    }
  }
};

static void FillChunk(AsanChunk *m) {
  // FIXME: Use ReleaseMemoryPagesToOS.
  Flags &fl = *flags();

  if (fl.max_free_fill_size > 0) {
    // We have to skip the chunk header, it contains free_context_id.
    uptr scribble_start = (uptr)m + kChunkHeaderSize + kChunkHeader2Size;
    if (m->UsedSize() >= kChunkHeader2Size) {  // Skip Header2 in user area.
      uptr size_to_fill = m->UsedSize() - kChunkHeader2Size;
      size_to_fill = Min(size_to_fill, (uptr)fl.max_free_fill_size);
      REAL(memset)((void *)scribble_start, fl.free_fill_byte, size_to_fill);
    }
  }
}

struct QuarantineCallback {
  QuarantineCallback(AllocatorCache *cache, BufferedStackTrace *stack)
      : cache_(cache),
        stack_(stack) {
  }

  void PreQuarantine(AsanChunk *m) const {
    FillChunk(m);
    // Poison the region.
    PoisonShadow(m->Beg(), RoundUpTo(m->UsedSize(), ASAN_SHADOW_GRANULARITY),
                 kAsanHeapFreeMagic);
  }

  void Recycle(AsanChunk *m) const {
    void *p = get_allocator().GetBlockBegin(m);

    // The secondary will immediately unpoison and unmap the memory, so this
    // branch is unnecessary.
    if (get_allocator().FromPrimary(p)) {
      if (p != m) {
        // Clear the magic value, as allocator internals may overwrite the
        // contents of deallocated chunk, confusing GetAsanChunk lookup.
        reinterpret_cast<LargeChunkHeader *>(p)->Set(nullptr);
      }

      u8 old_chunk_state = CHUNK_QUARANTINE;
      if (!atomic_compare_exchange_strong(&m->chunk_state, &old_chunk_state,
                                          CHUNK_INVALID,
                                          memory_order_acquire)) {
        CHECK_EQ(old_chunk_state, CHUNK_QUARANTINE);
      }

      PoisonShadow(m->Beg(), RoundUpTo(m->UsedSize(), ASAN_SHADOW_GRANULARITY),
                   kAsanHeapLeftRedzoneMagic);
    }

    // Statistics.
    AsanStats &thread_stats = GetCurrentThreadStats();
    thread_stats.real_frees++;
    thread_stats.really_freed += m->UsedSize();

    get_allocator().Deallocate(cache_, p);
  }

  void RecyclePassThrough(AsanChunk *m) const {
    // Recycle for the secondary will immediately unpoison and unmap the
    // memory, so quarantine preparation is unnecessary.
    if (get_allocator().FromPrimary(m)) {
      // The primary allocation may need pattern fill if enabled.
      FillChunk(m);
    }
    Recycle(m);
  }

  void *Allocate(uptr size) const {
    void *res = get_allocator().Allocate(cache_, size, 1);
    // TODO(alekseys): Consider making quarantine OOM-friendly.
    if (UNLIKELY(!res))
      ReportOutOfMemory(size, stack_);
    return res;
  }

  void Deallocate(void *p) const { get_allocator().Deallocate(cache_, p); }

 private:
  AllocatorCache* const cache_;
  BufferedStackTrace* const stack_;
};

typedef Quarantine<QuarantineCallback, AsanChunk> AsanQuarantine;
typedef AsanQuarantine::Cache QuarantineCache;

void AsanMapUnmapCallback::OnMap(uptr p, uptr size) const {
  PoisonShadow(p, size, kAsanHeapLeftRedzoneMagic);
  // Statistics.
  AsanStats &thread_stats = GetCurrentThreadStats();
  thread_stats.mmaps++;
  thread_stats.mmaped += size;
}

void AsanMapUnmapCallback::OnMapSecondary(uptr p, uptr size, uptr user_begin,
                                          uptr user_size) const {
  uptr user_end = RoundDownTo(user_begin + user_size, ASAN_SHADOW_GRANULARITY);
  user_begin = RoundUpTo(user_begin, ASAN_SHADOW_GRANULARITY);
  // The secondary mapping will be immediately returned to user, no value
  // poisoning that with non-zero just before unpoisoning by Allocate(). So just
  // poison head/tail invisible to Allocate().
  PoisonShadow(p, user_begin - p, kAsanHeapLeftRedzoneMagic);
  PoisonShadow(user_end, size - (user_end - p), kAsanHeapLeftRedzoneMagic);
  // Statistics.
  AsanStats &thread_stats = GetCurrentThreadStats();
  thread_stats.mmaps++;
  thread_stats.mmaped += size;
}

void AsanMapUnmapCallback::OnUnmap(uptr p, uptr size) const {
  PoisonShadow(p, size, 0);
  // We are about to unmap a chunk of user memory.
  // Mark the corresponding shadow memory as not needed.
  FlushUnneededASanShadowMemory(p, size);
  // Statistics.
  AsanStats &thread_stats = GetCurrentThreadStats();
  thread_stats.munmaps++;
  thread_stats.munmaped += size;
}

// We can not use THREADLOCAL because it is not supported on some of the
// platforms we care about (OSX 10.6, Android).
// static THREADLOCAL AllocatorCache cache;
AllocatorCache *GetAllocatorCache(AsanThreadLocalMallocStorage *ms) {
  CHECK(ms);
  return &ms->allocator_cache;
}

QuarantineCache *GetQuarantineCache(AsanThreadLocalMallocStorage *ms) {
  CHECK(ms);
  CHECK_LE(sizeof(QuarantineCache), sizeof(ms->quarantine_cache));
  return reinterpret_cast<QuarantineCache *>(ms->quarantine_cache);
}

void AllocatorOptions::SetFrom(const Flags *f, const CommonFlags *cf) {
  quarantine_size_mb = f->quarantine_size_mb;
  thread_local_quarantine_size_kb = f->thread_local_quarantine_size_kb;
  min_redzone = f->redzone;
  max_redzone = f->max_redzone;
  may_return_null = cf->allocator_may_return_null;
  alloc_dealloc_mismatch = f->alloc_dealloc_mismatch;
  release_to_os_interval_ms = cf->allocator_release_to_os_interval_ms;
}

void AllocatorOptions::CopyTo(Flags *f, CommonFlags *cf) {
  f->quarantine_size_mb = quarantine_size_mb;
  f->thread_local_quarantine_size_kb = thread_local_quarantine_size_kb;
  f->redzone = min_redzone;
  f->max_redzone = max_redzone;
  cf->allocator_may_return_null = may_return_null;
  f->alloc_dealloc_mismatch = alloc_dealloc_mismatch;
  cf->allocator_release_to_os_interval_ms = release_to_os_interval_ms;
}

struct Allocator {
  static const uptr kMaxAllowedMallocSize =
      FIRST_32_SECOND_64(3UL << 30, 1ULL << 40);

  AsanAllocator allocator;
  AsanQuarantine quarantine;
  StaticSpinMutex fallback_mutex;
  AllocatorCache fallback_allocator_cache;
  QuarantineCache fallback_quarantine_cache;

  uptr max_user_defined_malloc_size;

  // ------------------- Options --------------------------
  atomic_uint16_t min_redzone;
  atomic_uint16_t max_redzone;
  atomic_uint8_t alloc_dealloc_mismatch;

  // ------------------- Initialization ------------------------
  explicit Allocator(LinkerInitialized)
      : quarantine(LINKER_INITIALIZED),
        fallback_quarantine_cache(LINKER_INITIALIZED) {}

  void CheckOptions(const AllocatorOptions &options) const {
    CHECK_GE(options.min_redzone, 16);
    CHECK_GE(options.max_redzone, options.min_redzone);
    CHECK_LE(options.max_redzone, 2048);
    CHECK(IsPowerOfTwo(options.min_redzone));
    CHECK(IsPowerOfTwo(options.max_redzone));
  }

  void SharedInitCode(const AllocatorOptions &options) {
    CheckOptions(options);
    quarantine.Init((uptr)options.quarantine_size_mb << 20,
                    (uptr)options.thread_local_quarantine_size_kb << 10);
    atomic_store(&alloc_dealloc_mismatch, options.alloc_dealloc_mismatch,
                 memory_order_release);
    atomic_store(&min_redzone, options.min_redzone, memory_order_release);
    atomic_store(&max_redzone, options.max_redzone, memory_order_release);
  }

  void InitLinkerInitialized(const AllocatorOptions &options) {
    SetAllocatorMayReturnNull(options.may_return_null);
    allocator.InitLinkerInitialized(options.release_to_os_interval_ms);
    SharedInitCode(options);
    max_user_defined_malloc_size = common_flags()->max_allocation_size_mb
                                       ? common_flags()->max_allocation_size_mb
                                             << 20
                                       : kMaxAllowedMallocSize;
  }

  void RePoisonChunk(uptr chunk) {
    // This could be a user-facing chunk (with redzones), or some internal
    // housekeeping chunk, like TransferBatch. Start by assuming the former.
    AsanChunk *ac = GetAsanChunk((void *)chunk);
    uptr allocated_size = allocator.GetActuallyAllocatedSize((void *)chunk);
    if (ac && atomic_load(&ac->chunk_state, memory_order_acquire) ==
                  CHUNK_ALLOCATED) {
      uptr beg = ac->Beg();
      uptr end = ac->Beg() + ac->UsedSize();
      uptr chunk_end = chunk + allocated_size;
      if (chunk < beg && beg < end && end <= chunk_end) {
        // Looks like a valid AsanChunk in use, poison redzones only.
        PoisonShadow(chunk, beg - chunk, kAsanHeapLeftRedzoneMagic);
        uptr end_aligned_down = RoundDownTo(end, ASAN_SHADOW_GRANULARITY);
        FastPoisonShadowPartialRightRedzone(
            end_aligned_down, end - end_aligned_down,
            chunk_end - end_aligned_down, kAsanHeapLeftRedzoneMagic);
        return;
      }
    }

    // This is either not an AsanChunk or freed or quarantined AsanChunk.
    // In either case, poison everything.
    PoisonShadow(chunk, allocated_size, kAsanHeapLeftRedzoneMagic);
  }

  void ReInitialize(const AllocatorOptions &options) {
    SetAllocatorMayReturnNull(options.may_return_null);
    allocator.SetReleaseToOSIntervalMs(options.release_to_os_interval_ms);
    SharedInitCode(options);

    // Poison all existing allocation's redzones.
    if (CanPoisonMemory()) {
      allocator.ForceLock();
      allocator.ForEachChunk(
          [](uptr chunk, void *alloc) {
            ((Allocator *)alloc)->RePoisonChunk(chunk);
          },
          this);
      allocator.ForceUnlock();
    }
  }

  void GetOptions(AllocatorOptions *options) const {
    options->quarantine_size_mb = quarantine.GetMaxSize() >> 20;
    options->thread_local_quarantine_size_kb =
        quarantine.GetMaxCacheSize() >> 10;
    options->min_redzone = atomic_load(&min_redzone, memory_order_acquire);
    options->max_redzone = atomic_load(&max_redzone, memory_order_acquire);
    options->may_return_null = AllocatorMayReturnNull();
    options->alloc_dealloc_mismatch =
        atomic_load(&alloc_dealloc_mismatch, memory_order_acquire);
    options->release_to_os_interval_ms = allocator.ReleaseToOSIntervalMs();
  }

  // -------------------- Helper methods. -------------------------
  uptr ComputeRZLog(uptr user_requested_size) {
    u32 rz_log = user_requested_size <= 64 - 16            ? 0
                 : user_requested_size <= 128 - 32         ? 1
                 : user_requested_size <= 512 - 64         ? 2
                 : user_requested_size <= 4096 - 128       ? 3
                 : user_requested_size <= (1 << 14) - 256  ? 4
                 : user_requested_size <= (1 << 15) - 512  ? 5
                 : user_requested_size <= (1 << 16) - 1024 ? 6
                                                           : 7;
    u32 hdr_log = RZSize2Log(RoundUpToPowerOfTwo(sizeof(ChunkHeader)));
    u32 min_log = RZSize2Log(atomic_load(&min_redzone, memory_order_acquire));
    u32 max_log = RZSize2Log(atomic_load(&max_redzone, memory_order_acquire));
    return Min(Max(rz_log, Max(min_log, hdr_log)), Max(max_log, hdr_log));
  }

  static uptr ComputeUserRequestedAlignmentLog(uptr user_requested_alignment) {
    if (user_requested_alignment < 8)
      return 0;
    if (user_requested_alignment > 512)
      user_requested_alignment = 512;
    return Log2(user_requested_alignment) - 2;
  }

  static uptr ComputeUserAlignment(uptr user_requested_alignment_log) {
    if (user_requested_alignment_log == 0)
      return 0;
    return 1LL << (user_requested_alignment_log + 2);
  }

  // We have an address between two chunks, and we want to report just one.
  AsanChunk *ChooseChunk(uptr addr, AsanChunk *left_chunk,
                         AsanChunk *right_chunk) {
    if (!left_chunk)
      return right_chunk;
    if (!right_chunk)
      return left_chunk;
    // Prefer an allocated chunk over freed chunk and freed chunk
    // over available chunk.
    u8 left_state = atomic_load(&left_chunk->chunk_state, memory_order_relaxed);
    u8 right_state =
        atomic_load(&right_chunk->chunk_state, memory_order_relaxed);
    if (left_state != right_state) {
      if (left_state == CHUNK_ALLOCATED)
        return left_chunk;
      if (right_state == CHUNK_ALLOCATED)
        return right_chunk;
      if (left_state == CHUNK_QUARANTINE)
        return left_chunk;
      if (right_state == CHUNK_QUARANTINE)
        return right_chunk;
    }
    // Same chunk_state: choose based on offset.
    sptr l_offset = 0, r_offset = 0;
    CHECK(AsanChunkView(left_chunk).AddrIsAtRight(addr, 1, &l_offset));
    CHECK(AsanChunkView(right_chunk).AddrIsAtLeft(addr, 1, &r_offset));
    if (l_offset < r_offset)
      return left_chunk;
    return right_chunk;
  }

  bool UpdateAllocationStack(uptr addr, BufferedStackTrace *stack) {
    AsanChunk *m = GetAsanChunkByAddr(addr);
    if (!m) return false;
    if (atomic_load(&m->chunk_state, memory_order_acquire) != CHUNK_ALLOCATED)
      return false;
    if (m->Beg() != addr) return false;
    AsanThread *t = GetCurrentThread();
    m->SetAllocContext(t ? t->tid() : kMainTid, StackDepotPut(*stack));
    return true;
  }

  // -------------------- Allocation/Deallocation routines ---------------
  void *Allocate(uptr size, uptr alignment, BufferedStackTrace *stack,
                 AllocType alloc_type, bool can_fill) {
    if (UNLIKELY(!AsanInited()))
      AsanInitFromRtl();
    if (UNLIKELY(IsRssLimitExceeded())) {
      if (AllocatorMayReturnNull())
        return nullptr;
      ReportRssLimitExceeded(stack);
    }
    Flags &fl = *flags();
    CHECK(stack);
    const uptr min_alignment = ASAN_SHADOW_GRANULARITY;
    const uptr user_requested_alignment_log =
        ComputeUserRequestedAlignmentLog(alignment);
    if (alignment < min_alignment)
      alignment = min_alignment;
    if (size == 0) {
      // We'd be happy to avoid allocating memory for zero-size requests, but
      // some programs/tests depend on this behavior and assume that malloc
      // would not return NULL even for zero-size allocations. Moreover, it
      // looks like operator new should never return NULL, and results of
      // consecutive "new" calls must be different even if the allocated size
      // is zero.
      size = 1;
    }
    CHECK(IsPowerOfTwo(alignment));
    uptr rz_log = ComputeRZLog(size);
    uptr rz_size = RZLog2Size(rz_log);
    uptr rounded_size = RoundUpTo(Max(size, kChunkHeader2Size), alignment);
    uptr needed_size = rounded_size + rz_size;
    if (alignment > min_alignment)
      needed_size += alignment;
    bool from_primary = PrimaryAllocator::CanAllocate(needed_size, alignment);
    // If we are allocating from the secondary allocator, there will be no
    // automatic right redzone, so add the right redzone manually.
    if (!from_primary)
      needed_size += rz_size;
    CHECK(IsAligned(needed_size, min_alignment));
    if (size > kMaxAllowedMallocSize || needed_size > kMaxAllowedMallocSize ||
        size > max_user_defined_malloc_size) {
      if (AllocatorMayReturnNull()) {
        Report("WARNING: AddressSanitizer failed to allocate 0x%zx bytes\n",
               size);
        return nullptr;
      }
      uptr malloc_limit =
          Min(kMaxAllowedMallocSize, max_user_defined_malloc_size);
      ReportAllocationSizeTooBig(size, needed_size, malloc_limit, stack);
    }

    AsanThread *t = GetCurrentThread();
    void *allocated;
    if (t) {
      AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
      allocated = allocator.Allocate(cache, needed_size, 8);
    } else {
      SpinMutexLock l(&fallback_mutex);
      AllocatorCache *cache = &fallback_allocator_cache;
      allocated = allocator.Allocate(cache, needed_size, 8);
    }
    if (UNLIKELY(!allocated)) {
      SetAllocatorOutOfMemory();
      if (AllocatorMayReturnNull())
        return nullptr;
      ReportOutOfMemory(size, stack);
    }

    uptr alloc_beg = reinterpret_cast<uptr>(allocated);
    uptr alloc_end = alloc_beg + needed_size;
    uptr user_beg = alloc_beg + rz_size;
    if (!IsAligned(user_beg, alignment))
      user_beg = RoundUpTo(user_beg, alignment);
    uptr user_end = user_beg + size;
    CHECK_LE(user_end, alloc_end);
    uptr chunk_beg = user_beg - kChunkHeaderSize;
    AsanChunk *m = reinterpret_cast<AsanChunk *>(chunk_beg);
    m->alloc_type = alloc_type;
    CHECK(size);
    m->SetUsedSize(size);
    m->user_requested_alignment_log = user_requested_alignment_log;

    m->SetAllocContext(t ? t->tid() : kMainTid, StackDepotPut(*stack));

    if (!from_primary || *(u8 *)MEM_TO_SHADOW((uptr)allocated) == 0) {
      // The allocator provides an unpoisoned chunk. This is possible for the
      // secondary allocator, or if CanPoisonMemory() was false for some time,
      // for example, due to flags()->start_disabled. Anyway, poison left and
      // right of the block before using it for anything else.
      uptr tail_beg = RoundUpTo(user_end, ASAN_SHADOW_GRANULARITY);
      uptr tail_end = alloc_beg + allocator.GetActuallyAllocatedSize(allocated);
      PoisonShadow(alloc_beg, user_beg - alloc_beg, kAsanHeapLeftRedzoneMagic);
      PoisonShadow(tail_beg, tail_end - tail_beg, kAsanHeapLeftRedzoneMagic);
    }

    uptr size_rounded_down_to_granularity =
        RoundDownTo(size, ASAN_SHADOW_GRANULARITY);
    // Unpoison the bulk of the memory region.
    if (size_rounded_down_to_granularity)
      PoisonShadow(user_beg, size_rounded_down_to_granularity, 0);
    // Deal with the end of the region if size is not aligned to granularity.
    if (size != size_rounded_down_to_granularity && CanPoisonMemory()) {
      u8 *shadow =
          (u8 *)MemToShadow(user_beg + size_rounded_down_to_granularity);
      *shadow = fl.poison_partial ? (size & (ASAN_SHADOW_GRANULARITY - 1)) : 0;
    }

    AsanStats &thread_stats = GetCurrentThreadStats();
    thread_stats.mallocs++;
    thread_stats.malloced += size;
    thread_stats.malloced_redzones += needed_size - size;
    if (needed_size > SizeClassMap::kMaxSize)
      thread_stats.malloc_large++;
    else
      thread_stats.malloced_by_size[SizeClassMap::ClassID(needed_size)]++;

    void *res = reinterpret_cast<void *>(user_beg);
    if (can_fill && fl.max_malloc_fill_size) {
      uptr fill_size = Min(size, (uptr)fl.max_malloc_fill_size);
      REAL(memset)(res, fl.malloc_fill_byte, fill_size);
    }
#if CAN_SANITIZE_LEAKS
    m->lsan_tag = __lsan::DisabledInThisThread() ? __lsan::kIgnored
                                                 : __lsan::kDirectlyLeaked;
#endif
    // Must be the last mutation of metadata in this function.
    atomic_store(&m->chunk_state, CHUNK_ALLOCATED, memory_order_release);
    if (alloc_beg != chunk_beg) {
      CHECK_LE(alloc_beg + sizeof(LargeChunkHeader), chunk_beg);
      reinterpret_cast<LargeChunkHeader *>(alloc_beg)->Set(m);
    }
    RunMallocHooks(res, size);
    return res;
  }

  // Set quarantine flag if chunk is allocated, issue ASan error report on
  // available and quarantined chunks. Return true on success, false otherwise.
  bool AtomicallySetQuarantineFlagIfAllocated(AsanChunk *m, void *ptr,
                                              BufferedStackTrace *stack) {
    u8 old_chunk_state = CHUNK_ALLOCATED;
    // Flip the chunk_state atomically to avoid race on double-free.
    if (!atomic_compare_exchange_strong(&m->chunk_state, &old_chunk_state,
                                        CHUNK_QUARANTINE,
                                        memory_order_acquire)) {
      ReportInvalidFree(ptr, old_chunk_state, stack);
      // It's not safe to push a chunk in quarantine on invalid free.
      return false;
    }
    CHECK_EQ(CHUNK_ALLOCATED, old_chunk_state);
    // It was a user data.
    m->SetFreeContext(kInvalidTid, 0);
    return true;
  }

  // Expects the chunk to already be marked as quarantined by using
  // AtomicallySetQuarantineFlagIfAllocated.
  void QuarantineChunk(AsanChunk *m, void *ptr, BufferedStackTrace *stack) {
    CHECK_EQ(atomic_load(&m->chunk_state, memory_order_relaxed),
             CHUNK_QUARANTINE);
    AsanThread *t = GetCurrentThread();
    m->SetFreeContext(t ? t->tid() : 0, StackDepotPut(*stack));

    // Push into quarantine.
    if (t) {
      AsanThreadLocalMallocStorage *ms = &t->malloc_storage();
      AllocatorCache *ac = GetAllocatorCache(ms);
      quarantine.Put(GetQuarantineCache(ms), QuarantineCallback(ac, stack), m,
                     m->UsedSize());
    } else {
      SpinMutexLock l(&fallback_mutex);
      AllocatorCache *ac = &fallback_allocator_cache;
      quarantine.Put(&fallback_quarantine_cache, QuarantineCallback(ac, stack),
                     m, m->UsedSize());
    }
  }

  void Deallocate(void *ptr, uptr delete_size, uptr delete_alignment,
                  BufferedStackTrace *stack, AllocType alloc_type) {
    uptr p = reinterpret_cast<uptr>(ptr);
    if (p == 0) return;

    uptr chunk_beg = p - kChunkHeaderSize;
    AsanChunk *m = reinterpret_cast<AsanChunk *>(chunk_beg);

    // On Windows, uninstrumented DLLs may allocate memory before ASan hooks
    // malloc. Don't report an invalid free in this case.
    if (SANITIZER_WINDOWS &&
        !get_allocator().PointerIsMine(ptr)) {
      if (!IsSystemHeapAddress(p))
        ReportFreeNotMalloced(p, stack);
      return;
    }

    if (RunFreeHooks(ptr)) {
      // Someone used __sanitizer_ignore_free_hook() and decided that they
      // didn't want the memory to __sanitizer_ignore_free_hook freed right now.
      // When they call free() on this pointer again at a later time, we should
      // ignore the alloc-type mismatch and allow them to deallocate the pointer
      // through free(), rather than the initial alloc type.
      m->alloc_type = FROM_MALLOC;
      return;
    }

    // Must mark the chunk as quarantined before any changes to its metadata.
    // Do not quarantine given chunk if we failed to set CHUNK_QUARANTINE flag.
    if (!AtomicallySetQuarantineFlagIfAllocated(m, ptr, stack)) return;

    if (m->alloc_type != alloc_type) {
      if (atomic_load(&alloc_dealloc_mismatch, memory_order_acquire)) {
        ReportAllocTypeMismatch((uptr)ptr, stack, (AllocType)m->alloc_type,
                                (AllocType)alloc_type);
      }
    } else {
      if (flags()->new_delete_type_mismatch &&
          (alloc_type == FROM_NEW || alloc_type == FROM_NEW_BR) &&
          ((delete_size && delete_size != m->UsedSize()) ||
           ComputeUserRequestedAlignmentLog(delete_alignment) !=
               m->user_requested_alignment_log)) {
        ReportNewDeleteTypeMismatch(p, delete_size, delete_alignment, stack);
      }
    }

    AsanStats &thread_stats = GetCurrentThreadStats();
    thread_stats.frees++;
    thread_stats.freed += m->UsedSize();

    QuarantineChunk(m, ptr, stack);
  }

  void *Reallocate(void *old_ptr, uptr new_size, BufferedStackTrace *stack) {
    CHECK(old_ptr && new_size);
    uptr p = reinterpret_cast<uptr>(old_ptr);
    uptr chunk_beg = p - kChunkHeaderSize;
    AsanChunk *m = reinterpret_cast<AsanChunk *>(chunk_beg);

    AsanStats &thread_stats = GetCurrentThreadStats();
    thread_stats.reallocs++;
    thread_stats.realloced += new_size;

    void *new_ptr = Allocate(new_size, 8, stack, FROM_MALLOC, true);
    if (new_ptr) {
      u8 chunk_state = atomic_load(&m->chunk_state, memory_order_acquire);
      if (chunk_state != CHUNK_ALLOCATED)
        ReportInvalidFree(old_ptr, chunk_state, stack);
      CHECK_NE(REAL(memcpy), nullptr);
      uptr memcpy_size = Min(new_size, m->UsedSize());
      // If realloc() races with free(), we may start copying freed memory.
      // However, we will report racy double-free later anyway.
      REAL(memcpy)(new_ptr, old_ptr, memcpy_size);
      Deallocate(old_ptr, 0, 0, stack, FROM_MALLOC);
    }
    return new_ptr;
  }

  void *Calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
    if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
      if (AllocatorMayReturnNull())
        return nullptr;
      ReportCallocOverflow(nmemb, size, stack);
    }
    void *ptr = Allocate(nmemb * size, 8, stack, FROM_MALLOC, false);
    // If the memory comes from the secondary allocator no need to clear it
    // as it comes directly from mmap.
    if (ptr && allocator.FromPrimary(ptr))
      REAL(memset)(ptr, 0, nmemb * size);
    return ptr;
  }

  void ReportInvalidFree(void *ptr, u8 chunk_state, BufferedStackTrace *stack) {
    if (chunk_state == CHUNK_QUARANTINE)
      ReportDoubleFree((uptr)ptr, stack);
    else
      ReportFreeNotMalloced((uptr)ptr, stack);
  }

  void CommitBack(AsanThreadLocalMallocStorage *ms, BufferedStackTrace *stack) {
    AllocatorCache *ac = GetAllocatorCache(ms);
    quarantine.Drain(GetQuarantineCache(ms), QuarantineCallback(ac, stack));
    allocator.SwallowCache(ac);
  }

  // -------------------------- Chunk lookup ----------------------

  // Assumes alloc_beg == allocator.GetBlockBegin(alloc_beg).
  // Returns nullptr if AsanChunk is not yet initialized just after
  // get_allocator().Allocate(), or is being destroyed just before
  // get_allocator().Deallocate().
  AsanChunk *GetAsanChunk(void *alloc_beg) {
    if (!alloc_beg)
      return nullptr;
    AsanChunk *p = reinterpret_cast<LargeChunkHeader *>(alloc_beg)->Get();
    if (!p) {
      if (!allocator.FromPrimary(alloc_beg))
        return nullptr;
      p = reinterpret_cast<AsanChunk *>(alloc_beg);
    }
    u8 state = atomic_load(&p->chunk_state, memory_order_relaxed);
    // It does not guaranty that Chunk is initialized, but it's
    // definitely not for any other value.
    if (state == CHUNK_ALLOCATED || state == CHUNK_QUARANTINE)
      return p;
    return nullptr;
  }

  AsanChunk *GetAsanChunkByAddr(uptr p) {
    void *alloc_beg = allocator.GetBlockBegin(reinterpret_cast<void *>(p));
    return GetAsanChunk(alloc_beg);
  }

  // Allocator must be locked when this function is called.
  AsanChunk *GetAsanChunkByAddrFastLocked(uptr p) {
    void *alloc_beg =
        allocator.GetBlockBeginFastLocked(reinterpret_cast<void *>(p));
    return GetAsanChunk(alloc_beg);
  }

  uptr AllocationSize(uptr p) {
    AsanChunk *m = GetAsanChunkByAddr(p);
    if (!m) return 0;
    if (atomic_load(&m->chunk_state, memory_order_acquire) != CHUNK_ALLOCATED)
      return 0;
    if (m->Beg() != p) return 0;
    return m->UsedSize();
  }

  uptr AllocationSizeFast(uptr p) {
    return reinterpret_cast<AsanChunk *>(p - kChunkHeaderSize)->UsedSize();
  }

  AsanChunkView FindHeapChunkByAddress(uptr addr) {
    AsanChunk *m1 = GetAsanChunkByAddr(addr);
    sptr offset = 0;
    if (!m1 || AsanChunkView(m1).AddrIsAtLeft(addr, 1, &offset)) {
      // The address is in the chunk's left redzone, so maybe it is actually
      // a right buffer overflow from the other chunk before.
      // Search a bit before to see if there is another chunk.
      AsanChunk *m2 = nullptr;
      for (uptr l = 1; l < GetPageSizeCached(); l++) {
        m2 = GetAsanChunkByAddr(addr - l);
        if (m2 == m1) continue;  // Still the same chunk.
        break;
      }
      if (m2 && AsanChunkView(m2).AddrIsAtRight(addr, 1, &offset))
        m1 = ChooseChunk(addr, m2, m1);
    }
    return AsanChunkView(m1);
  }

  void Purge(BufferedStackTrace *stack) {
    AsanThread *t = GetCurrentThread();
    if (t) {
      AsanThreadLocalMallocStorage *ms = &t->malloc_storage();
      quarantine.DrainAndRecycle(GetQuarantineCache(ms),
                                 QuarantineCallback(GetAllocatorCache(ms),
                                                    stack));
    }
    {
      SpinMutexLock l(&fallback_mutex);
      quarantine.DrainAndRecycle(&fallback_quarantine_cache,
                                 QuarantineCallback(&fallback_allocator_cache,
                                                    stack));
    }

    allocator.ForceReleaseToOS();
  }

  void PrintStats() {
    allocator.PrintStats();
    quarantine.PrintStats();
  }

  void ForceLock() SANITIZER_ACQUIRE(fallback_mutex) {
    allocator.ForceLock();
    fallback_mutex.Lock();
  }

  void ForceUnlock() SANITIZER_RELEASE(fallback_mutex) {
    fallback_mutex.Unlock();
    allocator.ForceUnlock();
  }
};

static Allocator instance(LINKER_INITIALIZED);

static AsanAllocator &get_allocator() {
  return instance.allocator;
}

bool AsanChunkView::IsValid() const {
  return chunk_ && atomic_load(&chunk_->chunk_state, memory_order_relaxed) !=
                       CHUNK_INVALID;
}
bool AsanChunkView::IsAllocated() const {
  return chunk_ && atomic_load(&chunk_->chunk_state, memory_order_relaxed) ==
                       CHUNK_ALLOCATED;
}
bool AsanChunkView::IsQuarantined() const {
  return chunk_ && atomic_load(&chunk_->chunk_state, memory_order_relaxed) ==
                       CHUNK_QUARANTINE;
}
uptr AsanChunkView::Beg() const { return chunk_->Beg(); }
uptr AsanChunkView::End() const { return Beg() + UsedSize(); }
uptr AsanChunkView::UsedSize() const { return chunk_->UsedSize(); }
u32 AsanChunkView::UserRequestedAlignment() const {
  return Allocator::ComputeUserAlignment(chunk_->user_requested_alignment_log);
}

uptr AsanChunkView::AllocTid() const {
  u32 tid = 0;
  u32 stack = 0;
  chunk_->GetAllocContext(tid, stack);
  return tid;
}

uptr AsanChunkView::FreeTid() const {
  if (!IsQuarantined())
    return kInvalidTid;
  u32 tid = 0;
  u32 stack = 0;
  chunk_->GetFreeContext(tid, stack);
  return tid;
}

AllocType AsanChunkView::GetAllocType() const {
  return (AllocType)chunk_->alloc_type;
}

u32 AsanChunkView::GetAllocStackId() const {
  u32 tid = 0;
  u32 stack = 0;
  chunk_->GetAllocContext(tid, stack);
  return stack;
}

u32 AsanChunkView::GetFreeStackId() const {
  if (!IsQuarantined())
    return 0;
  u32 tid = 0;
  u32 stack = 0;
  chunk_->GetFreeContext(tid, stack);
  return stack;
}

void InitializeAllocator(const AllocatorOptions &options) {
  instance.InitLinkerInitialized(options);
}

void ReInitializeAllocator(const AllocatorOptions &options) {
  instance.ReInitialize(options);
}

void GetAllocatorOptions(AllocatorOptions *options) {
  instance.GetOptions(options);
}

AsanChunkView FindHeapChunkByAddress(uptr addr) {
  return instance.FindHeapChunkByAddress(addr);
}
AsanChunkView FindHeapChunkByAllocBeg(uptr addr) {
  return AsanChunkView(instance.GetAsanChunk(reinterpret_cast<void*>(addr)));
}

void AsanThreadLocalMallocStorage::CommitBack() {
  GET_STACK_TRACE_MALLOC;
  instance.CommitBack(this, &stack);
}

void PrintInternalAllocatorStats() {
  instance.PrintStats();
}

void asan_free(void *ptr, BufferedStackTrace *stack, AllocType alloc_type) {
  instance.Deallocate(ptr, 0, 0, stack, alloc_type);
}

void asan_delete(void *ptr, uptr size, uptr alignment,
                 BufferedStackTrace *stack, AllocType alloc_type) {
  instance.Deallocate(ptr, size, alignment, stack, alloc_type);
}

void *asan_malloc(uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(instance.Allocate(size, 8, stack, FROM_MALLOC, true));
}

void *asan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(instance.Calloc(nmemb, size, stack));
}

void *asan_reallocarray(void *p, uptr nmemb, uptr size,
                        BufferedStackTrace *stack) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportReallocArrayOverflow(nmemb, size, stack);
  }
  return asan_realloc(p, nmemb * size, stack);
}

void *asan_realloc(void *p, uptr size, BufferedStackTrace *stack) {
  if (!p)
    return SetErrnoOnNull(instance.Allocate(size, 8, stack, FROM_MALLOC, true));
  if (size == 0) {
    if (flags()->allocator_frees_and_returns_null_on_realloc_zero) {
      instance.Deallocate(p, 0, 0, stack, FROM_MALLOC);
      return nullptr;
    }
    // Allocate a size of 1 if we shouldn't free() on Realloc to 0
    size = 1;
  }
  return SetErrnoOnNull(instance.Reallocate(p, size, stack));
}

void *asan_valloc(uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(
      instance.Allocate(size, GetPageSizeCached(), stack, FROM_MALLOC, true));
}

void *asan_pvalloc(uptr size, BufferedStackTrace *stack) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(size, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportPvallocOverflow(size, stack);
  }
  // pvalloc(0) should allocate one page.
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return SetErrnoOnNull(
      instance.Allocate(size, PageSize, stack, FROM_MALLOC, true));
}

void *asan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                    AllocType alloc_type) {
  if (UNLIKELY(!IsPowerOfTwo(alignment))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAllocationAlignment(alignment, stack);
  }
  return SetErrnoOnNull(
      instance.Allocate(size, alignment, stack, alloc_type, true));
}

void *asan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAlignedAllocAlignment(size, alignment, stack);
  }
  return SetErrnoOnNull(
      instance.Allocate(size, alignment, stack, FROM_MALLOC, true));
}

int asan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    ReportInvalidPosixMemalignAlignment(alignment, stack);
  }
  void *ptr = instance.Allocate(size, alignment, stack, FROM_MALLOC, true);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by Allocate.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

uptr asan_malloc_usable_size(const void *ptr, uptr pc, uptr bp) {
  if (!ptr) return 0;
  uptr usable_size = instance.AllocationSize(reinterpret_cast<uptr>(ptr));
  if (flags()->check_malloc_usable_size && (usable_size == 0)) {
    GET_STACK_TRACE_FATAL(pc, bp);
    ReportMallocUsableSizeNotOwned((uptr)ptr, &stack);
  }
  return usable_size;
}

uptr asan_mz_size(const void *ptr) {
  return instance.AllocationSize(reinterpret_cast<uptr>(ptr));
}

void asan_mz_force_lock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  instance.ForceLock();
}

void asan_mz_force_unlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  instance.ForceUnlock();
}

}  // namespace __asan

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {
void LockAllocator() {
  __asan::get_allocator().ForceLock();
}

void UnlockAllocator() {
  __asan::get_allocator().ForceUnlock();
}

void GetAllocatorGlobalRange(uptr *begin, uptr *end) {
  *begin = (uptr)&__asan::get_allocator();
  *end = *begin + sizeof(__asan::get_allocator());
}

uptr PointsIntoChunk(void *p) {
  uptr addr = reinterpret_cast<uptr>(p);
  __asan::AsanChunk *m = __asan::instance.GetAsanChunkByAddrFastLocked(addr);
  if (!m || atomic_load(&m->chunk_state, memory_order_acquire) !=
                __asan::CHUNK_ALLOCATED)
    return 0;
  uptr chunk = m->Beg();
  if (m->AddrIsInside(addr))
    return chunk;
  if (IsSpecialCaseOfOperatorNew0(chunk, m->UsedSize(), addr))
    return chunk;
  return 0;
}

uptr GetUserBegin(uptr chunk) {
  // FIXME: All usecases provide chunk address, GetAsanChunkByAddrFastLocked is
  // not needed.
  __asan::AsanChunk *m = __asan::instance.GetAsanChunkByAddrFastLocked(chunk);
  return m ? m->Beg() : 0;
}

uptr GetUserAddr(uptr chunk) {
  return chunk;
}

LsanMetadata::LsanMetadata(uptr chunk) {
  metadata_ = chunk ? reinterpret_cast<void *>(chunk - __asan::kChunkHeaderSize)
                    : nullptr;
}

bool LsanMetadata::allocated() const {
  if (!metadata_)
    return false;
  __asan::AsanChunk *m = reinterpret_cast<__asan::AsanChunk *>(metadata_);
  return atomic_load(&m->chunk_state, memory_order_relaxed) ==
         __asan::CHUNK_ALLOCATED;
}

ChunkTag LsanMetadata::tag() const {
  __asan::AsanChunk *m = reinterpret_cast<__asan::AsanChunk *>(metadata_);
  return static_cast<ChunkTag>(m->lsan_tag);
}

void LsanMetadata::set_tag(ChunkTag value) {
  __asan::AsanChunk *m = reinterpret_cast<__asan::AsanChunk *>(metadata_);
  m->lsan_tag = value;
}

uptr LsanMetadata::requested_size() const {
  __asan::AsanChunk *m = reinterpret_cast<__asan::AsanChunk *>(metadata_);
  return m->UsedSize();
}

u32 LsanMetadata::stack_trace_id() const {
  __asan::AsanChunk *m = reinterpret_cast<__asan::AsanChunk *>(metadata_);
  u32 tid = 0;
  u32 stack = 0;
  m->GetAllocContext(tid, stack);
  return stack;
}

void ForEachChunk(ForEachChunkCallback callback, void *arg) {
  __asan::get_allocator().ForEachChunk(callback, arg);
}

IgnoreObjectResult IgnoreObject(const void *p) {
  uptr addr = reinterpret_cast<uptr>(p);
  __asan::AsanChunk *m = __asan::instance.GetAsanChunkByAddr(addr);
  if (!m ||
      (atomic_load(&m->chunk_state, memory_order_acquire) !=
       __asan::CHUNK_ALLOCATED) ||
      !m->AddrIsInside(addr)) {
    return kIgnoreObjectInvalid;
  }
  if (m->lsan_tag == kIgnored)
    return kIgnoreObjectAlreadyIgnored;
  m->lsan_tag = __lsan::kIgnored;
  return kIgnoreObjectSuccess;
}

}  // namespace __lsan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;

static const void *AllocationBegin(const void *p) {
  AsanChunk *m = __asan::instance.GetAsanChunkByAddr((uptr)p);
  if (!m)
    return nullptr;
  if (atomic_load(&m->chunk_state, memory_order_acquire) != CHUNK_ALLOCATED)
    return nullptr;
  if (m->UsedSize() == 0)
    return nullptr;
  return (const void *)(m->Beg());
}

// ASan allocator doesn't reserve extra bytes, so normally we would
// just return "size". We don't want to expose our redzone sizes, etc here.
uptr __sanitizer_get_estimated_allocated_size(uptr size) {
  return size;
}

int __sanitizer_get_ownership(const void *p) {
  uptr ptr = reinterpret_cast<uptr>(p);
  return instance.AllocationSize(ptr) > 0;
}

uptr __sanitizer_get_allocated_size(const void *p) {
  if (!p) return 0;
  uptr ptr = reinterpret_cast<uptr>(p);
  uptr allocated_size = instance.AllocationSize(ptr);
  // Die if p is not malloced or if it is already freed.
  if (allocated_size == 0) {
    GET_STACK_TRACE_FATAL_HERE;
    ReportSanitizerGetAllocatedSizeNotOwned(ptr, &stack);
  }
  return allocated_size;
}

uptr __sanitizer_get_allocated_size_fast(const void *p) {
  DCHECK_EQ(p, __sanitizer_get_allocated_begin(p));
  uptr ret = instance.AllocationSizeFast(reinterpret_cast<uptr>(p));
  DCHECK_EQ(ret, __sanitizer_get_allocated_size(p));
  return ret;
}

const void *__sanitizer_get_allocated_begin(const void *p) {
  return AllocationBegin(p);
}

void __sanitizer_purge_allocator() {
  GET_STACK_TRACE_MALLOC;
  instance.Purge(&stack);
}

int __asan_update_allocation_context(void* addr) {
  GET_STACK_TRACE_MALLOC;
  return instance.UpdateAllocationStack((uptr)addr, &stack);
}
