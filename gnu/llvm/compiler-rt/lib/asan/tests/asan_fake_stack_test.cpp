//===-- asan_fake_stack_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Tests for FakeStack.
// This test file should be compiled w/o asan instrumentation.
//===----------------------------------------------------------------------===//

#include "asan_fake_stack.h"
#include "asan_test_utils.h"
#include "sanitizer_common/sanitizer_common.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <map>

namespace __asan {

TEST(FakeStack, FlagsSize) {
  EXPECT_EQ(FakeStack::SizeRequiredForFlags(10), 1U << 5);
  EXPECT_EQ(FakeStack::SizeRequiredForFlags(11), 1U << 6);
  EXPECT_EQ(FakeStack::SizeRequiredForFlags(20), 1U << 15);
}

TEST(FakeStack, RequiredSize) {
  // for (int i = 15; i < 20; i++) {
  //  uptr alloc_size = FakeStack::RequiredSize(i);
  //  printf("%zdK ==> %zd\n", 1 << (i - 10), alloc_size);
  // }
  EXPECT_EQ(FakeStack::RequiredSize(15), 365568U);
  EXPECT_EQ(FakeStack::RequiredSize(16), 727040U);
  EXPECT_EQ(FakeStack::RequiredSize(17), 1449984U);
  EXPECT_EQ(FakeStack::RequiredSize(18), 2895872U);
  EXPECT_EQ(FakeStack::RequiredSize(19), 5787648U);
}

TEST(FakeStack, FlagsOffset) {
  for (uptr stack_size_log = 15; stack_size_log <= 20; stack_size_log++) {
    uptr stack_size = 1UL << stack_size_log;
    uptr offset = 0;
    for (uptr class_id = 0; class_id < FakeStack::kNumberOfSizeClasses;
         class_id++) {
      uptr frame_size = FakeStack::BytesInSizeClass(class_id);
      uptr num_flags = stack_size / frame_size;
      EXPECT_EQ(offset, FakeStack::FlagsOffset(stack_size_log, class_id));
      // printf("%zd: %zd => %zd %zd\n", stack_size_log, class_id, offset,
      //        FakeStack::FlagsOffset(stack_size_log, class_id));
      offset += num_flags;
    }
  }
}

#if !defined(_WIN32)  // FIXME: Fails due to OOM on Windows.
TEST(FakeStack, CreateDestroy) {
  for (int i = 0; i < 1000; i++) {
    for (uptr stack_size_log = 20; stack_size_log <= 22; stack_size_log++) {
      FakeStack *fake_stack = FakeStack::Create(stack_size_log);
      fake_stack->Destroy(0);
    }
  }
}
#endif

TEST(FakeStack, ModuloNumberOfFrames) {
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, 0), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<15)), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<10)), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<9)), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<8)), 1U<<8);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<15) + 1), 1U);

  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 0), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 1<<9), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 1<<8), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 1<<7), 1U<<7);

  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 0), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 1), 1U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 15), 15U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 16), 0U);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 17), 1U);
}

TEST(FakeStack, GetFrame) {
  const uptr stack_size_log = 20;
  const uptr stack_size = 1 << stack_size_log;
  FakeStack *fs = FakeStack::Create(stack_size_log);
  u8 *base = fs->GetFrame(stack_size_log, 0, 0);
  EXPECT_EQ(base, reinterpret_cast<u8 *>(fs) +
                      fs->SizeRequiredForFlags(stack_size_log) + 4096);
  EXPECT_EQ(base + 0*stack_size + 64 * 7, fs->GetFrame(stack_size_log, 0, 7U));
  EXPECT_EQ(base + 1*stack_size + 128 * 3, fs->GetFrame(stack_size_log, 1, 3U));
  EXPECT_EQ(base + 2*stack_size + 256 * 5, fs->GetFrame(stack_size_log, 2, 5U));
  fs->Destroy(0);
}

TEST(FakeStack, Allocate) {
  const uptr stack_size_log = 19;
  FakeStack *fs = FakeStack::Create(stack_size_log);
  std::map<FakeFrame *, uptr> s;
  for (int iter = 0; iter < 2; iter++) {
    s.clear();
    for (uptr cid = 0; cid < FakeStack::kNumberOfSizeClasses; cid++) {
      uptr n = FakeStack::NumberOfFrames(stack_size_log, cid);
      uptr bytes_in_class = FakeStack::BytesInSizeClass(cid);
      for (uptr j = 0; j < n; j++) {
        FakeFrame *ff = fs->Allocate(stack_size_log, cid, 0);
        uptr x = reinterpret_cast<uptr>(ff);
        EXPECT_TRUE(s.insert(std::make_pair(ff, cid)).second);
        EXPECT_EQ(x, fs->AddrIsInFakeStack(x));
        EXPECT_EQ(x, fs->AddrIsInFakeStack(x + 1));
        EXPECT_EQ(x, fs->AddrIsInFakeStack(x + bytes_in_class - 1));
        EXPECT_NE(x, fs->AddrIsInFakeStack(x + bytes_in_class));
      }
      // We are out of fake stack, so Allocate should return 0.
      EXPECT_EQ(0UL, fs->Allocate(stack_size_log, cid, 0));
    }
    for (std::map<FakeFrame *, uptr>::iterator it = s.begin(); it != s.end();
         ++it) {
      fs->Deallocate(reinterpret_cast<uptr>(it->first), it->second);
    }
  }
  fs->Destroy(0);
}

static void RecursiveFunction(FakeStack *fs, int depth) {
  uptr class_id = depth / 3;
  FakeFrame *ff = fs->Allocate(fs->stack_size_log(), class_id, 0);
  if (depth) {
    RecursiveFunction(fs, depth - 1);
    RecursiveFunction(fs, depth - 1);
  }
  fs->Deallocate(reinterpret_cast<uptr>(ff), class_id);
}

TEST(FakeStack, RecursiveStressTest) {
  const uptr stack_size_log = 16;
  FakeStack *fs = FakeStack::Create(stack_size_log);
  RecursiveFunction(fs, 22);  // with 26 runs for 2-3 seconds.
  fs->Destroy(0);
}

}  // namespace __asan
