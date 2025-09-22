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
#include "sanitizer_common/sanitizer_vector.h"
#include "gtest/gtest.h"

namespace __sanitizer {

TEST(Vector, Basic) {
  Vector<int> v;
  EXPECT_EQ(v.Size(), 0u);
  v.PushBack(42);
  EXPECT_EQ(v.Size(), 1u);
  EXPECT_EQ(v[0], 42);
  v.PushBack(43);
  EXPECT_EQ(v.Size(), 2u);
  EXPECT_EQ(v[0], 42);
  EXPECT_EQ(v[1], 43);
}

TEST(Vector, Stride) {
  Vector<int> v;
  for (int i = 0; i < 1000; i++) {
    v.PushBack(i);
    EXPECT_EQ(v.Size(), i + 1u);
    EXPECT_EQ(v[i], i);
  }
  for (int i = 0; i < 1000; i++) {
    EXPECT_EQ(v[i], i);
  }
}

TEST(Vector, ResizeReduction) {
  Vector<int> v;
  v.PushBack(0);
  v.PushBack(0);
  EXPECT_EQ(v.Size(), 2u);
  v.Resize(1);
  EXPECT_EQ(v.Size(), 1u);
}

}  // namespace __sanitizer
