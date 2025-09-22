//===-- primary32.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_PRIMARY32_H_
#define SCUDO_PRIMARY32_H_

#include "allocator_common.h"
#include "bytemap.h"
#include "common.h"
#include "list.h"
#include "local_cache.h"
#include "options.h"
#include "release.h"
#include "report.h"
#include "stats.h"
#include "string_utils.h"
#include "thread_annotations.h"

namespace scudo {

// SizeClassAllocator32 is an allocator for 32 or 64-bit address space.
//
// It maps Regions of 2^RegionSizeLog bytes aligned on a 2^RegionSizeLog bytes
// boundary, and keeps a bytemap of the mappable address space to track the size
// class they are associated with.
//
// Mapped regions are split into equally sized Blocks according to the size
// class they belong to, and the associated pointers are shuffled to prevent any
// predictable address pattern (the predictability increases with the block
// size).
//
// Regions for size class 0 are special and used to hold TransferBatches, which
// allow to transfer arrays of pointers from the global size class freelist to
// the thread specific freelist for said class, and back.
//
// Memory used by this allocator is never unmapped but can be partially
// reclaimed if the platform allows for it.

template <typename Config> class SizeClassAllocator32 {
public:
  typedef typename Config::CompactPtrT CompactPtrT;
  typedef typename Config::SizeClassMap SizeClassMap;
  static const uptr GroupSizeLog = Config::getGroupSizeLog();
  // The bytemap can only track UINT8_MAX - 1 classes.
  static_assert(SizeClassMap::LargestClassId <= (UINT8_MAX - 1), "");
  // Regions should be large enough to hold the largest Block.
  static_assert((1UL << Config::getRegionSizeLog()) >= SizeClassMap::MaxSize,
                "");
  typedef SizeClassAllocator32<Config> ThisT;
  typedef SizeClassAllocatorLocalCache<ThisT> CacheT;
  typedef TransferBatch<ThisT> TransferBatchT;
  typedef BatchGroup<ThisT> BatchGroupT;

  static_assert(sizeof(BatchGroupT) <= sizeof(TransferBatchT),
                "BatchGroupT uses the same class size as TransferBatchT");

  static uptr getSizeByClassId(uptr ClassId) {
    return (ClassId == SizeClassMap::BatchClassId)
               ? sizeof(TransferBatchT)
               : SizeClassMap::getSizeByClassId(ClassId);
  }

  static bool canAllocate(uptr Size) { return Size <= SizeClassMap::MaxSize; }

  void init(s32 ReleaseToOsInterval) NO_THREAD_SAFETY_ANALYSIS {
    if (SCUDO_FUCHSIA)
      reportError("SizeClassAllocator32 is not supported on Fuchsia");

    if (SCUDO_TRUSTY)
      reportError("SizeClassAllocator32 is not supported on Trusty");

    DCHECK(isAligned(reinterpret_cast<uptr>(this), alignof(ThisT)));
    PossibleRegions.init();
    u32 Seed;
    const u64 Time = getMonotonicTimeFast();
    if (!getRandom(reinterpret_cast<void *>(&Seed), sizeof(Seed)))
      Seed = static_cast<u32>(
          Time ^ (reinterpret_cast<uptr>(SizeClassInfoArray) >> 6));
    for (uptr I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      Sci->RandState = getRandomU32(&Seed);
      // Sci->MaxRegionIndex is already initialized to 0.
      Sci->MinRegionIndex = NumRegions;
      Sci->ReleaseInfo.LastReleaseAtNs = Time;
    }

    // The default value in the primary config has the higher priority.
    if (Config::getDefaultReleaseToOsIntervalMs() != INT32_MIN)
      ReleaseToOsInterval = Config::getDefaultReleaseToOsIntervalMs();
    setOption(Option::ReleaseInterval, static_cast<sptr>(ReleaseToOsInterval));
  }

  void unmapTestOnly() {
    {
      ScopedLock L(RegionsStashMutex);
      while (NumberOfStashedRegions > 0) {
        unmap(reinterpret_cast<void *>(RegionsStash[--NumberOfStashedRegions]),
              RegionSize);
      }
    }

    uptr MinRegionIndex = NumRegions, MaxRegionIndex = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      ScopedLock L(Sci->Mutex);
      if (Sci->MinRegionIndex < MinRegionIndex)
        MinRegionIndex = Sci->MinRegionIndex;
      if (Sci->MaxRegionIndex > MaxRegionIndex)
        MaxRegionIndex = Sci->MaxRegionIndex;
      *Sci = {};
    }

    ScopedLock L(ByteMapMutex);
    for (uptr I = MinRegionIndex; I <= MaxRegionIndex; I++)
      if (PossibleRegions[I])
        unmap(reinterpret_cast<void *>(I * RegionSize), RegionSize);
    PossibleRegions.unmapTestOnly();
  }

  // When all blocks are freed, it has to be the same size as `AllocatedUser`.
  void verifyAllBlocksAreReleasedTestOnly() {
    // `BatchGroup` and `TransferBatch` also use the blocks from BatchClass.
    uptr BatchClassUsedInFreeLists = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      // We have to count BatchClassUsedInFreeLists in other regions first.
      if (I == SizeClassMap::BatchClassId)
        continue;
      SizeClassInfo *Sci = getSizeClassInfo(I);
      ScopedLock L1(Sci->Mutex);
      uptr TotalBlocks = 0;
      for (BatchGroupT &BG : Sci->FreeListInfo.BlockList) {
        // `BG::Batches` are `TransferBatches`. +1 for `BatchGroup`.
        BatchClassUsedInFreeLists += BG.Batches.size() + 1;
        for (const auto &It : BG.Batches)
          TotalBlocks += It.getCount();
      }

      const uptr BlockSize = getSizeByClassId(I);
      DCHECK_EQ(TotalBlocks, Sci->AllocatedUser / BlockSize);
      DCHECK_EQ(Sci->FreeListInfo.PushedBlocks, Sci->FreeListInfo.PoppedBlocks);
    }

    SizeClassInfo *Sci = getSizeClassInfo(SizeClassMap::BatchClassId);
    ScopedLock L1(Sci->Mutex);
    uptr TotalBlocks = 0;
    for (BatchGroupT &BG : Sci->FreeListInfo.BlockList) {
      if (LIKELY(!BG.Batches.empty())) {
        for (const auto &It : BG.Batches)
          TotalBlocks += It.getCount();
      } else {
        // `BatchGroup` with empty freelist doesn't have `TransferBatch` record
        // itself.
        ++TotalBlocks;
      }
    }

