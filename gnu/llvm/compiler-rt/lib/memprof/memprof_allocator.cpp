//===-- memprof_allocator.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Implementation of MemProf's memory allocator, which uses the allocator
// from sanitizer_common.
//
//===----------------------------------------------------------------------===//

#include "memprof_allocator.h"
#include "memprof_mapping.h"
#include "memprof_mibmap.h"
#include "memprof_rawprofile.h"
#include "memprof_stack.h"
#include "memprof_thread.h"
#include "profile/MemProfData.inc"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_array_ref.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

#include <sched.h>
#include <time.h>

#define MAX_HISTOGRAM_PRINT_SIZE 32U

extern bool __memprof_histogram;

namespace __memprof {
namespace {
using ::llvm::memprof::MemInfoBlock;

void Print(const MemInfoBlock &M, const u64 id, bool print_terse) {
  u64 p;

  if (print_terse) {
    p = M.TotalSize * 100 / M.AllocCount;
    Printf("MIB:%llu/%u/%llu.%02llu/%u/%u/", id, M.AllocCount, p / 100, p % 100,
           M.MinSize, M.MaxSize);
    p = M.TotalAccessCount * 100 / M.AllocCount;
    Printf("%llu.%02llu/%llu/%llu/", p / 100, p % 100, M.MinAccessCount,
           M.MaxAccessCount);
    p = M.TotalLifetime * 100 / M.AllocCount;
    Printf("%llu.%02llu/%u/%u/", p / 100, p % 100, M.MinLifetime,
           M.MaxLifetime);
    Printf("%u/%u/%u/%u\n", M.NumMigratedCpu, M.NumLifetimeOverlaps,
           M.NumSameAllocCpu, M.NumSameDeallocCpu);
  } else {
    p = M.TotalSize * 100 / M.AllocCount;
    Printf("Memory allocation stack id = %llu\n", id);
    Printf("\talloc_count %u, size (ave/min/max) %llu.%02llu / %u / %u\n",
           M.AllocCount, p / 100, p % 100, M.MinSize, M.MaxSize);
    p = M.TotalAccessCount * 100 / M.AllocCount;
    Printf("\taccess_count (ave/min/max): %llu.%02llu / %llu / %llu\n", p / 100,
           p % 100, M.MinAccessCount, M.MaxAccessCount);
    p = M.TotalLifetime * 100 / M.AllocCount;
    Printf("\tlifetime (ave/min/max): %llu.%02llu / %u / %u\n", p / 100,
           p % 100, M.MinLifetime, M.MaxLifetime);
    Printf("\tnum migrated: %u, num lifetime overlaps: %u, num same alloc "
           "cpu: %u, num same dealloc_cpu: %u\n",
           M.NumMigratedCpu, M.NumLifetimeOverlaps, M.NumSameAllocCpu,
           M.NumSameDeallocCpu);
    Printf("AccessCountHistogram[%u]: ", M.AccessHistogramSize);
    uint32_t PrintSize = M.AccessHistogramSize > MAX_HISTOGRAM_PRINT_SIZE
                             ? MAX_HISTOGRAM_PRINT_SIZE
                             : M.AccessHistogramSize;
    for (size_t i = 0; i < PrintSize; ++i) {
      Printf("%llu ", ((uint64_t *)M.AccessHistogram)[i]);
    }
    Printf("\n");
  }
}
} // namespace

static int GetCpuId(void) {
  // _memprof_preinit is called via the preinit_array, which subsequently calls
  // malloc. Since this is before _dl_init calls VDSO_SETUP, sched_getcpu
  // will seg fault as the address of __vdso_getcpu will be null.
  if (!memprof_inited)
    return -1;
  return sched_getcpu();
}

// Compute the timestamp in ms.
static int GetTimestamp(void) {
  // timespec_get will segfault if called from dl_init
  if (!memprof_timestamp_inited) {
    // By returning 0, this will be effectively treated as being
    // timestamped at memprof init time (when memprof_init_timestamp_s
    // is initialized).
    return 0;
  }
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (ts.tv_sec - memprof_init_timestamp_s) * 1000 + ts.tv_nsec / 1000000;
}

static MemprofAllocator &get_allocator();

// The memory chunk allocated from the underlying allocator looks like this:
// H H U U U U U U
//   H -- ChunkHeader (32 bytes)
//   U -- user memory.

// If there is left padding before the ChunkHeader (due to use of memalign),
// we store a magic value in the first uptr word of the memory block and
// store the address of ChunkHeader in the next uptr.
// M B L L L L L L L L L  H H U U U U U U
//   |                    ^
//   ---------------------|
//   M -- magic value kAllocBegMagic
//   B -- address of ChunkHeader pointing to the first 'H'

constexpr uptr kMaxAllowedMallocBits = 40;

// Should be no more than 32-bytes
struct ChunkHeader {
  // 1-st 4 bytes.
  u32 alloc_context_id;
  // 2-nd 4 bytes
  u32 cpu_id;
  // 3-rd 4 bytes
  u32 timestamp_ms;
  // 4-th 4 bytes
  // Note only 1 bit is needed for this flag if we need space in the future for
  // more fields.
  u32 from_memalign;
  // 5-th and 6-th 4 bytes
  // The max size of an allocation is 2^40 (kMaxAllowedMallocSize), so this
  // could be shrunk to kMaxAllowedMallocBits if we need space in the future for
  // more fields.
  atomic_uint64_t user_requested_size;
  // 23 bits available
  // 7-th and 8-th 4 bytes
  u64 data_type_id; // TODO: hash of type name
};

static const uptr kChunkHeaderSize = sizeof(ChunkHeader);
COMPILER_CHECK(kChunkHeaderSize == 32);

struct MemprofChunk : ChunkHeader {
  uptr Beg() { return reinterpret_cast<uptr>(this) + kChunkHeaderSize; }
  uptr UsedSize() {
    return atomic_load(&user_requested_size, memory_order_relaxed);
  }
  void *AllocBeg() {
    if (from_memalign)
      return get_allocator().GetBlockBegin(reinterpret_cast<void *>(this));
    return reinterpret_cast<void *>(this);
  }
};

class LargeChunkHeader {
  static constexpr uptr kAllocBegMagic =
      FIRST_32_SECOND_64(0xCC6E96B9, 0xCC6E96B9CC6E96B9ULL);
  atomic_uintptr_t magic;
  MemprofChunk *chunk_header;

public:
  MemprofChunk *Get() const {
    return atomic_load(&magic, memory_order_acquire) == kAllocBegMagic
               ? chunk_header
               : nullptr;
  }

