//===-- memtag_test.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "common.h"
#include "mem_map.h"
#include "memtag.h"
#include "platform.h"
#include "tests/scudo_unit_test.h"

extern "C" void __hwasan_init() __attribute__((weak));

#if SCUDO_LINUX
namespace scudo {

TEST(MemtagBasicDeathTest, Unsupported) {
  if (archSupportsMemoryTagging())
    TEST_SKIP("Memory tagging is not supported");
  // Skip when running with HWASan.
  if (&__hwasan_init != 0)
    TEST_SKIP("Incompatible with HWASan");

  EXPECT_DEATH(archMemoryTagGranuleSize(), "not supported");
  EXPECT_DEATH(untagPointer((uptr)0), "not supported");
  EXPECT_DEATH(extractTag((uptr)0), "not supported");

  EXPECT_DEATH(systemSupportsMemoryTagging(), "not supported");
  EXPECT_DEATH(systemDetectsMemoryTagFaultsTestOnly(), "not supported");
  EXPECT_DEATH(enableSystemMemoryTaggingTestOnly(), "not supported");

  EXPECT_DEATH(selectRandomTag((uptr)0, 0), "not supported");
  EXPECT_DEATH(addFixedTag((uptr)0, 1), "not supported");
  EXPECT_DEATH(storeTags((uptr)0, (uptr)0 + sizeof(0)), "not supported");
  EXPECT_DEATH(storeTag((uptr)0), "not supported");
  EXPECT_DEATH(loadTag((uptr)0), "not supported");

  EXPECT_DEATH(setRandomTag(nullptr, 64, 0, nullptr, nullptr), "not supported");
  EXPECT_DEATH(untagPointer(nullptr), "not supported");
  EXPECT_DEATH(loadTag(nullptr), "not supported");
  EXPECT_DEATH(addFixedTag(nullptr, 0), "not supported");
}

class MemtagTest : public Test {
protected:
  void SetUp() override {
    if (!archSupportsMemoryTagging() || !systemDetectsMemoryTagFaultsTestOnly())
      TEST_SKIP("Memory tagging is not supported");

    BufferSize = getPageSizeCached();
    ASSERT_FALSE(MemMap.isAllocated());
    ASSERT_TRUE(MemMap.map(/*Addr=*/0U, BufferSize, "MemtagTest", MAP_MEMTAG));
    ASSERT_NE(MemMap.getBase(), 0U);
    Addr = MemMap.getBase();
    Buffer = reinterpret_cast<u8 *>(Addr);
    EXPECT_TRUE(isAligned(Addr, archMemoryTagGranuleSize()));
    EXPECT_EQ(Addr, untagPointer(Addr));
  }

  void TearDown() override {
    if (Buffer) {
      ASSERT_TRUE(MemMap.isAllocated());
      MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
    }
  }

