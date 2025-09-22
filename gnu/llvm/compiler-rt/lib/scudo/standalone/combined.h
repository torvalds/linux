//===-- combined.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_COMBINED_H_
#define SCUDO_COMBINED_H_

#include "allocator_config_wrapper.h"
#include "atomic_helpers.h"
#include "chunk.h"
#include "common.h"
#include "flags.h"
#include "flags_parser.h"
#include "local_cache.h"
#include "mem_map.h"
#include "memtag.h"
#include "mutex.h"
#include "options.h"
#include "quarantine.h"
#include "report.h"
#include "secondary.h"
#include "stack_depot.h"
#include "string_utils.h"
#include "tsd.h"

#include "scudo/interface.h"

#ifdef GWP_ASAN_HOOKS
#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/optional/backtrace.h"
#include "gwp_asan/optional/segv_handler.h"
#endif // GWP_ASAN_HOOKS

extern "C" inline void EmptyCallback() {}

#ifdef HAVE_ANDROID_UNSAFE_FRAME_POINTER_CHASE
// This function is not part of the NDK so it does not appear in any public
// header files. We only declare/use it when targeting the platform.
extern "C" size_t android_unsafe_frame_pointer_chase(scudo::uptr *buf,
                                                     size_t num_entries);
#endif

namespace scudo {

template <class Config, void (*PostInitCallback)(void) = EmptyCallback>
class Allocator {
public:
  using AllocatorConfig = BaseConfig<Config>;
  using PrimaryT =
      typename AllocatorConfig::template PrimaryT<PrimaryConfig<Config>>;
  using SecondaryT =
      typename AllocatorConfig::template SecondaryT<SecondaryConfig<Config>>;
  using CacheT = typename PrimaryT::CacheT;
  typedef Allocator<Config, PostInitCallback> ThisT;
  typedef typename AllocatorConfig::template TSDRegistryT<ThisT> TSDRegistryT;

  void callPostInitCallback() {
    pthread_once(&PostInitNonce, PostInitCallback);
  }

  struct QuarantineCallback {
    explicit QuarantineCallback(ThisT &Instance, CacheT &LocalCache)
        : Allocator(Instance), Cache(LocalCache) {}

    // Chunk recycling function, returns a quarantined chunk to the backend,
    // first making sure it hasn't been tampered with.
    void recycle(void *Ptr) {
      Chunk::UnpackedHeader Header;
      Chunk::loadHeader(Allocator.Cookie, Ptr, &Header);
      if (UNLIKELY(Header.State != Chunk::State::Quarantined))
        reportInvalidChunkState(AllocatorAction::Recycling, Ptr);

      Header.State = Chunk::State::Available;
      Chunk::storeHeader(Allocator.Cookie, Ptr, &Header);

      if (allocatorSupportsMemoryTagging<AllocatorConfig>())
        Ptr = untagPointer(Ptr);
      void *BlockBegin = Allocator::getBlockBegin(Ptr, &Header);
      Cache.deallocate(Header.ClassId, BlockBegin);
    }

    // We take a shortcut when allocating a quarantine batch by working with the
    // appropriate class ID instead of using Size. The compiler should optimize
    // the class ID computation and work with the associated cache directly.
    void *allocate(UNUSED uptr Size) {
      const uptr QuarantineClassId = SizeClassMap::getClassIdBySize(
          sizeof(QuarantineBatch) + Chunk::getHeaderSize());
      void *Ptr = Cache.allocate(QuarantineClassId);
      // Quarantine batch allocation failure is fatal.
      if (UNLIKELY(!Ptr))
        reportOutOfMemory(SizeClassMap::getSizeByClassId(QuarantineClassId));

      Ptr = reinterpret_cast<void *>(reinterpret_cast<uptr>(Ptr) +
                                     Chunk::getHeaderSize());
      Chunk::UnpackedHeader Header = {};
      Header.ClassId = QuarantineClassId & Chunk::ClassIdMask;
      Header.SizeOrUnusedBytes = sizeof(QuarantineBatch);
      Header.State = Chunk::State::Allocated;
      Chunk::storeHeader(Allocator.Cookie, Ptr, &Header);

      // Reset tag to 0 as this chunk may have been previously used for a tagged
      // user allocation.
      if (UNLIKELY(useMemoryTagging<AllocatorConfig>(
              Allocator.Primary.Options.load())))
        storeTags(reinterpret_cast<uptr>(Ptr),
                  reinterpret_cast<uptr>(Ptr) + sizeof(QuarantineBatch));

      return Ptr;
    }

    void deallocate(void *Ptr) {
      const uptr QuarantineClassId = SizeClassMap::getClassIdBySize(
          sizeof(QuarantineBatch) + Chunk::getHeaderSize());
      Chunk::UnpackedHeader Header;
      Chunk::loadHeader(Allocator.Cookie, Ptr, &Header);

      if (UNLIKELY(Header.State != Chunk::State::Allocated))
        reportInvalidChunkState(AllocatorAction::Deallocating, Ptr);
      DCHECK_EQ(Header.ClassId, QuarantineClassId);
      DCHECK_EQ(Header.Offset, 0);
      DCHECK_EQ(Header.SizeOrUnusedBytes, sizeof(QuarantineBatch));

      Header.State = Chunk::State::Available;
      Chunk::storeHeader(Allocator.Cookie, Ptr, &Header);
      Cache.deallocate(QuarantineClassId,
                       reinterpret_cast<void *>(reinterpret_cast<uptr>(Ptr) -
                                                Chunk::getHeaderSize()));
    }

  private:
    ThisT &Allocator;
    CacheT &Cache;
  };

  typedef GlobalQuarantine<QuarantineCallback, void> QuarantineT;
  typedef typename QuarantineT::CacheT QuarantineCacheT;

  void init() {
    performSanityChecks();

    // Check if hardware CRC32 is supported in the binary and by the platform,
    // if so, opt for the CRC32 hardware version of the checksum.
    if (&computeHardwareCRC32 && hasHardwareCRC32())
      HashAlgorithm = Checksum::HardwareCRC32;

    if (UNLIKELY(!getRandom(&Cookie, sizeof(Cookie))))
      Cookie = static_cast<u32>(getMonotonicTime() ^
                                (reinterpret_cast<uptr>(this) >> 4));

    initFlags();
    reportUnrecognizedFlags();

    // Store some flags locally.
    if (getFlags()->may_return_null)
      Primary.Options.set(OptionBit::MayReturnNull);
    if (getFlags()->zero_contents)
      Primary.Options.setFillContentsMode(ZeroFill);
    else if (getFlags()->pattern_fill_contents)
      Primary.Options.setFillContentsMode(PatternOrZeroFill);
    if (getFlags()->dealloc_type_mismatch)
      Primary.Options.set(OptionBit::DeallocTypeMismatch);
    if (getFlags()->delete_size_mismatch)
      Primary.Options.set(OptionBit::DeleteSizeMismatch);
    if (allocatorSupportsMemoryTagging<AllocatorConfig>() &&
        systemSupportsMemoryTagging())
      Primary.Options.set(OptionBit::UseMemoryTagging);

    QuarantineMaxChunkSize =
        static_cast<u32>(getFlags()->quarantine_max_chunk_size);

    Stats.init();
    // TODO(chiahungduan): Given that we support setting the default value in
    // the PrimaryConfig and CacheConfig, consider to deprecate the use of
    // `release_to_os_interval_ms` flag.
    const s32 ReleaseToOsIntervalMs = getFlags()->release_to_os_interval_ms;
    Primary.init(ReleaseToOsIntervalMs);
    Secondary.init(&Stats, ReleaseToOsIntervalMs);
    Quarantine.init(
        static_cast<uptr>(getFlags()->quarantine_size_kb << 10),
        static_cast<uptr>(getFlags()->thread_local_quarantine_size_kb << 10));
  }

  void enableRingBuffer() NO_THREAD_SAFETY_ANALYSIS {
    AllocationRingBuffer *RB = getRingBuffer();
    if (RB)
      RB->Depot->enable();
    RingBufferInitLock.unlock();
  }

  void disableRingBuffer() NO_THREAD_SAFETY_ANALYSIS {
    RingBufferInitLock.lock();
    AllocationRingBuffer *RB = getRingBuffer();
    if (RB)
      RB->Depot->disable();
  }

  // Initialize the embedded GWP-ASan instance. Requires the main allocator to
  // be functional, best called from PostInitCallback.
  void initGwpAsan() {
#ifdef GWP_ASAN_HOOKS
    gwp_asan::options::Options Opt;
    Opt.Enabled = getFlags()->GWP_ASAN_Enabled;
    Opt.MaxSimultaneousAllocations =
        getFlags()->GWP_ASAN_MaxSimultaneousAllocations;
    Opt.SampleRate = getFlags()->GWP_ASAN_SampleRate;
    Opt.InstallSignalHandlers = getFlags()->GWP_ASAN_InstallSignalHandlers;
    Opt.Recoverable = getFlags()->GWP_ASAN_Recoverable;
    // Embedded GWP-ASan is locked through the Scudo atfork handler (via
    // Allocator::disable calling GWPASan.disable). Disable GWP-ASan's atfork
    // handler.
    Opt.InstallForkHandlers = false;
    Opt.Backtrace = gwp_asan::backtrace::getBacktraceFunction();
    GuardedAlloc.init(Opt);

    if (Opt.InstallSignalHandlers)
      gwp_asan::segv_handler::installSignalHandlers(
          &GuardedAlloc, Printf,
          gwp_asan::backtrace::getPrintBacktraceFunction(),
          gwp_asan::backtrace::getSegvBacktraceFunction(),
          Opt.Recoverable);

    GuardedAllocSlotSize =
        GuardedAlloc.getAllocatorState()->maximumAllocationSize();
    Stats.add(StatFree, static_cast<uptr>(Opt.MaxSimultaneousAllocations) *
                            GuardedAllocSlotSize);
#endif // GWP_ASAN_HOOKS
  }

#ifdef GWP_ASAN_HOOKS
  const gwp_asan::AllocationMetadata *getGwpAsanAllocationMetadata() {
    return GuardedAlloc.getMetadataRegion();
  }

