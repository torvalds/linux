//===-- mutex.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_MUTEX_H_
#define SCUDO_MUTEX_H_

#include "atomic_helpers.h"
#include "common.h"
#include "thread_annotations.h"

#include <string.h>

#if SCUDO_FUCHSIA
#include <lib/sync/mutex.h> // for sync_mutex_t
#endif

namespace scudo {

class CAPABILITY("mutex") HybridMutex {
public:
  bool tryLock() TRY_ACQUIRE(true);
  NOINLINE void lock() ACQUIRE() {
    if (LIKELY(tryLock()))
      return;
      // The compiler may try to fully unroll the loop, ending up in a
      // NumberOfTries*NumberOfYields block of pauses mixed with tryLocks. This
      // is large, ugly and unneeded, a compact loop is better for our purpose
      // here. Use a pragma to tell the compiler not to unroll the loop.
#ifdef __clang__
#pragma nounroll
#endif
    for (u8 I = 0U; I < NumberOfTries; I++) {
      delayLoop();
      if (tryLock())
        return;
    }
    lockSlow();
  }
  void unlock() RELEASE();

  // TODO(chiahungduan): In general, we may want to assert the owner of lock as
  // well. Given the current uses of HybridMutex, it's acceptable without
  // asserting the owner. Re-evaluate this when we have certain scenarios which
  // requires a more fine-grained lock granularity.
  ALWAYS_INLINE void assertHeld() ASSERT_CAPABILITY(this) {
    if (SCUDO_DEBUG)
      assertHeldImpl();
  }

private:
  void delayLoop() {
    // The value comes from the average time spent in accessing caches (which
    // are the fastest operations) so that we are unlikely to wait too long for
    // fast operations.
    constexpr u32 SpinTimes = 16;
    volatile u32 V = 0;
    for (u32 I = 0; I < SpinTimes; ++I) {
      u32 Tmp = V + 1;
      V = Tmp;
    }
  }

  void assertHeldImpl();

  // TODO(chiahungduan): Adapt this value based on scenarios. E.g., primary and
  // secondary allocator have different allocation times.
  static constexpr u8 NumberOfTries = 32U;

#if SCUDO_LINUX
  atomic_u32 M = {};
#elif SCUDO_FUCHSIA
  sync_mutex_t M = {};
#endif

  void lockSlow() ACQUIRE();
};

class SCOPED_CAPABILITY ScopedLock {
public:
  explicit ScopedLock(HybridMutex &M) ACQUIRE(M) : Mutex(M) { Mutex.lock(); }
  ~ScopedLock() RELEASE() { Mutex.unlock(); }

private:
  HybridMutex &Mutex;

  ScopedLock(const ScopedLock &) = delete;
  void operator=(const ScopedLock &) = delete;
};

} // namespace scudo

#endif // SCUDO_MUTEX_H_
