//===-- tsan_mutexset_test.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_mutexset.h"
#include "gtest/gtest.h"

namespace __tsan {

static void Expect(const MutexSet &mset, uptr i, u64 id, bool write, u64 epoch,
    int count) {
  MutexSet::Desc d = mset.Get(i);
  EXPECT_EQ(id, d.id);
  EXPECT_EQ(write, d.write);
  EXPECT_EQ(epoch, d.epoch);
  EXPECT_EQ(count, d.count);
}

TEST(MutexSet, Basic) {
  MutexSet mset;
  EXPECT_EQ(mset.Size(), (uptr)0);

  mset.Add(1, true, 2);
  EXPECT_EQ(mset.Size(), (uptr)1);
  Expect(mset, 0, 1, true, 2, 1);
  mset.Del(1, true);
  EXPECT_EQ(mset.Size(), (uptr)0);

  mset.Add(3, true, 4);
  mset.Add(5, false, 6);
  EXPECT_EQ(mset.Size(), (uptr)2);
  Expect(mset, 0, 3, true, 4, 1);
  Expect(mset, 1, 5, false, 6, 1);
  mset.Del(3, true);
  EXPECT_EQ(mset.Size(), (uptr)1);
  mset.Del(5, false);
  EXPECT_EQ(mset.Size(), (uptr)0);
}

TEST(MutexSet, DoubleAdd) {
  MutexSet mset;
  mset.Add(1, true, 2);
  EXPECT_EQ(mset.Size(), (uptr)1);
  Expect(mset, 0, 1, true, 2, 1);

  mset.Add(1, true, 2);
  EXPECT_EQ(mset.Size(), (uptr)1);
  Expect(mset, 0, 1, true, 2, 2);

  mset.Del(1, true);
  EXPECT_EQ(mset.Size(), (uptr)1);
  Expect(mset, 0, 1, true, 2, 1);

  mset.Del(1, true);
  EXPECT_EQ(mset.Size(), (uptr)0);
}

TEST(MutexSet, DoubleDel) {
  MutexSet mset;
  mset.Add(1, true, 2);
  EXPECT_EQ(mset.Size(), (uptr)1);
  mset.Del(1, true);
  EXPECT_EQ(mset.Size(), (uptr)0);
  mset.Del(1, true);
  EXPECT_EQ(mset.Size(), (uptr)0);
}

TEST(MutexSet, Remove) {
  MutexSet mset;
  mset.Add(1, true, 2);
  mset.Add(1, true, 2);
  mset.Add(3, true, 4);
  mset.Add(3, true, 4);
  EXPECT_EQ(mset.Size(), (uptr)2);

  mset.Remove(1);
  EXPECT_EQ(mset.Size(), (uptr)1);
  Expect(mset, 0, 3, true, 4, 2);
}

TEST(MutexSet, Full) {
  MutexSet mset;
  for (uptr i = 0; i < MutexSet::kMaxSize; i++) {
    mset.Add(i, true, i + 1);
  }
  EXPECT_EQ(mset.Size(), MutexSet::kMaxSize);
  for (uptr i = 0; i < MutexSet::kMaxSize; i++) {
    Expect(mset, i, i, true, i + 1, 1);
  }

  for (uptr i = 0; i < MutexSet::kMaxSize; i++) {
    mset.Add(i, true, i + 1);
  }
  EXPECT_EQ(mset.Size(), MutexSet::kMaxSize);
  for (uptr i = 0; i < MutexSet::kMaxSize; i++) {
    Expect(mset, i, i, true, i + 1, 2);
  }
}

TEST(MutexSet, Overflow) {
  MutexSet mset;
  for (uptr i = 0; i < MutexSet::kMaxSize; i++) {
    mset.Add(i, true, i + 1);
    mset.Add(i, true, i + 1);
  }
  mset.Add(100, true, 200);
  EXPECT_EQ(mset.Size(), MutexSet::kMaxSize);
  for (uptr i = 0; i < MutexSet::kMaxSize; i++) {
    if (i == 0)
      Expect(mset, i, MutexSet::kMaxSize - 1,
             true, MutexSet::kMaxSize, 2);
    else if (i == MutexSet::kMaxSize - 1)
      Expect(mset, i, 100, true, 200, 1);
    else
      Expect(mset, i, i, true, i + 1, 2);
  }
}

}  // namespace __tsan