  const gwp_asan::AllocatorState *getGwpAsanAllocatorState() {
    return GuardedAlloc.getAllocatorState();
  }
#endif // GWP_ASAN_HOOKS

  ALWAYS_INLINE void initThreadMaybe(bool MinimalInit = false) {
    TSDRegistry.initThreadMaybe(this, MinimalInit);
  }

  void unmapTestOnly() {
    unmapRingBuffer();
    TSDRegistry.unmapTestOnly(this);
    Primary.unmapTestOnly();
    Secondary.unmapTestOnly();
#ifdef GWP_ASAN_HOOKS
    if (getFlags()->GWP_ASAN_InstallSignalHandlers)
      gwp_asan::segv_handler::uninstallSignalHandlers();
    GuardedAlloc.uninitTestOnly();
#endif // GWP_ASAN_HOOKS
  }

  TSDRegistryT *getTSDRegistry() { return &TSDRegistry; }
  QuarantineT *getQuarantine() { return &Quarantine; }

  // The Cache must be provided zero-initialized.
  void initCache(CacheT *Cache) { Cache->init(&Stats, &Primary); }

  // Release the resources used by a TSD, which involves:
  // - draining the local quarantine cache to the global quarantine;
  // - releasing the cached pointers back to the Primary;
  // - unlinking the local stats from the global ones (destroying the cache does
  //   the last two items).
  void commitBack(TSD<ThisT> *TSD) {
    TSD->assertLocked(/*BypassCheck=*/true);
    Quarantine.drain(&TSD->getQuarantineCache(),
                     QuarantineCallback(*this, TSD->getCache()));
    TSD->getCache().destroy(&Stats);
  }

  void drainCache(TSD<ThisT> *TSD) {
    TSD->assertLocked(/*BypassCheck=*/true);
    Quarantine.drainAndRecycle(&TSD->getQuarantineCache(),
                               QuarantineCallback(*this, TSD->getCache()));
    TSD->getCache().drain();
  }
  void drainCaches() { TSDRegistry.drainCaches(this); }

  ALWAYS_INLINE void *getHeaderTaggedPointer(void *Ptr) {
    if (!allocatorSupportsMemoryTagging<AllocatorConfig>())
      return Ptr;
    auto UntaggedPtr = untagPointer(Ptr);
    if (UntaggedPtr != Ptr)
      return UntaggedPtr;
    // Secondary, or pointer allocated while memory tagging is unsupported or
    // disabled. The tag mismatch is okay in the latter case because tags will
    // not be checked.
    return addHeaderTag(Ptr);
  }

  ALWAYS_INLINE uptr addHeaderTag(uptr Ptr) {
    if (!allocatorSupportsMemoryTagging<AllocatorConfig>())
      return Ptr;
    return addFixedTag(Ptr, 2);
  }

  ALWAYS_INLINE void *addHeaderTag(void *Ptr) {
    return reinterpret_cast<void *>(addHeaderTag(reinterpret_cast<uptr>(Ptr)));
  }

  NOINLINE u32 collectStackTrace(UNUSED StackDepot *Depot) {
#ifdef HAVE_ANDROID_UNSAFE_FRAME_POINTER_CHASE
    // Discard collectStackTrace() frame and allocator function frame.
    constexpr uptr DiscardFrames = 2;
    uptr Stack[MaxTraceSize + DiscardFrames];
    uptr Size =
        android_unsafe_frame_pointer_chase(Stack, MaxTraceSize + DiscardFrames);
    Size = Min<uptr>(Size, MaxTraceSize + DiscardFrames);
    return Depot->insert(Stack + Min<uptr>(DiscardFrames, Size), Stack + Size);
#else
    return 0;
#endif
  }

  uptr computeOddEvenMaskForPointerMaybe(const Options &Options, uptr Ptr,
                                         uptr ClassId) {
    if (!Options.get(OptionBit::UseOddEvenTags))
      return 0;

    // If a chunk's tag is odd, we want the tags of the surrounding blocks to be
    // even, and vice versa. Blocks are laid out Size bytes apart, and adding
    // Size to Ptr will flip the least significant set bit of Size in Ptr, so
    // that bit will have the pattern 010101... for consecutive blocks, which we
    // can use to determine which tag mask to use.
    return 0x5555U << ((Ptr >> SizeClassMap::getSizeLSBByClassId(ClassId)) & 1);
  }

  NOINLINE void *allocate(uptr Size, Chunk::Origin Origin,
                          uptr Alignment = MinAlignment,
                          bool ZeroContents = false) NO_THREAD_SAFETY_ANALYSIS {
    initThreadMaybe();

    const Options Options = Primary.Options.load();
    if (UNLIKELY(Alignment > MaxAlignment)) {
      if (Options.get(OptionBit::MayReturnNull))
        return nullptr;
      reportAlignmentTooBig(Alignment, MaxAlignment);
    }
    if (Alignment < MinAlignment)
      Alignment = MinAlignment;

#ifdef GWP_ASAN_HOOKS
    if (UNLIKELY(GuardedAlloc.shouldSample())) {
      if (void *Ptr = GuardedAlloc.allocate(Size, Alignment)) {
        Stats.lock();
        Stats.add(StatAllocated, GuardedAllocSlotSize);
        Stats.sub(StatFree, GuardedAllocSlotSize);
        Stats.unlock();
        return Ptr;
      }
    }
#endif // GWP_ASAN_HOOKS

    const FillContentsMode FillContents = ZeroContents ? ZeroFill
                                          : TSDRegistry.getDisableMemInit()
                                              ? NoFill
                                              : Options.getFillContentsMode();

    // If the requested size happens to be 0 (more common than you might think),
    // allocate MinAlignment bytes on top of the header. Then add the extra
    // bytes required to fulfill the alignment requirements: we allocate enough
    // to be sure that there will be an address in the block that will satisfy
    // the alignment.
    const uptr NeededSize =
        roundUp(Size, MinAlignment) +
        ((Alignment > MinAlignment) ? Alignment : Chunk::getHeaderSize());

    // Takes care of extravagantly large sizes as well as integer overflows.
    static_assert(MaxAllowedMallocSize < UINTPTR_MAX - MaxAlignment, "");
    if (UNLIKELY(Size >= MaxAllowedMallocSize)) {
      if (Options.get(OptionBit::MayReturnNull))
        return nullptr;
      reportAllocationSizeTooBig(Size, NeededSize, MaxAllowedMallocSize);
    }
    DCHECK_LE(Size, NeededSize);

    void *Block = nullptr;
    uptr ClassId = 0;
    uptr SecondaryBlockEnd = 0;
    if (LIKELY(PrimaryT::canAllocate(NeededSize))) {
      ClassId = SizeClassMap::getClassIdBySize(NeededSize);
      DCHECK_NE(ClassId, 0U);
      typename TSDRegistryT::ScopedTSD TSD(TSDRegistry);
      Block = TSD->getCache().allocate(ClassId);
      // If the allocation failed, retry in each successively larger class until
      // it fits. If it fails to fit in the largest class, fallback to the
      // Secondary.
      if (UNLIKELY(!Block)) {
        while (ClassId < SizeClassMap::LargestClassId && !Block)
          Block = TSD->getCache().allocate(++ClassId);
        if (!Block)
          ClassId = 0;
      }
    }
    if (UNLIKELY(ClassId == 0)) {
      Block = Secondary.allocate(Options, Size, Alignment, &SecondaryBlockEnd,
                                 FillContents);
    }

    if (UNLIKELY(!Block)) {
      if (Options.get(OptionBit::MayReturnNull))
        return nullptr;
      printStats();
      reportOutOfMemory(NeededSize);
    }

    const uptr UserPtr = roundUp(
        reinterpret_cast<uptr>(Block) + Chunk::getHeaderSize(), Alignment);
    const uptr SizeOrUnusedBytes =
        ClassId ? Size : SecondaryBlockEnd - (UserPtr + Size);

    if (LIKELY(!useMemoryTagging<AllocatorConfig>(Options))) {
      return initChunk(ClassId, Origin, Block, UserPtr, SizeOrUnusedBytes,
                       FillContents);
    }

    return initChunkWithMemoryTagging(ClassId, Origin, Block, UserPtr, Size,
                                      SizeOrUnusedBytes, FillContents);
  }

