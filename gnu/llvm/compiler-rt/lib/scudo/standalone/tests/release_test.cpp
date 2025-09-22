//===-- release_test.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "list.h"
#include "release.h"
#include "size_class_map.h"

#include <string.h>

#include <algorithm>
#include <random>
#include <set>

TEST(ScudoReleaseTest, RegionPageMap) {
  for (scudo::uptr I = 0; I < SCUDO_WORDSIZE; I++) {
    // Various valid counter's max values packed into one word.
    scudo::RegionPageMap PageMap2N(1U, 1U, 1UL << I);
    ASSERT_TRUE(PageMap2N.isAllocated());
    EXPECT_EQ(1U, PageMap2N.getBufferNumElements());
    // Check the "all bit set" values too.
    scudo::RegionPageMap PageMap2N1_1(1U, 1U, ~0UL >> I);
    ASSERT_TRUE(PageMap2N1_1.isAllocated());
    EXPECT_EQ(1U, PageMap2N1_1.getBufferNumElements());
    // Verify the packing ratio, the counter is Expected to be packed into the
    // closest power of 2 bits.
    scudo::RegionPageMap PageMap(1U, SCUDO_WORDSIZE, 1UL << I);
    ASSERT_TRUE(PageMap.isAllocated());
    EXPECT_EQ(scudo::roundUpPowerOfTwo(I + 1), PageMap.getBufferNumElements());
  }

  // Go through 1, 2, 4, 8, .. {32,64} bits per counter.
  for (scudo::uptr I = 0; (SCUDO_WORDSIZE >> I) != 0; I++) {
    // Make sure counters request one memory page for the buffer.
    const scudo::uptr NumCounters =
        (scudo::getPageSizeCached() / 8) * (SCUDO_WORDSIZE >> I);
    scudo::RegionPageMap PageMap(1U, NumCounters,
                                       1UL << ((1UL << I) - 1));
    ASSERT_TRUE(PageMap.isAllocated());
    PageMap.inc(0U, 0U);
    for (scudo::uptr C = 1; C < NumCounters - 1; C++) {
      EXPECT_EQ(0UL, PageMap.get(0U, C));
      PageMap.inc(0U, C);
      EXPECT_EQ(1UL, PageMap.get(0U, C - 1));
    }
    EXPECT_EQ(0UL, PageMap.get(0U, NumCounters - 1));
    PageMap.inc(0U, NumCounters - 1);
    if (I > 0) {
      PageMap.incRange(0u, 0U, NumCounters - 1);
      for (scudo::uptr C = 0; C < NumCounters; C++)
        EXPECT_EQ(2UL, PageMap.get(0U, C));
    }
  }

  // Similar to the above except that we are using incN().
  for (scudo::uptr I = 0; (SCUDO_WORDSIZE >> I) != 0; I++) {
    // Make sure counters request one memory page for the buffer.
    const scudo::uptr NumCounters =
        (scudo::getPageSizeCached() / 8) * (SCUDO_WORDSIZE >> I);
    scudo::uptr MaxValue = 1UL << ((1UL << I) - 1);
    if (MaxValue <= 1U)
      continue;

    scudo::RegionPageMap PageMap(1U, NumCounters, MaxValue);

    scudo::uptr N = MaxValue / 2;
    PageMap.incN(0U, 0, N);
    for (scudo::uptr C = 1; C < NumCounters; C++) {
      EXPECT_EQ(0UL, PageMap.get(0U, C));
      PageMap.incN(0U, C, N);
      EXPECT_EQ(N, PageMap.get(0U, C - 1));
    }
    EXPECT_EQ(N, PageMap.get(0U, NumCounters - 1));
  }
}

class StringRangeRecorder {
public:
  std::string ReportedPages;

  StringRangeRecorder()
      : PageSizeScaledLog(scudo::getLog2(scudo::getPageSizeCached())) {}

  void releasePageRangeToOS(scudo::uptr From, scudo::uptr To) {
    From >>= PageSizeScaledLog;
    To >>= PageSizeScaledLog;
    EXPECT_LT(From, To);
    if (!ReportedPages.empty())
      EXPECT_LT(LastPageReported, From);
    ReportedPages.append(From - LastPageReported, '.');
    ReportedPages.append(To - From, 'x');
    LastPageReported = To;
  }

private:
  const scudo::uptr PageSizeScaledLog;
  scudo::uptr LastPageReported = 0;
};

