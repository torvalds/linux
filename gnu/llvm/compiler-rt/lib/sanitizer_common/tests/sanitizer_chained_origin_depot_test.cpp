//===-- sanitizer_chained_origin_depot_test.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer runtime.
// Tests for sanitizer_chained_origin_depot.h.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_chained_origin_depot.h"

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_libc.h"

namespace __sanitizer {

static ChainedOriginDepot chainedOriginDepot;

TEST(SanitizerCommon, ChainedOriginDepotBasic) {
  u32 new_id;
  EXPECT_TRUE(chainedOriginDepot.Put(1, 2, &new_id));
  u32 prev_id;
  EXPECT_EQ(chainedOriginDepot.Get(new_id, &prev_id), 1U);
  EXPECT_EQ(prev_id, 2U);
}

TEST(SanitizerCommon, ChainedOriginDepotAbsent) {
  u32 prev_id;
  EXPECT_EQ(0U, chainedOriginDepot.Get(99, &prev_id));
  EXPECT_EQ(0U, prev_id);
}

TEST(SanitizerCommon, ChainedOriginDepotZeroId) {
  u32 prev_id;
  EXPECT_EQ(0U, chainedOriginDepot.Get(0, &prev_id));
  EXPECT_EQ(0U, prev_id);
}

TEST(SanitizerCommon, ChainedOriginDepotSame) {
  u32 new_id1;
  EXPECT_TRUE(chainedOriginDepot.Put(11, 12, &new_id1));
  u32 new_id2;
  EXPECT_FALSE(chainedOriginDepot.Put(11, 12, &new_id2));
  EXPECT_EQ(new_id1, new_id2);

  u32 prev_id;
  EXPECT_EQ(chainedOriginDepot.Get(new_id1, &prev_id), 11U);
  EXPECT_EQ(prev_id, 12U);
}

TEST(SanitizerCommon, ChainedOriginDepotDifferent) {
  u32 new_id1;
  EXPECT_TRUE(chainedOriginDepot.Put(21, 22, &new_id1));
  u32 new_id2;
  EXPECT_TRUE(chainedOriginDepot.Put(21, 23, &new_id2));
  EXPECT_NE(new_id1, new_id2);

  u32 prev_id;
  EXPECT_EQ(chainedOriginDepot.Get(new_id1, &prev_id), 21U);
  EXPECT_EQ(prev_id, 22U);
  EXPECT_EQ(chainedOriginDepot.Get(new_id2, &prev_id), 21U);
  EXPECT_EQ(prev_id, 23U);
}

TEST(SanitizerCommon, ChainedOriginDepotStats) {
  chainedOriginDepot.TestOnlyUnmap();
  StackDepotStats stats0 = chainedOriginDepot.GetStats();

  u32 new_id;
  EXPECT_TRUE(chainedOriginDepot.Put(33, 34, &new_id));
  StackDepotStats stats1 = chainedOriginDepot.GetStats();
  EXPECT_EQ(stats1.n_uniq_ids, stats0.n_uniq_ids + 1);
  EXPECT_GT(stats1.allocated, stats0.allocated);

  EXPECT_FALSE(chainedOriginDepot.Put(33, 34, &new_id));
  StackDepotStats stats2 = chainedOriginDepot.GetStats();
  EXPECT_EQ(stats2.n_uniq_ids, stats1.n_uniq_ids);
  EXPECT_EQ(stats2.allocated, stats1.allocated);

  for (int i = 0; i < 100000; ++i) {
    ASSERT_TRUE(chainedOriginDepot.Put(35, i, &new_id));
    StackDepotStats stats3 = chainedOriginDepot.GetStats();
    ASSERT_EQ(stats3.n_uniq_ids, stats2.n_uniq_ids + 1 + i);
  }
  EXPECT_GT(chainedOriginDepot.GetStats().allocated, stats2.allocated);
}

}  // namespace __sanitizer
