//===-- stats_test.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "stats.h"

TEST(ScudoStatsTest, LocalStats) {
  scudo::LocalStats LStats;
  LStats.init();
  for (scudo::uptr I = 0; I < scudo::StatCount; I++)
    EXPECT_EQ(LStats.get(static_cast<scudo::StatType>(I)), 0U);
  LStats.add(scudo::StatAllocated, 4096U);
  EXPECT_EQ(LStats.get(scudo::StatAllocated), 4096U);
  LStats.sub(scudo::StatAllocated, 4096U);
  EXPECT_EQ(LStats.get(scudo::StatAllocated), 0U);
  LStats.set(scudo::StatAllocated, 4096U);
  EXPECT_EQ(LStats.get(scudo::StatAllocated), 4096U);
}

TEST(ScudoStatsTest, GlobalStats) {
  scudo::GlobalStats GStats;
  GStats.init();
  scudo::uptr Counters[scudo::StatCount] = {};
  GStats.get(Counters);
  for (scudo::uptr I = 0; I < scudo::StatCount; I++)
    EXPECT_EQ(Counters[I], 0U);
  scudo::LocalStats LStats;
  LStats.init();
  GStats.link(&LStats);
  for (scudo::uptr I = 0; I < scudo::StatCount; I++)
    LStats.add(static_cast<scudo::StatType>(I), 4096U);
  GStats.get(Counters);
  for (scudo::uptr I = 0; I < scudo::StatCount; I++)
    EXPECT_EQ(Counters[I], 4096U);
  // Unlinking the local stats move numbers to the global stats.
  GStats.unlink(&LStats);
  GStats.get(Counters);
  for (scudo::uptr I = 0; I < scudo::StatCount; I++)
    EXPECT_EQ(Counters[I], 4096U);
}