  void Set(MemprofChunk *p) {
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

void FlushUnneededMemProfShadowMemory(uptr p, uptr size) {
  // Since memprof's mapping is compacting, the shadow chunk may be
  // not page-aligned, so we only flush the page-aligned portion.
  ReleaseMemoryPagesToOS(MemToShadow(p), MemToShadow(p + size));
}

void MemprofMapUnmapCallback::OnMap(uptr p, uptr size) const {
  // Statistics.
  MemprofStats &thread_stats = GetCurrentThreadStats();
  thread_stats.mmaps++;
  thread_stats.mmaped += size;
}

void MemprofMapUnmapCallback::OnUnmap(uptr p, uptr size) const {
  // We are about to unmap a chunk of user memory.
  // Mark the corresponding shadow memory as not needed.
  FlushUnneededMemProfShadowMemory(p, size);
  // Statistics.
  MemprofStats &thread_stats = GetCurrentThreadStats();
  thread_stats.munmaps++;
  thread_stats.munmaped += size;
}

AllocatorCache *GetAllocatorCache(MemprofThreadLocalMallocStorage *ms) {
  CHECK(ms);
  return &ms->allocator_cache;
}

// Accumulates the access count from the shadow for the given pointer and size.
u64 GetShadowCount(uptr p, u32 size) {
  u64 *shadow = (u64 *)MEM_TO_SHADOW(p);
  u64 *shadow_end = (u64 *)MEM_TO_SHADOW(p + size);
  u64 count = 0;
  for (; shadow <= shadow_end; shadow++)
    count += *shadow;
  return count;
}

// Accumulates the access count from the shadow for the given pointer and size.
// See memprof_mapping.h for an overview on histogram counters.
u64 GetShadowCountHistogram(uptr p, u32 size) {
  u8 *shadow = (u8 *)HISTOGRAM_MEM_TO_SHADOW(p);
  u8 *shadow_end = (u8 *)HISTOGRAM_MEM_TO_SHADOW(p + size);
  u64 count = 0;
  for (; shadow <= shadow_end; shadow++)
    count += *shadow;
  return count;
}

// Clears the shadow counters (when memory is allocated).
void ClearShadow(uptr addr, uptr size) {
  CHECK(AddrIsAlignedByGranularity(addr));
  CHECK(AddrIsInMem(addr));
  CHECK(AddrIsAlignedByGranularity(addr + size));
  CHECK(AddrIsInMem(addr + size - SHADOW_GRANULARITY));
  CHECK(REAL(memset));
  uptr shadow_beg;
  uptr shadow_end;
  if (__memprof_histogram) {
    shadow_beg = HISTOGRAM_MEM_TO_SHADOW(addr);
    shadow_end = HISTOGRAM_MEM_TO_SHADOW(addr + size);
  } else {
    shadow_beg = MEM_TO_SHADOW(addr);
    shadow_end = MEM_TO_SHADOW(addr + size - SHADOW_GRANULARITY) + 1;
  }

  if (shadow_end - shadow_beg < common_flags()->clear_shadow_mmap_threshold) {
    REAL(memset)((void *)shadow_beg, 0, shadow_end - shadow_beg);
  } else {
    uptr page_size = GetPageSizeCached();
    uptr page_beg = RoundUpTo(shadow_beg, page_size);
    uptr page_end = RoundDownTo(shadow_end, page_size);

    if (page_beg >= page_end) {
      REAL(memset)((void *)shadow_beg, 0, shadow_end - shadow_beg);
    } else {
      if (page_beg != shadow_beg) {
        REAL(memset)((void *)shadow_beg, 0, page_beg - shadow_beg);
      }
      if (page_end != shadow_end) {
        REAL(memset)((void *)page_end, 0, shadow_end - page_end);
      }
      ReserveShadowMemoryRange(page_beg, page_end - 1, nullptr);
    }
  }
}

struct Allocator {
  static const uptr kMaxAllowedMallocSize = 1ULL << kMaxAllowedMallocBits;

  MemprofAllocator allocator;
  StaticSpinMutex fallback_mutex;
  AllocatorCache fallback_allocator_cache;

  uptr max_user_defined_malloc_size;

  // Holds the mapping of stack ids to MemInfoBlocks.
  MIBMapTy MIBMap;

  atomic_uint8_t destructing;
  atomic_uint8_t constructed;
  bool print_text;

  // ------------------- Initialization ------------------------
  explicit Allocator(LinkerInitialized) : print_text(flags()->print_text) {
    atomic_store_relaxed(&destructing, 0);
    atomic_store_relaxed(&constructed, 1);
  }

  ~Allocator() {
    atomic_store_relaxed(&destructing, 1);
    FinishAndWrite();
  }

  static void PrintCallback(const uptr Key, LockedMemInfoBlock *const &Value,
                            void *Arg) {
    SpinMutexLock l(&Value->mutex);
    Print(Value->mib, Key, bool(Arg));
  }

  // See memprof_mapping.h for an overview on histogram counters.
  static MemInfoBlock CreateNewMIB(uptr p, MemprofChunk *m, u64 user_size) {
    if (__memprof_histogram) {
      return CreateNewMIBWithHistogram(p, m, user_size);
    } else {
      return CreateNewMIBWithoutHistogram(p, m, user_size);
    }
  }

  static MemInfoBlock CreateNewMIBWithHistogram(uptr p, MemprofChunk *m,
                                                u64 user_size) {

    u64 c = GetShadowCountHistogram(p, user_size);
    long curtime = GetTimestamp();
    uint32_t HistogramSize =
        RoundUpTo(user_size, HISTOGRAM_GRANULARITY) / HISTOGRAM_GRANULARITY;
    uintptr_t Histogram =
        (uintptr_t)InternalAlloc(HistogramSize * sizeof(uint64_t));
    memset((void *)Histogram, 0, HistogramSize * sizeof(uint64_t));
    for (size_t i = 0; i < HistogramSize; ++i) {
      u8 Counter =
          *((u8 *)HISTOGRAM_MEM_TO_SHADOW(p + HISTOGRAM_GRANULARITY * i));
      ((uint64_t *)Histogram)[i] = (uint64_t)Counter;
    }
    MemInfoBlock newMIB(user_size, c, m->timestamp_ms, curtime, m->cpu_id,
                        GetCpuId(), Histogram, HistogramSize);
    return newMIB;
  }

  static MemInfoBlock CreateNewMIBWithoutHistogram(uptr p, MemprofChunk *m,
                                                   u64 user_size) {
    u64 c = GetShadowCount(p, user_size);
    long curtime = GetTimestamp();
    MemInfoBlock newMIB(user_size, c, m->timestamp_ms, curtime, m->cpu_id,
                        GetCpuId(), 0, 0);
    return newMIB;
  }

  void FinishAndWrite() {
    if (print_text && common_flags()->print_module_map)
      DumpProcessMap();

    allocator.ForceLock();

    InsertLiveBlocks();
    if (print_text) {
      if (!flags()->print_terse)
        Printf("Recorded MIBs (incl. live on exit):\n");
      MIBMap.ForEach(PrintCallback,
                     reinterpret_cast<void *>(flags()->print_terse));
      StackDepotPrintAll();
    } else {
      // Serialize the contents to a raw profile. Format documented in
      // memprof_rawprofile.h.
      char *Buffer = nullptr;

      __sanitizer::ListOfModules List;
      List.init();
      ArrayRef<LoadedModule> Modules(List.begin(), List.end());
      u64 BytesSerialized = SerializeToRawProfile(MIBMap, Modules, Buffer);
      CHECK(Buffer && BytesSerialized && "could not serialize to buffer");
      report_file.Write(Buffer, BytesSerialized);
    }

    allocator.ForceUnlock();
  }

  // Inserts any blocks which have been allocated but not yet deallocated.
  void InsertLiveBlocks() {
    allocator.ForEachChunk(
        [](uptr chunk, void *alloc) {
          u64 user_requested_size;
          Allocator *A = (Allocator *)alloc;
          MemprofChunk *m =
              A->GetMemprofChunk((void *)chunk, user_requested_size);
          if (!m)
            return;
          uptr user_beg = ((uptr)m) + kChunkHeaderSize;
          MemInfoBlock newMIB = CreateNewMIB(user_beg, m, user_requested_size);
          InsertOrMerge(m->alloc_context_id, newMIB, A->MIBMap);
        },
        this);
  }

  void InitLinkerInitialized() {
    SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
    allocator.InitLinkerInitialized(
        common_flags()->allocator_release_to_os_interval_ms);
    max_user_defined_malloc_size = common_flags()->max_allocation_size_mb
                                       ? common_flags()->max_allocation_size_mb
                                             << 20
                                       : kMaxAllowedMallocSize;
  }

  // -------------------- Allocation/Deallocation routines ---------------
  void *Allocate(uptr size, uptr alignment, BufferedStackTrace *stack,
                 AllocType alloc_type) {
    if (UNLIKELY(!memprof_inited))
      MemprofInitFromRtl();
    if (UNLIKELY(IsRssLimitExceeded())) {
      if (AllocatorMayReturnNull())
        return nullptr;
      ReportRssLimitExceeded(stack);
    }
    CHECK(stack);
    const uptr min_alignment = MEMPROF_ALIGNMENT;
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
    uptr rounded_size = RoundUpTo(size, alignment);
    uptr needed_size = rounded_size + kChunkHeaderSize;
    if (alignment > min_alignment)
      needed_size += alignment;
    CHECK(IsAligned(needed_size, min_alignment));
    if (size > kMaxAllowedMallocSize || needed_size > kMaxAllowedMallocSize ||
        size > max_user_defined_malloc_size) {
      if (AllocatorMayReturnNull()) {
        Report("WARNING: MemProfiler failed to allocate 0x%zx bytes\n", size);
        return nullptr;
      }
      uptr malloc_limit =
          Min(kMaxAllowedMallocSize, max_user_defined_malloc_size);
      ReportAllocationSizeTooBig(size, malloc_limit, stack);
    }

    MemprofThread *t = GetCurrentThread();
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
    uptr beg_plus_header = alloc_beg + kChunkHeaderSize;
    uptr user_beg = beg_plus_header;
    if (!IsAligned(user_beg, alignment))
      user_beg = RoundUpTo(user_beg, alignment);
    uptr user_end = user_beg + size;
    CHECK_LE(user_end, alloc_end);
    uptr chunk_beg = user_beg - kChunkHeaderSize;
    MemprofChunk *m = reinterpret_cast<MemprofChunk *>(chunk_beg);
    m->from_memalign = alloc_beg != chunk_beg;
    CHECK(size);

    m->cpu_id = GetCpuId();
    m->timestamp_ms = GetTimestamp();
    m->alloc_context_id = StackDepotPut(*stack);

    uptr size_rounded_down_to_granularity =
        RoundDownTo(size, SHADOW_GRANULARITY);
    if (size_rounded_down_to_granularity)
      ClearShadow(user_beg, size_rounded_down_to_granularity);

    MemprofStats &thread_stats = GetCurrentThreadStats();
    thread_stats.mallocs++;
    thread_stats.malloced += size;
    thread_stats.malloced_overhead += needed_size - size;
    if (needed_size > SizeClassMap::kMaxSize)
      thread_stats.malloc_large++;
    else
      thread_stats.malloced_by_size[SizeClassMap::ClassID(needed_size)]++;

    void *res = reinterpret_cast<void *>(user_beg);
    atomic_store(&m->user_requested_size, size, memory_order_release);
    if (alloc_beg != chunk_beg) {
      CHECK_LE(alloc_beg + sizeof(LargeChunkHeader), chunk_beg);
      reinterpret_cast<LargeChunkHeader *>(alloc_beg)->Set(m);
    }
    RunMallocHooks(res, size);
    return res;
  }

  void Deallocate(void *ptr, uptr delete_size, uptr delete_alignment,
                  BufferedStackTrace *stack, AllocType alloc_type) {
    uptr p = reinterpret_cast<uptr>(ptr);
    if (p == 0)
      return;

    RunFreeHooks(ptr);

    uptr chunk_beg = p - kChunkHeaderSize;
    MemprofChunk *m = reinterpret_cast<MemprofChunk *>(chunk_beg);

    u64 user_requested_size =
        atomic_exchange(&m->user_requested_size, 0, memory_order_acquire);
    if (memprof_inited && atomic_load_relaxed(&constructed) &&
        !atomic_load_relaxed(&destructing)) {
      MemInfoBlock newMIB = this->CreateNewMIB(p, m, user_requested_size);
      InsertOrMerge(m->alloc_context_id, newMIB, MIBMap);
    }

    MemprofStats &thread_stats = GetCurrentThreadStats();
    thread_stats.frees++;
    thread_stats.freed += user_requested_size;

    void *alloc_beg = m->AllocBeg();
    if (alloc_beg != m) {
      // Clear the magic value, as allocator internals may overwrite the
      // contents of deallocated chunk, confusing GetMemprofChunk lookup.
      reinterpret_cast<LargeChunkHeader *>(alloc_beg)->Set(nullptr);
    }

    MemprofThread *t = GetCurrentThread();
    if (t) {
      AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
      allocator.Deallocate(cache, alloc_beg);
    } else {
      SpinMutexLock l(&fallback_mutex);
      AllocatorCache *cache = &fallback_allocator_cache;
      allocator.Deallocate(cache, alloc_beg);
    }
  }

  void *Reallocate(void *old_ptr, uptr new_size, BufferedStackTrace *stack) {
    CHECK(old_ptr && new_size);
    uptr p = reinterpret_cast<uptr>(old_ptr);
    uptr chunk_beg = p - kChunkHeaderSize;
    MemprofChunk *m = reinterpret_cast<MemprofChunk *>(chunk_beg);

    MemprofStats &thread_stats = GetCurrentThreadStats();
    thread_stats.reallocs++;
    thread_stats.realloced += new_size;

    void *new_ptr = Allocate(new_size, 8, stack, FROM_MALLOC);
    if (new_ptr) {
      CHECK_NE(REAL(memcpy), nullptr);
      uptr memcpy_size = Min(new_size, m->UsedSize());
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
    void *ptr = Allocate(nmemb * size, 8, stack, FROM_MALLOC);
    // If the memory comes from the secondary allocator no need to clear it
    // as it comes directly from mmap.
    if (ptr && allocator.FromPrimary(ptr))
      REAL(memset)(ptr, 0, nmemb * size);
    return ptr;
  }

  void CommitBack(MemprofThreadLocalMallocStorage *ms) {
    AllocatorCache *ac = GetAllocatorCache(ms);
    allocator.SwallowCache(ac);
  }

  // -------------------------- Chunk lookup ----------------------

  // Assumes alloc_beg == allocator.GetBlockBegin(alloc_beg).
  MemprofChunk *GetMemprofChunk(void *alloc_beg, u64 &user_requested_size) {
    if (!alloc_beg)
      return nullptr;
    MemprofChunk *p = reinterpret_cast<LargeChunkHeader *>(alloc_beg)->Get();
    if (!p) {
      if (!allocator.FromPrimary(alloc_beg))
        return nullptr;
      p = reinterpret_cast<MemprofChunk *>(alloc_beg);
    }
    // The size is reset to 0 on deallocation (and a min of 1 on
    // allocation).
    user_requested_size =
        atomic_load(&p->user_requested_size, memory_order_acquire);
    if (user_requested_size)
      return p;
    return nullptr;
  }

  MemprofChunk *GetMemprofChunkByAddr(uptr p, u64 &user_requested_size) {
    void *alloc_beg = allocator.GetBlockBegin(reinterpret_cast<void *>(p));
    return GetMemprofChunk(alloc_beg, user_requested_size);
  }

  uptr AllocationSize(uptr p) {
    u64 user_requested_size;
    MemprofChunk *m = GetMemprofChunkByAddr(p, user_requested_size);
    if (!m)
      return 0;
    if (m->Beg() != p)
      return 0;
    return user_requested_size;
  }

  uptr AllocationSizeFast(uptr p) {
    return reinterpret_cast<MemprofChunk *>(p - kChunkHeaderSize)->UsedSize();
  }

  void Purge() { allocator.ForceReleaseToOS(); }

  void PrintStats() { allocator.PrintStats(); }

  void ForceLock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
    allocator.ForceLock();
    fallback_mutex.Lock();
  }

  void ForceUnlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
    fallback_mutex.Unlock();
    allocator.ForceUnlock();
  }
};

static Allocator instance(LINKER_INITIALIZED);

static MemprofAllocator &get_allocator() { return instance.allocator; }

void InitializeAllocator() { instance.InitLinkerInitialized(); }

void MemprofThreadLocalMallocStorage::CommitBack() {
  instance.CommitBack(this);
}

void PrintInternalAllocatorStats() { instance.PrintStats(); }

void memprof_free(void *ptr, BufferedStackTrace *stack, AllocType alloc_type) {
  instance.Deallocate(ptr, 0, 0, stack, alloc_type);
}

void memprof_delete(void *ptr, uptr size, uptr alignment,
                    BufferedStackTrace *stack, AllocType alloc_type) {
  instance.Deallocate(ptr, size, alignment, stack, alloc_type);
}

void *memprof_malloc(uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(instance.Allocate(size, 8, stack, FROM_MALLOC));
}

void *memprof_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(instance.Calloc(nmemb, size, stack));
}

void *memprof_reallocarray(void *p, uptr nmemb, uptr size,
                           BufferedStackTrace *stack) {
  if (UNLIKELY(CheckForCallocOverflow(size, nmemb))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportReallocArrayOverflow(nmemb, size, stack);
  }
  return memprof_realloc(p, nmemb * size, stack);
}

void *memprof_realloc(void *p, uptr size, BufferedStackTrace *stack) {
  if (!p)
    return SetErrnoOnNull(instance.Allocate(size, 8, stack, FROM_MALLOC));
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

void *memprof_valloc(uptr size, BufferedStackTrace *stack) {
  return SetErrnoOnNull(
      instance.Allocate(size, GetPageSizeCached(), stack, FROM_MALLOC));
}

void *memprof_pvalloc(uptr size, BufferedStackTrace *stack) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(size, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportPvallocOverflow(size, stack);
  }
  // pvalloc(0) should allocate one page.
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return SetErrnoOnNull(instance.Allocate(size, PageSize, stack, FROM_MALLOC));
}

void *memprof_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                       AllocType alloc_type) {
  if (UNLIKELY(!IsPowerOfTwo(alignment))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAllocationAlignment(alignment, stack);
  }
  return SetErrnoOnNull(instance.Allocate(size, alignment, stack, alloc_type));
}

