//===- sanitizer_array_ref.cpp - ArrayRef unit tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_array_ref.h"

#include <vector>

#include "gtest/gtest.h"
#include "sanitizer_internal_defs.h"

using namespace __sanitizer;
namespace {

TEST(ArrayRefTest, Constructors) {
  ArrayRef<int> ar0;
  EXPECT_TRUE(ar0.empty());
  EXPECT_EQ(ar0.size(), 0u);

  static const int kTheNumbers[] = {4, 8, 15, 16, 23, 42};
  ArrayRef<int> ar1(kTheNumbers);
  EXPECT_FALSE(ar1.empty());
  EXPECT_EQ(ar1.size(), ARRAY_SIZE(kTheNumbers));

  ArrayRef<int> ar2(&kTheNumbers[0], &kTheNumbers[2]);
  EXPECT_FALSE(ar2.empty());
  EXPECT_EQ(ar2.size(), 2u);

  ArrayRef<int> ar3(&kTheNumbers[0], 3);
  EXPECT_FALSE(ar3.empty());
  EXPECT_EQ(ar3.size(), 3u);

  std::vector<int> v(4, 1);
  ArrayRef<int> ar4(v);
  EXPECT_FALSE(ar4.empty());
  EXPECT_EQ(ar4.size(), 4u);

  int n;
  ArrayRef<int> ar5(n);
  EXPECT_FALSE(ar5.empty());
  EXPECT_EQ(ar5.size(), 1u);
}

TEST(ArrayRefTest, DropBack) {
  static const int kTheNumbers[] = {4, 8, 15, 16, 23, 42};
  ArrayRef<int> ar1(kTheNumbers);
  ArrayRef<int> ar2(kTheNumbers, ar1.size() - 1);
  EXPECT_TRUE(ar1.drop_back().equals(ar2));
}

TEST(ArrayRefTest, DropFront) {
  static const int kTheNumbers[] = {4, 8, 15, 16, 23, 42};
  ArrayRef<int> ar1(kTheNumbers);
  ArrayRef<int> ar2(&kTheNumbers[2], ar1.size() - 2);
  EXPECT_TRUE(ar1.drop_front(2).equals(ar2));
}

TEST(ArrayRefTest, TakeBack) {
  static const int kTheNumbers[] = {4, 8, 15, 16, 23, 42};
  ArrayRef<int> ar1(kTheNumbers);
  ArrayRef<int> ar2(ar1.end() - 1, 1);
  EXPECT_TRUE(ar1.take_back().equals(ar2));
}

TEST(ArrayRefTest, TakeFront) {
  static const int kTheNumbers[] = {4, 8, 15, 16, 23, 42};
  ArrayRef<int> ar1(kTheNumbers);
  ArrayRef<int> ar2(ar1.data(), 2);
  EXPECT_TRUE(ar1.take_front(2).equals(ar2));
}

TEST(ArrayRefTest, Equals) {
  static const int kA1[] = {1, 2, 3, 4, 5, 6, 7, 8};
  ArrayRef<int> ar1(kA1);
  EXPECT_TRUE(ar1.equals(std::vector<int>({1, 2, 3, 4, 5, 6, 7, 8})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({8, 1, 2, 4, 5, 6, 6, 7})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({2, 4, 5, 6, 6, 7, 8, 1})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({0, 1, 2, 4, 5, 6, 6, 7})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({1, 2, 42, 4, 5, 6, 7, 8})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({42, 2, 3, 4, 5, 6, 7, 8})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({1, 2, 3, 4, 5, 6, 7, 42})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({1, 2, 3, 4, 5, 6, 7})));
  EXPECT_FALSE(ar1.equals(std::vector<int>({1, 2, 3, 4, 5, 6, 7, 8, 9})));

  ArrayRef<int> ar1_a = ar1.drop_back();
  EXPECT_TRUE(ar1_a.equals(std::vector<int>({1, 2, 3, 4, 5, 6, 7})));
  EXPECT_FALSE(ar1_a.equals(std::vector<int>({1, 2, 3, 4, 5, 6, 7, 8})));

  ArrayRef<int> ar1_b = ar1_a.slice(2, 4);
  EXPECT_TRUE(ar1_b.equals(std::vector<int>({3, 4, 5, 6})));
  EXPECT_FALSE(ar1_b.equals(std::vector<int>({2, 3, 4, 5, 6})));
  EXPECT_FALSE(ar1_b.equals(std::vector<int>({3, 4, 5, 6, 7})));
}

TEST(ArrayRefTest, EmptyEquals) {
  EXPECT_TRUE(ArrayRef<unsigned>() == ArrayRef<unsigned>());
}

TEST(ArrayRefTest, ConstConvert) {
  int buf[4];
  for (int i = 0; i < 4; ++i) buf[i] = i;

  static int *ptrs[] = {&buf[0], &buf[1], &buf[2], &buf[3]};
  ArrayRef<const int *> a((ArrayRef<int *>(ptrs)));
  a = ArrayRef<int *>(ptrs);
}

TEST(ArrayRefTest, ArrayRef) {
  static const int kA1[] = {1, 2, 3, 4, 5, 6, 7, 8};

  // A copy is expected for non-const ArrayRef (thin copy)
  ArrayRef<int> ar1(kA1);
  const ArrayRef<int> &ar1_ref = ArrayRef<int>(ar1);
  EXPECT_NE(&ar1, &ar1_ref);
  EXPECT_TRUE(ar1.equals(ar1_ref));

  // A copy is expected for non-const ArrayRef (thin copy)
  const ArrayRef<int> ar2(kA1);
  const ArrayRef<int> &ar2_ref = ArrayRef<int>(ar2);
  EXPECT_NE(&ar2_ref, &ar2);
  EXPECT_TRUE(ar2.equals(ar2_ref));
}

static_assert(std::is_trivially_copyable_v<ArrayRef<int>>,
              "trivially copyable");

}  // namespace
