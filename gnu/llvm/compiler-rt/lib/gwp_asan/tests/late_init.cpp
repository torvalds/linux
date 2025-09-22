//===-- late_init.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/options.h"
#include "gwp_asan/tests/harness.h"

TEST(LateInit, CheckLateInitIsOK) {
  gwp_asan::GuardedPoolAllocator GPA;

  for (size_t i = 0; i < 0x100; ++i)
    EXPECT_FALSE(GPA.shouldSample());

  gwp_asan::options::Options Opts;
  Opts.Enabled = true;
  Opts.SampleRate = 1;

  GPA.init(Opts);
  EXPECT_TRUE(GPA.shouldSample());
  GPA.uninitTestOnly();
}
