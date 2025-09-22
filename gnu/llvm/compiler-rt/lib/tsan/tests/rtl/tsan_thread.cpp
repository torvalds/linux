//===-- tsan_thread.cpp ---------------------------------------------------===//
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
#include "tsan_test_util.h"
#include "gtest/gtest.h"

TEST_F(ThreadSanitizer, ThreadSync) {
  MainThread t0;
  MemLoc l;
  t0.Write1(l);
  {
    ScopedThread t1;
    t1.Write1(l);
  }
  t0.Write1(l);
}

TEST_F(ThreadSanitizer, ThreadDetach1) {
  ScopedThread t1(true);
  MemLoc l;
  t1.Write1(l);
}

TEST_F(ThreadSanitizer, ThreadDetach2) {
  ScopedThread t1;
  MemLoc l;
  t1.Write1(l);
  t1.Detach();
}

static void *thread_alot_func(void *arg) {
  (void)arg;
  int usleep(unsigned);
  usleep(50);
  return 0;
}

TEST(DISABLED_SLOW_ThreadSanitizer, ThreadALot) {
  const int kThreads = 70000;
  const int kAlive = 1000;
  pthread_t threads[kAlive] = {};
  for (int i = 0; i < kThreads; i++) {
    if (threads[i % kAlive])
      pthread_join(threads[i % kAlive], 0);
    pthread_create(&threads[i % kAlive], 0, thread_alot_func, 0);
  }
  for (int i = 0; i < kAlive; i++) {
    pthread_join(threads[i], 0);
  }
}