TEST(ScudoReleaseTest, FreePagesRangeTracker) {
  // 'x' denotes a page to be released, '.' denotes a page to be kept around.
  const char *TestCases[] = {
      "",
      ".",
      "x",
      "........",
      "xxxxxxxxxxx",
      "..............xxxxx",
      "xxxxxxxxxxxxxxxxxx.....",
      "......xxxxxxxx........",
      "xxx..........xxxxxxxxxxxxxxx",
      "......xxxx....xxxx........",
      "xxx..........xxxxxxxx....xxxxxxx",
      "x.x.x.x.x.x.x.x.x.x.x.x.",
      ".x.x.x.x.x.x.x.x.x.x.x.x",
      ".x.x.x.x.x.x.x.x.x.x.x.x.",
      "x.x.x.x.x.x.x.x.x.x.x.x.x",
  };
  typedef scudo::FreePagesRangeTracker<StringRangeRecorder> RangeTracker;

  for (auto TestCase : TestCases) {
    StringRangeRecorder Recorder;
    RangeTracker Tracker(Recorder);
    for (scudo::uptr I = 0; TestCase[I] != 0; I++)
      Tracker.processNextPage(TestCase[I] == 'x');
    Tracker.finish();
    // Strip trailing '.'-pages before comparing the results as they are not
    // going to be reported to range_recorder anyway.
    const char *LastX = strrchr(TestCase, 'x');
    std::string Expected(
        TestCase,
        LastX == nullptr ? 0U : static_cast<size_t>(LastX - TestCase + 1));
    EXPECT_STREQ(Expected.c_str(), Recorder.ReportedPages.c_str());
  }
}

class ReleasedPagesRecorder {
public:
  ReleasedPagesRecorder() = default;
  explicit ReleasedPagesRecorder(scudo::uptr Base) : Base(Base) {}
  std::set<scudo::uptr> ReportedPages;

  void releasePageRangeToOS(scudo::uptr From, scudo::uptr To) {
    const scudo::uptr PageSize = scudo::getPageSizeCached();
    for (scudo::uptr I = From; I < To; I += PageSize)
      ReportedPages.insert(I + getBase());
  }

  scudo::uptr getBase() const { return Base; }
  scudo::uptr Base = 0;
};

// Simplified version of a TransferBatch.
template <class SizeClassMap> struct FreeBatch {
  static const scudo::u16 MaxCount = SizeClassMap::MaxNumCachedHint;
  void clear() { Count = 0; }
  void add(scudo::uptr P) {
    DCHECK_LT(Count, MaxCount);
    Batch[Count++] = P;
  }
  scudo::u16 getCount() const { return Count; }
  scudo::uptr get(scudo::u16 I) const {
    DCHECK_LE(I, Count);
    return Batch[I];
  }
  FreeBatch *Next;

private:
  scudo::uptr Batch[MaxCount];
  scudo::u16 Count;
};

