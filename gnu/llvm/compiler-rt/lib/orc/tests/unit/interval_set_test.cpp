//===-- interval_set_test.cpp ---------------------------------------------===//
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

#include "interval_set.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

TEST(IntervalSetTest, DefaultConstructed) {
  // Check that a default-constructed IntervalSet behaves as expected.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  EXPECT_TRUE(S.empty());
  EXPECT_TRUE(S.begin() == S.end());
  EXPECT_TRUE(S.find(0) == S.end());
}

TEST(IntervalSetTest, InsertSingleElement) {
  // Check that a set with a single element inserted behaves as expected.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(7, 8);

  EXPECT_FALSE(S.empty());
  EXPECT_EQ(std::next(S.begin()), S.end());
  EXPECT_EQ(S.find(7), S.begin());
  EXPECT_EQ(S.find(8), S.end());
}

TEST(IntervalSetTest, InsertCoalesceWithPrevious) {
  // Check that insertions coalesce with previous ranges.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(7, 8);
  S.insert(8, 9);

  EXPECT_FALSE(S.empty());
  EXPECT_EQ(std::next(S.begin()), S.end()); // Should see just one range.
  EXPECT_EQ(S.find(7), S.find(8)); // 7 and 8 should point to same range.
}

TEST(IntervalSetTest, InsertCoalesceWithFollowing) {
  // Check that insertions coalesce with following ranges.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(8, 9);
  S.insert(7, 8);

  EXPECT_FALSE(S.empty());
  EXPECT_EQ(std::next(S.begin()), S.end()); // Should see just one range.
  EXPECT_EQ(S.find(7), S.find(8)); // 7 and 8 should point to same range.
}

TEST(IntervalSetTest, InsertCoalesceBoth) {
  // Check that insertions coalesce with ranges on both sides.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(7, 8);
  S.insert(9, 10);

  // Check no coalescing yet.
  EXPECT_NE(S.find(7), S.find(9));

  // Insert a 3rd range to trigger coalescing on both sides.
  S.insert(8, 9);

  EXPECT_FALSE(S.empty());
  EXPECT_EQ(std::next(S.begin()), S.end()); // Should see just one range.
  EXPECT_EQ(S.find(7), S.find(8)); // 7, 8, and 9 should point to same range.
  EXPECT_EQ(S.find(8), S.find(9));
}

TEST(IntervalSetTest, EraseSplittingLeft) {
  // Check that removal of a trailing subrange succeeds, but leaves the
  // residual range in-place.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(7, 10);
  EXPECT_FALSE(S.empty());
  S.erase(9, 10);
  EXPECT_EQ(std::next(S.begin()), S.end());
  EXPECT_EQ(S.begin()->first, 7U);
  EXPECT_EQ(S.begin()->second, 9U);
}

TEST(IntervalSetTest, EraseSplittingRight) {
  // Check that removal of a leading subrange succeeds, but leaves the
  // residual range in-place.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(7, 10);
  EXPECT_FALSE(S.empty());
  S.erase(7, 8);
  EXPECT_EQ(std::next(S.begin()), S.end());
  EXPECT_EQ(S.begin()->first, 8U);
  EXPECT_EQ(S.begin()->second, 10U);
}

TEST(IntervalSetTest, EraseSplittingBoth) {
  // Check that removal of an interior subrange leaves both the leading and
  // trailing residual subranges in-place.
  IntervalSet<unsigned, IntervalCoalescing::Enabled> S;

  S.insert(7, 10);
  EXPECT_FALSE(S.empty());
  S.erase(8, 9);
  EXPECT_EQ(std::next(std::next(S.begin())), S.end());
  EXPECT_EQ(S.begin()->first, 7U);
  EXPECT_EQ(S.begin()->second, 8U);
  EXPECT_EQ(std::next(S.begin())->first, 9U);
  EXPECT_EQ(std::next(S.begin())->second, 10U);
}
