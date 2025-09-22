//===-- sanitizer_vector_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of *Sanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_ring_buffer.h"
#include "gtest/gtest.h"

namespace __sanitizer {

struct LargeStruct {
  int64_t v;
  int64_t extra[3];

  explicit LargeStruct(int64_t v) : v(v) {}
  operator int64_t() { return v; }
};

struct Struct10Bytes {
  short t[3];
};

TEST(RingBuffer, Construct) {
  RingBuffer<int64_t> *RBlong = RingBuffer<int64_t>::New(20);
  EXPECT_EQ(RBlong->size(), 20U);
  RBlong->Delete();
}

template <class T> void TestRB() {
  RingBuffer<T> *RB;
  const size_t Sizes[] = {1, 2, 3, 5, 8, 16, 20, 40, 10000};
  for (size_t Size : Sizes) {
    RB = RingBuffer<T>::New(Size);
    EXPECT_EQ(RB->size(), Size);
    RB->Delete();
  }

  RB = RingBuffer<T>::New(4);
  EXPECT_EQ(RB->size(), 4U);
#define EXPECT_RING_BUFFER(a0, a1, a2, a3) \
  EXPECT_EQ((int64_t)(*RB)[0], (int64_t)a0);                 \
  EXPECT_EQ((int64_t)(*RB)[1], (int64_t)a1);                 \
  EXPECT_EQ((int64_t)(*RB)[2], (int64_t)a2);                 \
  EXPECT_EQ((int64_t)(*RB)[3], (int64_t)a3);

  RB->push(T(1)); EXPECT_RING_BUFFER(1, 0, 0, 0);
  RB->push(T(2)); EXPECT_RING_BUFFER(2, 1, 0, 0);
  RB->push(T(3)); EXPECT_RING_BUFFER(3, 2, 1, 0);
  RB->push(T(4)); EXPECT_RING_BUFFER(4, 3, 2, 1);
  RB->push(T(5)); EXPECT_RING_BUFFER(5, 4, 3, 2);
  RB->push(T(6)); EXPECT_RING_BUFFER(6, 5, 4, 3);
  RB->push(T(7)); EXPECT_RING_BUFFER(7, 6, 5, 4);
  RB->push(T(8)); EXPECT_RING_BUFFER(8, 7, 6, 5);
  RB->push(T(9)); EXPECT_RING_BUFFER(9, 8, 7, 6);
  RB->push(T(10)); EXPECT_RING_BUFFER(10, 9, 8, 7);
  RB->push(T(11)); EXPECT_RING_BUFFER(11, 10, 9, 8);
  RB->push(T(12)); EXPECT_RING_BUFFER(12, 11, 10, 9);

#undef EXPECT_RING_BUFFER
}

#if SANITIZER_WORDSIZE == 64
TEST(RingBuffer, int64) {
  TestRB<int64_t>();
}

TEST(RingBuffer, LargeStruct) {
  TestRB<LargeStruct>();
}

template<typename T>
CompactRingBuffer<T> *AllocCompactRingBuffer(size_t count) {
  size_t sz = sizeof(T) * count;
  EXPECT_EQ(0ULL, sz % 4096);
  void *p = MmapAlignedOrDieOnFatalError(sz, sz * 2, "CompactRingBuffer");
  return new CompactRingBuffer<T>(p, sz);
}

TEST(CompactRingBuffer, int64) {
  const size_t page_sizes[] = {1, 2, 4, 128};

  for (size_t pages : page_sizes) {
    size_t count = 4096 * pages / sizeof(int64_t);
    auto R = AllocCompactRingBuffer<int64_t>(count);
    int64_t top = count * 3 + 13;
    for (int64_t i = 0; i < top; ++i) R->push(i);
    for (int64_t i = 0; i < (int64_t)count; ++i)
      EXPECT_EQ(top - i - 1, (*R)[i]);
  }
}
#endif
}  // namespace __sanitizer