template <class SizeClassMap> void testReleaseFreeMemoryToOS() {
  typedef FreeBatch<SizeClassMap> Batch;
  const scudo::uptr PagesCount = 1024;
  const scudo::uptr PageSize = scudo::getPageSizeCached();
  const scudo::uptr PageSizeLog = scudo::getLog2(PageSize);
  std::mt19937 R;
  scudo::u32 RandState = 42;

  for (scudo::uptr I = 1; I <= SizeClassMap::LargestClassId; I++) {
    const scudo::uptr BlockSize = SizeClassMap::getSizeByClassId(I);
    const scudo::uptr MaxBlocks = PagesCount * PageSize / BlockSize;

    // Generate the random free list.
    std::vector<scudo::uptr> FreeArray;
    bool InFreeRange = false;
    scudo::uptr CurrentRangeEnd = 0;
    for (scudo::uptr I = 0; I < MaxBlocks; I++) {
      if (I == CurrentRangeEnd) {
        InFreeRange = (scudo::getRandomU32(&RandState) & 1U) == 1;
        CurrentRangeEnd += (scudo::getRandomU32(&RandState) & 0x7f) + 1;
      }
      if (InFreeRange)
        FreeArray.push_back(I * BlockSize);
    }
    if (FreeArray.empty())
      continue;
    // Shuffle the array to ensure that the order is irrelevant.
    std::shuffle(FreeArray.begin(), FreeArray.end(), R);

    // Build the FreeList from the FreeArray.
    scudo::SinglyLinkedList<Batch> FreeList;
    FreeList.clear();
    Batch *CurrentBatch = nullptr;
    for (auto const &Block : FreeArray) {
      if (!CurrentBatch) {
        CurrentBatch = new Batch;
        CurrentBatch->clear();
        FreeList.push_back(CurrentBatch);
      }
      CurrentBatch->add(Block);
      if (CurrentBatch->getCount() == Batch::MaxCount)
        CurrentBatch = nullptr;
    }

    // Release the memory.
    auto SkipRegion = [](UNUSED scudo::uptr RegionIndex) { return false; };
    auto DecompactPtr = [](scudo::uptr P) { return P; };
    ReleasedPagesRecorder Recorder;
    scudo::PageReleaseContext Context(BlockSize, /*NumberOfRegions=*/1U,
                                      /*ReleaseSize=*/MaxBlocks * BlockSize);
    ASSERT_FALSE(Context.hasBlockMarked());
    Context.markFreeBlocksInRegion(FreeList, DecompactPtr, Recorder.getBase(),
                                   /*RegionIndex=*/0, MaxBlocks * BlockSize,
                                   /*MayContainLastBlockInRegion=*/true);
    ASSERT_TRUE(Context.hasBlockMarked());
    releaseFreeMemoryToOS(Context, Recorder, SkipRegion);
    scudo::RegionPageMap &PageMap = Context.PageMap;

    // Verify that there are no released pages touched by used chunks and all
    // ranges of free chunks big enough to contain the entire memory pages had
    // these pages released.
    scudo::uptr VerifiedReleasedPages = 0;
    std::set<scudo::uptr> FreeBlocks(FreeArray.begin(), FreeArray.end());

    scudo::uptr CurrentBlock = 0;
    InFreeRange = false;
    scudo::uptr CurrentFreeRangeStart = 0;
    for (scudo::uptr I = 0; I < MaxBlocks; I++) {
      const bool IsFreeBlock =
          FreeBlocks.find(CurrentBlock) != FreeBlocks.end();
      if (IsFreeBlock) {
        if (!InFreeRange) {
          InFreeRange = true;
          CurrentFreeRangeStart = CurrentBlock;
        }
      } else {
        // Verify that this used chunk does not touch any released page.
        const scudo::uptr StartPage = CurrentBlock / PageSize;
        const scudo::uptr EndPage = (CurrentBlock + BlockSize - 1) / PageSize;
        for (scudo::uptr J = StartPage; J <= EndPage; J++) {
          const bool PageReleased = Recorder.ReportedPages.find(J * PageSize) !=
                                    Recorder.ReportedPages.end();
          EXPECT_EQ(false, PageReleased);
          EXPECT_EQ(false,
                    PageMap.isAllCounted(0, (J * PageSize) >> PageSizeLog));
        }

        if (InFreeRange) {
          InFreeRange = false;
          // Verify that all entire memory pages covered by this range of free
          // chunks were released.
          scudo::uptr P = scudo::roundUp(CurrentFreeRangeStart, PageSize);
          while (P + PageSize <= CurrentBlock) {
            const bool PageReleased =
                Recorder.ReportedPages.find(P) != Recorder.ReportedPages.end();
            EXPECT_EQ(true, PageReleased);
            EXPECT_EQ(true, PageMap.isAllCounted(0, P >> PageSizeLog));
            VerifiedReleasedPages++;
            P += PageSize;
          }
        }
      }

      CurrentBlock += BlockSize;
    }

    if (InFreeRange) {
      scudo::uptr P = scudo::roundUp(CurrentFreeRangeStart, PageSize);
      const scudo::uptr EndPage =
          scudo::roundUp(MaxBlocks * BlockSize, PageSize);
      while (P + PageSize <= EndPage) {
        const bool PageReleased =
            Recorder.ReportedPages.find(P) != Recorder.ReportedPages.end();
        EXPECT_EQ(true, PageReleased);
        EXPECT_EQ(true, PageMap.isAllCounted(0, P >> PageSizeLog));
        VerifiedReleasedPages++;
        P += PageSize;
      }
    }

    EXPECT_EQ(Recorder.ReportedPages.size(), VerifiedReleasedPages);

    while (!FreeList.empty()) {
      CurrentBatch = FreeList.front();
      FreeList.pop_front();
      delete CurrentBatch;
    }
  }
}