    const uptr BlockSize = getSizeByClassId(SizeClassMap::BatchClassId);
    DCHECK_EQ(TotalBlocks + BatchClassUsedInFreeLists,
              Sci->AllocatedUser / BlockSize);
    const uptr BlocksInUse =
        Sci->FreeListInfo.PoppedBlocks - Sci->FreeListInfo.PushedBlocks;
    DCHECK_EQ(BlocksInUse, BatchClassUsedInFreeLists);
  }

  CompactPtrT compactPtr(UNUSED uptr ClassId, uptr Ptr) const {
    return static_cast<CompactPtrT>(Ptr);
  }

  void *decompactPtr(UNUSED uptr ClassId, CompactPtrT CompactPtr) const {
    return reinterpret_cast<void *>(static_cast<uptr>(CompactPtr));
  }

  uptr compactPtrGroupBase(CompactPtrT CompactPtr) {
    const uptr Mask = (static_cast<uptr>(1) << GroupSizeLog) - 1;
    return CompactPtr & ~Mask;
  }

  uptr decompactGroupBase(uptr CompactPtrGroupBase) {
    return CompactPtrGroupBase;
  }

  ALWAYS_INLINE static bool isSmallBlock(uptr BlockSize) {
    const uptr PageSize = getPageSizeCached();
    return BlockSize < PageSize / 16U;
  }

  ALWAYS_INLINE static bool isLargeBlock(uptr BlockSize) {
    const uptr PageSize = getPageSizeCached();
    return BlockSize > PageSize;
  }

  u16 popBlocks(CacheT *C, uptr ClassId, CompactPtrT *ToArray,
                const u16 MaxBlockCount) {
    DCHECK_LT(ClassId, NumClasses);
    SizeClassInfo *Sci = getSizeClassInfo(ClassId);
    ScopedLock L(Sci->Mutex);

    u16 PopCount = popBlocksImpl(C, ClassId, Sci, ToArray, MaxBlockCount);
    if (UNLIKELY(PopCount == 0)) {
      if (UNLIKELY(!populateFreeList(C, ClassId, Sci)))
        return 0U;
      PopCount = popBlocksImpl(C, ClassId, Sci, ToArray, MaxBlockCount);
      DCHECK_NE(PopCount, 0U);
    }

    return PopCount;
  }

  // Push the array of free blocks to the designated batch group.
  void pushBlocks(CacheT *C, uptr ClassId, CompactPtrT *Array, u32 Size) {
    DCHECK_LT(ClassId, NumClasses);
    DCHECK_GT(Size, 0);

    SizeClassInfo *Sci = getSizeClassInfo(ClassId);
    if (ClassId == SizeClassMap::BatchClassId) {
      ScopedLock L(Sci->Mutex);
      pushBatchClassBlocks(Sci, Array, Size);
      return;
    }

    // TODO(chiahungduan): Consider not doing grouping if the group size is not
    // greater than the block size with a certain scale.

    // Sort the blocks so that blocks belonging to the same group can be pushed
    // together.
    bool SameGroup = true;
    for (u32 I = 1; I < Size; ++I) {
      if (compactPtrGroupBase(Array[I - 1]) != compactPtrGroupBase(Array[I]))
        SameGroup = false;
      CompactPtrT Cur = Array[I];
      u32 J = I;
      while (J > 0 &&
             compactPtrGroupBase(Cur) < compactPtrGroupBase(Array[J - 1])) {
        Array[J] = Array[J - 1];
        --J;
      }
      Array[J] = Cur;
    }

    ScopedLock L(Sci->Mutex);
    pushBlocksImpl(C, ClassId, Sci, Array, Size, SameGroup);
  }

  void disable() NO_THREAD_SAFETY_ANALYSIS {
    // The BatchClassId must be locked last since other classes can use it.
    for (sptr I = static_cast<sptr>(NumClasses) - 1; I >= 0; I--) {
      if (static_cast<uptr>(I) == SizeClassMap::BatchClassId)
        continue;
      getSizeClassInfo(static_cast<uptr>(I))->Mutex.lock();
    }
    getSizeClassInfo(SizeClassMap::BatchClassId)->Mutex.lock();
    RegionsStashMutex.lock();
    ByteMapMutex.lock();
  }

  void enable() NO_THREAD_SAFETY_ANALYSIS {
    ByteMapMutex.unlock();
    RegionsStashMutex.unlock();
    getSizeClassInfo(SizeClassMap::BatchClassId)->Mutex.unlock();
    for (uptr I = 0; I < NumClasses; I++) {
      if (I == SizeClassMap::BatchClassId)
        continue;
      getSizeClassInfo(I)->Mutex.unlock();
    }
  }

  template <typename F> void iterateOverBlocks(F Callback) {
    uptr MinRegionIndex = NumRegions, MaxRegionIndex = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      // TODO: The call of `iterateOverBlocks` requires disabling
      // SizeClassAllocator32. We may consider locking each region on demand
      // only.
      Sci->Mutex.assertHeld();
      if (Sci->MinRegionIndex < MinRegionIndex)
        MinRegionIndex = Sci->MinRegionIndex;
      if (Sci->MaxRegionIndex > MaxRegionIndex)
        MaxRegionIndex = Sci->MaxRegionIndex;
    }

    // SizeClassAllocator32 is disabled, i.e., ByteMapMutex is held.
    ByteMapMutex.assertHeld();

    for (uptr I = MinRegionIndex; I <= MaxRegionIndex; I++) {
      if (PossibleRegions[I] &&
          (PossibleRegions[I] - 1U) != SizeClassMap::BatchClassId) {
        const uptr BlockSize = getSizeByClassId(PossibleRegions[I] - 1U);
        const uptr From = I * RegionSize;
        const uptr To = From + (RegionSize / BlockSize) * BlockSize;
        for (uptr Block = From; Block < To; Block += BlockSize)
          Callback(Block);
      }
    }
  }

  void getStats(ScopedString *Str) {
    // TODO(kostyak): get the RSS per region.
    uptr TotalMapped = 0;
    uptr PoppedBlocks = 0;
    uptr PushedBlocks = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      ScopedLock L(Sci->Mutex);
      TotalMapped += Sci->AllocatedUser;
      PoppedBlocks += Sci->FreeListInfo.PoppedBlocks;
      PushedBlocks += Sci->FreeListInfo.PushedBlocks;
    }
    Str->append("Stats: SizeClassAllocator32: %zuM mapped in %zu allocations; "
                "remains %zu\n",
                TotalMapped >> 20, PoppedBlocks, PoppedBlocks - PushedBlocks);
    for (uptr I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      ScopedLock L(Sci->Mutex);
      getStats(Str, I, Sci);
    }
  }

  void getFragmentationInfo(ScopedString *Str) {
    Str->append(
        "Fragmentation Stats: SizeClassAllocator32: page size = %zu bytes\n",
        getPageSizeCached());

    for (uptr I = 1; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      ScopedLock L(Sci->Mutex);
      getSizeClassFragmentationInfo(Sci, I, Str);
    }
  }

  bool setOption(Option O, sptr Value) {
    if (O == Option::ReleaseInterval) {
      const s32 Interval = Max(
          Min(static_cast<s32>(Value), Config::getMaxReleaseToOsIntervalMs()),
          Config::getMinReleaseToOsIntervalMs());
      atomic_store_relaxed(&ReleaseToOsIntervalMs, Interval);
      return true;
    }
    // Not supported by the Primary, but not an error either.
    return true;
  }

  uptr tryReleaseToOS(uptr ClassId, ReleaseToOS ReleaseType) {
    SizeClassInfo *Sci = getSizeClassInfo(ClassId);
    // TODO: Once we have separate locks like primary64, we may consider using
    // tryLock() as well.
    ScopedLock L(Sci->Mutex);
    return releaseToOSMaybe(Sci, ClassId, ReleaseType);
  }

  uptr releaseToOS(ReleaseToOS ReleaseType) {
    uptr TotalReleasedBytes = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      if (I == SizeClassMap::BatchClassId)
        continue;
      SizeClassInfo *Sci = getSizeClassInfo(I);
      ScopedLock L(Sci->Mutex);
      TotalReleasedBytes += releaseToOSMaybe(Sci, I, ReleaseType);
    }
    return TotalReleasedBytes;
  }

  const char *getRegionInfoArrayAddress() const { return nullptr; }
  static uptr getRegionInfoArraySize() { return 0; }

  static BlockInfo findNearestBlock(UNUSED const char *RegionInfoData,
                                    UNUSED uptr Ptr) {
    return {};
  }

  AtomicOptions Options;