  NOINLINE void deallocate(void *Ptr, Chunk::Origin Origin, uptr DeleteSize = 0,
                           UNUSED uptr Alignment = MinAlignment) {
    if (UNLIKELY(!Ptr))
      return;

    // For a deallocation, we only ensure minimal initialization, meaning thread
    // local data will be left uninitialized for now (when using ELF TLS). The
    // fallback cache will be used instead. This is a workaround for a situation
    // where the only heap operation performed in a thread would be a free past
    // the TLS destructors, ending up in initialized thread specific data never
    // being destroyed properly. Any other heap operation will do a full init.
    initThreadMaybe(/*MinimalInit=*/true);

#ifdef GWP_ASAN_HOOKS
    if (UNLIKELY(GuardedAlloc.pointerIsMine(Ptr))) {
      GuardedAlloc.deallocate(Ptr);
      Stats.lock();
      Stats.add(StatFree, GuardedAllocSlotSize);
      Stats.sub(StatAllocated, GuardedAllocSlotSize);
      Stats.unlock();
      return;
    }
#endif // GWP_ASAN_HOOKS

    if (UNLIKELY(!isAligned(reinterpret_cast<uptr>(Ptr), MinAlignment)))
      reportMisalignedPointer(AllocatorAction::Deallocating, Ptr);

    void *TaggedPtr = Ptr;
    Ptr = getHeaderTaggedPointer(Ptr);

    Chunk::UnpackedHeader Header;
    Chunk::loadHeader(Cookie, Ptr, &Header);

    if (UNLIKELY(Header.State != Chunk::State::Allocated))
      reportInvalidChunkState(AllocatorAction::Deallocating, Ptr);

    const Options Options = Primary.Options.load();
    if (Options.get(OptionBit::DeallocTypeMismatch)) {
      if (UNLIKELY(Header.OriginOrWasZeroed != Origin)) {
        // With the exception of memalign'd chunks, that can be still be free'd.
        if (Header.OriginOrWasZeroed != Chunk::Origin::Memalign ||
            Origin != Chunk::Origin::Malloc)
          reportDeallocTypeMismatch(AllocatorAction::Deallocating, Ptr,
                                    Header.OriginOrWasZeroed, Origin);
      }
    }

    const uptr Size = getSize(Ptr, &Header);
    if (DeleteSize && Options.get(OptionBit::DeleteSizeMismatch)) {
      if (UNLIKELY(DeleteSize != Size))
        reportDeleteSizeMismatch(Ptr, DeleteSize, Size);
    }

    quarantineOrDeallocateChunk(Options, TaggedPtr, &Header, Size);
  }

  void *reallocate(void *OldPtr, uptr NewSize, uptr Alignment = MinAlignment) {
    initThreadMaybe();

    const Options Options = Primary.Options.load();
    if (UNLIKELY(NewSize >= MaxAllowedMallocSize)) {
      if (Options.get(OptionBit::MayReturnNull))
        return nullptr;
      reportAllocationSizeTooBig(NewSize, 0, MaxAllowedMallocSize);
    }

    // The following cases are handled by the C wrappers.
    DCHECK_NE(OldPtr, nullptr);
    DCHECK_NE(NewSize, 0);

#ifdef GWP_ASAN_HOOKS
    if (UNLIKELY(GuardedAlloc.pointerIsMine(OldPtr))) {
      uptr OldSize = GuardedAlloc.getSize(OldPtr);
      void *NewPtr = allocate(NewSize, Chunk::Origin::Malloc, Alignment);
      if (NewPtr)
        memcpy(NewPtr, OldPtr, (NewSize < OldSize) ? NewSize : OldSize);
      GuardedAlloc.deallocate(OldPtr);
      Stats.lock();
      Stats.add(StatFree, GuardedAllocSlotSize);
      Stats.sub(StatAllocated, GuardedAllocSlotSize);
      Stats.unlock();
      return NewPtr;
    }
#endif // GWP_ASAN_HOOKS

    void *OldTaggedPtr = OldPtr;
    OldPtr = getHeaderTaggedPointer(OldPtr);

    if (UNLIKELY(!isAligned(reinterpret_cast<uptr>(OldPtr), MinAlignment)))
      reportMisalignedPointer(AllocatorAction::Reallocating, OldPtr);

    Chunk::UnpackedHeader Header;
    Chunk::loadHeader(Cookie, OldPtr, &Header);

    if (UNLIKELY(Header.State != Chunk::State::Allocated))
      reportInvalidChunkState(AllocatorAction::Reallocating, OldPtr);

    // Pointer has to be allocated with a malloc-type function. Some
    // applications think that it is OK to realloc a memalign'ed pointer, which
    // will trigger this check. It really isn't.
    if (Options.get(OptionBit::DeallocTypeMismatch)) {
      if (UNLIKELY(Header.OriginOrWasZeroed != Chunk::Origin::Malloc))
        reportDeallocTypeMismatch(AllocatorAction::Reallocating, OldPtr,
                                  Header.OriginOrWasZeroed,
                                  Chunk::Origin::Malloc);
    }

    void *BlockBegin = getBlockBegin(OldTaggedPtr, &Header);
    uptr BlockEnd;
    uptr OldSize;
    const uptr ClassId = Header.ClassId;
    if (LIKELY(ClassId)) {
      BlockEnd = reinterpret_cast<uptr>(BlockBegin) +
                 SizeClassMap::getSizeByClassId(ClassId);
      OldSize = Header.SizeOrUnusedBytes;
    } else {
      BlockEnd = SecondaryT::getBlockEnd(BlockBegin);
      OldSize = BlockEnd - (reinterpret_cast<uptr>(OldTaggedPtr) +
                            Header.SizeOrUnusedBytes);
    }
    // If the new chunk still fits in the previously allocated block (with a
    // reasonable delta), we just keep the old block, and update the chunk
    // header to reflect the size change.
    if (reinterpret_cast<uptr>(OldTaggedPtr) + NewSize <= BlockEnd) {
      if (NewSize > OldSize || (OldSize - NewSize) < getPageSizeCached()) {
        // If we have reduced the size, set the extra bytes to the fill value
        // so that we are ready to grow it again in the future.
        if (NewSize < OldSize) {
          const FillContentsMode FillContents =
              TSDRegistry.getDisableMemInit() ? NoFill
                                              : Options.getFillContentsMode();
          if (FillContents != NoFill) {
            memset(reinterpret_cast<char *>(OldTaggedPtr) + NewSize,
                   FillContents == ZeroFill ? 0 : PatternFillByte,
                   OldSize - NewSize);
          }
        }

        Header.SizeOrUnusedBytes =
            (ClassId ? NewSize
                     : BlockEnd -
                           (reinterpret_cast<uptr>(OldTaggedPtr) + NewSize)) &
            Chunk::SizeOrUnusedBytesMask;
        Chunk::storeHeader(Cookie, OldPtr, &Header);
        if (UNLIKELY(useMemoryTagging<AllocatorConfig>(Options))) {
          if (ClassId) {
            resizeTaggedChunk(reinterpret_cast<uptr>(OldTaggedPtr) + OldSize,
                              reinterpret_cast<uptr>(OldTaggedPtr) + NewSize,
                              NewSize, untagPointer(BlockEnd));
            storePrimaryAllocationStackMaybe(Options, OldPtr);
          } else {
            storeSecondaryAllocationStackMaybe(Options, OldPtr, NewSize);
          }
        }
        return OldTaggedPtr;
      }
    }

    // Otherwise we allocate a new one, and deallocate the old one. Some
    // allocators will allocate an even larger chunk (by a fixed factor) to
    // allow for potential further in-place realloc. The gains of such a trick
    // are currently unclear.
    void *NewPtr = allocate(NewSize, Chunk::Origin::Malloc, Alignment);
    if (LIKELY(NewPtr)) {
      memcpy(NewPtr, OldTaggedPtr, Min(NewSize, OldSize));
      quarantineOrDeallocateChunk(Options, OldTaggedPtr, &Header, OldSize);
    }
    return NewPtr;
  }

  // TODO(kostyak): disable() is currently best-effort. There are some small
  //                windows of time when an allocation could still succeed after
  //                this function finishes. We will revisit that later.
  void disable() NO_THREAD_SAFETY_ANALYSIS {
    initThreadMaybe();
#ifdef GWP_ASAN_HOOKS
    GuardedAlloc.disable();
#endif
    TSDRegistry.disable();
    Stats.disable();
    Quarantine.disable();
    Primary.disable();
    Secondary.disable();
    disableRingBuffer();
  }

  void enable() NO_THREAD_SAFETY_ANALYSIS {
    initThreadMaybe();
    enableRingBuffer();
    Secondary.enable();
    Primary.enable();
    Quarantine.enable();
    Stats.enable();
    TSDRegistry.enable();
#ifdef GWP_ASAN_HOOKS
    GuardedAlloc.enable();
#endif
  }

  // The function returns the amount of bytes required to store the statistics,
  // which might be larger than the amount of bytes provided. Note that the
  // statistics buffer is not necessarily constant between calls to this
  // function. This can be called with a null buffer or zero size for buffer
  // sizing purposes.
  uptr getStats(char *Buffer, uptr Size) {
    ScopedString Str;
    const uptr Length = getStats(&Str) + 1;
    if (Length < Size)
      Size = Length;
    if (Buffer && Size) {
      memcpy(Buffer, Str.data(), Size);
      Buffer[Size - 1] = '\0';
    }
    return Length;
  }

  void printStats() {
    ScopedString Str;
    getStats(&Str);
    Str.output();
  }

  void printFragmentationInfo() {
    ScopedString Str;
    Primary.getFragmentationInfo(&Str);
    // Secondary allocator dumps the fragmentation data in getStats().
    Str.output();
  }

  void releaseToOS(ReleaseToOS ReleaseType) {
    initThreadMaybe();
    if (ReleaseType == ReleaseToOS::ForceAll)
      drainCaches();
    Primary.releaseToOS(ReleaseType);
    Secondary.releaseToOS();
  }