template <class SizeClassMap> void testPageMapMarkRange() {
  const scudo::uptr PageSize = scudo::getPageSizeCached();

  for (scudo::uptr I = 1; I <= SizeClassMap::LargestClassId; I++) {
    const scudo::uptr BlockSize = SizeClassMap::getSizeByClassId(I);

    const scudo::uptr GroupNum = 2;
    const scudo::uptr GroupSize = scudo::roundUp(BlockSize, PageSize) * 2;
    const scudo::uptr RegionSize =
        scudo::roundUpSlow(GroupSize * GroupNum, BlockSize);
    const scudo::uptr RoundedRegionSize = scudo::roundUp(RegionSize, PageSize);

    std::vector<scudo::uptr> Pages(RoundedRegionSize / PageSize, 0);
    for (scudo::uptr Block = 0; Block < RoundedRegionSize; Block += BlockSize) {
      for (scudo::uptr Page = Block / PageSize;
           Page <= (Block + BlockSize - 1) / PageSize &&
           Page < RoundedRegionSize / PageSize;
           ++Page) {
        ASSERT_LT(Page, Pages.size());
        ++Pages[Page];
      }
    }

    for (scudo::uptr GroupId = 0; GroupId < GroupNum; ++GroupId) {
      const scudo::uptr GroupBeg = GroupId * GroupSize;
      const scudo::uptr GroupEnd = GroupBeg + GroupSize;

      scudo::PageReleaseContext Context(BlockSize, /*NumberOfRegions=*/1U,
                                        /*ReleaseSize=*/RegionSize);
      Context.markRangeAsAllCounted(GroupBeg, GroupEnd, /*Base=*/0U,
                                    /*RegionIndex=*/0, RegionSize);

      scudo::uptr FirstBlock =
          ((GroupBeg + BlockSize - 1) / BlockSize) * BlockSize;

      // All the pages before first block page are not supposed to be marked.
      if (FirstBlock / PageSize > 0) {
        for (scudo::uptr Page = 0; Page <= FirstBlock / PageSize - 1; ++Page)
          EXPECT_EQ(Context.PageMap.get(/*Region=*/0, Page), 0U);
      }

      // Verify the pages used by the blocks in the group except that if the
      // end of the last block is not aligned with `GroupEnd`, it'll be verified
      // later.
      scudo::uptr Block;
      for (Block = FirstBlock; Block + BlockSize <= GroupEnd;
           Block += BlockSize) {
        for (scudo::uptr Page = Block / PageSize;
             Page <= (Block + BlockSize - 1) / PageSize; ++Page) {
          // First used page in the group has two cases, which are w/ and w/o
          // block sitting across the boundary.
          if (Page == FirstBlock / PageSize) {
            if (FirstBlock % PageSize == 0) {
              EXPECT_TRUE(Context.PageMap.isAllCounted(/*Region=*/0U, Page));
            } else {
              // There's a block straddling `GroupBeg`, it's supposed to only
              // increment the counter and we expect it should be 1 less
              // (exclude the straddling one) than the total blocks on the page.
              EXPECT_EQ(Context.PageMap.get(/*Region=*/0U, Page),
                        Pages[Page] - 1);
            }
          } else {
            EXPECT_TRUE(Context.PageMap.isAllCounted(/*Region=*/0, Page));
          }
        }
      }

      if (Block == GroupEnd)
        continue;

      // Examine the last block which sits across the group boundary.
      if (Block + BlockSize == RegionSize) {
        // This is the last block in the region, it's supposed to mark all the
        // pages as all counted.
        for (scudo::uptr Page = Block / PageSize;
             Page <= (Block + BlockSize - 1) / PageSize; ++Page) {
          EXPECT_TRUE(Context.PageMap.isAllCounted(/*Region=*/0, Page));
        }
      } else {
        for (scudo::uptr Page = Block / PageSize;
             Page <= (Block + BlockSize - 1) / PageSize; ++Page) {
          if (Page <= (GroupEnd - 1) / PageSize)
            EXPECT_TRUE(Context.PageMap.isAllCounted(/*Region=*/0, Page));
          else
            EXPECT_EQ(Context.PageMap.get(/*Region=*/0U, Page), 1U);
        }
      }

      const scudo::uptr FirstUncountedPage =
          scudo::roundUp(Block + BlockSize, PageSize);
      for (scudo::uptr Page = FirstUncountedPage;
           Page <= RoundedRegionSize / PageSize; ++Page) {
        EXPECT_EQ(Context.PageMap.get(/*Region=*/0U, Page), 0U);
      }
    } // Iterate each Group

    // Release the entire region. This is to ensure the last page is counted.
    scudo::PageReleaseContext Context(BlockSize, /*NumberOfRegions=*/1U,
                                      /*ReleaseSize=*/RegionSize);
    Context.markRangeAsAllCounted(/*From=*/0U, /*To=*/RegionSize, /*Base=*/0,
                                  /*RegionIndex=*/0, RegionSize);
    for (scudo::uptr Page = 0; Page < RoundedRegionSize / PageSize; ++Page)
      EXPECT_TRUE(Context.PageMap.isAllCounted(/*Region=*/0, Page));
  } // Iterate each size class
}

