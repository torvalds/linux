//===-- tsd.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TSD_H_
#define SCUDO_TSD_H_

#include "atomic_helpers.h"
#include "common.h"
#include "mutex.h"
#include "thread_annotations.h"

#include <limits.h> // for PTHREAD_DESTRUCTOR_ITERATIONS
#include <pthread.h>

// With some build setups, this might still not be defined.
#ifndef PTHREAD_DESTRUCTOR_ITERATIONS
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#endif

namespace scudo {

template <class Allocator> struct alignas(SCUDO_CACHE_LINE_SIZE) TSD {
  using ThisT = TSD<Allocator>;
  u8 DestructorIterations = 0;

  void init(Allocator *Instance) NO_THREAD_SAFETY_ANALYSIS {
    DCHECK_EQ(DestructorIterations, 0U);
    DCHECK(isAligned(reinterpret_cast<uptr>(this), alignof(ThisT)));
    Instance->initCache(&Cache);
    DestructorIterations = PTHREAD_DESTRUCTOR_ITERATIONS;
  }

  inline bool tryLock() NO_THREAD_SAFETY_ANALYSIS {
    if (Mutex.tryLock()) {
      atomic_store_relaxed(&Precedence, 0);
      return true;
    }
    if (atomic_load_relaxed(&Precedence) == 0)
      atomic_store_relaxed(
          &Precedence,
          static_cast<uptr>(getMonotonicTime() >> FIRST_32_SECOND_64(16, 0)));
    return false;
  }
  inline void lock() NO_THREAD_SAFETY_ANALYSIS {
    atomic_store_relaxed(&Precedence, 0);
    Mutex.lock();
  }
  inline void unlock() NO_THREAD_SAFETY_ANALYSIS { Mutex.unlock(); }
  inline uptr getPrecedence() { return atomic_load_relaxed(&Precedence); }

  void commitBack(Allocator *Instance) { Instance->commitBack(this); }

  // As the comments attached to `getCache()`, the TSD doesn't always need to be
  // locked. In that case, we would only skip the check before we have all TSDs
  // locked in all paths.
  void assertLocked(bool BypassCheck) ASSERT_CAPABILITY(Mutex) {
    if (SCUDO_DEBUG && !BypassCheck)
      Mutex.assertHeld();
  }

  // Ideally, we may want to assert that all the operations on
  // Cache/QuarantineCache always have the `Mutex` acquired. However, the
  // current architecture of accessing TSD is not easy to cooperate with the
  // thread-safety analysis because of pointer aliasing. So now we just add the
  // assertion on the getters of Cache/QuarantineCache.
  //
  // TODO(chiahungduan): Ideally, we want to do `Mutex.assertHeld` but acquiring
  // TSD doesn't always require holding the lock. Add this assertion while the
  // lock is always acquired.
  typename Allocator::CacheT &getCache() REQUIRES(Mutex) { return Cache; }
  typename Allocator::QuarantineCacheT &getQuarantineCache() REQUIRES(Mutex) {
    return QuarantineCache;
  }

private:
  HybridMutex Mutex;
  atomic_uptr Precedence = {};

  typename Allocator::CacheT Cache GUARDED_BY(Mutex);
  typename Allocator::QuarantineCacheT QuarantineCache GUARDED_BY(Mutex);
};

} // namespace scudo

#endif // SCUDO_TSD_H_
