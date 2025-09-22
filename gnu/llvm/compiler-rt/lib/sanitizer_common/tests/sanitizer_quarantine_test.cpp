//===-- sanitizer_quarantine_test.cpp -------------------------------------===//
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
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_quarantine.h"
#include "gtest/gtest.h"

#include <stdlib.h>

namespace __sanitizer {

struct QuarantineCallback {
  void Recycle(void *m) {}
  void *Allocate(uptr size) {
    return malloc(size);
  }
  void Deallocate(void *p) {
    free(p);
  }
};

typedef QuarantineCache<QuarantineCallback> Cache;

static void* kFakePtr = reinterpret_cast<void*>(0xFA83FA83);
static const size_t kBlockSize = 8;

static QuarantineCallback cb;

static void DeallocateCache(Cache *cache) {
  while (QuarantineBatch *batch = cache->DequeueBatch())
    cb.Deallocate(batch);
}

TEST(SanitizerCommon, QuarantineBatchMerge) {
  // Verify the trivial case.
  QuarantineBatch into;
  into.init(kFakePtr, 4UL);
  QuarantineBatch from;
  from.init(kFakePtr, 8UL);

  into.merge(&from);

  ASSERT_EQ(into.count, 2UL);
  ASSERT_EQ(into.batch[0], kFakePtr);
  ASSERT_EQ(into.batch[1], kFakePtr);
  ASSERT_EQ(into.size, 12UL + sizeof(QuarantineBatch));
  ASSERT_EQ(into.quarantined_size(), 12UL);

  ASSERT_EQ(from.count, 0UL);
  ASSERT_EQ(from.size, sizeof(QuarantineBatch));
  ASSERT_EQ(from.quarantined_size(), 0UL);

  // Merge the batch to the limit.
  for (uptr i = 2; i < QuarantineBatch::kSize; ++i)
    from.push_back(kFakePtr, 8UL);
  ASSERT_TRUE(into.count + from.count == QuarantineBatch::kSize);
  ASSERT_TRUE(into.can_merge(&from));

  into.merge(&from);
  ASSERT_TRUE(into.count == QuarantineBatch::kSize);

  // No more space, not even for one element.
  from.init(kFakePtr, 8UL);

  ASSERT_FALSE(into.can_merge(&from));
}

TEST(SanitizerCommon, QuarantineCacheMergeBatchesEmpty) {
  Cache cache;
  Cache to_deallocate;
  cache.MergeBatches(&to_deallocate);

  ASSERT_EQ(to_deallocate.Size(), 0UL);
  ASSERT_EQ(to_deallocate.DequeueBatch(), nullptr);
}

TEST(SanitizerCommon, QuarantineCacheMergeBatchesOneBatch) {
  Cache cache;
  cache.Enqueue(cb, kFakePtr, kBlockSize);
  ASSERT_EQ(kBlockSize + sizeof(QuarantineBatch), cache.Size());

  Cache to_deallocate;
  cache.MergeBatches(&to_deallocate);

  // Nothing to merge, nothing to deallocate.
  ASSERT_EQ(kBlockSize + sizeof(QuarantineBatch), cache.Size());

  ASSERT_EQ(to_deallocate.Size(), 0UL);
  ASSERT_EQ(to_deallocate.DequeueBatch(), nullptr);

  DeallocateCache(&cache);
}

TEST(SanitizerCommon, QuarantineCacheMergeBatchesSmallBatches) {
  // Make a cache with two batches small enough to merge.
  Cache from;
  from.Enqueue(cb, kFakePtr, kBlockSize);
  Cache cache;
  cache.Enqueue(cb, kFakePtr, kBlockSize);

  cache.Transfer(&from);
  ASSERT_EQ(kBlockSize * 2 + sizeof(QuarantineBatch) * 2, cache.Size());

  Cache to_deallocate;
  cache.MergeBatches(&to_deallocate);

  // Batches merged, one batch to deallocate.
  ASSERT_EQ(kBlockSize * 2 + sizeof(QuarantineBatch), cache.Size());
  ASSERT_EQ(to_deallocate.Size(), sizeof(QuarantineBatch));

  DeallocateCache(&cache);
  DeallocateCache(&to_deallocate);
}

TEST(SanitizerCommon, QuarantineCacheMergeBatchesTooBigToMerge) {
  const uptr kNumBlocks = QuarantineBatch::kSize - 1;

  // Make a cache with two batches small enough to merge.
  Cache from;
  Cache cache;
  for (uptr i = 0; i < kNumBlocks; ++i) {
    from.Enqueue(cb, kFakePtr, kBlockSize);
    cache.Enqueue(cb, kFakePtr, kBlockSize);
  }
  cache.Transfer(&from);
  ASSERT_EQ(kBlockSize * kNumBlocks * 2 +
            sizeof(QuarantineBatch) * 2, cache.Size());

  Cache to_deallocate;
  cache.MergeBatches(&to_deallocate);

  // Batches cannot be merged.
  ASSERT_EQ(kBlockSize * kNumBlocks * 2 +
            sizeof(QuarantineBatch) * 2, cache.Size());
  ASSERT_EQ(to_deallocate.Size(), 0UL);

  DeallocateCache(&cache);
}

TEST(SanitizerCommon, QuarantineCacheMergeBatchesALotOfBatches) {
  const uptr kNumBatchesAfterMerge = 3;
  const uptr kNumBlocks = QuarantineBatch::kSize * kNumBatchesAfterMerge;
  const uptr kNumBatchesBeforeMerge = kNumBlocks;

  // Make a cache with many small batches.
  Cache cache;
  for (uptr i = 0; i < kNumBlocks; ++i) {
    Cache from;
    from.Enqueue(cb, kFakePtr, kBlockSize);
    cache.Transfer(&from);
  }

  ASSERT_EQ(kBlockSize * kNumBlocks +
            sizeof(QuarantineBatch) * kNumBatchesBeforeMerge, cache.Size());

  Cache to_deallocate;
  cache.MergeBatches(&to_deallocate);

  // All blocks should fit into 3 batches.
  ASSERT_EQ(kBlockSize * kNumBlocks +
            sizeof(QuarantineBatch) * kNumBatchesAfterMerge, cache.Size());

  ASSERT_EQ(to_deallocate.Size(),
            sizeof(QuarantineBatch) *
                (kNumBatchesBeforeMerge - kNumBatchesAfterMerge));

  DeallocateCache(&cache);
  DeallocateCache(&to_deallocate);
}

}  // namespace __sanitizer
