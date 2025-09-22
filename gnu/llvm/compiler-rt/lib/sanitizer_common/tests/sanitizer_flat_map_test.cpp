//===-- sanitizer_flat_map_test.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_flat_map.h"

#include "gtest/gtest.h"
#include "sanitizer_common/tests/sanitizer_pthread_wrappers.h"

using namespace __sanitizer;

namespace {

struct TestStruct {
  int data[125] = {};
  TestStruct(uptr v = 0) { data[11] = v; }
  bool operator==(const TestStruct &other) const {
    return 0 == memcmp(data, other.data, sizeof(data));
  }
};

template <typename T>
class FlatMapTest : public ::testing::Test {};

using FlatMapTestTypes = ::testing::Types<u8, u64, TestStruct>;
TYPED_TEST_SUITE(FlatMapTest, FlatMapTestTypes, );

TYPED_TEST(FlatMapTest, TwoLevelByteMap) {
  const u64 kSize1 = 1 << 6, kSize2 = 1 << 12;
  const u64 n = kSize1 * kSize2;
  TwoLevelMap<TypeParam, kSize1, kSize2> m;
  m.Init();

  m[7] = {10};
  for (u64 i = 0; i < kSize2; ++i) {
    EXPECT_TRUE(m.contains(i));
  }
  EXPECT_FALSE(m.contains(kSize2));

  for (u64 i = 0; i < n; i += 7) {
    m[i] = TypeParam((i % 100) + 1);
  }
  for (u64 j = 0; j < n; j++) {
    EXPECT_TRUE(m.contains(j));
    if (j % 7)
      EXPECT_EQ(m[j], TypeParam());
    else
      EXPECT_EQ(m[j], TypeParam((j % 100) + 1));
  }

  m.TestOnlyUnmap();
}

template <typename TypeParam, typename AddressSpaceView>
using TestMapASVT = TwoLevelMap<TypeParam, 1 << 8, 1 << 7, AddressSpaceView>;
template <typename TypeParam>
using TestMap = TestMapASVT<TypeParam, LocalAddressSpaceView>;

template <typename TypeParam>
struct TestMapParam {
  TestMap<TypeParam> *m;
  size_t shard;
  size_t num_shards;
};

template <typename TypeParam>
static void *TwoLevelMapUserThread(void *param) {
  TestMapParam<TypeParam> *p = (TestMapParam<TypeParam> *)param;
  for (size_t i = p->shard; i < p->m->size(); i += p->num_shards) {
    TypeParam val = (i % 100) + 1;
    (*p->m)[i] = val;
    EXPECT_EQ((*p->m)[i], val);
  }
  return 0;
}

TYPED_TEST(FlatMapTest, ThreadedTwoLevelByteMap) {
  TestMap<TypeParam> m;
  m.Init();
  static const int kNumThreads = 4;
  pthread_t t[kNumThreads];
  TestMapParam<TypeParam> p[kNumThreads];
  for (int i = 0; i < kNumThreads; i++) {
    p[i].m = &m;
    p[i].shard = i;
    p[i].num_shards = kNumThreads;
    PTHREAD_CREATE(&t[i], 0, TwoLevelMapUserThread<TypeParam>, &p[i]);
  }
  for (int i = 0; i < kNumThreads; i++) PTHREAD_JOIN(t[i], 0);
  m.TestOnlyUnmap();
}

}  // namespace
