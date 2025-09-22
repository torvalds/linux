//===-- secondary.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_SECONDARY_H_
#define SCUDO_SECONDARY_H_

#include "chunk.h"
#include "common.h"
#include "list.h"
#include "mem_map.h"
#include "memtag.h"
#include "mutex.h"
#include "options.h"
#include "stats.h"
#include "string_utils.h"
#include "thread_annotations.h"

namespace scudo {

// This allocator wraps the platform allocation primitives, and as such is on
// the slower side and should preferably be used for larger sized allocations.
// Blocks allocated will be preceded and followed by a guard page, and hold
// their own header that is not checksummed: the guard pages and the Combined
// header should be enough for our purpose.

namespace LargeBlock {

struct alignas(Max<uptr>(archSupportsMemoryTagging()
                             ? archMemoryTagGranuleSize()
                             : 1,
                         1U << SCUDO_MIN_ALIGNMENT_LOG)) Header {
  LargeBlock::Header *Prev;
  LargeBlock::Header *Next;
  uptr CommitBase;
  uptr CommitSize;
  MemMapT MemMap;
};

static_assert(sizeof(Header) % (1U << SCUDO_MIN_ALIGNMENT_LOG) == 0, "");
static_assert(!archSupportsMemoryTagging() ||
                  sizeof(Header) % archMemoryTagGranuleSize() == 0,
              "");

constexpr uptr getHeaderSize() { return sizeof(Header); }

template <typename Config> static uptr addHeaderTag(uptr Ptr) {
  if (allocatorSupportsMemoryTagging<Config>())
    return addFixedTag(Ptr, 1);
  return Ptr;
}

template <typename Config> static Header *getHeader(uptr Ptr) {
  return reinterpret_cast<Header *>(addHeaderTag<Config>(Ptr)) - 1;
}

template <typename Config> static Header *getHeader(const void *Ptr) {
  return getHeader<Config>(reinterpret_cast<uptr>(Ptr));
}

} // namespace LargeBlock

static inline void unmap(LargeBlock::Header *H) {
  // Note that the `H->MapMap` is stored on the pages managed by itself. Take
  // over the ownership before unmap() so that any operation along with unmap()
  // won't touch inaccessible pages.
  MemMapT MemMap = H->MemMap;
  MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
}

namespace {
struct CachedBlock {
  uptr CommitBase = 0;
  uptr CommitSize = 0;
  uptr BlockBegin = 0;
  MemMapT MemMap = {};
  u64 Time = 0;

  bool isValid() { return CommitBase != 0; }