  // Iterate over all chunks and call a callback for all busy chunks located
  // within the provided memory range. Said callback must not use this allocator
  // or a deadlock can ensue. This fits Android's malloc_iterate() needs.
  void iterateOverChunks(uptr Base, uptr Size, iterate_callback Callback,
                         void *Arg) {
    initThreadMaybe();
    if (archSupportsMemoryTagging())
      Base = untagPointer(Base);
    const uptr From = Base;
    const uptr To = Base + Size;
    bool MayHaveTaggedPrimary =
        allocatorSupportsMemoryTagging<AllocatorConfig>() &&
        systemSupportsMemoryTagging();
    auto Lambda = [this, From, To, MayHaveTaggedPrimary, Callback,
                   Arg](uptr Block) {
      if (Block < From || Block >= To)
        return;
      uptr Chunk;
      Chunk::UnpackedHeader Header;
      if (MayHaveTaggedPrimary) {
        // A chunk header can either have a zero tag (tagged primary) or the
        // header tag (secondary, or untagged primary). We don't know which so
        // try both.
        ScopedDisableMemoryTagChecks x;
        if (!getChunkFromBlock(Block, &Chunk, &Header) &&
            !getChunkFromBlock(addHeaderTag(Block), &Chunk, &Header))
          return;
      } else {
        if (!getChunkFromBlock(addHeaderTag(Block), &Chunk, &Header))
          return;
      }
      if (Header.State == Chunk::State::Allocated) {
        uptr TaggedChunk = Chunk;
        if (allocatorSupportsMemoryTagging<AllocatorConfig>())
          TaggedChunk = untagPointer(TaggedChunk);
        if (useMemoryTagging<AllocatorConfig>(Primary.Options.load()))
          TaggedChunk = loadTag(Chunk);
        Callback(TaggedChunk, getSize(reinterpret_cast<void *>(Chunk), &Header),
                 Arg);
      }
    };
    Primary.iterateOverBlocks(Lambda);
    Secondary.iterateOverBlocks(Lambda);
#ifdef GWP_ASAN_HOOKS
    GuardedAlloc.iterate(reinterpret_cast<void *>(Base), Size, Callback, Arg);
#endif
  }

  bool canReturnNull() {
    initThreadMaybe();
    return Primary.Options.load().get(OptionBit::MayReturnNull);
  }

  bool setOption(Option O, sptr Value) {
    initThreadMaybe();
    if (O == Option::MemtagTuning) {
      // Enabling odd/even tags involves a tradeoff between use-after-free
      // detection and buffer overflow detection. Odd/even tags make it more
      // likely for buffer overflows to be detected by increasing the size of
      // the guaranteed "red zone" around the allocation, but on the other hand
      // use-after-free is less likely to be detected because the tag space for
      // any particular chunk is cut in half. Therefore we use this tuning
      // setting to control whether odd/even tags are enabled.
      if (Value == M_MEMTAG_TUNING_BUFFER_OVERFLOW)
        Primary.Options.set(OptionBit::UseOddEvenTags);
      else if (Value == M_MEMTAG_TUNING_UAF)
        Primary.Options.clear(OptionBit::UseOddEvenTags);
      return true;
    } else {
      // We leave it to the various sub-components to decide whether or not they
      // want to handle the option, but we do not want to short-circuit
      // execution if one of the setOption was to return false.
      const bool PrimaryResult = Primary.setOption(O, Value);
      const bool SecondaryResult = Secondary.setOption(O, Value);
      const bool RegistryResult = TSDRegistry.setOption(O, Value);
      return PrimaryResult && SecondaryResult && RegistryResult;
    }
    return false;
  }

  // Return the usable size for a given chunk. Technically we lie, as we just
  // report the actual size of a chunk. This is done to counteract code actively
  // writing past the end of a chunk (like sqlite3) when the usable size allows
  // for it, which then forces realloc to copy the usable size of a chunk as
  // opposed to its actual size.
  uptr getUsableSize(const void *Ptr) {
    if (UNLIKELY(!Ptr))
      return 0;

    return getAllocSize(Ptr);
  }

  uptr getAllocSize(const void *Ptr) {
    initThreadMaybe();

#ifdef GWP_ASAN_HOOKS
    if (UNLIKELY(GuardedAlloc.pointerIsMine(Ptr)))
      return GuardedAlloc.getSize(Ptr);
#endif // GWP_ASAN_HOOKS

    Ptr = getHeaderTaggedPointer(const_cast<void *>(Ptr));
    Chunk::UnpackedHeader Header;
    Chunk::loadHeader(Cookie, Ptr, &Header);

    // Getting the alloc size of a chunk only makes sense if it's allocated.
    if (UNLIKELY(Header.State != Chunk::State::Allocated))
      reportInvalidChunkState(AllocatorAction::Sizing, const_cast<void *>(Ptr));

    return getSize(Ptr, &Header);
  }

  void getStats(StatCounters S) {
    initThreadMaybe();
    Stats.get(S);
  }

  // Returns true if the pointer provided was allocated by the current
  // allocator instance, which is compliant with tcmalloc's ownership concept.
  // A corrupted chunk will not be reported as owned, which is WAI.
  bool isOwned(const void *Ptr) {
    initThreadMaybe();
#ifdef GWP_ASAN_HOOKS
    if (GuardedAlloc.pointerIsMine(Ptr))
      return true;
#endif // GWP_ASAN_HOOKS
    if (!Ptr || !isAligned(reinterpret_cast<uptr>(Ptr), MinAlignment))
      return false;
    Ptr = getHeaderTaggedPointer(const_cast<void *>(Ptr));
    Chunk::UnpackedHeader Header;
    return Chunk::isValid(Cookie, Ptr, &Header) &&
           Header.State == Chunk::State::Allocated;
  }

  bool useMemoryTaggingTestOnly() const {
    return useMemoryTagging<AllocatorConfig>(Primary.Options.load());
  }
  void disableMemoryTagging() {
    // If we haven't been initialized yet, we need to initialize now in order to
    // prevent a future call to initThreadMaybe() from enabling memory tagging
    // based on feature detection. But don't call initThreadMaybe() because it
    // may end up calling the allocator (via pthread_atfork, via the post-init
    // callback), which may cause mappings to be created with memory tagging
    // enabled.
    TSDRegistry.initOnceMaybe(this);
    if (allocatorSupportsMemoryTagging<AllocatorConfig>()) {
      Secondary.disableMemoryTagging();
      Primary.Options.clear(OptionBit::UseMemoryTagging);
    }
  }

  void setTrackAllocationStacks(bool Track) {
    initThreadMaybe();
    if (getFlags()->allocation_ring_buffer_size <= 0) {
      DCHECK(!Primary.Options.load().get(OptionBit::TrackAllocationStacks));
      return;
    }

    if (Track) {
      initRingBufferMaybe();
      Primary.Options.set(OptionBit::TrackAllocationStacks);
    } else
      Primary.Options.clear(OptionBit::TrackAllocationStacks);
  }

  void setFillContents(FillContentsMode FillContents) {
    initThreadMaybe();
    Primary.Options.setFillContentsMode(FillContents);
  }

  void setAddLargeAllocationSlack(bool AddSlack) {
    initThreadMaybe();
    if (AddSlack)
      Primary.Options.set(OptionBit::AddLargeAllocationSlack);
    else
      Primary.Options.clear(OptionBit::AddLargeAllocationSlack);
  }

  const char *getStackDepotAddress() {
    initThreadMaybe();
    AllocationRingBuffer *RB = getRingBuffer();
    return RB ? reinterpret_cast<char *>(RB->Depot) : nullptr;
  }

  uptr getStackDepotSize() {
    initThreadMaybe();
    AllocationRingBuffer *RB = getRingBuffer();
    return RB ? RB->StackDepotSize : 0;
  }

  const char *getRegionInfoArrayAddress() const {
    return Primary.getRegionInfoArrayAddress();
  }

  static uptr getRegionInfoArraySize() {
    return PrimaryT::getRegionInfoArraySize();
  }

  const char *getRingBufferAddress() {
    initThreadMaybe();
    return reinterpret_cast<char *>(getRingBuffer());
  }

  uptr getRingBufferSize() {
    initThreadMaybe();
    AllocationRingBuffer *RB = getRingBuffer();
    return RB && RB->RingBufferElements
               ? ringBufferSizeInBytes(RB->RingBufferElements)
               : 0;
  }

  static const uptr MaxTraceSize = 64;

  static void collectTraceMaybe(const StackDepot *Depot,
                                uintptr_t (&Trace)[MaxTraceSize], u32 Hash) {
    uptr RingPos, Size;
    if (!Depot->find(Hash, &RingPos, &Size))
      return;
    for (unsigned I = 0; I != Size && I != MaxTraceSize; ++I)
      Trace[I] = static_cast<uintptr_t>(Depot->at(RingPos + I));
  }

  static void getErrorInfo(struct scudo_error_info *ErrorInfo,
                           uintptr_t FaultAddr, const char *DepotPtr,
                           size_t DepotSize, const char *RegionInfoPtr,
                           const char *RingBufferPtr, size_t RingBufferSize,
                           const char *Memory, const char *MemoryTags,
                           uintptr_t MemoryAddr, size_t MemorySize) {
    // N.B. we need to support corrupted data in any of the buffers here. We get
    // this information from an external process (the crashing process) that
    // should not be able to crash the crash dumper (crash_dump on Android).
    // See also the get_error_info_fuzzer.
    *ErrorInfo = {};
    if (!allocatorSupportsMemoryTagging<AllocatorConfig>() ||
        MemoryAddr + MemorySize < MemoryAddr)
      return;

    const StackDepot *Depot = nullptr;
    if (DepotPtr) {
      // check for corrupted StackDepot. First we need to check whether we can
      // read the metadata, then whether the metadata matches the size.
      if (DepotSize < sizeof(*Depot))
        return;
      Depot = reinterpret_cast<const StackDepot *>(DepotPtr);
      if (!Depot->isValid(DepotSize))
        return;
    }

    size_t NextErrorReport = 0;

    // Check for OOB in the current block and the two surrounding blocks. Beyond
    // that, UAF is more likely.
    if (extractTag(FaultAddr) != 0)
      getInlineErrorInfo(ErrorInfo, NextErrorReport, FaultAddr, Depot,
                         RegionInfoPtr, Memory, MemoryTags, MemoryAddr,
                         MemorySize, 0, 2);

    // Check the ring buffer. For primary allocations this will only find UAF;
    // for secondary allocations we can find either UAF or OOB.
    getRingBufferErrorInfo(ErrorInfo, NextErrorReport, FaultAddr, Depot,
                           RingBufferPtr, RingBufferSize);

    // Check for OOB in the 28 blocks surrounding the 3 we checked earlier.
    // Beyond that we are likely to hit false positives.
    if (extractTag(FaultAddr) != 0)
      getInlineErrorInfo(ErrorInfo, NextErrorReport, FaultAddr, Depot,
                         RegionInfoPtr, Memory, MemoryTags, MemoryAddr,
                         MemorySize, 2, 16);
  }

private:
  typedef typename PrimaryT::SizeClassMap SizeClassMap;

