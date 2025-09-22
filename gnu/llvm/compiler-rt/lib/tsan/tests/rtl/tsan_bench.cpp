//===-- tsan_bench.cpp ----------------------------------------------------===//
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
#include "tsan_interface.h"
#include "tsan_defs.h"
#include "gtest/gtest.h"
#include <stdint.h>

const int kSize = 128;
const int kRepeat = 2*1024*1024;

void noinstr(void *p) {}

template<typename T, void(*__tsan_mop)(void *p)>
static void Benchmark() {
  volatile T data[kSize];
  for (int i = 0; i < kRepeat; i++) {
    for (int j = 0; j < kSize; j++) {
      __tsan_mop((void*)&data[j]);
      data[j]++;
    }
  }
}

TEST(DISABLED_BENCH, Mop1) {
  Benchmark<uint8_t, noinstr>();
}

TEST(DISABLED_BENCH, Mop1Read) {
  Benchmark<uint8_t, __tsan_read1>();
}

TEST(DISABLED_BENCH, Mop1Write) {
  Benchmark<uint8_t, __tsan_write1>();
}

TEST(DISABLED_BENCH, Mop2) {
  Benchmark<uint16_t, noinstr>();
}

TEST(DISABLED_BENCH, Mop2Read) {
  Benchmark<uint16_t, __tsan_read2>();
}

TEST(DISABLED_BENCH, Mop2Write) {
  Benchmark<uint16_t, __tsan_write2>();
}

TEST(DISABLED_BENCH, Mop4) {
  Benchmark<uint32_t, noinstr>();
}

TEST(DISABLED_BENCH, Mop4Read) {
  Benchmark<uint32_t, __tsan_read4>();
}

TEST(DISABLED_BENCH, Mop4Write) {
  Benchmark<uint32_t, __tsan_write4>();
}

TEST(DISABLED_BENCH, Mop8) {
  Benchmark<uint8_t, noinstr>();
}

TEST(DISABLED_BENCH, Mop8Read) {
  Benchmark<uint64_t, __tsan_read8>();
}

TEST(DISABLED_BENCH, Mop8Write) {
  Benchmark<uint64_t, __tsan_write8>();
}

TEST(DISABLED_BENCH, FuncCall) {
  for (int i = 0; i < kRepeat; i++) {
    for (int j = 0; j < kSize; j++)
      __tsan_func_entry((void*)(uintptr_t)j);
    for (int j = 0; j < kSize; j++)
      __tsan_func_exit();
  }
}

TEST(DISABLED_BENCH, MutexLocal) {
  UserMutex m;
  ScopedThread().Create(m);
  for (int i = 0; i < 50; i++) {
    ScopedThread t;
    t.Lock(m);
    t.Unlock(m);
  }
  for (int i = 0; i < 16*1024*1024; i++) {
    m.Lock();
    m.Unlock();
  }
  ScopedThread().Destroy(m);
}
