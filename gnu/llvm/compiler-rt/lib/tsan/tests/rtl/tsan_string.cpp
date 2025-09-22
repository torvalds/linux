//===-- tsan_string.cpp ---------------------------------------------------===//
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
#include <string.h>

namespace __tsan {

TEST_F(ThreadSanitizer, Memcpy) {
  char data0[7] = {1, 2, 3, 4, 5, 6, 7};
  char data[7] = {42, 42, 42, 42, 42, 42, 42};
  MainThread().Memcpy(data+1, data0+1, 5);
  EXPECT_EQ(data[0], 42);
  EXPECT_EQ(data[1], 2);
  EXPECT_EQ(data[2], 3);
  EXPECT_EQ(data[3], 4);
  EXPECT_EQ(data[4], 5);
  EXPECT_EQ(data[5], 6);
  EXPECT_EQ(data[6], 42);
  MainThread().Memset(data+1, 13, 5);
  EXPECT_EQ(data[0], 42);
  EXPECT_EQ(data[1], 13);
  EXPECT_EQ(data[2], 13);
  EXPECT_EQ(data[3], 13);
  EXPECT_EQ(data[4], 13);
  EXPECT_EQ(data[5], 13);
  EXPECT_EQ(data[6], 42);
}

TEST_F(ThreadSanitizer, MemcpyRace1) {
  char *data = new char[10];
  char *data1 = new char[10];
  char *data2 = new char[10];
  ScopedThread t1, t2;
  t1.Memcpy(data, data1, 10);
  t2.Memcpy(data, data2, 10, true);
}

TEST_F(ThreadSanitizer, MemcpyRace2) {
  char *data = new char[10];
  char *data1 = new char[10];
  char *data2 = new char[10];
  ScopedThread t1, t2;
  t1.Memcpy(data+5, data1, 1);
  t2.Memcpy(data+3, data2, 4, true);
}

TEST_F(ThreadSanitizer, MemcpyRace3) {
  char *data = new char[10];
  char *data1 = new char[10];
  char *data2 = new char[10];
  ScopedThread t1, t2;
  t1.Memcpy(data, data1, 10);
  t2.Memcpy(data1, data2, 10, true);
}

TEST_F(ThreadSanitizer, MemcpyStack) {
  char *data = new char[10];
  char *data1 = new char[10];
  ScopedThread t1, t2;
  t1.Memcpy(data, data1, 10);
  t2.Memcpy(data, data1, 10, true);
}

TEST_F(ThreadSanitizer, MemsetRace1) {
  char *data = new char[10];
  ScopedThread t1, t2;
  t1.Memset(data, 1, 10);
  t2.Memset(data, 2, 10, true);
}

}  // namespace __tsan