  static const uptr MinAlignmentLog = SCUDO_MIN_ALIGNMENT_LOG;
  static const uptr MaxAlignmentLog = 24U; // 16 MB seems reasonable.
  static const uptr MinAlignment = 1UL << MinAlignmentLog;
  static const uptr MaxAlignment = 1UL << MaxAlignmentLog;
  static const uptr MaxAllowedMallocSize =
      FIRST_32_SECOND_64(1UL << 31, 1ULL << 40);

  static_assert(MinAlignment >= sizeof(Chunk::PackedHeader),
                "Minimal alignment must at least cover a chunk header.");
  static_assert(!allocatorSupportsMemoryTagging<AllocatorConfig>() ||
                    MinAlignment >= archMemoryTagGranuleSize(),
                "");

  static const u32 BlockMarker = 0x44554353U;

  // These are indexes into an "array" of 32-bit values that store information
  // inline with a chunk that is relevant to diagnosing memory tag faults, where
  // 0 corresponds to the address of the user memory. This means that only
  // negative indexes may be used. The smallest index that may be used is -2,
  // which corresponds to 8 bytes before the user memory, because the chunk
  // header size is 8 bytes and in allocators that support memory tagging the
  // minimum alignment is at least the tag granule size (16 on aarch64).
  static const sptr MemTagAllocationTraceIndex = -2;
  static const sptr MemTagAllocationTidIndex = -1;

  u32 Cookie = 0;
  u32 QuarantineMaxChunkSize = 0;

  GlobalStats Stats;
  PrimaryT Primary;
  SecondaryT Secondary;
  QuarantineT Quarantine;
  TSDRegistryT TSDRegistry;
  pthread_once_t PostInitNonce = PTHREAD_ONCE_INIT;

#ifdef GWP_ASAN_HOOKS
  gwp_asan::GuardedPoolAllocator GuardedAlloc;
  uptr GuardedAllocSlotSize = 0;
#endif // GWP_ASAN_HOOKS

  struct AllocationRingBuffer {
    struct Entry {
      atomic_uptr Ptr;
      atomic_uptr AllocationSize;
      atomic_u32 AllocationTrace;
      atomic_u32 AllocationTid;
      atomic_u32 DeallocationTrace;
      atomic_u32 DeallocationTid;
    };
    StackDepot *Depot = nullptr;
    uptr StackDepotSize = 0;
    MemMapT RawRingBufferMap;
    MemMapT RawStackDepotMap;
    u32 RingBufferElements = 0;
    atomic_uptr Pos;
    // An array of Size (at least one) elements of type Entry is immediately
    // following to this struct.
  };
  static_assert(sizeof(AllocationRingBuffer) %
                        alignof(typename AllocationRingBuffer::Entry) ==
                    0,
                "invalid alignment");

  // Lock to initialize the RingBuffer
  HybridMutex RingBufferInitLock;

  // Pointer to memory mapped area starting with AllocationRingBuffer struct,
  // and immediately followed by Size elements of type Entry.
  atomic_uptr RingBufferAddress = {};

  AllocationRingBuffer *getRingBuffer() {
    return reinterpret_cast<AllocationRingBuffer *>(
        atomic_load(&RingBufferAddress, memory_order_acquire));
  }

  // The following might get optimized out by the compiler.
  NOINLINE void performSanityChecks() {
    // Verify that the header offset field can hold the maximum offset. In the
    // case of the Secondary allocator, it takes care of alignment and the
    // offset will always be small. In the case of the Primary, the worst case
    // scenario happens in the last size class, when the backend allocation
    // would already be aligned on the requested alignment, which would happen
    // to be the maximum alignment that would fit in that size class. As a
    // result, the maximum offset will be at most the maximum alignment for the
    // last size class minus the header size, in multiples of MinAlignment.
    Chunk::UnpackedHeader Header = {};
    const uptr MaxPrimaryAlignment = 1UL << getMostSignificantSetBitIndex(
                                         SizeClassMap::MaxSize - MinAlignment);
    const uptr MaxOffset =
        (MaxPrimaryAlignment - Chunk::getHeaderSize()) >> MinAlignmentLog;
    Header.Offset = MaxOffset & Chunk::OffsetMask;
    if (UNLIKELY(Header.Offset != MaxOffset))
      reportSanityCheckError("offset");

    // Verify that we can fit the maximum size or amount of unused bytes in the
    // header. Given that the Secondary fits the allocation to a page, the worst
    // case scenario happens in the Primary. It will depend on the second to
    // last and last class sizes, as well as the dynamic base for the Primary.
    // The following is an over-approximation that works for our needs.
    const uptr MaxSizeOrUnusedBytes = SizeClassMap::MaxSize - 1;
    Header.SizeOrUnusedBytes = MaxSizeOrUnusedBytes;
    if (UNLIKELY(Header.SizeOrUnusedBytes != MaxSizeOrUnusedBytes))
      reportSanityCheckError("size (or unused bytes)");

    const uptr LargestClassId = SizeClassMap::LargestClassId;
    Header.ClassId = LargestClassId;
    if (UNLIKELY(Header.ClassId != LargestClassId))
      reportSanityCheckError("class ID");
  }

  static inline void *getBlockBegin(const void *Ptr,
                                    Chunk::UnpackedHeader *Header) {
    return reinterpret_cast<void *>(
        reinterpret_cast<uptr>(Ptr) - Chunk::getHeaderSize() -
        (static_cast<uptr>(Header->Offset) << MinAlignmentLog));
  }

  // Return the size of a chunk as requested during its allocation.
  inline uptr getSize(const void *Ptr, Chunk::UnpackedHeader *Header) {
    const uptr SizeOrUnusedBytes = Header->SizeOrUnusedBytes;
    if (LIKELY(Header->ClassId))
      return SizeOrUnusedBytes;
    if (allocatorSupportsMemoryTagging<AllocatorConfig>())
      Ptr = untagPointer(const_cast<void *>(Ptr));
    return SecondaryT::getBlockEnd(getBlockBegin(Ptr, Header)) -
           reinterpret_cast<uptr>(Ptr) - SizeOrUnusedBytes;
  }

  ALWAYS_INLINE void *initChunk(const uptr ClassId, const Chunk::Origin Origin,
                                void *Block, const uptr UserPtr,
                                const uptr SizeOrUnusedBytes,
                                const FillContentsMode FillContents) {
    // Compute the default pointer before adding the header tag
    const uptr DefaultAlignedPtr =
        reinterpret_cast<uptr>(Block) + Chunk::getHeaderSize();

    Block = addHeaderTag(Block);
    // Only do content fill when it's from primary allocator because secondary
    // allocator has filled the content.
    if (ClassId != 0 && UNLIKELY(FillContents != NoFill)) {
      // This condition is not necessarily unlikely, but since memset is
      // costly, we might as well mark it as such.
      memset(Block, FillContents == ZeroFill ? 0 : PatternFillByte,
             PrimaryT::getSizeByClassId(ClassId));
    }

    Chunk::UnpackedHeader Header = {};

    if (UNLIKELY(DefaultAlignedPtr != UserPtr)) {
      const uptr Offset = UserPtr - DefaultAlignedPtr;
      DCHECK_GE(Offset, 2 * sizeof(u32));
      // The BlockMarker has no security purpose, but is specifically meant for
      // the chunk iteration function that can be used in debugging situations.
      // It is the only situation where we have to locate the start of a chunk
      // based on its block address.
      reinterpret_cast<u32 *>(Block)[0] = BlockMarker;
      reinterpret_cast<u32 *>(Block)[1] = static_cast<u32>(Offset);
      Header.Offset = (Offset >> MinAlignmentLog) & Chunk::OffsetMask;
    }

    Header.ClassId = ClassId & Chunk::ClassIdMask;
    Header.State = Chunk::State::Allocated;
    Header.OriginOrWasZeroed = Origin & Chunk::OriginMask;
    Header.SizeOrUnusedBytes = SizeOrUnusedBytes & Chunk::SizeOrUnusedBytesMask;
    Chunk::storeHeader(Cookie, reinterpret_cast<void *>(addHeaderTag(UserPtr)),
                       &Header);

    return reinterpret_cast<void *>(UserPtr);
  }

