//===-- thread_contention.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/tests/harness.h"

// Note: Compilation of <atomic> and <thread> are extremely expensive for
// non-opt builds of clang.
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

void asyncTask(gwp_asan::GuardedPoolAllocator *GPA,
               std::atomic<bool> *StartingGun, unsigned NumIterations) {
  while (!*StartingGun) {
    // Wait for starting gun.
  }

  // Get ourselves a new allocation.
  for (unsigned i = 0; i < NumIterations; ++i) {
    volatile char *Ptr = reinterpret_cast<volatile char *>(
        GPA->allocate(GPA->getAllocatorState()->maximumAllocationSize()));
    // Do any other threads have access to this page?
    EXPECT_EQ(*Ptr, 0);

    // Mark the page as from malloc. Wait to see if another thread also takes
    // this page.
    *Ptr = 'A';
    std::this_thread::sleep_for(std::chrono::nanoseconds(10000));

    // Check we still own the page.
    EXPECT_EQ(*Ptr, 'A');

    // And now release it.
    *Ptr = 0;
    GPA->deallocate(const_cast<char *>(Ptr));
  }
}

void runThreadContentionTest(unsigned NumThreads, unsigned NumIterations,
                             gwp_asan::GuardedPoolAllocator *GPA) {
  std::atomic<bool> StartingGun{false};
  std::vector<std::thread> Threads;

  for (unsigned i = 0; i < NumThreads; ++i) {
    Threads.emplace_back(asyncTask, GPA, &StartingGun, NumIterations);
  }

  StartingGun = true;

  for (auto &T : Threads)
    T.join();
}

TEST_F(CustomGuardedPoolAllocator, ThreadContention) {
  unsigned NumThreads = 4;
  unsigned NumIterations = 10000;
  InitNumSlots(NumThreads);
  runThreadContentionTest(NumThreads, NumIterations, &GPA);
}