void *memprof_aligned_alloc(uptr alignment, uptr size,
                            BufferedStackTrace *stack) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    ReportInvalidAlignedAllocAlignment(size, alignment, stack);
  }
  return SetErrnoOnNull(instance.Allocate(size, alignment, stack, FROM_MALLOC));
}

int memprof_posix_memalign(void **memptr, uptr alignment, uptr size,
                           BufferedStackTrace *stack) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    ReportInvalidPosixMemalignAlignment(alignment, stack);
  }
  void *ptr = instance.Allocate(size, alignment, stack, FROM_MALLOC);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by Allocate.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

static const void *memprof_malloc_begin(const void *p) {
  u64 user_requested_size;
  MemprofChunk *m =
      instance.GetMemprofChunkByAddr((uptr)p, user_requested_size);
  if (!m)
    return nullptr;
  if (user_requested_size == 0)
    return nullptr;

  return (const void *)m->Beg();
}

uptr memprof_malloc_usable_size(const void *ptr) {
  if (!ptr)
    return 0;
  uptr usable_size = instance.AllocationSize(reinterpret_cast<uptr>(ptr));
  return usable_size;
}

} // namespace __memprof

// ---------------------- Interface ---------------- {{{1
using namespace __memprof;

uptr __sanitizer_get_estimated_allocated_size(uptr size) { return size; }

int __sanitizer_get_ownership(const void *p) {
  return memprof_malloc_usable_size(p) != 0;
}

const void *__sanitizer_get_allocated_begin(const void *p) {
  return memprof_malloc_begin(p);
}

uptr __sanitizer_get_allocated_size(const void *p) {
  return memprof_malloc_usable_size(p);
}

uptr __sanitizer_get_allocated_size_fast(const void *p) {
  DCHECK_EQ(p, __sanitizer_get_allocated_begin(p));
  uptr ret = instance.AllocationSizeFast(reinterpret_cast<uptr>(p));
  DCHECK_EQ(ret, __sanitizer_get_allocated_size(p));
  return ret;
}

void __sanitizer_purge_allocator() { instance.Purge(); }

int __memprof_profile_dump() {
  instance.FinishAndWrite();
  // In the future we may want to return non-zero if there are any errors
  // detected during the dumping process.
  return 0;
}

void __memprof_profile_reset() {
  if (report_file.fd != kInvalidFd && report_file.fd != kStdoutFd &&
      report_file.fd != kStderrFd) {
    CloseFile(report_file.fd);
    // Setting the file descriptor to kInvalidFd ensures that we will reopen the
    // file when invoking Write again.
    report_file.fd = kInvalidFd;
  }
}
