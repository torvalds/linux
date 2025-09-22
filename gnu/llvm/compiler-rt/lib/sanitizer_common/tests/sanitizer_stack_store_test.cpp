//===-- sanitizer_stack_store_test.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_stack_store.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include "gtest/gtest.h"
#include "sanitizer_atomic.h"
#include "sanitizer_hash.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

class StackStoreTest : public testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override { store_.TestOnlyUnmap(); }

  template <typename Fn>
  void ForEachTrace(Fn fn, uptr n = 1000000) {
    std::vector<uptr> frames(kStackTraceMax);
    std::iota(frames.begin(), frames.end(), 0x100000);
    MurMur2HashBuilder h(0);
    for (uptr i = 0; i < n; ++i) {
      h.add(i);
      u32 size = h.get() % kStackTraceMax;
      h.add(i);
      uptr tag = h.get() % 256;
      StackTrace s(frames.data(), size, tag);
      if (!s.size && !s.tag)
        continue;
      fn(s);
      if (HasFailure())
        return;
      std::next_permutation(frames.begin(), frames.end());
    };
  }

  using BlockInfo = StackStore::BlockInfo;

  uptr GetTotalFramesCount() const {
    return atomic_load_relaxed(&store_.total_frames_);
  }

  uptr CountReadyToPackBlocks() {
    uptr res = 0;
    for (BlockInfo& b : store_.blocks_) res += b.Stored(0);
    return res;
  }

  uptr CountPackedBlocks() const {
    uptr res = 0;
    for (const BlockInfo& b : store_.blocks_) res += b.IsPacked();
    return res;
  }

  uptr IdToOffset(StackStore::Id id) const { return store_.IdToOffset(id); }

  static constexpr uptr kBlockSizeFrames = StackStore::kBlockSizeFrames;
  static constexpr uptr kBlockSizeBytes = StackStore::kBlockSizeBytes;

  StackStore store_ = {};
};

TEST_F(StackStoreTest, Empty) {
  uptr before = store_.Allocated();
  uptr pack = 0;
  EXPECT_EQ(0u, store_.Store({}, &pack));
  uptr after = store_.Allocated();
  EXPECT_EQ(before, after);
}

TEST_F(StackStoreTest, Basic) {
  std::vector<StackStore::Id> ids;
  ForEachTrace([&](const StackTrace& s) {
    uptr pack = 0;
    ids.push_back(store_.Store(s, &pack));
  });

  auto id = ids.begin();
  ForEachTrace([&](const StackTrace& s) {
    StackTrace trace = store_.Load(*(id++));
    EXPECT_EQ(s.size, trace.size);
    EXPECT_EQ(s.tag, trace.tag);
    EXPECT_EQ(std::vector<uptr>(s.trace, s.trace + s.size),
              std::vector<uptr>(trace.trace, trace.trace + trace.size));
  });
}

TEST_F(StackStoreTest, Allocated) {
  EXPECT_LE(store_.Allocated(), 0x100000u);
  std::vector<StackStore::Id> ids;
  ForEachTrace([&](const StackTrace& s) {
    uptr pack = 0;
    ids.push_back(store_.Store(s, &pack));
  });
  EXPECT_NEAR(store_.Allocated(), FIRST_32_SECOND_64(500000000u, 1000000000u),
              FIRST_32_SECOND_64(50000000u, 100000000u));
  store_.TestOnlyUnmap();
  EXPECT_LE(store_.Allocated(), 0x100000u);
}

TEST_F(StackStoreTest, ReadyToPack) {
  uptr next_pack = kBlockSizeFrames;
  uptr total_ready = 0;
  ForEachTrace(
      [&](const StackTrace& s) {
        uptr pack = 0;
        StackStore::Id id = store_.Store(s, &pack);
        uptr end_idx = IdToOffset(id) + 1 + s.size;
        if (end_idx >= next_pack) {
          EXPECT_EQ(1u, pack);
          next_pack += kBlockSizeFrames;
        } else {
          EXPECT_EQ(0u, pack);
        }
        total_ready += pack;
        EXPECT_EQ(CountReadyToPackBlocks(), total_ready);
      },
      100000);
  EXPECT_EQ(GetTotalFramesCount() / kBlockSizeFrames, total_ready);
}

struct StackStorePackTest : public StackStoreTest,
                            public ::testing::WithParamInterface<
                                std::pair<StackStore::Compression, uptr>> {};

INSTANTIATE_TEST_SUITE_P(
    PackUnpacks, StackStorePackTest,
    ::testing::ValuesIn({
        StackStorePackTest::ParamType(StackStore::Compression::Delta,
                                      FIRST_32_SECOND_64(2, 6)),
        StackStorePackTest::ParamType(StackStore::Compression::LZW,
                                      FIRST_32_SECOND_64(60, 125)),
    }));

TEST_P(StackStorePackTest, PackUnpack) {
  std::vector<StackStore::Id> ids;
  StackStore::Compression type = GetParam().first;
  uptr expected_ratio = GetParam().second;
  ForEachTrace([&](const StackTrace& s) {
    uptr pack = 0;
    ids.push_back(store_.Store(s, &pack));
    if (pack) {
      uptr before = store_.Allocated();
      uptr diff = store_.Pack(type);
      uptr after = store_.Allocated();
      EXPECT_EQ(before - after, diff);
      EXPECT_LT(after, before);
      EXPECT_GE(kBlockSizeBytes / (kBlockSizeBytes - (before - after)),
                expected_ratio);
    }
  });
  uptr packed_blocks = CountPackedBlocks();
  // Unpack random block.
  store_.Load(kBlockSizeFrames * 7 + 123);
  EXPECT_EQ(packed_blocks - 1, CountPackedBlocks());

  // Unpack all blocks.
  auto id = ids.begin();
  ForEachTrace([&](const StackTrace& s) {
    StackTrace trace = store_.Load(*(id++));
    EXPECT_EQ(s.size, trace.size);
    EXPECT_EQ(s.tag, trace.tag);
    EXPECT_EQ(std::vector<uptr>(s.trace, s.trace + s.size),
              std::vector<uptr>(trace.trace, trace.trace + trace.size));
  });
  EXPECT_EQ(0u, CountPackedBlocks());

  EXPECT_EQ(0u, store_.Pack(type));
  EXPECT_EQ(0u, CountPackedBlocks());
}

TEST_P(StackStorePackTest, Failed) {
  MurMur2Hash64Builder h(0);
  StackStore::Compression type = GetParam().first;
  std::vector<uptr> frames(200);
  for (uptr i = 0; i < kBlockSizeFrames * 4 / frames.size(); ++i) {
    for (uptr& f : frames) {
      h.add(1);
      // Make it difficult to pack.
      f = h.get();
    }
    uptr pack = 0;
    store_.Store(StackTrace(frames.data(), frames.size()), &pack);
    if (pack)
      EXPECT_EQ(0u, store_.Pack(type));
  }

  EXPECT_EQ(0u, CountPackedBlocks());
}

}  // namespace __sanitizer
