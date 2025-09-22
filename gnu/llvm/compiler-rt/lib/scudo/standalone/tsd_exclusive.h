//===-- tsd_exclusive.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TSD_EXCLUSIVE_H_
#define SCUDO_TSD_EXCLUSIVE_H_

#include "tsd.h"

#include "string_utils.h"

namespace scudo {

struct ThreadState {
  bool DisableMemInit : 1;
  enum : unsigned {
    NotInitialized = 0,
    Initialized,
    TornDown,
  } InitState : 2;
};

template <class Allocator> void teardownThread(void *Ptr);

template <class Allocator> struct TSDRegistryExT {
  using ThisT = TSDRegistryExT<Allocator>;

  struct ScopedTSD {
    ALWAYS_INLINE ScopedTSD(ThisT &TSDRegistry) {
      CurrentTSD = TSDRegistry.getTSDAndLock(&UnlockRequired);
      DCHECK_NE(CurrentTSD, nullptr);
    }

    ~ScopedTSD() {
      if (UNLIKELY(UnlockRequired))
        CurrentTSD->unlock();
    }

    TSD<Allocator> &operator*() { return *CurrentTSD; }

    TSD<Allocator> *operator->() {
      CurrentTSD->assertLocked(/*BypassCheck=*/!UnlockRequired);
      return CurrentTSD;
    }

  private:
    TSD<Allocator> *CurrentTSD;
    bool UnlockRequired;
  };

  void init(Allocator *Instance) REQUIRES(Mutex) {
    DCHECK(!Initialized);
    Instance->init();
    CHECK_EQ(pthread_key_create(&PThreadKey, teardownThread<Allocator>), 0);
    FallbackTSD.init(Instance);
    Initialized = true;
  }

  void initOnceMaybe(Allocator *Instance) EXCLUDES(Mutex) {
    ScopedLock L(Mutex);
    if (LIKELY(Initialized))
      return;
    init(Instance); // Sets Initialized.
  }

  void unmapTestOnly(Allocator *Instance) EXCLUDES(Mutex) {
    DCHECK(Instance);
    if (reinterpret_cast<Allocator *>(pthread_getspecific(PThreadKey))) {
      DCHECK_EQ(reinterpret_cast<Allocator *>(pthread_getspecific(PThreadKey)),
                Instance);
      ThreadTSD.commitBack(Instance);
      ThreadTSD = {};
    }
    CHECK_EQ(pthread_key_delete(PThreadKey), 0);
    PThreadKey = {};
    FallbackTSD.commitBack(Instance);
    FallbackTSD = {};
    State = {};
    ScopedLock L(Mutex);
    Initialized = false;
  }

  void drainCaches(Allocator *Instance) {
    // We don't have a way to iterate all thread local `ThreadTSD`s. Simply
    // drain the `ThreadTSD` of current thread and `FallbackTSD`.
    Instance->drainCache(&ThreadTSD);
    FallbackTSD.lock();
    Instance->drainCache(&FallbackTSD);
    FallbackTSD.unlock();
  }

  ALWAYS_INLINE void initThreadMaybe(Allocator *Instance, bool MinimalInit) {
    if (LIKELY(State.InitState != ThreadState::NotInitialized))
      return;
    initThread(Instance, MinimalInit);
  }

  // To disable the exclusive TSD registry, we effectively lock the fallback TSD
  // and force all threads to attempt to use it instead of their local one.
  void disable() NO_THREAD_SAFETY_ANALYSIS {
    Mutex.lock();
    FallbackTSD.lock();
    atomic_store(&Disabled, 1U, memory_order_release);
  }

  void enable() NO_THREAD_SAFETY_ANALYSIS {
    atomic_store(&Disabled, 0U, memory_order_release);
    FallbackTSD.unlock();
    Mutex.unlock();
  }

  bool setOption(Option O, sptr Value) {
    if (O == Option::ThreadDisableMemInit)
      State.DisableMemInit = Value;
    if (O == Option::MaxTSDsCount)
      return false;
    return true;
  }

  bool getDisableMemInit() { return State.DisableMemInit; }

  void getStats(ScopedString *Str) {
    // We don't have a way to iterate all thread local `ThreadTSD`s. Instead of
    // printing only self `ThreadTSD` which may mislead the usage, we just skip
    // it.
    Str->append("Exclusive TSD don't support iterating each TSD\n");
  }

private:
  ALWAYS_INLINE TSD<Allocator> *
  getTSDAndLock(bool *UnlockRequired) NO_THREAD_SAFETY_ANALYSIS {
    if (LIKELY(State.InitState == ThreadState::Initialized &&
               !atomic_load(&Disabled, memory_order_acquire))) {
      *UnlockRequired = false;
      return &ThreadTSD;
    }
    FallbackTSD.lock();
    *UnlockRequired = true;
    return &FallbackTSD;
  }

  // Using minimal initialization allows for global initialization while keeping
  // the thread specific structure untouched. The fallback structure will be
  // used instead.
  NOINLINE void initThread(Allocator *Instance, bool MinimalInit) {
    initOnceMaybe(Instance);
    if (UNLIKELY(MinimalInit))
      return;
    CHECK_EQ(
        pthread_setspecific(PThreadKey, reinterpret_cast<void *>(Instance)), 0);
    ThreadTSD.init(Instance);
    State.InitState = ThreadState::Initialized;
    Instance->callPostInitCallback();
  }

  pthread_key_t PThreadKey = {};
  bool Initialized GUARDED_BY(Mutex) = false;
  atomic_u8 Disabled = {};
  TSD<Allocator> FallbackTSD;
  HybridMutex Mutex;
  static thread_local ThreadState State;
  static thread_local TSD<Allocator> ThreadTSD;

  friend void teardownThread<Allocator>(void *Ptr);
};

template <class Allocator>
thread_local TSD<Allocator> TSDRegistryExT<Allocator>::ThreadTSD;
template <class Allocator>
thread_local ThreadState TSDRegistryExT<Allocator>::State;

template <class Allocator>
void teardownThread(void *Ptr) NO_THREAD_SAFETY_ANALYSIS {
  typedef TSDRegistryExT<Allocator> TSDRegistryT;
  Allocator *Instance = reinterpret_cast<Allocator *>(Ptr);
  // The glibc POSIX thread-local-storage deallocation routine calls user
  // provided destructors in a loop of PTHREAD_DESTRUCTOR_ITERATIONS.
  // We want to be called last since other destructors might call free and the
  // like, so we wait until PTHREAD_DESTRUCTOR_ITERATIONS before draining the
  // quarantine and swallowing the cache.
  if (TSDRegistryT::ThreadTSD.DestructorIterations > 1) {
    TSDRegistryT::ThreadTSD.DestructorIterations--;
    // If pthread_setspecific fails, we will go ahead with the teardown.
    if (LIKELY(pthread_setspecific(Instance->getTSDRegistry()->PThreadKey,
                                   Ptr) == 0))
      return;
  }
  TSDRegistryT::ThreadTSD.commitBack(Instance);
  TSDRegistryT::State.InitState = ThreadState::TornDown;
}

} // namespace scudo

#endif // SCUDO_TSD_EXCLUSIVE_H_