  void invalidate() { CommitBase = 0; }
};
} // namespace

template <typename Config> class MapAllocatorNoCache {
public:
  void init(UNUSED s32 ReleaseToOsInterval) {}
  bool retrieve(UNUSED Options Options, UNUSED uptr Size, UNUSED uptr Alignment,
                UNUSED uptr HeadersSize, UNUSED LargeBlock::Header **H,
                UNUSED bool *Zeroed) {
    return false;
  }
  void store(UNUSED Options Options, LargeBlock::Header *H) { unmap(H); }
  bool canCache(UNUSED uptr Size) { return false; }
  void disable() {}
  void enable() {}
  void releaseToOS() {}
  void disableMemoryTagging() {}
  void unmapTestOnly() {}
  bool setOption(Option O, UNUSED sptr Value) {
    if (O == Option::ReleaseInterval || O == Option::MaxCacheEntriesCount ||
        O == Option::MaxCacheEntrySize)
      return false;
    // Not supported by the Secondary Cache, but not an error either.
    return true;
  }

  void getStats(UNUSED ScopedString *Str) {
    Str->append("Secondary Cache Disabled\n");
  }
};

static const uptr MaxUnusedCachePages = 4U;

template <typename Config>
bool mapSecondary(const Options &Options, uptr CommitBase, uptr CommitSize,
                  uptr AllocPos, uptr Flags, MemMapT &MemMap) {
  Flags |= MAP_RESIZABLE;
  Flags |= MAP_ALLOWNOMEM;

  const uptr PageSize = getPageSizeCached();
  if (SCUDO_TRUSTY) {
    /*
     * On Trusty we need AllocPos to be usable for shared memory, which cannot
     * cross multiple mappings. This means we need to split around AllocPos
     * and not over it. We can only do this if the address is page-aligned.
     */
    const uptr TaggedSize = AllocPos - CommitBase;
    if (useMemoryTagging<Config>(Options) && isAligned(TaggedSize, PageSize)) {
      DCHECK_GT(TaggedSize, 0);
      return MemMap.remap(CommitBase, TaggedSize, "scudo:secondary",
                          MAP_MEMTAG | Flags) &&
             MemMap.remap(AllocPos, CommitSize - TaggedSize, "scudo:secondary",
                          Flags);
    } else {
      const uptr RemapFlags =
          (useMemoryTagging<Config>(Options) ? MAP_MEMTAG : 0) | Flags;
      return MemMap.remap(CommitBase, CommitSize, "scudo:secondary",
                          RemapFlags);
    }
  }

  const uptr MaxUnusedCacheBytes = MaxUnusedCachePages * PageSize;
  if (useMemoryTagging<Config>(Options) && CommitSize > MaxUnusedCacheBytes) {
    const uptr UntaggedPos = Max(AllocPos, CommitBase + MaxUnusedCacheBytes);
    return MemMap.remap(CommitBase, UntaggedPos - CommitBase, "scudo:secondary",
                        MAP_MEMTAG | Flags) &&
           MemMap.remap(UntaggedPos, CommitBase + CommitSize - UntaggedPos,
                        "scudo:secondary", Flags);
  } else {
    const uptr RemapFlags =
        (useMemoryTagging<Config>(Options) ? MAP_MEMTAG : 0) | Flags;
    return MemMap.remap(CommitBase, CommitSize, "scudo:secondary", RemapFlags);
  }
}

// Template specialization to avoid producing zero-length array
template <typename T, size_t Size> class NonZeroLengthArray {
public:
  T &operator[](uptr Idx) { return values[Idx]; }

private:
  T values[Size];
};
template <typename T> class NonZeroLengthArray<T, 0> {
public:
  T &operator[](uptr UNUSED Idx) { UNREACHABLE("Unsupported!"); }
};

template <typename Config> class MapAllocatorCache {
public:
  void getStats(ScopedString *Str) {
    ScopedLock L(Mutex);
    uptr Integral;
    uptr Fractional;
    computePercentage(SuccessfulRetrieves, CallsToRetrieve, &Integral,
                      &Fractional);
    const s32 Interval = atomic_load_relaxed(&ReleaseToOsIntervalMs);
    Str->append(
        "Stats: MapAllocatorCache: EntriesCount: %d, "
        "MaxEntriesCount: %u, MaxEntrySize: %zu, ReleaseToOsIntervalMs = %d\n",
        EntriesCount, atomic_load_relaxed(&MaxEntriesCount),
        atomic_load_relaxed(&MaxEntrySize), Interval >= 0 ? Interval : -1);
    Str->append("Stats: CacheRetrievalStats: SuccessRate: %u/%u "
                "(%zu.%02zu%%)\n",
                SuccessfulRetrieves, CallsToRetrieve, Integral, Fractional);
    for (CachedBlock Entry : Entries) {
      if (!Entry.isValid())
        continue;
      Str->append("StartBlockAddress: 0x%zx, EndBlockAddress: 0x%zx, "
                  "BlockSize: %zu %s\n",
                  Entry.CommitBase, Entry.CommitBase + Entry.CommitSize,
                  Entry.CommitSize, Entry.Time == 0 ? "[R]" : "");
    }
  }

  // Ensure the default maximum specified fits the array.
  static_assert(Config::getDefaultMaxEntriesCount() <=
                    Config::getEntriesArraySize(),
                "");

  void init(s32 ReleaseToOsInterval) NO_THREAD_SAFETY_ANALYSIS {
    DCHECK_EQ(EntriesCount, 0U);
    setOption(Option::MaxCacheEntriesCount,
              static_cast<sptr>(Config::getDefaultMaxEntriesCount()));
    setOption(Option::MaxCacheEntrySize,
              static_cast<sptr>(Config::getDefaultMaxEntrySize()));
    // The default value in the cache config has the higher priority.
    if (Config::getDefaultReleaseToOsIntervalMs() != INT32_MIN)
      ReleaseToOsInterval = Config::getDefaultReleaseToOsIntervalMs();
    setOption(Option::ReleaseInterval, static_cast<sptr>(ReleaseToOsInterval));
  }

  void store(const Options &Options, LargeBlock::Header *H) EXCLUDES(Mutex) {
    if (!canCache(H->CommitSize))
      return unmap(H);

    bool EntryCached = false;
    bool EmptyCache = false;
    const s32 Interval = atomic_load_relaxed(&ReleaseToOsIntervalMs);
    const u64 Time = getMonotonicTimeFast();
    const u32 MaxCount = atomic_load_relaxed(&MaxEntriesCount);
    CachedBlock Entry;
    Entry.CommitBase = H->CommitBase;
    Entry.CommitSize = H->CommitSize;
    Entry.BlockBegin = reinterpret_cast<uptr>(H + 1);
    Entry.MemMap = H->MemMap;
    Entry.Time = Time;
    if (useMemoryTagging<Config>(Options)) {
      if (Interval == 0 && !SCUDO_FUCHSIA) {
        // Release the memory and make it inaccessible at the same time by
        // creating a new MAP_NOACCESS mapping on top of the existing mapping.
        // Fuchsia does not support replacing mappings by creating a new mapping
        // on top so we just do the two syscalls there.
        Entry.Time = 0;
        mapSecondary<Config>(Options, Entry.CommitBase, Entry.CommitSize,
                             Entry.CommitBase, MAP_NOACCESS, Entry.MemMap);
      } else {
        Entry.MemMap.setMemoryPermission(Entry.CommitBase, Entry.CommitSize,
                                         MAP_NOACCESS);
      }
    } else if (Interval == 0) {
      Entry.MemMap.releaseAndZeroPagesToOS(Entry.CommitBase, Entry.CommitSize);
      Entry.Time = 0;
    }
    do {
      ScopedLock L(Mutex);
      if (useMemoryTagging<Config>(Options) && QuarantinePos == -1U) {
        // If we get here then memory tagging was disabled in between when we
        // read Options and when we locked Mutex. We can't insert our entry into
        // the quarantine or the cache because the permissions would be wrong so
        // just unmap it.
        break;
      }
      if (Config::getQuarantineSize() && useMemoryTagging<Config>(Options)) {
        QuarantinePos =
            (QuarantinePos + 1) % Max(Config::getQuarantineSize(), 1u);
        if (!Quarantine[QuarantinePos].isValid()) {
          Quarantine[QuarantinePos] = Entry;
          return;
        }
        CachedBlock PrevEntry = Quarantine[QuarantinePos];
        Quarantine[QuarantinePos] = Entry;
        if (OldestTime == 0)
          OldestTime = Entry.Time;
        Entry = PrevEntry;
      }
      if (EntriesCount >= MaxCount) {
        if (IsFullEvents++ == 4U)
          EmptyCache = true;
      } else {
        for (u32 I = 0; I < MaxCount; I++) {
          if (Entries[I].isValid())
            continue;
          if (I != 0)
            Entries[I] = Entries[0];
          Entries[0] = Entry;
          EntriesCount++;
          if (OldestTime == 0)
            OldestTime = Entry.Time;
          EntryCached = true;
          break;
        }
      }
    } while (0);
    if (EmptyCache)
      empty();
    else if (Interval >= 0)
      releaseOlderThan(Time - static_cast<u64>(Interval) * 1000000);
    if (!EntryCached)
      Entry.MemMap.unmap(Entry.MemMap.getBase(), Entry.MemMap.getCapacity());
  }

  bool retrieve(Options Options, uptr Size, uptr Alignment, uptr HeadersSize,
                LargeBlock::Header **H, bool *Zeroed) EXCLUDES(Mutex) {
    const uptr PageSize = getPageSizeCached();
    const u32 MaxCount = atomic_load_relaxed(&MaxEntriesCount);
    // 10% of the requested size proved to be the optimal choice for
    // retrieving cached blocks after testing several options.
    constexpr u32 FragmentedBytesDivisor = 10;
    bool Found = false;
    CachedBlock Entry;
    uptr EntryHeaderPos = 0;
    {
      ScopedLock L(Mutex);
      CallsToRetrieve++;
      if (EntriesCount == 0)
        return false;
      u32 OptimalFitIndex = 0;
      uptr MinDiff = UINTPTR_MAX;
      for (u32 I = 0; I < MaxCount; I++) {
        if (!Entries[I].isValid())
          continue;
        const uptr CommitBase = Entries[I].CommitBase;
        const uptr CommitSize = Entries[I].CommitSize;
        const uptr AllocPos =
            roundDown(CommitBase + CommitSize - Size, Alignment);
        const uptr HeaderPos = AllocPos - HeadersSize;
        if (HeaderPos > CommitBase + CommitSize)
          continue;
        if (HeaderPos < CommitBase ||
            AllocPos > CommitBase + PageSize * MaxUnusedCachePages) {
          continue;
        }
        Found = true;
        const uptr Diff = HeaderPos - CommitBase;
        // immediately use a cached block if it's size is close enough to the
        // requested size.
        const uptr MaxAllowedFragmentedBytes =
            (CommitBase + CommitSize - HeaderPos) / FragmentedBytesDivisor;
        if (Diff <= MaxAllowedFragmentedBytes) {
          OptimalFitIndex = I;
          EntryHeaderPos = HeaderPos;
          break;
        }
        // keep track of the smallest cached block
        // that is greater than (AllocSize + HeaderSize)
        if (Diff > MinDiff)
          continue;
        OptimalFitIndex = I;
        MinDiff = Diff;
        EntryHeaderPos = HeaderPos;
      }
      if (Found) {
        Entry = Entries[OptimalFitIndex];
        Entries[OptimalFitIndex].invalidate();
        EntriesCount--;
        SuccessfulRetrieves++;
      }
    }
    if (!Found)
      return false;

    *H = reinterpret_cast<LargeBlock::Header *>(
        LargeBlock::addHeaderTag<Config>(EntryHeaderPos));
    *Zeroed = Entry.Time == 0;
    if (useMemoryTagging<Config>(Options))
      Entry.MemMap.setMemoryPermission(Entry.CommitBase, Entry.CommitSize, 0);
    uptr NewBlockBegin = reinterpret_cast<uptr>(*H + 1);
    if (useMemoryTagging<Config>(Options)) {
      if (*Zeroed) {
        storeTags(LargeBlock::addHeaderTag<Config>(Entry.CommitBase),
                  NewBlockBegin);
      } else if (Entry.BlockBegin < NewBlockBegin) {
        storeTags(Entry.BlockBegin, NewBlockBegin);
      } else {
        storeTags(untagPointer(NewBlockBegin), untagPointer(Entry.BlockBegin));
      }
    }
    (*H)->CommitBase = Entry.CommitBase;
    (*H)->CommitSize = Entry.CommitSize;
    (*H)->MemMap = Entry.MemMap;
    return true;
  }

  bool canCache(uptr Size) {
    return atomic_load_relaxed(&MaxEntriesCount) != 0U &&
           Size <= atomic_load_relaxed(&MaxEntrySize);
  }

  bool setOption(Option O, sptr Value) {
    if (O == Option::ReleaseInterval) {
      const s32 Interval = Max(
          Min(static_cast<s32>(Value), Config::getMaxReleaseToOsIntervalMs()),
          Config::getMinReleaseToOsIntervalMs());
      atomic_store_relaxed(&ReleaseToOsIntervalMs, Interval);
      return true;
    }
    if (O == Option::MaxCacheEntriesCount) {
      if (Value < 0)
        return false;
      atomic_store_relaxed(
          &MaxEntriesCount,
          Min<u32>(static_cast<u32>(Value), Config::getEntriesArraySize()));
      return true;
    }
    if (O == Option::MaxCacheEntrySize) {
      atomic_store_relaxed(&MaxEntrySize, static_cast<uptr>(Value));
      return true;
    }
    // Not supported by the Secondary Cache, but not an error either.
    return true;
  }

  void releaseToOS() { releaseOlderThan(UINT64_MAX); }

  void disableMemoryTagging() EXCLUDES(Mutex) {
    ScopedLock L(Mutex);
    for (u32 I = 0; I != Config::getQuarantineSize(); ++I) {
      if (Quarantine[I].isValid()) {
        MemMapT &MemMap = Quarantine[I].MemMap;
        MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
        Quarantine[I].invalidate();
      }
    }
    const u32 MaxCount = atomic_load_relaxed(&MaxEntriesCount);
    for (u32 I = 0; I < MaxCount; I++) {
      if (Entries[I].isValid()) {
        Entries[I].MemMap.setMemoryPermission(Entries[I].CommitBase,
                                              Entries[I].CommitSize, 0);
      }
    }
    QuarantinePos = -1U;
  }

  void disable() NO_THREAD_SAFETY_ANALYSIS { Mutex.lock(); }

  void enable() NO_THREAD_SAFETY_ANALYSIS { Mutex.unlock(); }

  void unmapTestOnly() { empty(); }

private:
  void empty() {
    MemMapT MapInfo[Config::getEntriesArraySize()];
    uptr N = 0;
    {
      ScopedLock L(Mutex);
      for (uptr I = 0; I < Config::getEntriesArraySize(); I++) {
        if (!Entries[I].isValid())
          continue;
        MapInfo[N] = Entries[I].MemMap;
        Entries[I].invalidate();
        N++;
      }
      EntriesCount = 0;
      IsFullEvents = 0;
    }
    for (uptr I = 0; I < N; I++) {
      MemMapT &MemMap = MapInfo[I];
      MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
    }
  }

  void releaseIfOlderThan(CachedBlock &Entry, u64 Time) REQUIRES(Mutex) {
    if (!Entry.isValid() || !Entry.Time)
      return;
    if (Entry.Time > Time) {
      if (OldestTime == 0 || Entry.Time < OldestTime)
        OldestTime = Entry.Time;
      return;
    }
    Entry.MemMap.releaseAndZeroPagesToOS(Entry.CommitBase, Entry.CommitSize);
    Entry.Time = 0;
  }

  void releaseOlderThan(u64 Time) EXCLUDES(Mutex) {
    ScopedLock L(Mutex);
    if (!EntriesCount || OldestTime == 0 || OldestTime > Time)
      return;
    OldestTime = 0;
    for (uptr I = 0; I < Config::getQuarantineSize(); I++)
      releaseIfOlderThan(Quarantine[I], Time);
    for (uptr I = 0; I < Config::getEntriesArraySize(); I++)
      releaseIfOlderThan(Entries[I], Time);
  }

  HybridMutex Mutex;
  u32 EntriesCount GUARDED_BY(Mutex) = 0;
  u32 QuarantinePos GUARDED_BY(Mutex) = 0;
  atomic_u32 MaxEntriesCount = {};
  atomic_uptr MaxEntrySize = {};
  u64 OldestTime GUARDED_BY(Mutex) = 0;
  u32 IsFullEvents GUARDED_BY(Mutex) = 0;
  atomic_s32 ReleaseToOsIntervalMs = {};
  u32 CallsToRetrieve GUARDED_BY(Mutex) = 0;
  u32 SuccessfulRetrieves GUARDED_BY(Mutex) = 0;

  CachedBlock Entries[Config::getEntriesArraySize()] GUARDED_BY(Mutex) = {};
  NonZeroLengthArray<CachedBlock, Config::getQuarantineSize()>
      Quarantine GUARDED_BY(Mutex) = {};
};

template <typename Config> class MapAllocator {
public:
  void init(GlobalStats *S,
            s32 ReleaseToOsInterval = -1) NO_THREAD_SAFETY_ANALYSIS {
    DCHECK_EQ(AllocatedBytes, 0U);
    DCHECK_EQ(FreedBytes, 0U);
    Cache.init(ReleaseToOsInterval);
    Stats.init();
    if (LIKELY(S))
      S->link(&Stats);
  }

  void *allocate(const Options &Options, uptr Size, uptr AlignmentHint = 0,
                 uptr *BlockEnd = nullptr,
                 FillContentsMode FillContents = NoFill);

  void deallocate(const Options &Options, void *Ptr);

  static uptr getBlockEnd(void *Ptr) {
    auto *B = LargeBlock::getHeader<Config>(Ptr);
    return B->CommitBase + B->CommitSize;
  }

  static uptr getBlockSize(void *Ptr) {
    return getBlockEnd(Ptr) - reinterpret_cast<uptr>(Ptr);
  }

  static constexpr uptr getHeadersSize() {
    return Chunk::getHeaderSize() + LargeBlock::getHeaderSize();
  }

  void disable() NO_THREAD_SAFETY_ANALYSIS {
    Mutex.lock();
    Cache.disable();
  }

  void enable() NO_THREAD_SAFETY_ANALYSIS {
    Cache.enable();
    Mutex.unlock();
  }

  template <typename F> void iterateOverBlocks(F Callback) const {
    Mutex.assertHeld();

    for (const auto &H : InUseBlocks) {
      uptr Ptr = reinterpret_cast<uptr>(&H) + LargeBlock::getHeaderSize();
      if (allocatorSupportsMemoryTagging<Config>())
        Ptr = untagPointer(Ptr);
      Callback(Ptr);
    }
  }

  bool canCache(uptr Size) { return Cache.canCache(Size); }

  bool setOption(Option O, sptr Value) { return Cache.setOption(O, Value); }

  void releaseToOS() { Cache.releaseToOS(); }

  void disableMemoryTagging() { Cache.disableMemoryTagging(); }

  void unmapTestOnly() { Cache.unmapTestOnly(); }

  void getStats(ScopedString *Str);

private:
  typename Config::template CacheT<typename Config::CacheConfig> Cache;

  mutable HybridMutex Mutex;
  DoublyLinkedList<LargeBlock::Header> InUseBlocks GUARDED_BY(Mutex);
  uptr AllocatedBytes GUARDED_BY(Mutex) = 0;
  uptr FreedBytes GUARDED_BY(Mutex) = 0;
  uptr FragmentedBytes GUARDED_BY(Mutex) = 0;
  uptr LargestSize GUARDED_BY(Mutex) = 0;
  u32 NumberOfAllocs GUARDED_BY(Mutex) = 0;
  u32 NumberOfFrees GUARDED_BY(Mutex) = 0;
  LocalStats Stats GUARDED_BY(Mutex);
};

// As with the Primary, the size passed to this function includes any desired
// alignment, so that the frontend can align the user allocation. The hint
// parameter allows us to unmap spurious memory when dealing with larger
// (greater than a page) alignments on 32-bit platforms.
// Due to the sparsity of address space available on those platforms, requesting
// an allocation from the Secondary with a large alignment would end up wasting
// VA space (even though we are not committing the whole thing), hence the need
// to trim off some of the reserved space.
// For allocations requested with an alignment greater than or equal to a page,
// the committed memory will amount to something close to Size - AlignmentHint
// (pending rounding and headers).
template <typename Config>
void *MapAllocator<Config>::allocate(const Options &Options, uptr Size,
                                     uptr Alignment, uptr *BlockEndPtr,
                                     FillContentsMode FillContents) {
  if (Options.get(OptionBit::AddLargeAllocationSlack))
    Size += 1UL << SCUDO_MIN_ALIGNMENT_LOG;
  Alignment = Max(Alignment, uptr(1U) << SCUDO_MIN_ALIGNMENT_LOG);
  const uptr PageSize = getPageSizeCached();

  // Note that cached blocks may have aligned address already. Thus we simply
  // pass the required size (`Size` + `getHeadersSize()`) to do cache look up.
  const uptr MinNeededSizeForCache = roundUp(Size + getHeadersSize(), PageSize);

  if (Alignment < PageSize && Cache.canCache(MinNeededSizeForCache)) {
    LargeBlock::Header *H;
    bool Zeroed;
    if (Cache.retrieve(Options, Size, Alignment, getHeadersSize(), &H,
                       &Zeroed)) {
      const uptr BlockEnd = H->CommitBase + H->CommitSize;
      if (BlockEndPtr)
        *BlockEndPtr = BlockEnd;
      uptr HInt = reinterpret_cast<uptr>(H);
      if (allocatorSupportsMemoryTagging<Config>())
        HInt = untagPointer(HInt);
      const uptr PtrInt = HInt + LargeBlock::getHeaderSize();
      void *Ptr = reinterpret_cast<void *>(PtrInt);
      if (FillContents && !Zeroed)
        memset(Ptr, FillContents == ZeroFill ? 0 : PatternFillByte,
               BlockEnd - PtrInt);
      {
        ScopedLock L(Mutex);
        InUseBlocks.push_back(H);
        AllocatedBytes += H->CommitSize;
        FragmentedBytes += H->MemMap.getCapacity() - H->CommitSize;
        NumberOfAllocs++;
        Stats.add(StatAllocated, H->CommitSize);
        Stats.add(StatMapped, H->MemMap.getCapacity());
      }
      return Ptr;
    }
  }

  uptr RoundedSize =
      roundUp(roundUp(Size, Alignment) + getHeadersSize(), PageSize);
  if (Alignment > PageSize)
    RoundedSize += Alignment - PageSize;

  ReservedMemoryT ReservedMemory;
  const uptr MapSize = RoundedSize + 2 * PageSize;
  if (UNLIKELY(!ReservedMemory.create(/*Addr=*/0U, MapSize, nullptr,
                                      MAP_ALLOWNOMEM))) {
    return nullptr;
  }

  // Take the entire ownership of reserved region.
  MemMapT MemMap = ReservedMemory.dispatch(ReservedMemory.getBase(),
                                           ReservedMemory.getCapacity());
  uptr MapBase = MemMap.getBase();
  uptr CommitBase = MapBase + PageSize;
  uptr MapEnd = MapBase + MapSize;

  // In the unlikely event of alignments larger than a page, adjust the amount
  // of memory we want to commit, and trim the extra memory.
  if (UNLIKELY(Alignment >= PageSize)) {
    // For alignments greater than or equal to a page, the user pointer (eg: the
    // pointer that is returned by the C or C++ allocation APIs) ends up on a
    // page boundary , and our headers will live in the preceding page.
    CommitBase = roundUp(MapBase + PageSize + 1, Alignment) - PageSize;
    const uptr NewMapBase = CommitBase - PageSize;
    DCHECK_GE(NewMapBase, MapBase);
    // We only trim the extra memory on 32-bit platforms: 64-bit platforms
    // are less constrained memory wise, and that saves us two syscalls.
    if (SCUDO_WORDSIZE == 32U && NewMapBase != MapBase) {
      MemMap.unmap(MapBase, NewMapBase - MapBase);
      MapBase = NewMapBase;
    }
    const uptr NewMapEnd =
        CommitBase + PageSize + roundUp(Size, PageSize) + PageSize;
    DCHECK_LE(NewMapEnd, MapEnd);
    if (SCUDO_WORDSIZE == 32U && NewMapEnd != MapEnd) {
      MemMap.unmap(NewMapEnd, MapEnd - NewMapEnd);
      MapEnd = NewMapEnd;
    }
  }

  const uptr CommitSize = MapEnd - PageSize - CommitBase;
  const uptr AllocPos = roundDown(CommitBase + CommitSize - Size, Alignment);
  if (!mapSecondary<Config>(Options, CommitBase, CommitSize, AllocPos, 0,
                            MemMap)) {
    MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
    return nullptr;
  }
  const uptr HeaderPos = AllocPos - getHeadersSize();
  LargeBlock::Header *H = reinterpret_cast<LargeBlock::Header *>(
      LargeBlock::addHeaderTag<Config>(HeaderPos));
  if (useMemoryTagging<Config>(Options))
    storeTags(LargeBlock::addHeaderTag<Config>(CommitBase),
              reinterpret_cast<uptr>(H + 1));
  H->CommitBase = CommitBase;
  H->CommitSize = CommitSize;
  H->MemMap = MemMap;
  if (BlockEndPtr)
    *BlockEndPtr = CommitBase + CommitSize;
  {
    ScopedLock L(Mutex);
    InUseBlocks.push_back(H);
    AllocatedBytes += CommitSize;
    FragmentedBytes += H->MemMap.getCapacity() - CommitSize;
    if (LargestSize < CommitSize)
      LargestSize = CommitSize;
    NumberOfAllocs++;
    Stats.add(StatAllocated, CommitSize);
    Stats.add(StatMapped, H->MemMap.getCapacity());
  }
  return reinterpret_cast<void *>(HeaderPos + LargeBlock::getHeaderSize());
}

template <typename Config>
void MapAllocator<Config>::deallocate(const Options &Options, void *Ptr)
    EXCLUDES(Mutex) {
  LargeBlock::Header *H = LargeBlock::getHeader<Config>(Ptr);
  const uptr CommitSize = H->CommitSize;
  {
    ScopedLock L(Mutex);
    InUseBlocks.remove(H);
    FreedBytes += CommitSize;
    FragmentedBytes -= H->MemMap.getCapacity() - CommitSize;
    NumberOfFrees++;
    Stats.sub(StatAllocated, CommitSize);
    Stats.sub(StatMapped, H->MemMap.getCapacity());
  }
  Cache.store(Options, H);
}

template <typename Config>
void MapAllocator<Config>::getStats(ScopedString *Str) EXCLUDES(Mutex) {
  ScopedLock L(Mutex);
  Str->append("Stats: MapAllocator: allocated %u times (%zuK), freed %u times "
              "(%zuK), remains %u (%zuK) max %zuM, Fragmented %zuK\n",
              NumberOfAllocs, AllocatedBytes >> 10, NumberOfFrees,
              FreedBytes >> 10, NumberOfAllocs - NumberOfFrees,
              (AllocatedBytes - FreedBytes) >> 10, LargestSize >> 20,
              FragmentedBytes >> 10);
  Cache.getStats(Str);
}

} // namespace scudo

#endif // SCUDO_SECONDARY_H_