  uptr BufferSize = 0;
  scudo::MemMapT MemMap = {};
  u8 *Buffer = nullptr;
  uptr Addr = 0;
};

using MemtagDeathTest = MemtagTest;

TEST_F(MemtagTest, ArchMemoryTagGranuleSize) {
  EXPECT_GT(archMemoryTagGranuleSize(), 1u);
  EXPECT_TRUE(isPowerOfTwo(archMemoryTagGranuleSize()));
}

TEST_F(MemtagTest, ExtractTag) {
// The test is already skipped on anything other than 64 bit. But
// compiling on 32 bit leads to warnings/errors, so skip compiling the test.
#if defined(__LP64__)
  uptr Tags = 0;
  // Try all value for the top byte and check the tags values are in the
  // expected range.
  for (u64 Top = 0; Top < 0x100; ++Top)
    Tags = Tags | (1u << extractTag(Addr | (Top << 56)));
  EXPECT_EQ(0xffffull, Tags);
#endif
}

TEST_F(MemtagDeathTest, AddFixedTag) {
  for (uptr Tag = 0; Tag < 0x10; ++Tag)
    EXPECT_EQ(Tag, extractTag(addFixedTag(Addr, Tag)));
  if (SCUDO_DEBUG) {
    EXPECT_DEATH(addFixedTag(Addr, 16), "");
    EXPECT_DEATH(addFixedTag(~Addr, 0), "");
  }
}

TEST_F(MemtagTest, UntagPointer) {
  uptr UnTagMask = untagPointer(~uptr(0));
  for (u64 Top = 0; Top < 0x100; ++Top) {
    uptr Ptr = (Addr | (Top << 56)) & UnTagMask;
    EXPECT_EQ(addFixedTag(Ptr, 0), untagPointer(Ptr));
  }
}

TEST_F(MemtagDeathTest, ScopedDisableMemoryTagChecks) {
  u8 *P = reinterpret_cast<u8 *>(addFixedTag(Addr, 1));
  EXPECT_NE(P, Buffer);

  EXPECT_DEATH(*P = 20, "");
  ScopedDisableMemoryTagChecks Disable;
  *P = 10;
}

TEST_F(MemtagTest, SelectRandomTag) {
  for (uptr SrcTag = 0; SrcTag < 0x10; ++SrcTag) {
    uptr Ptr = addFixedTag(Addr, SrcTag);
    uptr Tags = 0;
    for (uptr I = 0; I < 100000; ++I)
      Tags = Tags | (1u << extractTag(selectRandomTag(Ptr, 0)));
    // std::popcnt is C++20
    int PopCnt = 0;
    while (Tags) {
      PopCnt += Tags & 1;
      Tags >>= 1;
    }
    // Random tags are not always very random, and this test is not about PRNG
    // quality.  Anything above half would be satisfactory.
    EXPECT_GE(PopCnt, 8);
  }
}

TEST_F(MemtagTest, SelectRandomTagWithMask) {
// The test is already skipped on anything other than 64 bit. But
// compiling on 32 bit leads to warnings/errors, so skip compiling the test.
#if defined(__LP64__)
  for (uptr j = 0; j < 32; ++j) {
    for (uptr i = 0; i < 1000; ++i)
      EXPECT_NE(j, extractTag(selectRandomTag(Addr, 1ull << j)));
  }
#endif
}

TEST_F(MemtagDeathTest, SKIP_NO_DEBUG(LoadStoreTagUnaligned)) {
  for (uptr P = Addr; P < Addr + 4 * archMemoryTagGranuleSize(); ++P) {
    if (P % archMemoryTagGranuleSize() == 0)
      continue;
    EXPECT_DEATH(loadTag(P), "");
    EXPECT_DEATH(storeTag(P), "");
  }
}

TEST_F(MemtagTest, LoadStoreTag) {
  uptr Base = Addr + 0x100;
  uptr Tagged = addFixedTag(Base, 7);
  storeTag(Tagged);

  EXPECT_EQ(Base - archMemoryTagGranuleSize(),
            loadTag(Base - archMemoryTagGranuleSize()));
  EXPECT_EQ(Tagged, loadTag(Base));
  EXPECT_EQ(Base + archMemoryTagGranuleSize(),
            loadTag(Base + archMemoryTagGranuleSize()));
}

TEST_F(MemtagDeathTest, SKIP_NO_DEBUG(StoreTagsUnaligned)) {
  for (uptr P = Addr; P < Addr + 4 * archMemoryTagGranuleSize(); ++P) {
    uptr Tagged = addFixedTag(P, 5);
    if (Tagged % archMemoryTagGranuleSize() == 0)
      continue;
    EXPECT_DEATH(storeTags(Tagged, Tagged), "");
  }
}

TEST_F(MemtagTest, StoreTags) {
// The test is already skipped on anything other than 64 bit. But
// compiling on 32 bit leads to warnings/errors, so skip compiling the test.
#if defined(__LP64__)
  const uptr MaxTaggedSize = 4 * archMemoryTagGranuleSize();
  for (uptr Size = 0; Size <= MaxTaggedSize; ++Size) {
    uptr NoTagBegin = Addr + archMemoryTagGranuleSize();
    uptr NoTagEnd = NoTagBegin + Size;

    u8 Tag = 5;

    uptr TaggedBegin = addFixedTag(NoTagBegin, Tag);
    uptr TaggedEnd = addFixedTag(NoTagEnd, Tag);

    EXPECT_EQ(roundUp(TaggedEnd, archMemoryTagGranuleSize()),
              storeTags(TaggedBegin, TaggedEnd));

    uptr LoadPtr = Addr;
    // Untagged left granule.
    EXPECT_EQ(LoadPtr, loadTag(LoadPtr));

    for (LoadPtr += archMemoryTagGranuleSize(); LoadPtr < NoTagEnd;
         LoadPtr += archMemoryTagGranuleSize()) {
      EXPECT_EQ(addFixedTag(LoadPtr, 5), loadTag(LoadPtr));
    }

    // Untagged right granule.
    EXPECT_EQ(LoadPtr, loadTag(LoadPtr));

    // Reset tags without using StoreTags.
    MemMap.releasePagesToOS(Addr, BufferSize);
  }
#endif
}

} // namespace scudo

#endif
