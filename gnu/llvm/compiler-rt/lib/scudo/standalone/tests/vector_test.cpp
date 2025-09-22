//===-- vector_test.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "vector.h"

TEST(ScudoVectorTest, Basic) {
  scudo::Vector<int, 64U> V;
  EXPECT_EQ(V.size(), 0U);
  V.push_back(42);
  EXPECT_EQ(V.size(), 1U);
  EXPECT_EQ(V[0], 42);
  V.push_back(43);
  EXPECT_EQ(V.size(), 2U);
  EXPECT_EQ(V[0], 42);
  EXPECT_EQ(V[1], 43);
}

TEST(ScudoVectorTest, Stride) {
  scudo::Vector<scudo::uptr, 32U> V;
  for (scudo::uptr I = 0; I < 1000; I++) {
    V.push_back(I);
    EXPECT_EQ(V.size(), I + 1U);
    EXPECT_EQ(V[I], I);
  }
  for (scudo::uptr I = 0; I < 1000; I++)
    EXPECT_EQ(V[I], I);
}

TEST(ScudoVectorTest, ResizeReduction) {
  scudo::Vector<int, 64U> V;
  V.push_back(0);
  V.push_back(0);
  EXPECT_EQ(V.size(), 2U);
  V.resize(1);
  EXPECT_EQ(V.size(), 1U);
}

#if defined(__linux__)

#include <sys/resource.h>

// Verify that if the reallocate fails, nothing new is added.
TEST(ScudoVectorTest, ReallocateFails) {
  scudo::Vector<char, 256U> V;
  scudo::uptr capacity = V.capacity();

  // Get the current address space size.
  rlimit Limit = {};
  EXPECT_EQ(0, getrlimit(RLIMIT_AS, &Limit));

  rlimit EmptyLimit = {.rlim_cur = 0, .rlim_max = Limit.rlim_max};
  EXPECT_EQ(0, setrlimit(RLIMIT_AS, &EmptyLimit));

  // qemu does not honor the setrlimit, so verify before proceeding.
  scudo::MemMapT MemMap;
  if (MemMap.map(/*Addr=*/0U, scudo::getPageSizeCached(), "scudo:test",
                 MAP_ALLOWNOMEM)) {
    MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
    setrlimit(RLIMIT_AS, &Limit);
    TEST_SKIP("Limiting address space does not prevent mmap.");
  }

  V.resize(capacity);
  // Set the last element so we can check it later.
  V.back() = '\0';

  // The reallocate should fail, so the capacity should not change.
  V.reserve(capacity + 1000);
  EXPECT_EQ(capacity, V.capacity());

  // Now try to do a push back and verify that the size does not change.
  scudo::uptr Size = V.size();
  V.push_back('2');
  EXPECT_EQ(Size, V.size());
  // Verify that the last element in the vector did not change.
  EXPECT_EQ('\0', V.back());

  EXPECT_EQ(0, setrlimit(RLIMIT_AS, &Limit));
}
#endif