  NOINLINE void *
  initChunkWithMemoryTagging(const uptr ClassId, const Chunk::Origin Origin,
                             void *Block, const uptr UserPtr, const uptr Size,
                             const uptr SizeOrUnusedBytes,
                             const FillContentsMode FillContents) {
    const Options Options = Primary.Options.load();
    DCHECK(useMemoryTagging<AllocatorConfig>(Options));

    // Compute the default pointer before adding the header tag
    const uptr DefaultAlignedPtr =
        reinterpret_cast<uptr>(Block) + Chunk::getHeaderSize();

    void *Ptr = reinterpret_cast<void *>(UserPtr);
    void *TaggedPtr = Ptr;

    if (LIKELY(ClassId)) {
      // Init the primary chunk.
      //
      // We only need to zero or tag the contents for Primary backed
      // allocations. We only set tags for primary allocations in order to avoid
      // faulting potentially large numbers of pages for large secondary
      // allocations. We assume that guard pages are enough to protect these
      // allocations.
      //
      // FIXME: When the kernel provides a way to set the background tag of a
      // mapping, we should be able to tag secondary allocations as well.
      //
      // When memory tagging is enabled, zeroing the contents is done as part of
      // setting the tag.

      Chunk::UnpackedHeader Header;
      const uptr BlockSize = PrimaryT::getSizeByClassId(ClassId);
      const uptr BlockUptr = reinterpret_cast<uptr>(Block);
      const uptr BlockEnd = BlockUptr + BlockSize;
      // If possible, try to reuse the UAF tag that was set by deallocate().
      // For simplicity, only reuse tags if we have the same start address as
      // the previous allocation. This handles the majority of cases since
      // most allocations will not be more aligned than the minimum alignment.
      //
      // We need to handle situations involving reclaimed chunks, and retag
      // the reclaimed portions if necessary. In the case where the chunk is
      // fully reclaimed, the chunk's header will be zero, which will trigger
      // the code path for new mappings and invalid chunks that prepares the
      // chunk from scratch. There are three possibilities for partial
      // reclaiming:
      //
      // (1) Header was reclaimed, data was partially reclaimed.
      // (2) Header was not reclaimed, all data was reclaimed (e.g. because
      //     data started on a page boundary).
      // (3) Header was not reclaimed, data was partially reclaimed.
      //
      // Case (1) will be handled in the same way as for full reclaiming,
      // since the header will be zero.
      //
      // We can detect case (2) by loading the tag from the start
      // of the chunk. If it is zero, it means that either all data was
      // reclaimed (since we never use zero as the chunk tag), or that the
      // previous allocation was of size zero. Either way, we need to prepare
      // a new chunk from scratch.
      //
      // We can detect case (3) by moving to the next page (if covered by the
      // chunk) and loading the tag of its first granule. If it is zero, it
      // means that all following pages may need to be retagged. On the other
      // hand, if it is nonzero, we can assume that all following pages are
      // still tagged, according to the logic that if any of the pages
      // following the next page were reclaimed, the next page would have been
      // reclaimed as well.
      uptr TaggedUserPtr;
      uptr PrevUserPtr;
      if (getChunkFromBlock(BlockUptr, &PrevUserPtr, &Header) &&
          PrevUserPtr == UserPtr &&
          (TaggedUserPtr = loadTag(UserPtr)) != UserPtr) {
        uptr PrevEnd = TaggedUserPtr + Header.SizeOrUnusedBytes;
        const uptr NextPage = roundUp(TaggedUserPtr, getPageSizeCached());
        if (NextPage < PrevEnd && loadTag(NextPage) != NextPage)
          PrevEnd = NextPage;
        TaggedPtr = reinterpret_cast<void *>(TaggedUserPtr);
        resizeTaggedChunk(PrevEnd, TaggedUserPtr + Size, Size, BlockEnd);
        if (UNLIKELY(FillContents != NoFill && !Header.OriginOrWasZeroed)) {
          // If an allocation needs to be zeroed (i.e. calloc) we can normally
          // avoid zeroing the memory now since we can rely on memory having
          // been zeroed on free, as this is normally done while setting the
          // UAF tag. But if tagging was disabled per-thread when the memory
          // was freed, it would not have been retagged and thus zeroed, and
          // therefore it needs to be zeroed now.
          memset(TaggedPtr, 0,
                 Min(Size, roundUp(PrevEnd - TaggedUserPtr,
                                   archMemoryTagGranuleSize())));
        } else if (Size) {
          // Clear any stack metadata that may have previously been stored in
          // the chunk data.
          memset(TaggedPtr, 0, archMemoryTagGranuleSize());
        }
      } else {
        const uptr OddEvenMask =
            computeOddEvenMaskForPointerMaybe(Options, BlockUptr, ClassId);
        TaggedPtr = prepareTaggedChunk(Ptr, Size, OddEvenMask, BlockEnd);
      }
      storePrimaryAllocationStackMaybe(Options, Ptr);
    } else {
      // Init the secondary chunk.

      Block = addHeaderTag(Block);
      Ptr = addHeaderTag(Ptr);
      storeTags(reinterpret_cast<uptr>(Block), reinterpret_cast<uptr>(Ptr));
      storeSecondaryAllocationStackMaybe(Options, Ptr, Size);
    }

    Chunk::UnpackedHeader Header = {};

    if (UNLIKELY(DefaultAlignedPtr != UserPtr)) {
      const uptr Offset = UserPtr - DefaultAlignedPtr;
      DCHECK_GE(Offset, 2 * sizeof(u32));
      // The BlockMarker has no security purpose, but is specifically meant for
      // the chunk iteration function that can be used in debugging situations.
      // It is the only situation where we have to locate the start of a chunk
      // based on its block address.
      reinterpret_cast<u32 *>(Block)[0] = BlockMarker;
      reinterpret_cast<u32 *>(Block)[1] = static_cast<u32>(Offset);
      Header.Offset = (Offset >> MinAlignmentLog) & Chunk::OffsetMask;
    }

    Header.ClassId = ClassId & Chunk::ClassIdMask;
    Header.State = Chunk::State::Allocated;
    Header.OriginOrWasZeroed = Origin & Chunk::OriginMask;
    Header.SizeOrUnusedBytes = SizeOrUnusedBytes & Chunk::SizeOrUnusedBytesMask;
    Chunk::storeHeader(Cookie, Ptr, &Header);

    return TaggedPtr;
  }

  void quarantineOrDeallocateChunk(const Options &Options, void *TaggedPtr,
                                   Chunk::UnpackedHeader *Header,
                                   uptr Size) NO_THREAD_SAFETY_ANALYSIS {
    void *Ptr = getHeaderTaggedPointer(TaggedPtr);
    // If the quarantine is disabled, the actual size of a chunk is 0 or larger
    // than the maximum allowed, we return a chunk directly to the backend.
    // This purposefully underflows for Size == 0.
    const bool BypassQuarantine = !Quarantine.getCacheSize() ||
                                  ((Size - 1) >= QuarantineMaxChunkSize) ||
                                  !Header->ClassId;
    if (BypassQuarantine)
      Header->State = Chunk::State::Available;
    else
      Header->State = Chunk::State::Quarantined;

    void *BlockBegin;
    if (LIKELY(!useMemoryTagging<AllocatorConfig>(Options))) {
      Header->OriginOrWasZeroed = 0U;
      if (BypassQuarantine && allocatorSupportsMemoryTagging<AllocatorConfig>())
        Ptr = untagPointer(Ptr);
      BlockBegin = getBlockBegin(Ptr, Header);
    } else {
      Header->OriginOrWasZeroed =
          Header->ClassId && !TSDRegistry.getDisableMemInit();
      BlockBegin =
          retagBlock(Options, TaggedPtr, Ptr, Header, Size, BypassQuarantine);
    }

    Chunk::storeHeader(Cookie, Ptr, Header);

    if (BypassQuarantine) {
      const uptr ClassId = Header->ClassId;
      if (LIKELY(ClassId)) {
        bool CacheDrained;
        {
          typename TSDRegistryT::ScopedTSD TSD(TSDRegistry);
          CacheDrained = TSD->getCache().deallocate(ClassId, BlockBegin);
        }
        // When we have drained some blocks back to the Primary from TSD, that
        // implies that we may have the chance to release some pages as well.
        // Note that in order not to block other thread's accessing the TSD,
        // release the TSD first then try the page release.
        if (CacheDrained)
          Primary.tryReleaseToOS(ClassId, ReleaseToOS::Normal);
      } else {
        Secondary.deallocate(Options, BlockBegin);
      }
    } else {
      typename TSDRegistryT::ScopedTSD TSD(TSDRegistry);
      Quarantine.put(&TSD->getQuarantineCache(),
                     QuarantineCallback(*this, TSD->getCache()), Ptr, Size);
    }
  }

  NOINLINE void *retagBlock(const Options &Options, void *TaggedPtr, void *&Ptr,
                            Chunk::UnpackedHeader *Header, const uptr Size,
                            bool BypassQuarantine) {
    DCHECK(useMemoryTagging<AllocatorConfig>(Options));

    const u8 PrevTag = extractTag(reinterpret_cast<uptr>(TaggedPtr));
    storeDeallocationStackMaybe(Options, Ptr, PrevTag, Size);
    if (Header->ClassId && !TSDRegistry.getDisableMemInit()) {
      uptr TaggedBegin, TaggedEnd;
      const uptr OddEvenMask = computeOddEvenMaskForPointerMaybe(
          Options, reinterpret_cast<uptr>(getBlockBegin(Ptr, Header)),
          Header->ClassId);
      // Exclude the previous tag so that immediate use after free is
      // detected 100% of the time.
      setRandomTag(Ptr, Size, OddEvenMask | (1UL << PrevTag), &TaggedBegin,
                   &TaggedEnd);
    }

    Ptr = untagPointer(Ptr);
    void *BlockBegin = getBlockBegin(Ptr, Header);
    if (BypassQuarantine && !Header->ClassId) {
      storeTags(reinterpret_cast<uptr>(BlockBegin),
                reinterpret_cast<uptr>(Ptr));
    }

    return BlockBegin;
  }