template <class SizeClassMap> void testReleasePartialRegion() {
  typedef FreeBatch<SizeClassMap> Batch;
  const scudo::uptr PageSize = scudo::getPageSizeCached();

  for (scudo::uptr I = 1; I <= SizeClassMap::LargestClassId; I++) {
    // In the following, we want to ensure the region includes at least 2 pages
    // and we will release all the pages except the first one. The handling of
    // the last block is tricky, so we always test the case that includes the
    // last block.
    const scudo::uptr BlockSize = SizeClassMap::getSizeByClassId(I);
    const scudo::uptr ReleaseBase = scudo::roundUp(BlockSize, PageSize);
    const scudo::uptr BasePageOffset = ReleaseBase / PageSize;
    const scudo::uptr RegionSize =
        scudo::roundUpSlow(scudo::roundUp(BlockSize, PageSize) + ReleaseBase,
                           BlockSize) +
        BlockSize;
    const scudo::uptr RoundedRegionSize = scudo::roundUp(RegionSize, PageSize);

    scudo::SinglyLinkedList<Batch> FreeList;
    FreeList.clear();

    // Skip the blocks in the first page and add the remaining.
    std::vector<scudo::uptr> Pages(RoundedRegionSize / PageSize, 0);
    for (scudo::uptr Block = scudo::roundUpSlow(ReleaseBase, BlockSize);
         Block + BlockSize <= RoundedRegionSize; Block += BlockSize) {
      for (scudo::uptr Page = Block / PageSize;
           Page <= (Block + BlockSize - 1) / PageSize; ++Page) {
        ASSERT_LT(Page, Pages.size());
        ++Pages[Page];
      }
    }

    // This follows the logic how we count the last page. It should be
    // consistent with how markFreeBlocksInRegion() handles the last block.
    if (RoundedRegionSize % BlockSize != 0)
      ++Pages.back();

    Batch *CurrentBatch = nullptr;
    for (scudo::uptr Block = scudo::roundUpSlow(ReleaseBase, BlockSize);
         Block < RegionSize; Block += BlockSize) {
      if (CurrentBatch == nullptr ||
          CurrentBatch->getCount() == Batch::MaxCount) {
        CurrentBatch = new Batch;
        CurrentBatch->clear();
        FreeList.push_back(CurrentBatch);
      }
      CurrentBatch->add(Block);
    }

    auto VerifyReleaseToOs = [&](scudo::PageReleaseContext &Context) {
      auto SkipRegion = [](UNUSED scudo::uptr RegionIndex) { return false; };
      ReleasedPagesRecorder Recorder(ReleaseBase);
      releaseFreeMemoryToOS(Context, Recorder, SkipRegion);
      const scudo::uptr FirstBlock = scudo::roundUpSlow(ReleaseBase, BlockSize);

      for (scudo::uptr P = 0; P < RoundedRegionSize; P += PageSize) {
        if (P < FirstBlock) {
          // If FirstBlock is not aligned with page boundary, the first touched
          // page will not be released either.
          EXPECT_TRUE(Recorder.ReportedPages.find(P) ==
                      Recorder.ReportedPages.end());
        } else {
          EXPECT_TRUE(Recorder.ReportedPages.find(P) !=
                      Recorder.ReportedPages.end());
        }
      }
    };

    // Test marking by visiting each block.
    {
      auto DecompactPtr = [](scudo::uptr P) { return P; };
      scudo::PageReleaseContext Context(BlockSize, /*NumberOfRegions=*/1U,
                                        /*ReleaseSize=*/RegionSize - PageSize,
                                        ReleaseBase);
      Context.markFreeBlocksInRegion(FreeList, DecompactPtr, /*Base=*/0U,
                                     /*RegionIndex=*/0, RegionSize,
                                     /*MayContainLastBlockInRegion=*/true);
      for (const Batch &It : FreeList) {
        for (scudo::u16 I = 0; I < It.getCount(); I++) {
          scudo::uptr Block = It.get(I);
          for (scudo::uptr Page = Block / PageSize;
               Page <= (Block + BlockSize - 1) / PageSize; ++Page) {
            EXPECT_EQ(Pages[Page], Context.PageMap.get(/*Region=*/0U,
                                                       Page - BasePageOffset));
          }
        }
      }

      VerifyReleaseToOs(Context);
    }

    // Test range marking.
    {
      scudo::PageReleaseContext Context(BlockSize, /*NumberOfRegions=*/1U,
                                        /*ReleaseSize=*/RegionSize - PageSize,
                                        ReleaseBase);
      Context.markRangeAsAllCounted(ReleaseBase, RegionSize, /*Base=*/0U,
                                    /*RegionIndex=*/0, RegionSize);
      for (scudo::uptr Page = ReleaseBase / PageSize;
           Page < RoundedRegionSize / PageSize; ++Page) {
        if (Context.PageMap.get(/*Region=*/0, Page - BasePageOffset) !=
            Pages[Page]) {
          EXPECT_TRUE(Context.PageMap.isAllCounted(/*Region=*/0,
                                                   Page - BasePageOffset));
        }
      }

      VerifyReleaseToOs(Context);
    }

    // Check the buffer size of PageMap.
    {
      scudo::PageReleaseContext Full(BlockSize, /*NumberOfRegions=*/1U,
                                     /*ReleaseSize=*/RegionSize);
      Full.ensurePageMapAllocated();
      scudo::PageReleaseContext Partial(BlockSize, /*NumberOfRegions=*/1U,
                                        /*ReleaseSize=*/RegionSize - PageSize,
                                        ReleaseBase);
      Partial.ensurePageMapAllocated();

      EXPECT_GE(Full.PageMap.getBufferNumElements(),
                Partial.PageMap.getBufferNumElements());
    }

    while (!FreeList.empty()) {
      CurrentBatch = FreeList.front();
      FreeList.pop_front();
      delete CurrentBatch;
    }
  } // Iterate each size class
}

