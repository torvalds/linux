//===-- allocator_test.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
//===----------------------------------------------------------------------===//

#include "xray_allocator.h"
#include "xray_buffer_queue.h"
#include "gtest/gtest.h"

namespace __xray {
namespace {

struct TestData {
  s64 First;
  s64 Second;
};

TEST(AllocatorTest, Construction) { Allocator<sizeof(TestData)> A(2 << 11); }

TEST(AllocatorTest, Allocate) {
  Allocator<sizeof(TestData)> A(2 << 11);
  auto B = A.Allocate();
  ASSERT_NE(B.Data, nullptr);
}

TEST(AllocatorTest, OverAllocate) {
  Allocator<sizeof(TestData)> A(sizeof(TestData));
  auto B1 = A.Allocate();
  ASSERT_NE(B1.Data, nullptr);
  auto B2 = A.Allocate();
  ASSERT_EQ(B2.Data, nullptr);
}

struct OddSizedData {
  s64 A;
  s32 B;
};

TEST(AllocatorTest, AllocateBoundaries) {
  Allocator<sizeof(OddSizedData)> A(GetPageSizeCached());

  // Keep allocating until we hit a nullptr block.
  unsigned C = 0;
  auto Expected =
      GetPageSizeCached() / RoundUpTo(sizeof(OddSizedData), kCacheLineSize);
  for (auto B = A.Allocate(); B.Data != nullptr; B = A.Allocate(), ++C)
    ;

  ASSERT_EQ(C, Expected);
}

TEST(AllocatorTest, AllocateFromNonOwned) {
  bool Success = false;
  BufferQueue BQ(GetPageSizeCached(), 10, Success);
  ASSERT_TRUE(Success);
  BufferQueue::Buffer B;
  ASSERT_EQ(BQ.getBuffer(B), BufferQueue::ErrorCode::Ok);
  {
    Allocator<sizeof(OddSizedData)> A(B.Data, B.Size);

    // Keep allocating until we hit a nullptr block.
    unsigned C = 0;
    auto Expected =
        GetPageSizeCached() / RoundUpTo(sizeof(OddSizedData), kCacheLineSize);
    for (auto B = A.Allocate(); B.Data != nullptr; B = A.Allocate(), ++C)
      ;

    ASSERT_EQ(C, Expected);
  }
  ASSERT_EQ(BQ.releaseBuffer(B), BufferQueue::ErrorCode::Ok);
}

} // namespace
} // namespace __xray
