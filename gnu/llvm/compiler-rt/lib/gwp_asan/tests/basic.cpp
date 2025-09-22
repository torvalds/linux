//===-- basic.cpp -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/tests/harness.h"

TEST_F(CustomGuardedPoolAllocator, BasicAllocation) {
  InitNumSlots(1);
  void *Ptr = GPA.allocate(1);
  EXPECT_NE(nullptr, Ptr);
  EXPECT_TRUE(GPA.pointerIsMine(Ptr));
  EXPECT_EQ(1u, GPA.getSize(Ptr));
  GPA.deallocate(Ptr);
}

TEST_F(DefaultGuardedPoolAllocator, NullptrIsNotMine) {
  EXPECT_FALSE(GPA.pointerIsMine(nullptr));
}

TEST_F(CustomGuardedPoolAllocator, SizedAllocations) {
  InitNumSlots(1);

  std::size_t MaxAllocSize = GPA.getAllocatorState()->maximumAllocationSize();
  EXPECT_TRUE(MaxAllocSize > 0);

  for (unsigned AllocSize = 1; AllocSize <= MaxAllocSize; AllocSize <<= 1) {
    void *Ptr = GPA.allocate(AllocSize);
    EXPECT_NE(nullptr, Ptr);
    EXPECT_TRUE(GPA.pointerIsMine(Ptr));
    EXPECT_EQ(AllocSize, GPA.getSize(Ptr));
    GPA.deallocate(Ptr);
  }
}

TEST_F(DefaultGuardedPoolAllocator, TooLargeAllocation) {
  EXPECT_EQ(nullptr,
            GPA.allocate(GPA.getAllocatorState()->maximumAllocationSize() + 1));
  EXPECT_EQ(nullptr, GPA.allocate(SIZE_MAX, 0));
  EXPECT_EQ(nullptr, GPA.allocate(SIZE_MAX, 1));
  EXPECT_EQ(nullptr, GPA.allocate(0, SIZE_MAX / 2));
  EXPECT_EQ(nullptr, GPA.allocate(1, SIZE_MAX / 2));
  EXPECT_EQ(nullptr, GPA.allocate(SIZE_MAX, SIZE_MAX / 2));
}

TEST_F(DefaultGuardedPoolAllocator, ZeroSizeAndAlignmentAllocations) {
  void *P;
  EXPECT_NE(nullptr, (P = GPA.allocate(0, 0)));
  GPA.deallocate(P);
  EXPECT_NE(nullptr, (P = GPA.allocate(1, 0)));
  GPA.deallocate(P);
  EXPECT_NE(nullptr, (P = GPA.allocate(0, 1)));
  GPA.deallocate(P);
}

TEST_F(DefaultGuardedPoolAllocator, NonPowerOfTwoAlignment) {
  EXPECT_EQ(nullptr, GPA.allocate(0, 3));
  EXPECT_EQ(nullptr, GPA.allocate(1, 3));
  EXPECT_EQ(nullptr, GPA.allocate(0, SIZE_MAX));
  EXPECT_EQ(nullptr, GPA.allocate(1, SIZE_MAX));
}

// Added multi-page slots? You'll need to expand this test.
TEST_F(DefaultGuardedPoolAllocator, TooBigForSinglePageSlots) {
  EXPECT_EQ(nullptr, GPA.allocate(0x1001, 0));
  EXPECT_EQ(nullptr, GPA.allocate(0x1001, 1));
  EXPECT_EQ(nullptr, GPA.allocate(0x1001, 0x1000));
  EXPECT_EQ(nullptr, GPA.allocate(1, 0x2000));
  EXPECT_EQ(nullptr, GPA.allocate(0, 0x2000));
}

TEST_F(CustomGuardedPoolAllocator, AllocAllSlots) {
  constexpr unsigned kNumSlots = 128;
  InitNumSlots(kNumSlots);
  void *Ptrs[kNumSlots];
  for (unsigned i = 0; i < kNumSlots; ++i) {
    Ptrs[i] = GPA.allocate(1);
    EXPECT_NE(nullptr, Ptrs[i]);
    EXPECT_TRUE(GPA.pointerIsMine(Ptrs[i]));
  }

  // This allocation should fail as all the slots are used.
  void *Ptr = GPA.allocate(1);
  EXPECT_EQ(nullptr, Ptr);
  EXPECT_FALSE(GPA.pointerIsMine(nullptr));

  for (unsigned i = 0; i < kNumSlots; ++i)
    GPA.deallocate(Ptrs[i]);
}