TEST(ScudoReleaseTest, ReleaseFreeMemoryToOSDefault) {
  testReleaseFreeMemoryToOS<scudo::DefaultSizeClassMap>();
}

TEST(ScudoReleaseTest, ReleaseFreeMemoryToOSAndroid) {
  testReleaseFreeMemoryToOS<scudo::AndroidSizeClassMap>();
}

TEST(ScudoReleaseTest, PageMapMarkRange) {
  testPageMapMarkRange<scudo::DefaultSizeClassMap>();
  testPageMapMarkRange<scudo::AndroidSizeClassMap>();
  testPageMapMarkRange<scudo::FuchsiaSizeClassMap>();
}

TEST(ScudoReleaseTest, ReleasePartialRegion) {
  testReleasePartialRegion<scudo::DefaultSizeClassMap>();
  testReleasePartialRegion<scudo::AndroidSizeClassMap>();
  testReleasePartialRegion<scudo::FuchsiaSizeClassMap>();
}

template <class SizeClassMap> void testReleaseRangeWithSingleBlock() {
  const scudo::uptr PageSize = scudo::getPageSizeCached();

  // We want to test if a memory group only contains single block that will be
  // handled properly. The case is like:
  //
  //   From                     To
  //     +----------------------+
  //  +------------+------------+
  //  |            |            |
  //  +------------+------------+
  //                            ^
  //                        RegionSize
  //
  // Note that `From` will be page aligned.
  //
  // If the second from the last block is aligned at `From`, then we expect all
  // the pages after `From` will be marked as can-be-released. Otherwise, the
  // pages only touched by the last blocks will be marked as can-be-released.
  for (scudo::uptr I = 1; I <= SizeClassMap::LargestClassId; I++) {
    const scudo::uptr BlockSize = SizeClassMap::getSizeByClassId(I);
    const scudo::uptr From = scudo::roundUp(BlockSize, PageSize);
    const scudo::uptr To =
        From % BlockSize == 0
            ? From + BlockSize
            : scudo::roundDownSlow(From + BlockSize, BlockSize) + BlockSize;
    const scudo::uptr RoundedRegionSize = scudo::roundUp(To, PageSize);

    std::vector<scudo::uptr> Pages(RoundedRegionSize / PageSize, 0);
    for (scudo::uptr Block = (To - BlockSize); Block < RoundedRegionSize;
         Block += BlockSize) {
      for (scudo::uptr Page = Block / PageSize;
           Page <= (Block + BlockSize - 1) / PageSize &&
           Page < RoundedRegionSize / PageSize;
           ++Page) {
        ASSERT_LT(Page, Pages.size());
        ++Pages[Page];
      }
    }

    scudo::PageReleaseContext Context(BlockSize, /*NumberOfRegions=*/1U,
                                      /*ReleaseSize=*/To,
                                      /*ReleaseBase=*/0U);
    Context.markRangeAsAllCounted(From, To, /*Base=*/0U, /*RegionIndex=*/0,
                                  /*RegionSize=*/To);

    for (scudo::uptr Page = 0; Page < RoundedRegionSize; Page += PageSize) {
      if (Context.PageMap.get(/*Region=*/0U, Page / PageSize) !=
          Pages[Page / PageSize]) {
        EXPECT_TRUE(
            Context.PageMap.isAllCounted(/*Region=*/0U, Page / PageSize));
      }
    }
  } // for each size class
}

