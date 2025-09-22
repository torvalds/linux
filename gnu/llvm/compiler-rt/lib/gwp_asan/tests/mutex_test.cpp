//===-- mutex_test.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/mutex.h"
#include "gwp_asan/tests/harness.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using gwp_asan::Mutex;
using gwp_asan::ScopedLock;

TEST(GwpAsanMutexTest, LockUnlockTest) {
  Mutex Mu;

  ASSERT_TRUE(Mu.tryLock());
  ASSERT_FALSE(Mu.tryLock());
  Mu.unlock();

  Mu.lock();
  Mu.unlock();

  // Ensure that the mutex actually unlocked.
  ASSERT_TRUE(Mu.tryLock());
  Mu.unlock();
}

TEST(GwpAsanMutexTest, ScopedLockUnlockTest) {
  Mutex Mu;
  { ScopedLock L(Mu); }
  // Locking will fail here if the scoped lock failed to unlock.
  EXPECT_TRUE(Mu.tryLock());
  Mu.unlock();

  {
    ScopedLock L(Mu);
    EXPECT_FALSE(Mu.tryLock()); // Check that the c'tor did lock.

    // Manually unlock and check that this succeeds.
    Mu.unlock();
    EXPECT_TRUE(Mu.tryLock()); // Manually lock.
  }
  EXPECT_TRUE(Mu.tryLock()); // Assert that the scoped destructor did unlock.
  Mu.unlock();
}

static void synchronousIncrementTask(std::atomic<bool> *StartingGun, Mutex *Mu,
                                     unsigned *Counter,
                                     unsigned NumIterations) {
  while (!StartingGun) {
    // Wait for starting gun.
  }
  for (unsigned i = 0; i < NumIterations; ++i) {
    ScopedLock L(*Mu);
    (*Counter)++;
  }
}

static void runSynchronisedTest(unsigned NumThreads, unsigned CounterMax) {
  std::vector<std::thread> Threads;

  ASSERT_TRUE(CounterMax % NumThreads == 0);

  std::atomic<bool> StartingGun{false};
  Mutex Mu;
  unsigned Counter = 0;

  for (unsigned i = 0; i < NumThreads; ++i)
    Threads.emplace_back(synchronousIncrementTask, &StartingGun, &Mu, &Counter,
                         CounterMax / NumThreads);

  StartingGun = true;
  for (auto &T : Threads)
    T.join();

  EXPECT_EQ(CounterMax, Counter);
}

TEST(GwpAsanMutexTest, SynchronisedCounterTest) {
  runSynchronisedTest(4, 1000000);
  runSynchronisedTest(100, 1000000);
}