  bool getChunkFromBlock(uptr Block, uptr *Chunk,
                         Chunk::UnpackedHeader *Header) {
    *Chunk =
        Block + getChunkOffsetFromBlock(reinterpret_cast<const char *>(Block));
    return Chunk::isValid(Cookie, reinterpret_cast<void *>(*Chunk), Header);
  }

  static uptr getChunkOffsetFromBlock(const char *Block) {
    u32 Offset = 0;
    if (reinterpret_cast<const u32 *>(Block)[0] == BlockMarker)
      Offset = reinterpret_cast<const u32 *>(Block)[1];
    return Offset + Chunk::getHeaderSize();
  }

  // Set the tag of the granule past the end of the allocation to 0, to catch
  // linear overflows even if a previous larger allocation used the same block
  // and tag. Only do this if the granule past the end is in our block, because
  // this would otherwise lead to a SEGV if the allocation covers the entire
  // block and our block is at the end of a mapping. The tag of the next block's
  // header granule will be set to 0, so it will serve the purpose of catching
  // linear overflows in this case.
  //
  // For allocations of size 0 we do not end up storing the address tag to the
  // memory tag space, which getInlineErrorInfo() normally relies on to match
  // address tags against chunks. To allow matching in this case we store the
  // address tag in the first byte of the chunk.
  void storeEndMarker(uptr End, uptr Size, uptr BlockEnd) {
    DCHECK_EQ(BlockEnd, untagPointer(BlockEnd));
    uptr UntaggedEnd = untagPointer(End);
    if (UntaggedEnd != BlockEnd) {
      storeTag(UntaggedEnd);
      if (Size == 0)
        *reinterpret_cast<u8 *>(UntaggedEnd) = extractTag(End);
    }
  }

  void *prepareTaggedChunk(void *Ptr, uptr Size, uptr ExcludeMask,
                           uptr BlockEnd) {
    // Prepare the granule before the chunk to store the chunk header by setting
    // its tag to 0. Normally its tag will already be 0, but in the case where a
    // chunk holding a low alignment allocation is reused for a higher alignment
    // allocation, the chunk may already have a non-zero tag from the previous
    // allocation.
    storeTag(reinterpret_cast<uptr>(Ptr) - archMemoryTagGranuleSize());

    uptr TaggedBegin, TaggedEnd;
    setRandomTag(Ptr, Size, ExcludeMask, &TaggedBegin, &TaggedEnd);

    storeEndMarker(TaggedEnd, Size, BlockEnd);
    return reinterpret_cast<void *>(TaggedBegin);
  }

  void resizeTaggedChunk(uptr OldPtr, uptr NewPtr, uptr NewSize,
                         uptr BlockEnd) {
    uptr RoundOldPtr = roundUp(OldPtr, archMemoryTagGranuleSize());
    uptr RoundNewPtr;
    if (RoundOldPtr >= NewPtr) {
      // If the allocation is shrinking we just need to set the tag past the end
      // of the allocation to 0. See explanation in storeEndMarker() above.
      RoundNewPtr = roundUp(NewPtr, archMemoryTagGranuleSize());
    } else {
      // Set the memory tag of the region
      // [RoundOldPtr, roundUp(NewPtr, archMemoryTagGranuleSize()))
      // to the pointer tag stored in OldPtr.
      RoundNewPtr = storeTags(RoundOldPtr, NewPtr);
    }
    storeEndMarker(RoundNewPtr, NewSize, BlockEnd);
  }

  void storePrimaryAllocationStackMaybe(const Options &Options, void *Ptr) {
    if (!UNLIKELY(Options.get(OptionBit::TrackAllocationStacks)))
      return;
    AllocationRingBuffer *RB = getRingBuffer();
    if (!RB)
      return;
    auto *Ptr32 = reinterpret_cast<u32 *>(Ptr);
    Ptr32[MemTagAllocationTraceIndex] = collectStackTrace(RB->Depot);
    Ptr32[MemTagAllocationTidIndex] = getThreadID();
  }

  void storeRingBufferEntry(AllocationRingBuffer *RB, void *Ptr,
                            u32 AllocationTrace, u32 AllocationTid,
                            uptr AllocationSize, u32 DeallocationTrace,
                            u32 DeallocationTid) {
    uptr Pos = atomic_fetch_add(&RB->Pos, 1, memory_order_relaxed);
    typename AllocationRingBuffer::Entry *Entry =
        getRingBufferEntry(RB, Pos % RB->RingBufferElements);

    // First invalidate our entry so that we don't attempt to interpret a
    // partially written state in getSecondaryErrorInfo(). The fences below
    // ensure that the compiler does not move the stores to Ptr in between the
    // stores to the other fields.
    atomic_store_relaxed(&Entry->Ptr, 0);

    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    atomic_store_relaxed(&Entry->AllocationTrace, AllocationTrace);
    atomic_store_relaxed(&Entry->AllocationTid, AllocationTid);
    atomic_store_relaxed(&Entry->AllocationSize, AllocationSize);
    atomic_store_relaxed(&Entry->DeallocationTrace, DeallocationTrace);
    atomic_store_relaxed(&Entry->DeallocationTid, DeallocationTid);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);

    atomic_store_relaxed(&Entry->Ptr, reinterpret_cast<uptr>(Ptr));
  }

  void storeSecondaryAllocationStackMaybe(const Options &Options, void *Ptr,
                                          uptr Size) {
    if (!UNLIKELY(Options.get(OptionBit::TrackAllocationStacks)))
      return;
    AllocationRingBuffer *RB = getRingBuffer();
    if (!RB)
      return;
    u32 Trace = collectStackTrace(RB->Depot);
    u32 Tid = getThreadID();

    auto *Ptr32 = reinterpret_cast<u32 *>(Ptr);
    Ptr32[MemTagAllocationTraceIndex] = Trace;
    Ptr32[MemTagAllocationTidIndex] = Tid;

    storeRingBufferEntry(RB, untagPointer(Ptr), Trace, Tid, Size, 0, 0);
  }

  void storeDeallocationStackMaybe(const Options &Options, void *Ptr,
                                   u8 PrevTag, uptr Size) {
    if (!UNLIKELY(Options.get(OptionBit::TrackAllocationStacks)))
      return;
    AllocationRingBuffer *RB = getRingBuffer();
    if (!RB)
      return;
    auto *Ptr32 = reinterpret_cast<u32 *>(Ptr);
    u32 AllocationTrace = Ptr32[MemTagAllocationTraceIndex];
    u32 AllocationTid = Ptr32[MemTagAllocationTidIndex];

    u32 DeallocationTrace = collectStackTrace(RB->Depot);
    u32 DeallocationTid = getThreadID();

    storeRingBufferEntry(RB, addFixedTag(untagPointer(Ptr), PrevTag),
                         AllocationTrace, AllocationTid, Size,
                         DeallocationTrace, DeallocationTid);
  }

  static const size_t NumErrorReports =
      sizeof(((scudo_error_info *)nullptr)->reports) /
      sizeof(((scudo_error_info *)nullptr)->reports[0]);

  static void getInlineErrorInfo(struct scudo_error_info *ErrorInfo,
                                 size_t &NextErrorReport, uintptr_t FaultAddr,
                                 const StackDepot *Depot,
                                 const char *RegionInfoPtr, const char *Memory,
                                 const char *MemoryTags, uintptr_t MemoryAddr,
                                 size_t MemorySize, size_t MinDistance,
                                 size_t MaxDistance) {
    uptr UntaggedFaultAddr = untagPointer(FaultAddr);
    u8 FaultAddrTag = extractTag(FaultAddr);
    BlockInfo Info =
        PrimaryT::findNearestBlock(RegionInfoPtr, UntaggedFaultAddr);

    auto GetGranule = [&](uptr Addr, const char **Data, uint8_t *Tag) -> bool {
      if (Addr < MemoryAddr || Addr + archMemoryTagGranuleSize() < Addr ||
          Addr + archMemoryTagGranuleSize() > MemoryAddr + MemorySize)
        return false;
      *Data = &Memory[Addr - MemoryAddr];
      *Tag = static_cast<u8>(
          MemoryTags[(Addr - MemoryAddr) / archMemoryTagGranuleSize()]);
      return true;
    };

    auto ReadBlock = [&](uptr Addr, uptr *ChunkAddr,
                         Chunk::UnpackedHeader *Header, const u32 **Data,
                         u8 *Tag) {
      const char *BlockBegin;
      u8 BlockBeginTag;
      if (!GetGranule(Addr, &BlockBegin, &BlockBeginTag))
        return false;
      uptr ChunkOffset = getChunkOffsetFromBlock(BlockBegin);
      *ChunkAddr = Addr + ChunkOffset;

      const char *ChunkBegin;
      if (!GetGranule(*ChunkAddr, &ChunkBegin, Tag))
        return false;
      *Header = *reinterpret_cast<const Chunk::UnpackedHeader *>(
          ChunkBegin - Chunk::getHeaderSize());
      *Data = reinterpret_cast<const u32 *>(ChunkBegin);

      // Allocations of size 0 will have stashed the tag in the first byte of
      // the chunk, see storeEndMarker().
      if (Header->SizeOrUnusedBytes == 0)
        *Tag = static_cast<u8>(*ChunkBegin);

      return true;
    };

    if (NextErrorReport == NumErrorReports)
      return;

    auto CheckOOB = [&](uptr BlockAddr) {
      if (BlockAddr < Info.RegionBegin || BlockAddr >= Info.RegionEnd)
        return false;

      uptr ChunkAddr;
      Chunk::UnpackedHeader Header;
      const u32 *Data;
      uint8_t Tag;
      if (!ReadBlock(BlockAddr, &ChunkAddr, &Header, &Data, &Tag) ||
          Header.State != Chunk::State::Allocated || Tag != FaultAddrTag)
        return false;

      auto *R = &ErrorInfo->reports[NextErrorReport++];
      R->error_type =
          UntaggedFaultAddr < ChunkAddr ? BUFFER_UNDERFLOW : BUFFER_OVERFLOW;
      R->allocation_address = ChunkAddr;
      R->allocation_size = Header.SizeOrUnusedBytes;
      if (Depot) {
        collectTraceMaybe(Depot, R->allocation_trace,
                          Data[MemTagAllocationTraceIndex]);
      }
      R->allocation_tid = Data[MemTagAllocationTidIndex];
      return NextErrorReport == NumErrorReports;
    };

    if (MinDistance == 0 && CheckOOB(Info.BlockBegin))
      return;

    for (size_t I = Max<size_t>(MinDistance, 1); I != MaxDistance; ++I)
      if (CheckOOB(Info.BlockBegin + I * Info.BlockSize) ||
          CheckOOB(Info.BlockBegin - I * Info.BlockSize))
        return;
  }