private:
  static const uptr NumClasses = SizeClassMap::NumClasses;
  static const uptr RegionSize = 1UL << Config::getRegionSizeLog();
  static const uptr NumRegions = SCUDO_MMAP_RANGE_SIZE >>
                                 Config::getRegionSizeLog();
  static const u32 MaxNumBatches = SCUDO_ANDROID ? 4U : 8U;
  typedef FlatByteMap<NumRegions> ByteMap;

  struct ReleaseToOsInfo {
    uptr BytesInFreeListAtLastCheckpoint;
    uptr RangesReleased;
    uptr LastReleasedBytes;
    u64 LastReleaseAtNs;
  };

  struct BlocksInfo {
    SinglyLinkedList<BatchGroupT> BlockList = {};
    uptr PoppedBlocks = 0;
    uptr PushedBlocks = 0;
  };

  struct alignas(SCUDO_CACHE_LINE_SIZE) SizeClassInfo {
    HybridMutex Mutex;
    BlocksInfo FreeListInfo GUARDED_BY(Mutex);
    uptr CurrentRegion GUARDED_BY(Mutex);
    uptr CurrentRegionAllocated GUARDED_BY(Mutex);
    u32 RandState;
    uptr AllocatedUser GUARDED_BY(Mutex);
    // Lowest & highest region index allocated for this size class, to avoid
    // looping through the whole NumRegions.
    uptr MinRegionIndex GUARDED_BY(Mutex);
    uptr MaxRegionIndex GUARDED_BY(Mutex);
    ReleaseToOsInfo ReleaseInfo GUARDED_BY(Mutex);
  };
  static_assert(sizeof(SizeClassInfo) % SCUDO_CACHE_LINE_SIZE == 0, "");

  uptr computeRegionId(uptr Mem) {
    const uptr Id = Mem >> Config::getRegionSizeLog();
    CHECK_LT(Id, NumRegions);
    return Id;
  }

  uptr allocateRegionSlow() {
    uptr MapSize = 2 * RegionSize;
    const uptr MapBase = reinterpret_cast<uptr>(
        map(nullptr, MapSize, "scudo:primary", MAP_ALLOWNOMEM));
    if (!MapBase)
      return 0;
    const uptr MapEnd = MapBase + MapSize;
    uptr Region = MapBase;
    if (isAligned(Region, RegionSize)) {
      ScopedLock L(RegionsStashMutex);
      if (NumberOfStashedRegions < MaxStashedRegions)
        RegionsStash[NumberOfStashedRegions++] = MapBase + RegionSize;
      else
        MapSize = RegionSize;
    } else {
      Region = roundUp(MapBase, RegionSize);
      unmap(reinterpret_cast<void *>(MapBase), Region - MapBase);
      MapSize = RegionSize;
    }
    const uptr End = Region + MapSize;
    if (End != MapEnd)
      unmap(reinterpret_cast<void *>(End), MapEnd - End);

    DCHECK_EQ(Region % RegionSize, 0U);
    static_assert(Config::getRegionSizeLog() == GroupSizeLog,
                  "Memory group should be the same size as Region");

    return Region;
  }

  uptr allocateRegion(SizeClassInfo *Sci, uptr ClassId) REQUIRES(Sci->Mutex) {
    DCHECK_LT(ClassId, NumClasses);
    uptr Region = 0;
    {
      ScopedLock L(RegionsStashMutex);
      if (NumberOfStashedRegions > 0)
        Region = RegionsStash[--NumberOfStashedRegions];
    }
    if (!Region)
      Region = allocateRegionSlow();
    if (LIKELY(Region)) {
      // Sci->Mutex is held by the caller, updating the Min/Max is safe.
      const uptr RegionIndex = computeRegionId(Region);
      if (RegionIndex < Sci->MinRegionIndex)
        Sci->MinRegionIndex = RegionIndex;
      if (RegionIndex > Sci->MaxRegionIndex)
        Sci->MaxRegionIndex = RegionIndex;
      ScopedLock L(ByteMapMutex);
      PossibleRegions.set(RegionIndex, static_cast<u8>(ClassId + 1U));
    }
    return Region;
  }

  SizeClassInfo *getSizeClassInfo(uptr ClassId) {
    DCHECK_LT(ClassId, NumClasses);
    return &SizeClassInfoArray[ClassId];
  }

  void pushBatchClassBlocks(SizeClassInfo *Sci, CompactPtrT *Array, u32 Size)
      REQUIRES(Sci->Mutex) {
    DCHECK_EQ(Sci, getSizeClassInfo(SizeClassMap::BatchClassId));

    // Free blocks are recorded by TransferBatch in freelist for all
    // size-classes. In addition, TransferBatch is allocated from BatchClassId.
    // In order not to use additional block to record the free blocks in
    // BatchClassId, they are self-contained. I.e., A TransferBatch records the
    // block address of itself. See the figure below:
    //
    // TransferBatch at 0xABCD
    // +----------------------------+
    // | Free blocks' addr          |
    // | +------+------+------+     |
    // | |0xABCD|...   |...   |     |
    // | +------+------+------+     |
    // +----------------------------+
    //
    // When we allocate all the free blocks in the TransferBatch, the block used
    // by TransferBatch is also free for use. We don't need to recycle the
    // TransferBatch. Note that the correctness is maintained by the invariant,
    //
    //   Each popBlocks() request returns the entire TransferBatch. Returning
    //   part of the blocks in a TransferBatch is invalid.
    //
    // This ensures that TransferBatch won't leak the address itself while it's
    // still holding other valid data.
    //
    // Besides, BatchGroup is also allocated from BatchClassId and has its
    // address recorded in the TransferBatch too. To maintain the correctness,
    //
    //   The address of BatchGroup is always recorded in the last TransferBatch
    //   in the freelist (also imply that the freelist should only be
    //   updated with push_front). Once the last TransferBatch is popped,
    //   the block used by BatchGroup is also free for use.
    //
    // With this approach, the blocks used by BatchGroup and TransferBatch are
    // reusable and don't need additional space for them.

    Sci->FreeListInfo.PushedBlocks += Size;
    BatchGroupT *BG = Sci->FreeListInfo.BlockList.front();

    if (BG == nullptr) {
      // Construct `BatchGroup` on the last element.
      BG = reinterpret_cast<BatchGroupT *>(
          decompactPtr(SizeClassMap::BatchClassId, Array[Size - 1]));
      --Size;
      BG->Batches.clear();
      // BatchClass hasn't enabled memory group. Use `0` to indicate there's no
      // memory group here.
      BG->CompactPtrGroupBase = 0;
      // `BG` is also the block of BatchClassId. Note that this is different
      // from `CreateGroup` in `pushBlocksImpl`
      BG->PushedBlocks = 1;
      BG->BytesInBGAtLastCheckpoint = 0;
      BG->MaxCachedPerBatch =
          CacheT::getMaxCached(getSizeByClassId(SizeClassMap::BatchClassId));

      Sci->FreeListInfo.BlockList.push_front(BG);
    }

    if (UNLIKELY(Size == 0))
      return;

    // This happens under 2 cases.
    //   1. just allocated a new `BatchGroup`.
    //   2. Only 1 block is pushed when the freelist is empty.
    if (BG->Batches.empty()) {
      // Construct the `TransferBatch` on the last element.
      TransferBatchT *TB = reinterpret_cast<TransferBatchT *>(
          decompactPtr(SizeClassMap::BatchClassId, Array[Size - 1]));
      TB->clear();
      // As mentioned above, addresses of `TransferBatch` and `BatchGroup` are
      // recorded in the TransferBatch.
      TB->add(Array[Size - 1]);
      TB->add(
          compactPtr(SizeClassMap::BatchClassId, reinterpret_cast<uptr>(BG)));
      --Size;
      DCHECK_EQ(BG->PushedBlocks, 1U);
      // `TB` is also the block of BatchClassId.
      BG->PushedBlocks += 1;
      BG->Batches.push_front(TB);
    }

    TransferBatchT *CurBatch = BG->Batches.front();
    DCHECK_NE(CurBatch, nullptr);

    for (u32 I = 0; I < Size;) {
      u16 UnusedSlots =
          static_cast<u16>(BG->MaxCachedPerBatch - CurBatch->getCount());
      if (UnusedSlots == 0) {
        CurBatch = reinterpret_cast<TransferBatchT *>(
            decompactPtr(SizeClassMap::BatchClassId, Array[I]));
        CurBatch->clear();
        // Self-contained
        CurBatch->add(Array[I]);
        ++I;
        // TODO(chiahungduan): Avoid the use of push_back() in `Batches` of
        // BatchClassId.
        BG->Batches.push_front(CurBatch);
        UnusedSlots = static_cast<u16>(BG->MaxCachedPerBatch - 1);
      }
      // `UnusedSlots` is u16 so the result will be also fit in u16.
      const u16 AppendSize = static_cast<u16>(Min<u32>(UnusedSlots, Size - I));
      CurBatch->appendFromArray(&Array[I], AppendSize);
      I += AppendSize;
    }

    BG->PushedBlocks += Size;
  }
  // Push the blocks to their batch group. The layout will be like,
  //
  // FreeListInfo.BlockList - > BG -> BG -> BG
  //                            |     |     |
  //                            v     v     v
  //                            TB    TB    TB
  //                            |
  //                            v
  //                            TB
  //
  // Each BlockGroup(BG) will associate with unique group id and the free blocks
  // are managed by a list of TransferBatch(TB). To reduce the time of inserting
  // blocks, BGs are sorted and the input `Array` are supposed to be sorted so
  // that we can get better performance of maintaining sorted property.
  // Use `SameGroup=true` to indicate that all blocks in the array are from the
  // same group then we will skip checking the group id of each block.
  //
  // The region mutex needs to be held while calling this method.
  void pushBlocksImpl(CacheT *C, uptr ClassId, SizeClassInfo *Sci,
                      CompactPtrT *Array, u32 Size, bool SameGroup = false)
      REQUIRES(Sci->Mutex) {
    DCHECK_NE(ClassId, SizeClassMap::BatchClassId);
    DCHECK_GT(Size, 0U);

    auto CreateGroup = [&](uptr CompactPtrGroupBase) {
      BatchGroupT *BG =
          reinterpret_cast<BatchGroupT *>(C->getBatchClassBlock());
      BG->Batches.clear();
      TransferBatchT *TB =
          reinterpret_cast<TransferBatchT *>(C->getBatchClassBlock());
      TB->clear();

      BG->CompactPtrGroupBase = CompactPtrGroupBase;
      BG->Batches.push_front(TB);
      BG->PushedBlocks = 0;
      BG->BytesInBGAtLastCheckpoint = 0;
      BG->MaxCachedPerBatch = TransferBatchT::MaxNumCached;

      return BG;
    };

    auto InsertBlocks = [&](BatchGroupT *BG, CompactPtrT *Array, u32 Size) {
      SinglyLinkedList<TransferBatchT> &Batches = BG->Batches;
      TransferBatchT *CurBatch = Batches.front();
      DCHECK_NE(CurBatch, nullptr);

      for (u32 I = 0; I < Size;) {
        DCHECK_GE(BG->MaxCachedPerBatch, CurBatch->getCount());
        u16 UnusedSlots =
            static_cast<u16>(BG->MaxCachedPerBatch - CurBatch->getCount());
        if (UnusedSlots == 0) {
          CurBatch =
              reinterpret_cast<TransferBatchT *>(C->getBatchClassBlock());
          CurBatch->clear();
          Batches.push_front(CurBatch);
          UnusedSlots = BG->MaxCachedPerBatch;
        }
        // `UnusedSlots` is u16 so the result will be also fit in u16.
        u16 AppendSize = static_cast<u16>(Min<u32>(UnusedSlots, Size - I));
        CurBatch->appendFromArray(&Array[I], AppendSize);
        I += AppendSize;
      }

      BG->PushedBlocks += Size;
    };

    Sci->FreeListInfo.PushedBlocks += Size;
    BatchGroupT *Cur = Sci->FreeListInfo.BlockList.front();

    // In the following, `Cur` always points to the BatchGroup for blocks that
    // will be pushed next. `Prev` is the element right before `Cur`.
    BatchGroupT *Prev = nullptr;

    while (Cur != nullptr &&
           compactPtrGroupBase(Array[0]) > Cur->CompactPtrGroupBase) {
      Prev = Cur;
      Cur = Cur->Next;
    }

    if (Cur == nullptr ||
        compactPtrGroupBase(Array[0]) != Cur->CompactPtrGroupBase) {
      Cur = CreateGroup(compactPtrGroupBase(Array[0]));
      if (Prev == nullptr)
        Sci->FreeListInfo.BlockList.push_front(Cur);
      else
        Sci->FreeListInfo.BlockList.insert(Prev, Cur);
    }

    // All the blocks are from the same group, just push without checking group
    // id.
    if (SameGroup) {
      for (u32 I = 0; I < Size; ++I)
        DCHECK_EQ(compactPtrGroupBase(Array[I]), Cur->CompactPtrGroupBase);

      InsertBlocks(Cur, Array, Size);
      return;
    }

    // The blocks are sorted by group id. Determine the segment of group and
    // push them to their group together.
    u32 Count = 1;
    for (u32 I = 1; I < Size; ++I) {
      if (compactPtrGroupBase(Array[I - 1]) != compactPtrGroupBase(Array[I])) {
        DCHECK_EQ(compactPtrGroupBase(Array[I - 1]), Cur->CompactPtrGroupBase);
        InsertBlocks(Cur, Array + I - Count, Count);

        while (Cur != nullptr &&
               compactPtrGroupBase(Array[I]) > Cur->CompactPtrGroupBase) {
          Prev = Cur;
          Cur = Cur->Next;
        }

        if (Cur == nullptr ||
            compactPtrGroupBase(Array[I]) != Cur->CompactPtrGroupBase) {
          Cur = CreateGroup(compactPtrGroupBase(Array[I]));
          DCHECK_NE(Prev, nullptr);
          Sci->FreeListInfo.BlockList.insert(Prev, Cur);
        }

        Count = 1;
      } else {
        ++Count;
      }
    }

    InsertBlocks(Cur, Array + Size - Count, Count);
  }

  u16 popBlocksImpl(CacheT *C, uptr ClassId, SizeClassInfo *Sci,
                    CompactPtrT *ToArray, const u16 MaxBlockCount)
      REQUIRES(Sci->Mutex) {
    if (Sci->FreeListInfo.BlockList.empty())
      return 0U;

    SinglyLinkedList<TransferBatchT> &Batches =
        Sci->FreeListInfo.BlockList.front()->Batches;

    if (Batches.empty()) {
      DCHECK_EQ(ClassId, SizeClassMap::BatchClassId);
      BatchGroupT *BG = Sci->FreeListInfo.BlockList.front();
      Sci->FreeListInfo.BlockList.pop_front();

      // Block used by `BatchGroup` is from BatchClassId. Turn the block into
      // `TransferBatch` with single block.
      TransferBatchT *TB = reinterpret_cast<TransferBatchT *>(BG);
      ToArray[0] =
          compactPtr(SizeClassMap::BatchClassId, reinterpret_cast<uptr>(TB));
      Sci->FreeListInfo.PoppedBlocks += 1;
      return 1U;
    }

    // So far, instead of always filling the blocks to `MaxBlockCount`, we only
    // examine single `TransferBatch` to minimize the time spent on the primary
    // allocator. Besides, the sizes of `TransferBatch` and
    // `CacheT::getMaxCached()` may also impact the time spent on accessing the
    // primary allocator.
    // TODO(chiahungduan): Evaluate if we want to always prepare `MaxBlockCount`
    // blocks and/or adjust the size of `TransferBatch` according to
    // `CacheT::getMaxCached()`.
    TransferBatchT *B = Batches.front();
    DCHECK_NE(B, nullptr);
    DCHECK_GT(B->getCount(), 0U);

    // BachClassId should always take all blocks in the TransferBatch. Read the
    // comment in `pushBatchClassBlocks()` for more details.
    const u16 PopCount = ClassId == SizeClassMap::BatchClassId
                             ? B->getCount()
                             : Min(MaxBlockCount, B->getCount());
    B->moveNToArray(ToArray, PopCount);

    // TODO(chiahungduan): The deallocation of unused BatchClassId blocks can be
    // done without holding `Mutex`.
    if (B->empty()) {
      Batches.pop_front();
      // `TransferBatch` of BatchClassId is self-contained, no need to
      // deallocate. Read the comment in `pushBatchClassBlocks()` for more
      // details.
      if (ClassId != SizeClassMap::BatchClassId)
        C->deallocate(SizeClassMap::BatchClassId, B);

      if (Batches.empty()) {
        BatchGroupT *BG = Sci->FreeListInfo.BlockList.front();
        Sci->FreeListInfo.BlockList.pop_front();

        // We don't keep BatchGroup with zero blocks to avoid empty-checking
        // while allocating. Note that block used for constructing BatchGroup is
        // recorded as free blocks in the last element of BatchGroup::Batches.
        // Which means, once we pop the last TransferBatch, the block is
        // implicitly deallocated.
        if (ClassId != SizeClassMap::BatchClassId)
          C->deallocate(SizeClassMap::BatchClassId, BG);
      }
    }

    Sci->FreeListInfo.PoppedBlocks += PopCount;
    return PopCount;
  }

  NOINLINE bool populateFreeList(CacheT *C, uptr ClassId, SizeClassInfo *Sci)
      REQUIRES(Sci->Mutex) {
    uptr Region;
    uptr Offset;
    // If the size-class currently has a region associated to it, use it. The
    // newly created blocks will be located after the currently allocated memory
    // for that region (up to RegionSize). Otherwise, create a new region, where
    // the new blocks will be carved from the beginning.
    if (Sci->CurrentRegion) {
      Region = Sci->CurrentRegion;
      DCHECK_GT(Sci->CurrentRegionAllocated, 0U);
      Offset = Sci->CurrentRegionAllocated;
    } else {
      DCHECK_EQ(Sci->CurrentRegionAllocated, 0U);
      Region = allocateRegion(Sci, ClassId);
      if (UNLIKELY(!Region))
        return false;
      C->getStats().add(StatMapped, RegionSize);
      Sci->CurrentRegion = Region;
      Offset = 0;
    }

    const uptr Size = getSizeByClassId(ClassId);
    const u16 MaxCount = CacheT::getMaxCached(Size);
    DCHECK_GT(MaxCount, 0U);
    // The maximum number of blocks we should carve in the region is dictated
    // by the maximum number of batches we want to fill, and the amount of
    // memory left in the current region (we use the lowest of the two). This
    // will not be 0 as we ensure that a region can at least hold one block (via
    // static_assert and at the end of this function).
    const u32 NumberOfBlocks =
        Min(MaxNumBatches * MaxCount,
            static_cast<u32>((RegionSize - Offset) / Size));
    DCHECK_GT(NumberOfBlocks, 0U);

    constexpr u32 ShuffleArraySize =
        MaxNumBatches * TransferBatchT::MaxNumCached;
    // Fill the transfer batches and put them in the size-class freelist. We
    // need to randomize the blocks for security purposes, so we first fill a
    // local array that we then shuffle before populating the batches.
    CompactPtrT ShuffleArray[ShuffleArraySize];
    DCHECK_LE(NumberOfBlocks, ShuffleArraySize);

    uptr P = Region + Offset;
    for (u32 I = 0; I < NumberOfBlocks; I++, P += Size)
      ShuffleArray[I] = reinterpret_cast<CompactPtrT>(P);

    if (ClassId != SizeClassMap::BatchClassId) {
      u32 N = 1;
      uptr CurGroup = compactPtrGroupBase(ShuffleArray[0]);
      for (u32 I = 1; I < NumberOfBlocks; I++) {
        if (UNLIKELY(compactPtrGroupBase(ShuffleArray[I]) != CurGroup)) {
          shuffle(ShuffleArray + I - N, N, &Sci->RandState);
          pushBlocksImpl(C, ClassId, Sci, ShuffleArray + I - N, N,
                         /*SameGroup=*/true);
          N = 1;
          CurGroup = compactPtrGroupBase(ShuffleArray[I]);
        } else {
          ++N;
        }
      }

      shuffle(ShuffleArray + NumberOfBlocks - N, N, &Sci->RandState);
      pushBlocksImpl(C, ClassId, Sci, &ShuffleArray[NumberOfBlocks - N], N,
                     /*SameGroup=*/true);
    } else {
      pushBatchClassBlocks(Sci, ShuffleArray, NumberOfBlocks);
    }

    // Note that `PushedBlocks` and `PoppedBlocks` are supposed to only record
    // the requests from `PushBlocks` and `PopBatch` which are external
    // interfaces. `populateFreeList` is the internal interface so we should set
    // the values back to avoid incorrectly setting the stats.
    Sci->FreeListInfo.PushedBlocks -= NumberOfBlocks;

    const uptr AllocatedUser = Size * NumberOfBlocks;
    C->getStats().add(StatFree, AllocatedUser);
    DCHECK_LE(Sci->CurrentRegionAllocated + AllocatedUser, RegionSize);
    // If there is not enough room in the region currently associated to fit
    // more blocks, we deassociate the region by resetting CurrentRegion and
    // CurrentRegionAllocated. Otherwise, update the allocated amount.
    if (RegionSize - (Sci->CurrentRegionAllocated + AllocatedUser) < Size) {
      Sci->CurrentRegion = 0;
      Sci->CurrentRegionAllocated = 0;
    } else {
      Sci->CurrentRegionAllocated += AllocatedUser;
    }
    Sci->AllocatedUser += AllocatedUser;

    return true;
  }

  void getStats(ScopedString *Str, uptr ClassId, SizeClassInfo *Sci)
      REQUIRES(Sci->Mutex) {
    if (Sci->AllocatedUser == 0)
      return;
    const uptr BlockSize = getSizeByClassId(ClassId);
    const uptr InUse =
        Sci->FreeListInfo.PoppedBlocks - Sci->FreeListInfo.PushedBlocks;
    const uptr BytesInFreeList = Sci->AllocatedUser - InUse * BlockSize;
    uptr PushedBytesDelta = 0;
    if (BytesInFreeList >= Sci->ReleaseInfo.BytesInFreeListAtLastCheckpoint) {
      PushedBytesDelta =
          BytesInFreeList - Sci->ReleaseInfo.BytesInFreeListAtLastCheckpoint;
    }
    const uptr AvailableChunks = Sci->AllocatedUser / BlockSize;
    Str->append("  %02zu (%6zu): mapped: %6zuK popped: %7zu pushed: %7zu "
                "inuse: %6zu avail: %6zu releases: %6zu last released: %6zuK "
                "latest pushed bytes: %6zuK\n",
                ClassId, getSizeByClassId(ClassId), Sci->AllocatedUser >> 10,
                Sci->FreeListInfo.PoppedBlocks, Sci->FreeListInfo.PushedBlocks,
                InUse, AvailableChunks, Sci->ReleaseInfo.RangesReleased,
                Sci->ReleaseInfo.LastReleasedBytes >> 10,
                PushedBytesDelta >> 10);
  }

  void getSizeClassFragmentationInfo(SizeClassInfo *Sci, uptr ClassId,
                                     ScopedString *Str) REQUIRES(Sci->Mutex) {
    const uptr BlockSize = getSizeByClassId(ClassId);
    const uptr First = Sci->MinRegionIndex;
    const uptr Last = Sci->MaxRegionIndex;
    const uptr Base = First * RegionSize;
    const uptr NumberOfRegions = Last - First + 1U;
    auto SkipRegion = [this, First, ClassId](uptr RegionIndex) {
      ScopedLock L(ByteMapMutex);
      return (PossibleRegions[First + RegionIndex] - 1U) != ClassId;
    };

    FragmentationRecorder Recorder;
    if (!Sci->FreeListInfo.BlockList.empty()) {
      PageReleaseContext Context =
          markFreeBlocks(Sci, ClassId, BlockSize, Base, NumberOfRegions,
                         ReleaseToOS::ForceAll);
      releaseFreeMemoryToOS(Context, Recorder, SkipRegion);
    }

    const uptr PageSize = getPageSizeCached();
    const uptr TotalBlocks = Sci->AllocatedUser / BlockSize;
    const uptr InUseBlocks =
        Sci->FreeListInfo.PoppedBlocks - Sci->FreeListInfo.PushedBlocks;
    uptr AllocatedPagesCount = 0;
    if (TotalBlocks != 0U) {
      for (uptr I = 0; I < NumberOfRegions; ++I) {
        if (SkipRegion(I))
          continue;
        AllocatedPagesCount += RegionSize / PageSize;
      }

      DCHECK_NE(AllocatedPagesCount, 0U);
    }

    DCHECK_GE(AllocatedPagesCount, Recorder.getReleasedPagesCount());
    const uptr InUsePages =
        AllocatedPagesCount - Recorder.getReleasedPagesCount();
    const uptr InUseBytes = InUsePages * PageSize;

    uptr Integral;
    uptr Fractional;
    computePercentage(BlockSize * InUseBlocks, InUsePages * PageSize, &Integral,
                      &Fractional);
    Str->append("  %02zu (%6zu): inuse/total blocks: %6zu/%6zu inuse/total "
                "pages: %6zu/%6zu inuse bytes: %6zuK util: %3zu.%02zu%%\n",
                ClassId, BlockSize, InUseBlocks, TotalBlocks, InUsePages,
                AllocatedPagesCount, InUseBytes >> 10, Integral, Fractional);
  }

  NOINLINE uptr releaseToOSMaybe(SizeClassInfo *Sci, uptr ClassId,
                                 ReleaseToOS ReleaseType = ReleaseToOS::Normal)
      REQUIRES(Sci->Mutex) {
    const uptr BlockSize = getSizeByClassId(ClassId);

    DCHECK_GE(Sci->FreeListInfo.PoppedBlocks, Sci->FreeListInfo.PushedBlocks);
    const uptr BytesInFreeList =
        Sci->AllocatedUser -
        (Sci->FreeListInfo.PoppedBlocks - Sci->FreeListInfo.PushedBlocks) *
            BlockSize;

    if (UNLIKELY(BytesInFreeList == 0))
      return 0;

    // ====================================================================== //
    // 1. Check if we have enough free blocks and if it's worth doing a page
    // release.
    // ====================================================================== //
    if (ReleaseType != ReleaseToOS::ForceAll &&
        !hasChanceToReleasePages(Sci, BlockSize, BytesInFreeList,
                                 ReleaseType)) {
      return 0;
    }

    const uptr First = Sci->MinRegionIndex;
    const uptr Last = Sci->MaxRegionIndex;
    DCHECK_NE(Last, 0U);
    DCHECK_LE(First, Last);
    uptr TotalReleasedBytes = 0;
    const uptr Base = First * RegionSize;
    const uptr NumberOfRegions = Last - First + 1U;

    // ==================================================================== //
    // 2. Mark the free blocks and we can tell which pages are in-use by
    //    querying `PageReleaseContext`.
    // ==================================================================== //
    PageReleaseContext Context = markFreeBlocks(Sci, ClassId, BlockSize, Base,
                                                NumberOfRegions, ReleaseType);
    if (!Context.hasBlockMarked())
      return 0;

    // ==================================================================== //
    // 3. Release the unused physical pages back to the OS.
    // ==================================================================== //
    ReleaseRecorder Recorder(Base);
    auto SkipRegion = [this, First, ClassId](uptr RegionIndex) {
      ScopedLock L(ByteMapMutex);
      return (PossibleRegions[First + RegionIndex] - 1U) != ClassId;
    };
    releaseFreeMemoryToOS(Context, Recorder, SkipRegion);

    if (Recorder.getReleasedRangesCount() > 0) {
      Sci->ReleaseInfo.BytesInFreeListAtLastCheckpoint = BytesInFreeList;
      Sci->ReleaseInfo.RangesReleased += Recorder.getReleasedRangesCount();
      Sci->ReleaseInfo.LastReleasedBytes = Recorder.getReleasedBytes();
      TotalReleasedBytes += Sci->ReleaseInfo.LastReleasedBytes;
    }
    Sci->ReleaseInfo.LastReleaseAtNs = getMonotonicTimeFast();

    return TotalReleasedBytes;
  }

  bool hasChanceToReleasePages(SizeClassInfo *Sci, uptr BlockSize,
                               uptr BytesInFreeList, ReleaseToOS ReleaseType)
      REQUIRES(Sci->Mutex) {
    DCHECK_GE(Sci->FreeListInfo.PoppedBlocks, Sci->FreeListInfo.PushedBlocks);
    const uptr PageSize = getPageSizeCached();

    if (BytesInFreeList <= Sci->ReleaseInfo.BytesInFreeListAtLastCheckpoint)
      Sci->ReleaseInfo.BytesInFreeListAtLastCheckpoint = BytesInFreeList;

    // Always update `BytesInFreeListAtLastCheckpoint` with the smallest value
    // so that we won't underestimate the releasable pages. For example, the
    // following is the region usage,
    //
    //  BytesInFreeListAtLastCheckpoint   AllocatedUser
    //                v                         v
    //  |--------------------------------------->
    //         ^                   ^
    //  BytesInFreeList     ReleaseThreshold
    //
    // In general, if we have collected enough bytes and the amount of free
    // bytes meets the ReleaseThreshold, we will try to do page release. If we
    // don't update `BytesInFreeListAtLastCheckpoint` when the current
    // `BytesInFreeList` is smaller, we may take longer time to wait for enough
    // freed blocks because we miss the bytes between
    // (BytesInFreeListAtLastCheckpoint - BytesInFreeList).
    const uptr PushedBytesDelta =
        BytesInFreeList - Sci->ReleaseInfo.BytesInFreeListAtLastCheckpoint;
    if (PushedBytesDelta < PageSize)
      return false;

    // Releasing smaller blocks is expensive, so we want to make sure that a
    // significant amount of bytes are free, and that there has been a good
    // amount of batches pushed to the freelist before attempting to release.
    if (isSmallBlock(BlockSize) && ReleaseType == ReleaseToOS::Normal)
      if (PushedBytesDelta < Sci->AllocatedUser / 16U)
        return false;

    if (ReleaseType == ReleaseToOS::Normal) {
      const s32 IntervalMs = atomic_load_relaxed(&ReleaseToOsIntervalMs);
      if (IntervalMs < 0)
        return false;

      // The constant 8 here is selected from profiling some apps and the number
      // of unreleased pages in the large size classes is around 16 pages or
      // more. Choose half of it as a heuristic and which also avoids page
      // release every time for every pushBlocks() attempt by large blocks.
      const bool ByPassReleaseInterval =
          isLargeBlock(BlockSize) && PushedBytesDelta > 8 * PageSize;
      if (!ByPassReleaseInterval) {
        if (Sci->ReleaseInfo.LastReleaseAtNs +
                static_cast<u64>(IntervalMs) * 1000000 >
            getMonotonicTimeFast()) {
          // Memory was returned recently.
          return false;
        }
      }
    } // if (ReleaseType == ReleaseToOS::Normal)

    return true;
  }

  PageReleaseContext markFreeBlocks(SizeClassInfo *Sci, const uptr ClassId,
                                    const uptr BlockSize, const uptr Base,
                                    const uptr NumberOfRegions,
                                    ReleaseToOS ReleaseType)
      REQUIRES(Sci->Mutex) {
    const uptr PageSize = getPageSizeCached();
    const uptr GroupSize = (1UL << GroupSizeLog);
    const uptr CurGroupBase =
        compactPtrGroupBase(compactPtr(ClassId, Sci->CurrentRegion));

    PageReleaseContext Context(BlockSize, NumberOfRegions,
                               /*ReleaseSize=*/RegionSize);

    auto DecompactPtr = [](CompactPtrT CompactPtr) {
      return reinterpret_cast<uptr>(CompactPtr);
    };
    for (BatchGroupT &BG : Sci->FreeListInfo.BlockList) {
      const uptr GroupBase = decompactGroupBase(BG.CompactPtrGroupBase);
      // The `GroupSize` may not be divided by `BlockSize`, which means there is
      // an unused space at the end of Region. Exclude that space to avoid
      // unused page map entry.
      uptr AllocatedGroupSize = GroupBase == CurGroupBase
                                    ? Sci->CurrentRegionAllocated
                                    : roundDownSlow(GroupSize, BlockSize);
      if (AllocatedGroupSize == 0)
        continue;

      // TransferBatches are pushed in front of BG.Batches. The first one may
      // not have all caches used.
      const uptr NumBlocks = (BG.Batches.size() - 1) * BG.MaxCachedPerBatch +
                             BG.Batches.front()->getCount();
      const uptr BytesInBG = NumBlocks * BlockSize;

      if (ReleaseType != ReleaseToOS::ForceAll) {
        if (BytesInBG <= BG.BytesInBGAtLastCheckpoint) {
          BG.BytesInBGAtLastCheckpoint = BytesInBG;
          continue;
        }

        const uptr PushedBytesDelta = BytesInBG - BG.BytesInBGAtLastCheckpoint;
        if (PushedBytesDelta < PageSize)
          continue;

        // Given the randomness property, we try to release the pages only if
        // the bytes used by free blocks exceed certain proportion of allocated
        // spaces.
        if (isSmallBlock(BlockSize) && (BytesInBG * 100U) / AllocatedGroupSize <
                                           (100U - 1U - BlockSize / 16U)) {
          continue;
        }
      }

      // TODO: Consider updating this after page release if `ReleaseRecorder`
      // can tell the released bytes in each group.
      BG.BytesInBGAtLastCheckpoint = BytesInBG;

      const uptr MaxContainedBlocks = AllocatedGroupSize / BlockSize;
      const uptr RegionIndex = (GroupBase - Base) / RegionSize;

      if (NumBlocks == MaxContainedBlocks) {
        for (const auto &It : BG.Batches)
          for (u16 I = 0; I < It.getCount(); ++I)
            DCHECK_EQ(compactPtrGroupBase(It.get(I)), BG.CompactPtrGroupBase);

        const uptr To = GroupBase + AllocatedGroupSize;
        Context.markRangeAsAllCounted(GroupBase, To, GroupBase, RegionIndex,
                                      AllocatedGroupSize);
      } else {
        DCHECK_LT(NumBlocks, MaxContainedBlocks);

        // Note that we don't always visit blocks in each BatchGroup so that we
        // may miss the chance of releasing certain pages that cross
        // BatchGroups.
        Context.markFreeBlocksInRegion(BG.Batches, DecompactPtr, GroupBase,
                                       RegionIndex, AllocatedGroupSize,
                                       /*MayContainLastBlockInRegion=*/true);
      }

      // We may not be able to do the page release In a rare case that we may
      // fail on PageMap allocation.
      if (UNLIKELY(!Context.hasBlockMarked()))
        break;
    }

    return Context;
  }

  SizeClassInfo SizeClassInfoArray[NumClasses] = {};

  HybridMutex ByteMapMutex;
  // Track the regions in use, 0 is unused, otherwise store ClassId + 1.
  ByteMap PossibleRegions GUARDED_BY(ByteMapMutex) = {};
  atomic_s32 ReleaseToOsIntervalMs = {};
  // Unless several threads request regions simultaneously from different size
  // classes, the stash rarely contains more than 1 entry.
  static constexpr uptr MaxStashedRegions = 4;
  HybridMutex RegionsStashMutex;
  uptr NumberOfStashedRegions GUARDED_BY(RegionsStashMutex) = 0;
  uptr RegionsStash[MaxStashedRegions] GUARDED_BY(RegionsStashMutex) = {};
};

} // namespace scudo

#endif // SCUDO_PRIMARY32_H_
