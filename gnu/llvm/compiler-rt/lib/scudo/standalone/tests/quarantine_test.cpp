//===-- quarantine_test.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "quarantine.h"

#include <pthread.h>
#include <stdlib.h>

static void *FakePtr = reinterpret_cast<void *>(0xFA83FA83);
static const scudo::uptr BlockSize = 8UL;
static const scudo::uptr LargeBlockSize = 16384UL;

struct QuarantineCallback {
  void recycle(void *P) { EXPECT_EQ(P, FakePtr); }
  void *allocate(scudo::uptr Size) { return malloc(Size); }
  void deallocate(void *P) { free(P); }
};

typedef scudo::GlobalQuarantine<QuarantineCallback, void> QuarantineT;
typedef typename QuarantineT::CacheT CacheT;

static QuarantineCallback Cb;

static void deallocateCache(CacheT *Cache) {
  while (scudo::QuarantineBatch *Batch = Cache->dequeueBatch())
    Cb.deallocate(Batch);
}

TEST(ScudoQuarantineTest, QuarantineBatchMerge) {
  // Verify the trivial case.
  scudo::QuarantineBatch Into;
  Into.init(FakePtr, 4UL);
  scudo::QuarantineBatch From;
  From.init(FakePtr, 8UL);

  Into.merge(&From);

  EXPECT_EQ(Into.Count, 2UL);
  EXPECT_EQ(Into.Batch[0], FakePtr);
  EXPECT_EQ(Into.Batch[1], FakePtr);
  EXPECT_EQ(Into.Size, 12UL + sizeof(scudo::QuarantineBatch));
  EXPECT_EQ(Into.getQuarantinedSize(), 12UL);

  EXPECT_EQ(From.Count, 0UL);
  EXPECT_EQ(From.Size, sizeof(scudo::QuarantineBatch));
  EXPECT_EQ(From.getQuarantinedSize(), 0UL);

  // Merge the batch to the limit.
  for (scudo::uptr I = 2; I < scudo::QuarantineBatch::MaxCount; ++I)
    From.push_back(FakePtr, 8UL);
  EXPECT_TRUE(Into.Count + From.Count == scudo::QuarantineBatch::MaxCount);
  EXPECT_TRUE(Into.canMerge(&From));

  Into.merge(&From);
  EXPECT_TRUE(Into.Count == scudo::QuarantineBatch::MaxCount);

  // No more space, not even for one element.
  From.init(FakePtr, 8UL);

  EXPECT_FALSE(Into.canMerge(&From));
}

TEST(ScudoQuarantineTest, QuarantineCacheMergeBatchesEmpty) {
  CacheT Cache;
  CacheT ToDeallocate;
  Cache.init();
  ToDeallocate.init();
  Cache.mergeBatches(&ToDeallocate);

  EXPECT_EQ(ToDeallocate.getSize(), 0UL);
  EXPECT_EQ(ToDeallocate.dequeueBatch(), nullptr);
}

TEST(SanitizerCommon, QuarantineCacheMergeBatchesOneBatch) {
  CacheT Cache;
  Cache.init();
  Cache.enqueue(Cb, FakePtr, BlockSize);
  EXPECT_EQ(BlockSize + sizeof(scudo::QuarantineBatch), Cache.getSize());

  CacheT ToDeallocate;
  ToDeallocate.init();
  Cache.mergeBatches(&ToDeallocate);

  // Nothing to merge, nothing to deallocate.
  EXPECT_EQ(BlockSize + sizeof(scudo::QuarantineBatch), Cache.getSize());

  EXPECT_EQ(ToDeallocate.getSize(), 0UL);
  EXPECT_EQ(ToDeallocate.dequeueBatch(), nullptr);

  deallocateCache(&Cache);
}

TEST(ScudoQuarantineTest, QuarantineCacheMergeBatchesSmallBatches) {
  // Make a Cache with two batches small enough to merge.
  CacheT From;
  From.init();
  From.enqueue(Cb, FakePtr, BlockSize);
  CacheT Cache;
  Cache.init();
  Cache.enqueue(Cb, FakePtr, BlockSize);

  Cache.transfer(&From);
  EXPECT_EQ(BlockSize * 2 + sizeof(scudo::QuarantineBatch) * 2,
            Cache.getSize());

  CacheT ToDeallocate;
  ToDeallocate.init();
  Cache.mergeBatches(&ToDeallocate);

  // Batches merged, one batch to deallocate.
  EXPECT_EQ(BlockSize * 2 + sizeof(scudo::QuarantineBatch), Cache.getSize());
  EXPECT_EQ(ToDeallocate.getSize(), sizeof(scudo::QuarantineBatch));

  deallocateCache(&Cache);
  deallocateCache(&ToDeallocate);
}