  static void getRingBufferErrorInfo(struct scudo_error_info *ErrorInfo,
                                     size_t &NextErrorReport,
                                     uintptr_t FaultAddr,
                                     const StackDepot *Depot,
                                     const char *RingBufferPtr,
                                     size_t RingBufferSize) {
    auto *RingBuffer =
        reinterpret_cast<const AllocationRingBuffer *>(RingBufferPtr);
    size_t RingBufferElements = ringBufferElementsFromBytes(RingBufferSize);
    if (!RingBuffer || RingBufferElements == 0 || !Depot)
      return;
    uptr Pos = atomic_load_relaxed(&RingBuffer->Pos);

    for (uptr I = Pos - 1; I != Pos - 1 - RingBufferElements &&
                           NextErrorReport != NumErrorReports;
         --I) {
      auto *Entry = getRingBufferEntry(RingBuffer, I % RingBufferElements);
      uptr EntryPtr = atomic_load_relaxed(&Entry->Ptr);
      if (!EntryPtr)
        continue;

      uptr UntaggedEntryPtr = untagPointer(EntryPtr);
      uptr EntrySize = atomic_load_relaxed(&Entry->AllocationSize);
      u32 AllocationTrace = atomic_load_relaxed(&Entry->AllocationTrace);
      u32 AllocationTid = atomic_load_relaxed(&Entry->AllocationTid);
      u32 DeallocationTrace = atomic_load_relaxed(&Entry->DeallocationTrace);
      u32 DeallocationTid = atomic_load_relaxed(&Entry->DeallocationTid);

      if (DeallocationTid) {
        // For UAF we only consider in-bounds fault addresses because
        // out-of-bounds UAF is rare and attempting to detect it is very likely
        // to result in false positives.
        if (FaultAddr < EntryPtr || FaultAddr >= EntryPtr + EntrySize)
          continue;
      } else {
        // Ring buffer OOB is only possible with secondary allocations. In this
        // case we are guaranteed a guard region of at least a page on either
        // side of the allocation (guard page on the right, guard page + tagged
        // region on the left), so ignore any faults outside of that range.
        if (FaultAddr < EntryPtr - getPageSizeCached() ||
            FaultAddr >= EntryPtr + EntrySize + getPageSizeCached())
          continue;

        // For UAF the ring buffer will contain two entries, one for the
        // allocation and another for the deallocation. Don't report buffer
        // overflow/underflow using the allocation entry if we have already
        // collected a report from the deallocation entry.
        bool Found = false;
        for (uptr J = 0; J != NextErrorReport; ++J) {
          if (ErrorInfo->reports[J].allocation_address == UntaggedEntryPtr) {
            Found = true;
            break;
          }
        }
        if (Found)
          continue;
      }

      auto *R = &ErrorInfo->reports[NextErrorReport++];
      if (DeallocationTid)
        R->error_type = USE_AFTER_FREE;
      else if (FaultAddr < EntryPtr)
        R->error_type = BUFFER_UNDERFLOW;
      else
        R->error_type = BUFFER_OVERFLOW;

      R->allocation_address = UntaggedEntryPtr;
      R->allocation_size = EntrySize;
      collectTraceMaybe(Depot, R->allocation_trace, AllocationTrace);
      R->allocation_tid = AllocationTid;
      collectTraceMaybe(Depot, R->deallocation_trace, DeallocationTrace);
      R->deallocation_tid = DeallocationTid;
    }
  }

  uptr getStats(ScopedString *Str) {
    Primary.getStats(Str);
    Secondary.getStats(Str);
    Quarantine.getStats(Str);
    TSDRegistry.getStats(Str);
    return Str->length();
  }

  static typename AllocationRingBuffer::Entry *
  getRingBufferEntry(AllocationRingBuffer *RB, uptr N) {
    char *RBEntryStart =
        &reinterpret_cast<char *>(RB)[sizeof(AllocationRingBuffer)];
    return &reinterpret_cast<typename AllocationRingBuffer::Entry *>(
        RBEntryStart)[N];
  }
  static const typename AllocationRingBuffer::Entry *
  getRingBufferEntry(const AllocationRingBuffer *RB, uptr N) {
    const char *RBEntryStart =
        &reinterpret_cast<const char *>(RB)[sizeof(AllocationRingBuffer)];
    return &reinterpret_cast<const typename AllocationRingBuffer::Entry *>(
        RBEntryStart)[N];
  }

  void initRingBufferMaybe() {
    ScopedLock L(RingBufferInitLock);
    if (getRingBuffer() != nullptr)
      return;

    int ring_buffer_size = getFlags()->allocation_ring_buffer_size;
    if (ring_buffer_size <= 0)
      return;

    u32 AllocationRingBufferSize = static_cast<u32>(ring_buffer_size);

    // We store alloc and free stacks for each entry.
    constexpr u32 kStacksPerRingBufferEntry = 2;
    constexpr u32 kMaxU32Pow2 = ~(UINT32_MAX >> 1);
    static_assert(isPowerOfTwo(kMaxU32Pow2));
    // On Android we always have 3 frames at the bottom: __start_main,
    // __libc_init, main, and 3 at the top: malloc, scudo_malloc and
    // Allocator::allocate. This leaves 10 frames for the user app. The next
    // smallest power of two (8) would only leave 2, which is clearly too
    // little.
    constexpr u32 kFramesPerStack = 16;
    static_assert(isPowerOfTwo(kFramesPerStack));

    if (AllocationRingBufferSize > kMaxU32Pow2 / kStacksPerRingBufferEntry)
      return;
    u32 TabSize = static_cast<u32>(roundUpPowerOfTwo(kStacksPerRingBufferEntry *
                                                     AllocationRingBufferSize));
    if (TabSize > UINT32_MAX / kFramesPerStack)
      return;
    u32 RingSize = static_cast<u32>(TabSize * kFramesPerStack);

    uptr StackDepotSize = sizeof(StackDepot) + sizeof(atomic_u64) * RingSize +
                          sizeof(atomic_u32) * TabSize;
    MemMapT DepotMap;
    DepotMap.map(
        /*Addr=*/0U, roundUp(StackDepotSize, getPageSizeCached()),
        "scudo:stack_depot");
    auto *Depot = reinterpret_cast<StackDepot *>(DepotMap.getBase());
    Depot->init(RingSize, TabSize);

    MemMapT MemMap;
    MemMap.map(
        /*Addr=*/0U,
        roundUp(ringBufferSizeInBytes(AllocationRingBufferSize),
                getPageSizeCached()),
        "scudo:ring_buffer");
    auto *RB = reinterpret_cast<AllocationRingBuffer *>(MemMap.getBase());
    RB->RawRingBufferMap = MemMap;
    RB->RingBufferElements = AllocationRingBufferSize;
    RB->Depot = Depot;
    RB->StackDepotSize = StackDepotSize;
    RB->RawStackDepotMap = DepotMap;

    atomic_store(&RingBufferAddress, reinterpret_cast<uptr>(RB),
                 memory_order_release);
  }

  void unmapRingBuffer() {
    AllocationRingBuffer *RB = getRingBuffer();
    if (RB == nullptr)
      return;
    // N.B. because RawStackDepotMap is part of RawRingBufferMap, the order
    // is very important.
    RB->RawStackDepotMap.unmap(RB->RawStackDepotMap.getBase(),
                               RB->RawStackDepotMap.getCapacity());
    // Note that the `RB->RawRingBufferMap` is stored on the pages managed by
    // itself. Take over the ownership before calling unmap() so that any
    // operation along with unmap() won't touch inaccessible pages.
    MemMapT RawRingBufferMap = RB->RawRingBufferMap;
    RawRingBufferMap.unmap(RawRingBufferMap.getBase(),
                           RawRingBufferMap.getCapacity());
    atomic_store(&RingBufferAddress, 0, memory_order_release);
  }

  static constexpr size_t ringBufferSizeInBytes(u32 RingBufferElements) {
    return sizeof(AllocationRingBuffer) +
           RingBufferElements * sizeof(typename AllocationRingBuffer::Entry);
  }

  static constexpr size_t ringBufferElementsFromBytes(size_t Bytes) {
    if (Bytes < sizeof(AllocationRingBuffer)) {
      return 0;
    }
    return (Bytes - sizeof(AllocationRingBuffer)) /
           sizeof(typename AllocationRingBuffer::Entry);
  }
};

} // namespace scudo

#endif // SCUDO_COMBINED_H_
