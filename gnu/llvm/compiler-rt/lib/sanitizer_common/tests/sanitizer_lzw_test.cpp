//===-- sanitizer_lzw_test.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_lzw.h"

#include <algorithm>
#include <iterator>

#include "gtest/gtest.h"
#include "sanitizer_hash.h"

namespace __sanitizer {

template <typename T>
struct LzwTest : public ::testing::Test {
  template <typename Generator>
  void Run(size_t n, Generator gen) {
    std::vector<T> data(n);
    std::generate(data.begin(), data.end(), gen);

    std::vector<u64> lzw;
    LzwEncode<T>(data.begin(), data.end(), std::back_inserter(lzw));

    std::vector<T> unlzw(data.size() * 2);
    auto unlzw_end = LzwDecode<T>(lzw.begin(), lzw.end(), unlzw.data());
    unlzw.resize(unlzw_end - unlzw.data());

    EXPECT_EQ(data, unlzw);
  }
};

static constexpr size_t kSizes[] = {0, 1, 2, 7, 13, 32, 129, 10000};

using LzwTestTypes = ::testing::Types<u8, u16, u32, u64>;
TYPED_TEST_SUITE(LzwTest, LzwTestTypes, );

TYPED_TEST(LzwTest, Same) {
  MurMur2Hash64Builder h(0);
  for (size_t sz : kSizes) {
    u64 v = 0;
    for (size_t i = 0; i < 100 && !this->HasFailure(); ++i) {
      this->Run(sz, [&] { return v; });
      h.add(i);
      v = h.get();
    }
  }
}

TYPED_TEST(LzwTest, Increment) {
  MurMur2Hash64Builder h(0);
  for (size_t sz : kSizes) {
    u64 v = 0;
    for (size_t i = 0; i < 100 && !this->HasFailure(); ++i) {
      this->Run(sz, [&v] { return v++; });
      h.add(i);
      v = h.get();
    }
  }
}

TYPED_TEST(LzwTest, IncrementMod) {
  MurMur2Hash64Builder h(0);
  for (size_t sz : kSizes) {
    u64 v = 0;
    for (size_t i = 1; i < 16 && !this->HasFailure(); ++i) {
      this->Run(sz, [&] { return v++ % i; });
      h.add(i);
      v = h.get();
    }
  }
}

TYPED_TEST(LzwTest, RandomLimited) {
  for (size_t sz : kSizes) {
    for (size_t i = 1; i < 1000 && !this->HasFailure(); i *= 2) {
      u64 v = 0;
      this->Run(sz, [&] {
        MurMur2Hash64Builder h(v % i /* Keep unique set limited */);
        v = h.get();
        return v;
      });
    }
  }
}

}  // namespace __sanitizer
