//===--- rtsan_test_context.cpp - Realtime Sanitizer ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "rtsan_test_utilities.h"

#include "rtsan_context.h"

TEST(TestRtsanContext, CanCreateContext) { __rtsan::Context context{}; }

TEST(TestRtsanContext, ExpectNotRealtimeDoesNotDieBeforeRealtimePush) {
  __rtsan::Context context{};
  context.ExpectNotRealtime("do_some_stuff");
}

TEST(TestRtsanContext, ExpectNotRealtimeDoesNotDieAfterPushAndPop) {
  __rtsan::Context context{};
  context.RealtimePush();
  context.RealtimePop();
  context.ExpectNotRealtime("do_some_stuff");
}

TEST(TestRtsanContext, ExpectNotRealtimeDiesAfterRealtimePush) {
  __rtsan::Context context{};

  context.RealtimePush();
  EXPECT_DEATH(context.ExpectNotRealtime("do_some_stuff"), "");
}

TEST(TestRtsanContext,
     ExpectNotRealtimeDiesAfterRealtimeAfterMorePushesThanPops) {
  __rtsan::Context context{};

  context.RealtimePush();
  context.RealtimePush();
  context.RealtimePush();
  context.RealtimePop();
  context.RealtimePop();
  EXPECT_DEATH(context.ExpectNotRealtime("do_some_stuff"), "");
}

TEST(TestRtsanContext, ExpectNotRealtimeDoesNotDieAfterBypassPush) {
  __rtsan::Context context{};

  context.RealtimePush();
  context.BypassPush();
  context.ExpectNotRealtime("do_some_stuff");
}

TEST(TestRtsanContext,
     ExpectNotRealtimeDoesNotDieIfBypassDepthIsGreaterThanZero) {
  __rtsan::Context context{};

  context.RealtimePush();
  context.BypassPush();
  context.BypassPush();
  context.BypassPush();
  context.BypassPop();
  context.BypassPop();
  context.ExpectNotRealtime("do_some_stuff");
  context.BypassPop();
  EXPECT_DEATH(context.ExpectNotRealtime("do_some_stuff"), "");
}
