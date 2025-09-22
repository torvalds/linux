//===-- tsan_clock_test.cpp -----------------------------------------------===//
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
#include "tsan_vector_clock.h"

#include "gtest/gtest.h"
#include "tsan_rtl.h"

namespace __tsan {

TEST(VectorClock, GetSet) {
  // Compiler won't ensure alignment on stack.
  VectorClock *vc = New<VectorClock>();
  for (uptr i = 0; i < kThreadSlotCount; i++)
    ASSERT_EQ(vc->Get(static_cast<Sid>(i)), kEpochZero);
  for (uptr i = 0; i < kThreadSlotCount; i++)
    vc->Set(static_cast<Sid>(i), static_cast<Epoch>(i));
  for (uptr i = 0; i < kThreadSlotCount; i++)
    ASSERT_EQ(vc->Get(static_cast<Sid>(i)), static_cast<Epoch>(i));
  vc->Reset();
  for (uptr i = 0; i < kThreadSlotCount; i++)
    ASSERT_EQ(vc->Get(static_cast<Sid>(i)), kEpochZero);
  DestroyAndFree(vc);
}

TEST(VectorClock, VectorOps) {
  VectorClock *vc1 = New<VectorClock>();
  VectorClock *vc2 = nullptr;
  VectorClock *vc3 = nullptr;

  vc1->Acquire(vc2);
  for (uptr i = 0; i < kThreadSlotCount; i++)
    ASSERT_EQ(vc1->Get(static_cast<Sid>(i)), kEpochZero);
  vc1->Release(&vc2);
  EXPECT_NE(vc2, nullptr);
  vc1->Acquire(vc2);
  for (uptr i = 0; i < kThreadSlotCount; i++)
    ASSERT_EQ(vc1->Get(static_cast<Sid>(i)), kEpochZero);

  for (uptr i = 0; i < kThreadSlotCount; i++) {
    vc1->Set(static_cast<Sid>(i), static_cast<Epoch>(i));
    vc2->Set(static_cast<Sid>(i), static_cast<Epoch>(kThreadSlotCount - i));
  }
  vc1->Acquire(vc2);
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    ASSERT_EQ(vc1->Get(static_cast<Sid>(i)),
              static_cast<Epoch>(i < kThreadSlotCount / 2 ? kThreadSlotCount - i
                                                          : i));
    ASSERT_EQ(vc2->Get(static_cast<Sid>(i)),
              static_cast<Epoch>(kThreadSlotCount - i));
  }
  vc2->ReleaseStore(&vc3);
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    ASSERT_EQ(vc3->Get(static_cast<Sid>(i)),
              static_cast<Epoch>(kThreadSlotCount - i));
    ASSERT_EQ(vc2->Get(static_cast<Sid>(i)),
              static_cast<Epoch>(kThreadSlotCount - i));
  }

  vc1->Reset();
  vc2->Reset();
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    vc1->Set(static_cast<Sid>(i), static_cast<Epoch>(i));
    vc2->Set(static_cast<Sid>(i), static_cast<Epoch>(kThreadSlotCount - i));
  }
  vc1->ReleaseAcquire(&vc2);
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    Epoch expect =
        static_cast<Epoch>(i < kThreadSlotCount / 2 ? kThreadSlotCount - i : i);
    ASSERT_EQ(vc1->Get(static_cast<Sid>(i)), expect);
    ASSERT_EQ(vc2->Get(static_cast<Sid>(i)), expect);
  }

  vc1->Reset();
  vc2->Reset();
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    vc1->Set(static_cast<Sid>(i), static_cast<Epoch>(i));
    vc2->Set(static_cast<Sid>(i), static_cast<Epoch>(kThreadSlotCount - i));
  }
  vc1->ReleaseStoreAcquire(&vc2);
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    ASSERT_EQ(vc1->Get(static_cast<Sid>(i)),
              static_cast<Epoch>(i < kThreadSlotCount / 2 ? kThreadSlotCount - i
                                                          : i));
    ASSERT_EQ(vc2->Get(static_cast<Sid>(i)), static_cast<Epoch>(i));
  }

  DestroyAndFree(vc1);
  DestroyAndFree(vc2);
  DestroyAndFree(vc3);
}

}  // namespace __tsan