TEST(ScudoReleaseTest, RangeReleaseRegionWithSingleBlock) {
  testReleaseRangeWithSingleBlock<scudo::DefaultSizeClassMap>();
  testReleaseRangeWithSingleBlock<scudo::AndroidSizeClassMap>();
  testReleaseRangeWithSingleBlock<scudo::FuchsiaSizeClassMap>();
}

TEST(ScudoReleaseTest, BufferPool) {
  constexpr scudo::uptr StaticBufferCount = SCUDO_WORDSIZE - 1;
  constexpr scudo::uptr StaticBufferNumElements = 512U;

  // Allocate the buffer pool on the heap because it is quite large (slightly
  // more than StaticBufferCount * StaticBufferNumElements * sizeof(uptr)) and
  // it may not fit in the stack on some platforms.
  using BufferPool =
      scudo::BufferPool<StaticBufferCount, StaticBufferNumElements>;
  std::unique_ptr<BufferPool> Pool(new BufferPool());

  std::vector<BufferPool::Buffer> Buffers;
  for (scudo::uptr I = 0; I < StaticBufferCount; ++I) {
    BufferPool::Buffer Buffer = Pool->getBuffer(StaticBufferNumElements);
    EXPECT_TRUE(Pool->isStaticBufferTestOnly(Buffer));
    Buffers.push_back(Buffer);
  }

  // The static buffer is supposed to be used up.
  BufferPool::Buffer Buffer = Pool->getBuffer(StaticBufferNumElements);
  EXPECT_FALSE(Pool->isStaticBufferTestOnly(Buffer));

  Pool->releaseBuffer(Buffer);
  for (auto &Buffer : Buffers)
    Pool->releaseBuffer(Buffer);
}
