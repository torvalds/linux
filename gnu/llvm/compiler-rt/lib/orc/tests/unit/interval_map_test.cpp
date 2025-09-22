//===-- interval_map_test.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime.
//
//===----------------------------------------------------------------------===//

#include "interval_map.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

TEST(IntervalMapTest, DefaultConstructed) {
  // Check that a default-constructed IntervalMap behaves as expected.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M;

  EXPECT_TRUE(M.empty());
  EXPECT_TRUE(M.begin() == M.end());
  EXPECT_TRUE(M.find(0) == M.end());
}

TEST(IntervalMapTest, InsertSingleElement) {
  // Check that a map with a single element inserted behaves as expected.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M;

  M.insert(7, 8, 42);

  EXPECT_FALSE(M.empty());
  EXPECT_EQ(std::next(M.begin()), M.end());
  EXPECT_EQ(M.find(7), M.begin());
  EXPECT_EQ(M.find(8), M.end());
  EXPECT_EQ(M.lookup(7), 42U);
  EXPECT_EQ(M.lookup(8), 0U); // 8 not present, so should return unsigned().
}

TEST(IntervalMapTest, InsertCoalesceWithPrevious) {
  // Check that insertions coalesce with previous ranges that share the same
  // value. Also check that they _don't_ coalesce if the values are different.

  // Check that insertion coalesces with previous range when values are equal.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M1;

  M1.insert(7, 8, 42);
  M1.insert(8, 9, 42);

  EXPECT_FALSE(M1.empty());
  EXPECT_EQ(std::next(M1.begin()), M1.end()); // Should see just one range.
  EXPECT_EQ(M1.find(7), M1.find(8)); // 7 and 8 should point to same range.
  EXPECT_EQ(M1.lookup(7), 42U);      // Value should be preserved.

  // Check that insertion does not coalesce with previous range when values are
  // not equal.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M2;

  M2.insert(7, 8, 42);
  M2.insert(8, 9, 7);

  EXPECT_FALSE(M2.empty());
  EXPECT_EQ(std::next(std::next(M2.begin())), M2.end()); // Expect two ranges.
  EXPECT_NE(M2.find(7), M2.find(8)); // 7 and 8 should be different ranges.
  EXPECT_EQ(M2.lookup(7), 42U); // Keys 7 and 8 should map to different values.
  EXPECT_EQ(M2.lookup(8), 7U);
}

TEST(IntervalMapTest, InsertCoalesceWithFollowing) {
  // Check that insertions coalesce with following ranges that share the same
  // value. Also check that they _don't_ coalesce if the values are different.

  // Check that insertion coalesces with following range when values are equal.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M1;

  M1.insert(8, 9, 42);
  M1.insert(7, 8, 42);

  EXPECT_FALSE(M1.empty());
  EXPECT_EQ(std::next(M1.begin()), M1.end()); // Should see just one range.
  EXPECT_EQ(M1.find(7), M1.find(8)); // 7 and 8 should point to same range.
  EXPECT_EQ(M1.lookup(7), 42U);      // Value should be preserved.

  // Check that insertion does not coalesce with previous range when values are
  // not equal.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M2;

  M2.insert(8, 9, 42);
  M2.insert(7, 8, 7);

  EXPECT_FALSE(M2.empty());
  EXPECT_EQ(std::next(std::next(M2.begin())), M2.end()); // Expect two ranges.
  EXPECT_EQ(M2.lookup(7), 7U); // Keys 7 and 8 should map to different values.
  EXPECT_EQ(M2.lookup(8), 42U);
}

