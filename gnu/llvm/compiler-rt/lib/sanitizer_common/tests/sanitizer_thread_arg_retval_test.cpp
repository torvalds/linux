//===-- sanitizer_thread_registry_test.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of shared sanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_thread_arg_retval.h"

#include "gtest/gtest.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

static int t;
static void* arg = &t;
static void* (*routine)(void*) = [](void*) { return arg; };
static void* retval = (&t) + 1;

TEST(ThreadArgRetvalTest, CreateFail) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 0; });
  EXPECT_EQ(0u, td.size());
}

TEST(ThreadArgRetvalTest, CreateRunJoin) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 1; });
  EXPECT_EQ(1u, td.size());

  EXPECT_EQ(arg, td.GetArgs(1).arg_retval);
  EXPECT_EQ(routine, td.GetArgs(1).routine);
  EXPECT_EQ(1u, td.size());

  td.Finish(1, retval);
  EXPECT_EQ(1u, td.size());

  td.Join(1, []() { return false; });
  EXPECT_EQ(1u, td.size());

  td.Join(1, []() { return true; });
  EXPECT_EQ(0u, td.size());
}

TEST(ThreadArgRetvalTest, CreateJoinRun) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 1; });
  EXPECT_EQ(1u, td.size());

  td.Join(1, []() { return false; });
  EXPECT_EQ(1u, td.size());

  td.Join(1, [&]() {
    // Expected to happen on another thread.
    EXPECT_EQ(1u, td.size());

    EXPECT_EQ(arg, td.GetArgs(1).arg_retval);
    EXPECT_EQ(routine, td.GetArgs(1).routine);
    EXPECT_EQ(1u, td.size());

    td.Finish(1, retval);
    EXPECT_EQ(1u, td.size());
    return true;
  });
  EXPECT_EQ(0u, td.size());
}

TEST(ThreadArgRetvalTest, CreateRunDetach) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 1; });
  EXPECT_EQ(1u, td.size());

  EXPECT_EQ(arg, td.GetArgs(1).arg_retval);
  EXPECT_EQ(routine, td.GetArgs(1).routine);
  EXPECT_EQ(1u, td.size());

  td.Finish(1, retval);
  EXPECT_EQ(1u, td.size());

  td.Detach(1, []() { return false; });
  EXPECT_EQ(1u, td.size());

  td.Detach(1, []() { return true; });
  EXPECT_EQ(0u, td.size());
}

TEST(ThreadArgRetvalTest, CreateDetachRun) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 1; });
  EXPECT_EQ(1u, td.size());

  td.Detach(1, []() { return true; });
  EXPECT_EQ(1u, td.size());

  EXPECT_EQ(arg, td.GetArgs(1).arg_retval);
  EXPECT_EQ(routine, td.GetArgs(1).routine);
  EXPECT_EQ(1u, td.size());

  td.Finish(1, retval);
  EXPECT_EQ(0u, td.size());
}

TEST(ThreadArgRetvalTest, CreateRunJoinReuse) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 1; });
  EXPECT_EQ(1u, td.size());

  td.Finish(1, retval);
  EXPECT_EQ(1u, td.size());

  td.Join(1, [&]() {
    // Reuse thread id.
    td.Create(false, {routine, arg}, []() { return 1; });
    EXPECT_EQ(1u, td.size());
    return true;
  });
  // Even if JoinFn succeeded, we can't erase mismatching thread.
  EXPECT_EQ(1u, td.size());

  // Now we can join another one.
  td.Join(1, []() { return true; });
  EXPECT_EQ(0u, td.size());
}

TEST(ThreadArgRetvalTest, GetAllPtrsLocked) {
  ThreadArgRetval td;
  td.Create(false, {routine, arg}, []() { return 1; });
  {
    GenericScopedLock<ThreadArgRetval> lock(&td);
    InternalMmapVector<uptr> ptrs;
    td.GetAllPtrsLocked(&ptrs);
    EXPECT_EQ(1u, ptrs.size());
    EXPECT_EQ((uptr)arg, ptrs[0]);
  }

  td.Finish(1, retval);
  {
    GenericScopedLock<ThreadArgRetval> lock(&td);
    InternalMmapVector<uptr> ptrs;
    td.GetAllPtrsLocked(&ptrs);
    EXPECT_EQ(1u, ptrs.size());
    EXPECT_EQ((uptr)retval, ptrs[0]);
  }

  td.Join(1, []() { return true; });
  {
    GenericScopedLock<ThreadArgRetval> lock(&td);
    InternalMmapVector<uptr> ptrs;
    td.GetAllPtrsLocked(&ptrs);
    EXPECT_TRUE(ptrs.empty());
  }
}

}  // namespace __sanitizer
