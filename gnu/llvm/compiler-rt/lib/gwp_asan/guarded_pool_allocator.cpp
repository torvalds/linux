//===-- guarded_pool_allocator.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/guarded_pool_allocator.h"

#include "gwp_asan/crash_handler.h"
#include "gwp_asan/options.h"
#include "gwp_asan/utilities.h"

#include <assert.h>
#include <stddef.h>

using AllocationMetadata = gwp_asan::AllocationMetadata;
using Error = gwp_asan::Error;

namespace gwp_asan {
namespace {
// Forward declare the pointer to the singleton version of this class.
// Instantiated during initialisation, this allows the signal handler
// to find this class in order to deduce the root cause of failures. Must not be
// referenced by users outside this translation unit, in order to avoid
// init-order-fiasco.
GuardedPoolAllocator *SingletonPtr = nullptr;

size_t roundUpTo(size_t Size, size_t Boundary) {
  return (Size + Boundary - 1) & ~(Boundary - 1);
}

uintptr_t getPageAddr(uintptr_t Ptr, uintptr_t PageSize) {
  return Ptr & ~(PageSize - 1);
}

bool isPowerOfTwo(uintptr_t X) { return (X & (X - 1)) == 0; }
} // anonymous namespace

// Gets the singleton implementation of this class. Thread-compatible until
// init() is called, thread-safe afterwards.
GuardedPoolAllocator *GuardedPoolAllocator::getSingleton() {
  return SingletonPtr;
}

void GuardedPoolAllocator::init(const options::Options &Opts) {
  // Note: We return from the constructor here if GWP-ASan is not available.
  // This will stop heap-allocation of class members, as well as mmap() of the
  // guarded slots.
  if (!Opts.Enabled || Opts.SampleRate == 0 ||
      Opts.MaxSimultaneousAllocations == 0)
    return;

  check(Opts.SampleRate >= 0, "GWP-ASan Error: SampleRate is < 0.");
  check(Opts.SampleRate < (1 << 30), "GWP-ASan Error: SampleRate is >= 2^30.");
  check(Opts.MaxSimultaneousAllocations >= 0,
        "GWP-ASan Error: MaxSimultaneousAllocations is < 0.");

  check(SingletonPtr == nullptr,
        "There's already a live GuardedPoolAllocator!");
  SingletonPtr = this;
  Backtrace = Opts.Backtrace;

  State.VersionMagic = {{AllocatorVersionMagic::kAllocatorVersionMagic[0],
                         AllocatorVersionMagic::kAllocatorVersionMagic[1],
                         AllocatorVersionMagic::kAllocatorVersionMagic[2],
                         AllocatorVersionMagic::kAllocatorVersionMagic[3]},
                        AllocatorVersionMagic::kAllocatorVersion,
                        0};

  State.MaxSimultaneousAllocations = Opts.MaxSimultaneousAllocations;

  const size_t PageSize = getPlatformPageSize();
  // getPageAddr() and roundUpTo() assume the page size to be a power of 2.
  assert((PageSize & (PageSize - 1)) == 0);
  State.PageSize = PageSize;

  // Number of pages required =
  //  + MaxSimultaneousAllocations * maximumAllocationSize (N pages per slot)
  //  + MaxSimultaneousAllocations (one guard on the left side of each slot)
  //  + 1 (an extra guard page at the end of the pool, on the right side)
  //  + 1 (an extra page that's used for reporting internally-detected crashes,
  //       like double free and invalid free, to the signal handler; see
  //       raiseInternallyDetectedError() for more info)
  size_t PoolBytesRequired =
      PageSize * (2 + State.MaxSimultaneousAllocations) +
      State.MaxSimultaneousAllocations * State.maximumAllocationSize();
  assert(PoolBytesRequired % PageSize == 0);
  void *GuardedPoolMemory = reserveGuardedPool(PoolBytesRequired);

  size_t BytesRequired =
      roundUpTo(State.MaxSimultaneousAllocations * sizeof(*Metadata), PageSize);
  Metadata = reinterpret_cast<AllocationMetadata *>(
      map(BytesRequired, kGwpAsanMetadataName));

  // Allocate memory and set up the free pages queue.
  BytesRequired = roundUpTo(
      State.MaxSimultaneousAllocations * sizeof(*FreeSlots), PageSize);
  FreeSlots =
      reinterpret_cast<size_t *>(map(BytesRequired, kGwpAsanFreeSlotsName));

  // Multiply the sample rate by 2 to give a good, fast approximation for (1 /
  // SampleRate) chance of sampling.
  if (Opts.SampleRate != 1)
    AdjustedSampleRatePlusOne = static_cast<uint32_t>(Opts.SampleRate) * 2 + 1;
  else
    AdjustedSampleRatePlusOne = 2;

  initPRNG();
  getThreadLocals()->NextSampleCounter =
      ((getRandomUnsigned32() % (AdjustedSampleRatePlusOne - 1)) + 1) &
      ThreadLocalPackedVariables::NextSampleCounterMask;

  State.GuardedPagePool = reinterpret_cast<uintptr_t>(GuardedPoolMemory);
  State.GuardedPagePoolEnd =
      reinterpret_cast<uintptr_t>(GuardedPoolMemory) + PoolBytesRequired;

  if (Opts.InstallForkHandlers)
    installAtFork();
}

void GuardedPoolAllocator::disable() {
  PoolMutex.lock();
  BacktraceMutex.lock();
}

void GuardedPoolAllocator::enable() {
  PoolMutex.unlock();
  BacktraceMutex.unlock();
}

void GuardedPoolAllocator::iterate(void *Base, size_t Size, iterate_callback Cb,
                                   void *Arg) {
  uintptr_t Start = reinterpret_cast<uintptr_t>(Base);
  for (size_t i = 0; i < State.MaxSimultaneousAllocations; ++i) {
    const AllocationMetadata &Meta = Metadata[i];
    if (Meta.Addr && !Meta.IsDeallocated && Meta.Addr >= Start &&
        Meta.Addr < Start + Size)
      Cb(Meta.Addr, Meta.RequestedSize, Arg);
  }
}

void GuardedPoolAllocator::uninitTestOnly() {
  if (State.GuardedPagePool) {
    unreserveGuardedPool();
    State.GuardedPagePool = 0;
    State.GuardedPagePoolEnd = 0;
  }
  if (Metadata) {
    unmap(Metadata,
          roundUpTo(State.MaxSimultaneousAllocations * sizeof(*Metadata),
                    State.PageSize));
    Metadata = nullptr;
  }
  if (FreeSlots) {
    unmap(FreeSlots,
          roundUpTo(State.MaxSimultaneousAllocations * sizeof(*FreeSlots),
                    State.PageSize));
    FreeSlots = nullptr;
  }
  *getThreadLocals() = ThreadLocalPackedVariables();
  SingletonPtr = nullptr;
}

// Note, minimum backing allocation size in GWP-ASan is always one page, and
// each slot could potentially be multiple pages (but always in
// page-increments). Thus, for anything that requires less than page size
// alignment, we don't need to allocate extra padding to ensure the alignment
// can be met.
size_t GuardedPoolAllocator::getRequiredBackingSize(size_t Size,
                                                    size_t Alignment,
                                                    size_t PageSize) {
  assert(isPowerOfTwo(Alignment) && "Alignment must be a power of two!");
  assert(Alignment != 0 && "Alignment should be non-zero");
  assert(Size != 0 && "Size should be non-zero");

  if (Alignment <= PageSize)
    return Size;

  return Size + Alignment - PageSize;
}

uintptr_t GuardedPoolAllocator::alignUp(uintptr_t Ptr, size_t Alignment) {
  assert(isPowerOfTwo(Alignment) && "Alignment must be a power of two!");
  assert(Alignment != 0 && "Alignment should be non-zero");
  if ((Ptr & (Alignment - 1)) == 0)
    return Ptr;

  Ptr += Alignment - (Ptr & (Alignment - 1));
  return Ptr;
}

uintptr_t GuardedPoolAllocator::alignDown(uintptr_t Ptr, size_t Alignment) {
  assert(isPowerOfTwo(Alignment) && "Alignment must be a power of two!");
  assert(Alignment != 0 && "Alignment should be non-zero");
  if ((Ptr & (Alignment - 1)) == 0)
    return Ptr;

  Ptr -= Ptr & (Alignment - 1);
  return Ptr;
}

void *GuardedPoolAllocator::allocate(size_t Size, size_t Alignment) {
  // GuardedPagePoolEnd == 0 when GWP-ASan is disabled. If we are disabled, fall
  // back to the supporting allocator.
  if (State.GuardedPagePoolEnd == 0) {
    getThreadLocals()->NextSampleCounter =
        (AdjustedSampleRatePlusOne - 1) &
        ThreadLocalPackedVariables::NextSampleCounterMask;
    return nullptr;
  }

  if (Size == 0)
    Size = 1;
  if (Alignment == 0)
    Alignment = alignof(max_align_t);

  if (!isPowerOfTwo(Alignment) || Alignment > State.maximumAllocationSize() ||
      Size > State.maximumAllocationSize())
    return nullptr;

  size_t BackingSize = getRequiredBackingSize(Size, Alignment, State.PageSize);
  if (BackingSize > State.maximumAllocationSize())
    return nullptr;

  // Protect against recursivity.
  if (getThreadLocals()->RecursiveGuard)
    return nullptr;
  ScopedRecursiveGuard SRG;

  size_t Index;
  {
    ScopedLock L(PoolMutex);
    Index = reserveSlot();
  }

  if (Index == kInvalidSlotID)
    return nullptr;

  uintptr_t SlotStart = State.slotToAddr(Index);
  AllocationMetadata *Meta = addrToMetadata(SlotStart);
  uintptr_t SlotEnd = State.slotToAddr(Index) + State.maximumAllocationSize();
  uintptr_t UserPtr;
  // Randomly choose whether to left-align or right-align the allocation, and
  // then apply the necessary adjustments to get an aligned pointer.
  if (getRandomUnsigned32() % 2 == 0)
    UserPtr = alignUp(SlotStart, Alignment);
  else
    UserPtr = alignDown(SlotEnd - Size, Alignment);

  assert(UserPtr >= SlotStart);
  assert(UserPtr + Size <= SlotEnd);

  // If a slot is multiple pages in size, and the allocation takes up a single
  // page, we can improve overflow detection by leaving the unused pages as
  // unmapped.
  const size_t PageSize = State.PageSize;
  allocateInGuardedPool(
      reinterpret_cast<void *>(getPageAddr(UserPtr, PageSize)),
      roundUpTo(Size, PageSize));

  Meta->RecordAllocation(UserPtr, Size);
  {
    ScopedLock UL(BacktraceMutex);
    Meta->AllocationTrace.RecordBacktrace(Backtrace);
  }

  return reinterpret_cast<void *>(UserPtr);
}

void GuardedPoolAllocator::raiseInternallyDetectedError(uintptr_t Address,
                                                        Error E) {
  // Disable the allocator before setting the internal failure state. In
  // non-recoverable mode, the allocator will be permanently disabled, and so
  // things will be accessed without locks.
  disable();

  // Races between internally- and externally-raised faults can happen. Right
  // now, in this thread we've locked the allocator in order to raise an
  // internally-detected fault, and another thread could SIGSEGV to raise an
  // externally-detected fault. What will happen is that the other thread will
  // wait in the signal handler, as we hold the allocator's locks from the
  // disable() above. We'll trigger the signal handler by touching the
  // internal-signal-raising address below, and the signal handler from our
  // thread will get to run first as we will continue to hold the allocator
  // locks until the enable() at the end of this function. Be careful though, if
  // this thread receives another SIGSEGV after the disable() above, but before
  // touching the internal-signal-raising address below, then this thread will
  // get an "externally-raised" SIGSEGV while *also* holding the allocator
  // locks, which means this thread's signal handler will deadlock. This could
  // be resolved with a re-entrant lock, but asking platforms to implement this
  // seems unnecessary given the only way to get a SIGSEGV in this critical
  // section is either a memory safety bug in the couple lines of code below (be
  // careful!), or someone outside uses `kill(this_thread, SIGSEGV)`, which
  // really shouldn't happen.

  State.FailureType = E;
  State.FailureAddress = Address;

  // Raise a SEGV by touching a specific address that identifies to the crash
  // handler that this is an internally-raised fault. Changing this address?
  // Don't forget to update __gwp_asan_get_internal_crash_address.
  volatile char *p =
      reinterpret_cast<char *>(State.internallyDetectedErrorFaultAddress());
  *p = 0;

  // This should never be reached in non-recoverable mode. Ensure that the
  // signal handler called handleRecoverablePostCrashReport(), which was
  // responsible for re-setting these fields.
  assert(State.FailureType == Error::UNKNOWN);
  assert(State.FailureAddress == 0u);

  // In recoverable mode, the signal handler (after dumping the crash) marked
  // the page containing the InternalFaultSegvAddress as read/writeable, to
  // allow the second touch to succeed after returning from the signal handler.
  // Now, we need to mark the page as non-read/write-able again, so future
  // internal faults can be raised.
  deallocateInGuardedPool(
      reinterpret_cast<void *>(getPageAddr(
          State.internallyDetectedErrorFaultAddress(), State.PageSize)),
      State.PageSize);

  // And now we're done with patching ourselves back up, enable the allocator.
  enable();
}

void GuardedPoolAllocator::deallocate(void *Ptr) {
  assert(pointerIsMine(Ptr) && "Pointer is not mine!");
  uintptr_t UPtr = reinterpret_cast<uintptr_t>(Ptr);
  size_t Slot = State.getNearestSlot(UPtr);
  uintptr_t SlotStart = State.slotToAddr(Slot);
  AllocationMetadata *Meta = addrToMetadata(UPtr);

  // If this allocation is responsible for crash, never recycle it. Turn the
  // deallocate() call into a no-op.
  if (Meta->HasCrashed)
    return;

  if (Meta->Addr != UPtr) {
    raiseInternallyDetectedError(UPtr, Error::INVALID_FREE);
    return;
  }
  if (Meta->IsDeallocated) {
    raiseInternallyDetectedError(UPtr, Error::DOUBLE_FREE);
    return;
  }

  // Intentionally scope the mutex here, so that other threads can access the
  // pool during the expensive markInaccessible() call.
  {
    ScopedLock L(PoolMutex);

    // Ensure that the deallocation is recorded before marking the page as
    // inaccessible. Otherwise, a racy use-after-free will have inconsistent
    // metadata.
    Meta->RecordDeallocation();

    // Ensure that the unwinder is not called if the recursive flag is set,
    // otherwise non-reentrant unwinders may deadlock.
    if (!getThreadLocals()->RecursiveGuard) {
      ScopedRecursiveGuard SRG;
      ScopedLock UL(BacktraceMutex);
      Meta->DeallocationTrace.RecordBacktrace(Backtrace);
    }
  }

  deallocateInGuardedPool(reinterpret_cast<void *>(SlotStart),
                          State.maximumAllocationSize());

  // And finally, lock again to release the slot back into the pool.
  ScopedLock L(PoolMutex);
  freeSlot(Slot);
}

// Thread-compatible, protected by PoolMutex.
static bool PreviousRecursiveGuard;

void GuardedPoolAllocator::preCrashReport(void *Ptr) {
  assert(pointerIsMine(Ptr) && "Pointer is not mine!");
  uintptr_t InternalCrashAddr = __gwp_asan_get_internal_crash_address(
      &State, reinterpret_cast<uintptr_t>(Ptr));
  if (!InternalCrashAddr)
    disable();

  // If something in the signal handler calls malloc() while dumping the
  // GWP-ASan report (e.g. backtrace_symbols()), make sure that GWP-ASan doesn't
  // service that allocation. `PreviousRecursiveGuard` is protected by the
  // allocator locks taken in disable(), either explicitly above for
  // externally-raised errors, or implicitly in raiseInternallyDetectedError()
  // for internally-detected errors.
  PreviousRecursiveGuard = getThreadLocals()->RecursiveGuard;
  getThreadLocals()->RecursiveGuard = true;
}

void GuardedPoolAllocator::postCrashReportRecoverableOnly(void *SignalPtr) {
  uintptr_t SignalUPtr = reinterpret_cast<uintptr_t>(SignalPtr);
  uintptr_t InternalCrashAddr =
      __gwp_asan_get_internal_crash_address(&State, SignalUPtr);
  uintptr_t ErrorUptr = InternalCrashAddr ?: SignalUPtr;

  AllocationMetadata *Metadata = addrToMetadata(ErrorUptr);
  Metadata->HasCrashed = true;

  allocateInGuardedPool(
      reinterpret_cast<void *>(getPageAddr(SignalUPtr, State.PageSize)),
      State.PageSize);

  // Clear the internal state in order to not confuse the crash handler if a
  // use-after-free or buffer-overflow comes from a different allocation in the
  // future.
  if (InternalCrashAddr) {
    State.FailureType = Error::UNKNOWN;
    State.FailureAddress = 0;
  }

  size_t Slot = State.getNearestSlot(ErrorUptr);
  // If the slot is available, remove it permanently.
  for (size_t i = 0; i < FreeSlotsLength; ++i) {
    if (FreeSlots[i] == Slot) {
      FreeSlots[i] = FreeSlots[FreeSlotsLength - 1];
      FreeSlotsLength -= 1;
      break;
    }
  }

  getThreadLocals()->RecursiveGuard = PreviousRecursiveGuard;
  if (!InternalCrashAddr)
    enable();
}

size_t GuardedPoolAllocator::getSize(const void *Ptr) {
  assert(pointerIsMine(Ptr));
  ScopedLock L(PoolMutex);
  AllocationMetadata *Meta = addrToMetadata(reinterpret_cast<uintptr_t>(Ptr));
  assert(Meta->Addr == reinterpret_cast<uintptr_t>(Ptr));
  return Meta->RequestedSize;
}

AllocationMetadata *GuardedPoolAllocator::addrToMetadata(uintptr_t Ptr) const {
  return &Metadata[State.getNearestSlot(Ptr)];
}

size_t GuardedPoolAllocator::reserveSlot() {
  // Avoid potential reuse of a slot before we have made at least a single
  // allocation in each slot. Helps with our use-after-free detection.
  if (NumSampledAllocations < State.MaxSimultaneousAllocations)
    return NumSampledAllocations++;

  if (FreeSlotsLength == 0)
    return kInvalidSlotID;

  size_t ReservedIndex = getRandomUnsigned32() % FreeSlotsLength;
  size_t SlotIndex = FreeSlots[ReservedIndex];
  FreeSlots[ReservedIndex] = FreeSlots[--FreeSlotsLength];
  return SlotIndex;
}

void GuardedPoolAllocator::freeSlot(size_t SlotIndex) {
  assert(FreeSlotsLength < State.MaxSimultaneousAllocations);
  FreeSlots[FreeSlotsLength++] = SlotIndex;
}

uint32_t GuardedPoolAllocator::getRandomUnsigned32() {
  uint32_t RandomState = getThreadLocals()->RandomState;
  RandomState ^= RandomState << 13;
  RandomState ^= RandomState >> 17;
  RandomState ^= RandomState << 5;
  getThreadLocals()->RandomState = RandomState;
  return RandomState;
}
} // namespace gwp_asan
