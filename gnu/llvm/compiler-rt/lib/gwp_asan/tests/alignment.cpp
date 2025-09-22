//===-- alignment.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/tests/harness.h"

class AlignmentTestGPA : public gwp_asan::GuardedPoolAllocator {
public:
  static size_t getRequiredBackingSize(size_t Size, size_t Alignment,
                                       size_t PageSize) {
    return GuardedPoolAllocator::getRequiredBackingSize(Size, Alignment,
                                                        PageSize);
  }
  static uintptr_t alignUp(uintptr_t Ptr, size_t Alignment) {
    return GuardedPoolAllocator::alignUp(Ptr, Alignment);
  }
  static uintptr_t alignDown(uintptr_t Ptr, size_t Alignment) {
    return GuardedPoolAllocator::alignDown(Ptr, Alignment);
  }
};

// Global assumptions for these tests:
//   1. Page size is 0x1000.
//   2. All tests assume a slot is multipage, between 0x4000 - 0x8000. While we
//      don't use multipage slots right now, this tests more boundary conditions
//      and allows us to add this feature at a later date without rewriting the
//      alignment functionality.
// These aren't actual requirements of the allocator - but just simplifies the
// numerics of the testing.
TEST(AlignmentTest, LeftAlignedAllocs) {
  // Alignment < Page Size.
  EXPECT_EQ(0x4000u, AlignmentTestGPA::alignUp(
                         /* Ptr */ 0x4000, /* Alignment */ 0x1));
  // Alignment == Page Size.
  EXPECT_EQ(0x4000u, AlignmentTestGPA::alignUp(
                         /* Ptr */ 0x4000, /* Alignment */ 0x1000));
  // Alignment > Page Size.
  EXPECT_EQ(0x4000u, AlignmentTestGPA::alignUp(
                         /* Ptr */ 0x4000, /* Alignment */ 0x4000));
}

TEST(AlignmentTest, SingleByteAllocs) {
  // Alignment < Page Size.
  EXPECT_EQ(0x1u,
            AlignmentTestGPA::getRequiredBackingSize(
                /* Size */ 0x1, /* Alignment */ 0x1, /* PageSize */ 0x1000));
  EXPECT_EQ(0x7fffu, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x1, /* Alignment */ 0x1));

  // Alignment == Page Size.
  EXPECT_EQ(0x1u,
            AlignmentTestGPA::getRequiredBackingSize(
                /* Size */ 0x1, /* Alignment */ 0x1000, /* PageSize */ 0x1000));
  EXPECT_EQ(0x7000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x1, /* Alignment */ 0x1000));

  // Alignment > Page Size.
  EXPECT_EQ(0x3001u,
            AlignmentTestGPA::getRequiredBackingSize(
                /* Size */ 0x1, /* Alignment */ 0x4000, /* PageSize */ 0x1000));
  EXPECT_EQ(0x4000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x1, /* Alignment */ 0x4000));
}

TEST(AlignmentTest, PageSizedAllocs) {
  // Alignment < Page Size.
  EXPECT_EQ(0x1000u,
            AlignmentTestGPA::getRequiredBackingSize(
                /* Size */ 0x1000, /* Alignment */ 0x1, /* PageSize */ 0x1000));
  EXPECT_EQ(0x7000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x1000, /* Alignment */ 0x1));

  // Alignment == Page Size.
  EXPECT_EQ(0x1000u, AlignmentTestGPA::getRequiredBackingSize(
                         /* Size */ 0x1000, /* Alignment */ 0x1000,
                         /* PageSize */ 0x1000));
  EXPECT_EQ(0x7000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x1000, /* Alignment */ 0x1000));

  // Alignment > Page Size.
  EXPECT_EQ(0x4000u, AlignmentTestGPA::getRequiredBackingSize(
                         /* Size */ 0x1000, /* Alignment */ 0x4000,
                         /* PageSize */ 0x1000));
  EXPECT_EQ(0x4000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x1000, /* Alignment */ 0x4000));
}

TEST(AlignmentTest, MoreThanPageAllocs) {
  // Alignment < Page Size.
  EXPECT_EQ(0x2fffu,
            AlignmentTestGPA::getRequiredBackingSize(
                /* Size */ 0x2fff, /* Alignment */ 0x1, /* PageSize */ 0x1000));
  EXPECT_EQ(0x5001u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x2fff, /* Alignment */ 0x1));

  // Alignment == Page Size.
  EXPECT_EQ(0x2fffu, AlignmentTestGPA::getRequiredBackingSize(
                         /* Size */ 0x2fff, /* Alignment */ 0x1000,
                         /* PageSize */ 0x1000));
  EXPECT_EQ(0x5000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x2fff, /* Alignment */ 0x1000));

  // Alignment > Page Size.
  EXPECT_EQ(0x5fffu, AlignmentTestGPA::getRequiredBackingSize(
                         /* Size */ 0x2fff, /* Alignment */ 0x4000,
                         /* PageSize */ 0x1000));
  EXPECT_EQ(0x4000u, AlignmentTestGPA::alignDown(
                         /* Ptr */ 0x8000 - 0x2fff, /* Alignment */ 0x4000));
}
