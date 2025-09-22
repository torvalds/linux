//===-- tsd_shared.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TSD_SHARED_H_
#define SCUDO_TSD_SHARED_H_

#include "tsd.h"

#include "string_utils.h"

#if SCUDO_HAS_PLATFORM_TLS_SLOT
// This is a platform-provided header that needs to be on the include path when
// Scudo is compiled. It must declare a function with the prototype:
//   uintptr_t *getPlatformAllocatorTlsSlot()
// that returns the address of a thread-local word of storage reserved for
// Scudo, that must be zero-initialized in newly created threads.
#include "scudo_platform_tls_slot.h"
#endif

namespace scudo {

template <class Allocator, u32 TSDsArraySize, u32 DefaultTSDCount>
struct TSDRegistrySharedT {
  using ThisT = TSDRegistrySharedT<Allocator, TSDsArraySize, DefaultTSDCount>;

  struct ScopedTSD {
    ALWAYS_INLINE ScopedTSD(ThisT &TSDRegistry) {
      CurrentTSD = TSDRegistry.getTSDAndLock();
      DCHECK_NE(CurrentTSD, nullptr);
    }

    ~ScopedTSD() { CurrentTSD->unlock(); }

    TSD<Allocator> &operator*() { return *CurrentTSD; }

    TSD<Allocator> *operator->() {
      CurrentTSD->assertLocked(/*BypassCheck=*/false);
      return CurrentTSD;
    }

  private:
    TSD<Allocator> *CurrentTSD;
  };

  void init(Allocator *Instance) REQUIRES(Mutex) {
    DCHECK(!Initialized);
    Instance->init();
    for (u32 I = 0; I < TSDsArraySize; I++)
      TSDs[I].init(Instance);
    const u32 NumberOfCPUs = getNumberOfCPUs();
    setNumberOfTSDs((NumberOfCPUs == 0) ? DefaultTSDCount
                                        : Min(NumberOfCPUs, DefaultTSDCount));
    Initialized = true;
  }

  void initOnceMaybe(Allocator *Instance) EXCLUDES(Mutex) {
    ScopedLock L(Mutex);
    if (LIKELY(Initialized))
      return;
    init(Instance); // Sets Initialized.
  }

  void unmapTestOnly(Allocator *Instance) EXCLUDES(Mutex) {
    for (u32 I = 0; I < TSDsArraySize; I++) {
      TSDs[I].commitBack(Instance);
      TSDs[I] = {};
    }
    setCurrentTSD(nullptr);
    ScopedLock L(Mutex);
    Initialized = false;
  }

  void drainCaches(Allocator *Instance) {
    ScopedLock L(MutexTSDs);
    for (uptr I = 0; I < NumberOfTSDs; ++I) {
      TSDs[I].lock();
      Instance->drainCache(&TSDs[I]);
      TSDs[I].unlock();
    }
  }

  ALWAYS_INLINE void initThreadMaybe(Allocator *Instance,
                                     UNUSED bool MinimalInit) {
    if (LIKELY(getCurrentTSD()))
      return;
    initThread(Instance);
  }

  void disable() NO_THREAD_SAFETY_ANALYSIS {
    Mutex.lock();
    for (u32 I = 0; I < TSDsArraySize; I++)
      TSDs[I].lock();
  }

  void enable() NO_THREAD_SAFETY_ANALYSIS {
    for (s32 I = static_cast<s32>(TSDsArraySize - 1); I >= 0; I--)
      TSDs[I].unlock();
    Mutex.unlock();
  }

  bool setOption(Option O, sptr Value) {
    if (O == Option::MaxTSDsCount)
      return setNumberOfTSDs(static_cast<u32>(Value));
    if (O == Option::ThreadDisableMemInit)
      setDisableMemInit(Value);
    // Not supported by the TSD Registry, but not an error either.
    return true;
  }

  bool getDisableMemInit() const { return *getTlsPtr() & 1; }

  void getStats(ScopedString *Str) EXCLUDES(MutexTSDs) {
    ScopedLock L(MutexTSDs);

    Str->append("Stats: SharedTSDs: %u available; total %u\n", NumberOfTSDs,
                TSDsArraySize);
    for (uptr I = 0; I < NumberOfTSDs; ++I) {
      TSDs[I].lock();
      // Theoretically, we want to mark TSD::lock()/TSD::unlock() with proper
      // thread annotations. However, given the TSD is only locked on shared
      // path, do the assertion in a separate path to avoid confusing the
      // analyzer.
      TSDs[I].assertLocked(/*BypassCheck=*/true);
      Str->append("  Shared TSD[%zu]:\n", I);
      TSDs[I].getCache().getStats(Str);
      TSDs[I].unlock();
    }
  }

private:
  ALWAYS_INLINE TSD<Allocator> *getTSDAndLock() NO_THREAD_SAFETY_ANALYSIS {
    TSD<Allocator> *TSD = getCurrentTSD();
    DCHECK(TSD);
    // Try to lock the currently associated context.
    if (TSD->tryLock())
      return TSD;
    // If that fails, go down the slow path.
    if (TSDsArraySize == 1U) {
      // Only 1 TSD, not need to go any further.
      // The compiler will optimize this one way or the other.
      TSD->lock();
      return TSD;
    }
    return getTSDAndLockSlow(TSD);
  }