TEST(IntervalMapTest, InsertCoalesceBoth) {
  // Check that insertions coalesce with ranges on both sides where posssible.
  // Also check that they _don't_ coalesce if the values are different.

  // Check that insertion coalesces with both previous and following ranges
  // when values are equal.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M1;

  M1.insert(7, 8, 42);
  M1.insert(9, 10, 42);

  // Check no coalescing yet.
  EXPECT_NE(M1.find(7), M1.find(9));

  // Insert a 3rd range to trigger coalescing on both sides.
  M1.insert(8, 9, 42);

  EXPECT_FALSE(M1.empty());
  EXPECT_EQ(std::next(M1.begin()), M1.end()); // Should see just one range.
  EXPECT_EQ(M1.find(7), M1.find(8)); // 7, 8, and 9 should point to same range.
  EXPECT_EQ(M1.find(8), M1.find(9));
  EXPECT_EQ(M1.lookup(7), 42U); // Value should be preserved.

  // Check that insertion does not coalesce with previous range when values are
  // not equal.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M2;

  M2.insert(7, 8, 42);
  M2.insert(8, 9, 7);
  M2.insert(9, 10, 42);

  EXPECT_FALSE(M2.empty());
  // Expect three ranges.
  EXPECT_EQ(std::next(std::next(std::next(M2.begin()))), M2.end());
  EXPECT_NE(M2.find(7), M2.find(8)); // All keys should map to different ranges.
  EXPECT_NE(M2.find(8), M2.find(9));
  EXPECT_EQ(M2.lookup(7), 42U); // Key 7, 8, and 9 should map to different vals.
  EXPECT_EQ(M2.lookup(8), 7U);
  EXPECT_EQ(M2.lookup(9), 42U);
}

TEST(IntervalMapTest, EraseSingleElement) {
  // Check that we can insert and then remove a single range.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M;

  M.insert(7, 10, 42);
  EXPECT_FALSE(M.empty());
  M.erase(7, 10);
  EXPECT_TRUE(M.empty());
}

TEST(IntervalMapTest, EraseSplittingLeft) {
  // Check that removal of a trailing subrange succeeds, but leaves the
  // residual range in-place.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M;

  M.insert(7, 10, 42);
  EXPECT_FALSE(M.empty());
  M.erase(9, 10);
  EXPECT_EQ(std::next(M.begin()), M.end());
  EXPECT_EQ(M.begin()->first.first, 7U);
  EXPECT_EQ(M.begin()->first.second, 9U);
}

TEST(IntervalMapTest, EraseSplittingRight) {
  // Check that removal of a leading subrange succeeds, but leaves the
  // residual range in-place.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M;

  M.insert(7, 10, 42);
  EXPECT_FALSE(M.empty());
  M.erase(7, 8);
  EXPECT_EQ(std::next(M.begin()), M.end());
  EXPECT_EQ(M.begin()->first.first, 8U);
  EXPECT_EQ(M.begin()->first.second, 10U);
}

TEST(IntervalMapTest, EraseSplittingBoth) {
  // Check that removal of an interior subrange leaves both the leading and
  // trailing residual subranges in-place.
  IntervalMap<unsigned, unsigned, IntervalCoalescing::Enabled> M;

  M.insert(7, 10, 42);
  EXPECT_FALSE(M.empty());
  M.erase(8, 9);
  EXPECT_EQ(std::next(std::next(M.begin())), M.end());
  EXPECT_EQ(M.begin()->first.first, 7U);
  EXPECT_EQ(M.begin()->first.second, 8U);
  EXPECT_EQ(std::next(M.begin())->first.first, 9U);
  EXPECT_EQ(std::next(M.begin())->first.second, 10U);
}

TEST(IntervalMapTest, NonCoalescingMapPermitsNonComparableKeys) {
  // Test that values that can't be equality-compared are still usable when
  // coalescing is disabled and behave as expected.

  struct S {}; // Struct with no equality comparison.

  IntervalMap<unsigned, S, IntervalCoalescing::Disabled> M;

  M.insert(7, 8, S());

  EXPECT_FALSE(M.empty());
  EXPECT_EQ(std::next(M.begin()), M.end());
  EXPECT_EQ(M.find(7), M.begin());
  EXPECT_EQ(M.find(8), M.end());
}