TEST(ScudoQuarantineTest, QuarantineCacheMergeBatchesTooBigToMerge) {
  const scudo::uptr NumBlocks = scudo::QuarantineBatch::MaxCount - 1;

  // Make a Cache with two batches small enough to merge.
  CacheT From;
  CacheT Cache;
  From.init();
  Cache.init();
  for (scudo::uptr I = 0; I < NumBlocks; ++I) {
    From.enqueue(Cb, FakePtr, BlockSize);
    Cache.enqueue(Cb, FakePtr, BlockSize);
  }
  Cache.transfer(&From);
  EXPECT_EQ(BlockSize * NumBlocks * 2 + sizeof(scudo::QuarantineBatch) * 2,
            Cache.getSize());

  CacheT ToDeallocate;
  ToDeallocate.init();
  Cache.mergeBatches(&ToDeallocate);

  // Batches cannot be merged.
  EXPECT_EQ(BlockSize * NumBlocks * 2 + sizeof(scudo::QuarantineBatch) * 2,
            Cache.getSize());
  EXPECT_EQ(ToDeallocate.getSize(), 0UL);

  deallocateCache(&Cache);
}

TEST(ScudoQuarantineTest, QuarantineCacheMergeBatchesALotOfBatches) {
  const scudo::uptr NumBatchesAfterMerge = 3;
  const scudo::uptr NumBlocks =
      scudo::QuarantineBatch::MaxCount * NumBatchesAfterMerge;
  const scudo::uptr NumBatchesBeforeMerge = NumBlocks;

  // Make a Cache with many small batches.
  CacheT Cache;
  Cache.init();
  for (scudo::uptr I = 0; I < NumBlocks; ++I) {
    CacheT From;
    From.init();
    From.enqueue(Cb, FakePtr, BlockSize);
    Cache.transfer(&From);
  }

  EXPECT_EQ(BlockSize * NumBlocks +
                sizeof(scudo::QuarantineBatch) * NumBatchesBeforeMerge,
            Cache.getSize());

  CacheT ToDeallocate;
  ToDeallocate.init();
  Cache.mergeBatches(&ToDeallocate);

  // All blocks should fit Into 3 batches.
  EXPECT_EQ(BlockSize * NumBlocks +
                sizeof(scudo::QuarantineBatch) * NumBatchesAfterMerge,
            Cache.getSize());

  EXPECT_EQ(ToDeallocate.getSize(),
            sizeof(scudo::QuarantineBatch) *
                (NumBatchesBeforeMerge - NumBatchesAfterMerge));

  deallocateCache(&Cache);
  deallocateCache(&ToDeallocate);
}

static const scudo::uptr MaxQuarantineSize = 1024UL << 10; // 1MB
static const scudo::uptr MaxCacheSize = 256UL << 10;       // 256KB

TEST(ScudoQuarantineTest, GlobalQuarantine) {
  QuarantineT Quarantine;
  CacheT Cache;
  Cache.init();
  Quarantine.init(MaxQuarantineSize, MaxCacheSize);
  EXPECT_EQ(Quarantine.getMaxSize(), MaxQuarantineSize);
  EXPECT_EQ(Quarantine.getCacheSize(), MaxCacheSize);

  bool DrainOccurred = false;
  scudo::uptr CacheSize = Cache.getSize();
  EXPECT_EQ(Cache.getSize(), 0UL);
  // We quarantine enough blocks that a drain has to occur. Verify this by
  // looking for a decrease of the size of the cache.
  for (scudo::uptr I = 0; I < 128UL; I++) {
    Quarantine.put(&Cache, Cb, FakePtr, LargeBlockSize);
    if (!DrainOccurred && Cache.getSize() < CacheSize)
      DrainOccurred = true;
    CacheSize = Cache.getSize();
  }
  EXPECT_TRUE(DrainOccurred);

  Quarantine.drainAndRecycle(&Cache, Cb);
  EXPECT_EQ(Cache.getSize(), 0UL);

  scudo::ScopedString Str;
  Quarantine.getStats(&Str);
  Str.output();
}

struct PopulateQuarantineThread {
  pthread_t Thread;
  QuarantineT *Quarantine;
  CacheT Cache;
};

void *populateQuarantine(void *Param) {
  PopulateQuarantineThread *P = static_cast<PopulateQuarantineThread *>(Param);
  P->Cache.init();
  for (scudo::uptr I = 0; I < 128UL; I++)
    P->Quarantine->put(&P->Cache, Cb, FakePtr, LargeBlockSize);
  return 0;
}

TEST(ScudoQuarantineTest, ThreadedGlobalQuarantine) {
  QuarantineT Quarantine;
  Quarantine.init(MaxQuarantineSize, MaxCacheSize);

  const scudo::uptr NumberOfThreads = 32U;
  PopulateQuarantineThread T[NumberOfThreads];
  for (scudo::uptr I = 0; I < NumberOfThreads; I++) {
    T[I].Quarantine = &Quarantine;
    pthread_create(&T[I].Thread, 0, populateQuarantine, &T[I]);
  }
  for (scudo::uptr I = 0; I < NumberOfThreads; I++)
    pthread_join(T[I].Thread, 0);

  scudo::ScopedString Str;
  Quarantine.getStats(&Str);
  Str.output();

  for (scudo::uptr I = 0; I < NumberOfThreads; I++)
    Quarantine.drainAndRecycle(&T[I].Cache, Cb);
}