  ALWAYS_INLINE uptr *getTlsPtr() const {
#if SCUDO_HAS_PLATFORM_TLS_SLOT
    return reinterpret_cast<uptr *>(getPlatformAllocatorTlsSlot());
#else
    static thread_local uptr ThreadTSD;
    return &ThreadTSD;
#endif
  }

  static_assert(alignof(TSD<Allocator>) >= 2, "");

  ALWAYS_INLINE void setCurrentTSD(TSD<Allocator> *CurrentTSD) {
    *getTlsPtr() &= 1;
    *getTlsPtr() |= reinterpret_cast<uptr>(CurrentTSD);
  }

  ALWAYS_INLINE TSD<Allocator> *getCurrentTSD() {
    return reinterpret_cast<TSD<Allocator> *>(*getTlsPtr() & ~1ULL);
  }

  bool setNumberOfTSDs(u32 N) EXCLUDES(MutexTSDs) {
    ScopedLock L(MutexTSDs);
    if (N < NumberOfTSDs)
      return false;
    if (N > TSDsArraySize)
      N = TSDsArraySize;
    NumberOfTSDs = N;
    NumberOfCoPrimes = 0;
    // Compute all the coprimes of NumberOfTSDs. This will be used to walk the
    // array of TSDs in a random order. For details, see:
    // https://lemire.me/blog/2017/09/18/visiting-all-values-in-an-array-exactly-once-in-random-order/
    for (u32 I = 0; I < N; I++) {
      u32 A = I + 1;
      u32 B = N;
      // Find the GCD between I + 1 and N. If 1, they are coprimes.
      while (B != 0) {
        const u32 T = A;
        A = B;
        B = T % B;
      }
      if (A == 1)
        CoPrimes[NumberOfCoPrimes++] = I + 1;
    }
    return true;
  }

  void setDisableMemInit(bool B) {
    *getTlsPtr() &= ~1ULL;
    *getTlsPtr() |= B;
  }

  NOINLINE void initThread(Allocator *Instance) NO_THREAD_SAFETY_ANALYSIS {
    initOnceMaybe(Instance);
    // Initial context assignment is done in a plain round-robin fashion.
    const u32 Index = atomic_fetch_add(&CurrentIndex, 1U, memory_order_relaxed);
    setCurrentTSD(&TSDs[Index % NumberOfTSDs]);
    Instance->callPostInitCallback();
  }

  // TSDs is an array of locks which is not supported for marking thread-safety
  // capability.
  NOINLINE TSD<Allocator> *getTSDAndLockSlow(TSD<Allocator> *CurrentTSD)
      EXCLUDES(MutexTSDs) {
    // Use the Precedence of the current TSD as our random seed. Since we are
    // in the slow path, it means that tryLock failed, and as a result it's
    // very likely that said Precedence is non-zero.
    const u32 R = static_cast<u32>(CurrentTSD->getPrecedence());
    u32 N, Inc;
    {
      ScopedLock L(MutexTSDs);
      N = NumberOfTSDs;
      DCHECK_NE(NumberOfCoPrimes, 0U);
      Inc = CoPrimes[R % NumberOfCoPrimes];
    }
    if (N > 1U) {
      u32 Index = R % N;
      uptr LowestPrecedence = UINTPTR_MAX;
      TSD<Allocator> *CandidateTSD = nullptr;
      // Go randomly through at most 4 contexts and find a candidate.
      for (u32 I = 0; I < Min(4U, N); I++) {
        if (TSDs[Index].tryLock()) {
          setCurrentTSD(&TSDs[Index]);
          return &TSDs[Index];
        }
        const uptr Precedence = TSDs[Index].getPrecedence();
        // A 0 precedence here means another thread just locked this TSD.
        if (Precedence && Precedence < LowestPrecedence) {
          CandidateTSD = &TSDs[Index];
          LowestPrecedence = Precedence;
        }
        Index += Inc;
        if (Index >= N)
          Index -= N;
      }
      if (CandidateTSD) {
        CandidateTSD->lock();
        setCurrentTSD(CandidateTSD);
        return CandidateTSD;
      }
    }
    // Last resort, stick with the current one.
    CurrentTSD->lock();
    return CurrentTSD;
  }

  atomic_u32 CurrentIndex = {};
  u32 NumberOfTSDs GUARDED_BY(MutexTSDs) = 0;
  u32 NumberOfCoPrimes GUARDED_BY(MutexTSDs) = 0;
  u32 CoPrimes[TSDsArraySize] GUARDED_BY(MutexTSDs) = {};
  bool Initialized GUARDED_BY(Mutex) = false;
  HybridMutex Mutex;
  HybridMutex MutexTSDs;
  TSD<Allocator> TSDs[TSDsArraySize];
};

} // namespace scudo

#endif // SCUDO_TSD_SHARED_H_
