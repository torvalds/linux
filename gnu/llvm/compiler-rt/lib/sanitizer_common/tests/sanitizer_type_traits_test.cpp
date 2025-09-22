//===-- sanitizer_type_traits_test.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_type_traits.h"

#include <vector>

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __sanitizer {

TEST(SanitizerCommon, IsSame) {
  ASSERT_TRUE((is_same<unsigned, unsigned>::value));
  ASSERT_TRUE((is_same<uptr, uptr>::value));
  ASSERT_TRUE((is_same<sptr, sptr>::value));
  ASSERT_TRUE((is_same<const uptr, const uptr>::value));

  ASSERT_FALSE((is_same<unsigned, signed>::value));
  ASSERT_FALSE((is_same<uptr, sptr>::value));
  ASSERT_FALSE((is_same<uptr, const uptr>::value));
}

TEST(SanitizerCommon, Conditional) {
  ASSERT_TRUE((is_same<int, conditional<true, int, double>::type>::value));
  ASSERT_TRUE((is_same<double, conditional<false, int, double>::type>::value));
}

TEST(SanitizerCommon, RemoveReference) {
  ASSERT_TRUE((is_same<int, remove_reference<int>::type>::value));
  ASSERT_TRUE((is_same<const int, remove_reference<const int>::type>::value));
  ASSERT_TRUE((is_same<int, remove_reference<int&>::type>::value));
  ASSERT_TRUE((is_same<const int, remove_reference<const int&>::type>::value));
  ASSERT_TRUE((is_same<int, remove_reference<int&&>::type>::value));
}

TEST(SanitizerCommon, Move) {
  std::vector<int> v = {1, 2, 3};
  auto v2 = __sanitizer::move(v);
  EXPECT_EQ(3u, v2.size());
  EXPECT_TRUE(v.empty());
}

TEST(SanitizerCommon, Forward) {
  std::vector<int> v = {1, 2, 3};
  auto v2 = __sanitizer::forward<std::vector<int>>(v);
  EXPECT_EQ(3u, v2.size());
  EXPECT_TRUE(v.empty());
}

TEST(SanitizerCommon, ForwardConst) {
  const std::vector<int> v = {1, 2, 3};
  auto v2 = __sanitizer::forward<const std::vector<int>&>(v);
  EXPECT_EQ(3u, v2.size());
  EXPECT_EQ(3u, v.size());
}

struct TestStruct {
  int a;
  float b;
};

TEST(SanitizerCommon, IsTriviallyDestructible) {
  ASSERT_TRUE((is_trivially_destructible<int>::value));
  ASSERT_TRUE((is_trivially_destructible<TestStruct>::value));
  ASSERT_FALSE((is_trivially_destructible<std::vector<int>>::value));
}

TEST(SanitizerCommon, IsTriviallyCopyable) {
  ASSERT_TRUE((is_trivially_copyable<int>::value));
  ASSERT_TRUE((is_trivially_copyable<TestStruct>::value));
  ASSERT_FALSE((is_trivially_copyable<std::vector<int>>::value));
}

}  // namespace __sanitizer