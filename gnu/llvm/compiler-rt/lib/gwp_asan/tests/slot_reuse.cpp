//===-- slot_reuse.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/tests/harness.h"

#include <set>

void singleByteGoodAllocDealloc(gwp_asan::GuardedPoolAllocator *GPA) {
  void *Ptr = GPA->allocate(1);
  EXPECT_NE(nullptr, Ptr);
  EXPECT_TRUE(GPA->pointerIsMine(Ptr));
  EXPECT_EQ(1u, GPA->getSize(Ptr));
  GPA->deallocate(Ptr);
}

TEST_F(CustomGuardedPoolAllocator, EnsureReuseOfQuarantine1) {
  InitNumSlots(1);
  for (unsigned i = 0; i < 128; ++i)
    singleByteGoodAllocDealloc(&GPA);
}

TEST_F(CustomGuardedPoolAllocator, EnsureReuseOfQuarantine2) {
  InitNumSlots(2);
  for (unsigned i = 0; i < 128; ++i)
    singleByteGoodAllocDealloc(&GPA);
}

TEST_F(CustomGuardedPoolAllocator, EnsureReuseOfQuarantine127) {
  InitNumSlots(127);
  for (unsigned i = 0; i < 128; ++i)
    singleByteGoodAllocDealloc(&GPA);
}

// This test ensures that our slots are not reused ahead of time. We increase
// the use-after-free detection by not reusing slots until all of them have been
// allocated. This is done by always using the slots from left-to-right in the
// pool before we used each slot once, at which point random selection takes
// over.
void runNoReuseBeforeNecessary(gwp_asan::GuardedPoolAllocator *GPA,
                               unsigned PoolSize) {
  std::set<void *> Ptrs;
  for (unsigned i = 0; i < PoolSize; ++i) {
    void *Ptr = GPA->allocate(1);

    EXPECT_TRUE(GPA->pointerIsMine(Ptr));
    EXPECT_EQ(0u, Ptrs.count(Ptr));

    Ptrs.insert(Ptr);
    GPA->deallocate(Ptr);
  }
}

TEST_F(CustomGuardedPoolAllocator, NoReuseBeforeNecessary2) {
  constexpr unsigned kPoolSize = 2;
  InitNumSlots(kPoolSize);
  runNoReuseBeforeNecessary(&GPA, kPoolSize);
}

TEST_F(CustomGuardedPoolAllocator, NoReuseBeforeNecessary128) {
  constexpr unsigned kPoolSize = 128;
  InitNumSlots(kPoolSize);
  runNoReuseBeforeNecessary(&GPA, kPoolSize);
}

TEST_F(CustomGuardedPoolAllocator, NoReuseBeforeNecessary129) {
  constexpr unsigned kPoolSize = 129;
  InitNumSlots(kPoolSize);
  runNoReuseBeforeNecessary(&GPA, kPoolSize);
}
