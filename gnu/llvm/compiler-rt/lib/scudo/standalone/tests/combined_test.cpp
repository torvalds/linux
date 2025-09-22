//===-- combined_test.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "memtag.h"
#include "stack_depot.h"
#include "tests/scudo_unit_test.h"

#include "allocator_config.h"
#include "chunk.h"
#include "combined.h"
#include "condition_variable.h"
#include "mem_map.h"
#include "size_class_map.h"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <stdlib.h>
#include <thread>
#include <vector>

static constexpr scudo::Chunk::Origin Origin = scudo::Chunk::Origin::Malloc;
static constexpr scudo::uptr MinAlignLog = FIRST_32_SECOND_64(3U, 4U);

// Fuchsia complains that the function is not used.
UNUSED static void disableDebuggerdMaybe() {
#if SCUDO_ANDROID
  // Disable the debuggerd signal handler on Android, without this we can end
  // up spending a significant amount of time creating tombstones.
  signal(SIGSEGV, SIG_DFL);
#endif
}

template <class AllocatorT>
bool isPrimaryAllocation(scudo::uptr Size, scudo::uptr Alignment) {
  const scudo::uptr MinAlignment = 1UL << SCUDO_MIN_ALIGNMENT_LOG;
  if (Alignment < MinAlignment)
    Alignment = MinAlignment;
  const scudo::uptr NeededSize =
      scudo::roundUp(Size, MinAlignment) +
      ((Alignment > MinAlignment) ? Alignment : scudo::Chunk::getHeaderSize());
  return AllocatorT::PrimaryT::canAllocate(NeededSize);
}

template <class AllocatorT>
void checkMemoryTaggingMaybe(AllocatorT *Allocator, void *P, scudo::uptr Size,
                             scudo::uptr Alignment) {
  const scudo::uptr MinAlignment = 1UL << SCUDO_MIN_ALIGNMENT_LOG;
  Size = scudo::roundUp(Size, MinAlignment);
  if (Allocator->useMemoryTaggingTestOnly())
    EXPECT_DEATH(
        {
          disableDebuggerdMaybe();
          reinterpret_cast<char *>(P)[-1] = 'A';
        },
        "");
  if (isPrimaryAllocation<AllocatorT>(Size, Alignment)
          ? Allocator->useMemoryTaggingTestOnly()
          : Alignment == MinAlignment) {
    EXPECT_DEATH(
        {
          disableDebuggerdMaybe();
          reinterpret_cast<char *>(P)[Size] = 'A';
        },
        "");
  }
}

template <typename Config> struct TestAllocator : scudo::Allocator<Config> {
  TestAllocator() {
    this->initThreadMaybe();
    if (scudo::archSupportsMemoryTagging() &&
        !scudo::systemDetectsMemoryTagFaultsTestOnly())
      this->disableMemoryTagging();
  }
  ~TestAllocator() { this->unmapTestOnly(); }

  void *operator new(size_t size);
  void operator delete(void *ptr);
};

constexpr size_t kMaxAlign = std::max({
  alignof(scudo::Allocator<scudo::DefaultConfig>),
#if SCUDO_CAN_USE_PRIMARY64
      alignof(scudo::Allocator<scudo::FuchsiaConfig>),
#endif
      alignof(scudo::Allocator<scudo::AndroidConfig>)
});

#if SCUDO_RISCV64
// The allocator is over 4MB large. Rather than creating an instance of this on
// the heap, keep it in a global storage to reduce fragmentation from having to
// mmap this at the start of every test.
struct TestAllocatorStorage {
  static constexpr size_t kMaxSize = std::max({
    sizeof(scudo::Allocator<scudo::DefaultConfig>),
#if SCUDO_CAN_USE_PRIMARY64
        sizeof(scudo::Allocator<scudo::FuchsiaConfig>),
#endif
        sizeof(scudo::Allocator<scudo::AndroidConfig>)
  });

  // To alleviate some problem, let's skip the thread safety analysis here.
  static void *get(size_t size) NO_THREAD_SAFETY_ANALYSIS {
    CHECK(size <= kMaxSize &&
          "Allocation size doesn't fit in the allocator storage");
    M.lock();
    return AllocatorStorage;
  }

  static void release(void *ptr) NO_THREAD_SAFETY_ANALYSIS {
    M.assertHeld();
    M.unlock();
    ASSERT_EQ(ptr, AllocatorStorage);
  }

  static scudo::HybridMutex M;
  static uint8_t AllocatorStorage[kMaxSize];
};
scudo::HybridMutex TestAllocatorStorage::M;
alignas(kMaxAlign) uint8_t TestAllocatorStorage::AllocatorStorage[kMaxSize];
#else
struct TestAllocatorStorage {
  static void *get(size_t size) NO_THREAD_SAFETY_ANALYSIS {
    void *p = nullptr;
    EXPECT_EQ(0, posix_memalign(&p, kMaxAlign, size));
    return p;
  }
  static void release(void *ptr) NO_THREAD_SAFETY_ANALYSIS { free(ptr); }
};
#endif

template <typename Config>
void *TestAllocator<Config>::operator new(size_t size) {
  return TestAllocatorStorage::get(size);
}

template <typename Config>
void TestAllocator<Config>::operator delete(void *ptr) {
  TestAllocatorStorage::release(ptr);
}

template <class TypeParam> struct ScudoCombinedTest : public Test {
  ScudoCombinedTest() {
    UseQuarantine = std::is_same<TypeParam, scudo::AndroidConfig>::value;
    Allocator = std::make_unique<AllocatorT>();
  }
  ~ScudoCombinedTest() {
    Allocator->releaseToOS(scudo::ReleaseToOS::Force);
    UseQuarantine = true;
  }

  void RunTest();

  void BasicTest(scudo::uptr SizeLog);

  using AllocatorT = TestAllocator<TypeParam>;
  std::unique_ptr<AllocatorT> Allocator;
};

template <typename T> using ScudoCombinedDeathTest = ScudoCombinedTest<T>;

namespace scudo {
struct TestConditionVariableConfig {
  static const bool MaySupportMemoryTagging = true;
  template <class A>
  using TSDRegistryT =
      scudo::TSDRegistrySharedT<A, 8U, 4U>; // Shared, max 8 TSDs.

  struct Primary {
    using SizeClassMap = scudo::AndroidSizeClassMap;
#if SCUDO_CAN_USE_PRIMARY64
    static const scudo::uptr RegionSizeLog = 28U;
    typedef scudo::u32 CompactPtrT;
    static const scudo::uptr CompactPtrScale = SCUDO_MIN_ALIGNMENT_LOG;
    static const scudo::uptr GroupSizeLog = 20U;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
#else
    static const scudo::uptr RegionSizeLog = 18U;
    static const scudo::uptr GroupSizeLog = 18U;
    typedef scudo::uptr CompactPtrT;
#endif
    static const scudo::s32 MinReleaseToOsIntervalMs = 1000;
    static const scudo::s32 MaxReleaseToOsIntervalMs = 1000;
#if SCUDO_LINUX
    using ConditionVariableT = scudo::ConditionVariableLinux;
#else
    using ConditionVariableT = scudo::ConditionVariableDummy;
#endif
  };
#if SCUDO_CAN_USE_PRIMARY64
  template <typename Config>
  using PrimaryT = scudo::SizeClassAllocator64<Config>;
#else
  template <typename Config>
  using PrimaryT = scudo::SizeClassAllocator32<Config>;
#endif

  struct Secondary {
    template <typename Config>
    using CacheT = scudo::MapAllocatorNoCache<Config>;
  };
  template <typename Config> using SecondaryT = scudo::MapAllocator<Config>;
};
} // namespace scudo

#if SCUDO_FUCHSIA
#define SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                              \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, FuchsiaConfig)
#else
#define SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                              \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, DefaultConfig)                          \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, AndroidConfig)                          \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConditionVariableConfig)
#endif

#define SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TYPE)                             \
  using FIXTURE##NAME##_##TYPE = FIXTURE##NAME<scudo::TYPE>;                   \
  TEST_F(FIXTURE##NAME##_##TYPE, NAME) { FIXTURE##NAME<scudo::TYPE>::Run(); }

#define SCUDO_TYPED_TEST(FIXTURE, NAME)                                        \
  template <class TypeParam>                                                   \
  struct FIXTURE##NAME : public FIXTURE<TypeParam> {                           \
    using BaseT = FIXTURE<TypeParam>;                                          \
    void Run();                                                                \
  };                                                                           \
  SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                                    \
  template <class TypeParam> void FIXTURE##NAME<TypeParam>::Run()

// Accessing `TSD->getCache()` requires `TSD::Mutex` which isn't easy to test
// using thread-safety analysis. Alternatively, we verify the thread safety
// through a runtime check in ScopedTSD and mark the test body with
// NO_THREAD_SAFETY_ANALYSIS.
#define SCUDO_TYPED_TEST_SKIP_THREAD_SAFETY(FIXTURE, NAME)                     \
  template <class TypeParam>                                                   \
  struct FIXTURE##NAME : public FIXTURE<TypeParam> {                           \
    using BaseT = FIXTURE<TypeParam>;                                          \
    void Run() NO_THREAD_SAFETY_ANALYSIS;                                      \
  };                                                                           \
  SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                                    \
  template <class TypeParam> void FIXTURE##NAME<TypeParam>::Run()

SCUDO_TYPED_TEST(ScudoCombinedTest, IsOwned) {
  auto *Allocator = this->Allocator.get();
  static scudo::u8 StaticBuffer[scudo::Chunk::getHeaderSize() + 1];
  EXPECT_FALSE(
      Allocator->isOwned(&StaticBuffer[scudo::Chunk::getHeaderSize()]));

  scudo::u8 StackBuffer[scudo::Chunk::getHeaderSize() + 1];
  for (scudo::uptr I = 0; I < sizeof(StackBuffer); I++)
    StackBuffer[I] = 0x42U;
  EXPECT_FALSE(Allocator->isOwned(&StackBuffer[scudo::Chunk::getHeaderSize()]));
  for (scudo::uptr I = 0; I < sizeof(StackBuffer); I++)
    EXPECT_EQ(StackBuffer[I], 0x42U);
}

template <class Config>
void ScudoCombinedTest<Config>::BasicTest(scudo::uptr SizeLog) {
  auto *Allocator = this->Allocator.get();

  // This allocates and deallocates a bunch of chunks, with a wide range of
  // sizes and alignments, with a focus on sizes that could trigger weird
  // behaviors (plus or minus a small delta of a power of two for example).
  for (scudo::uptr AlignLog = MinAlignLog; AlignLog <= 16U; AlignLog++) {
    const scudo::uptr Align = 1U << AlignLog;
    for (scudo::sptr Delta = -32; Delta <= 32; Delta++) {
      if ((1LL << SizeLog) + Delta < 0)
        continue;
      const scudo::uptr Size =
          static_cast<scudo::uptr>((1LL << SizeLog) + Delta);
      void *P = Allocator->allocate(Size, Origin, Align);
      EXPECT_NE(P, nullptr);
      EXPECT_TRUE(Allocator->isOwned(P));
      EXPECT_TRUE(scudo::isAligned(reinterpret_cast<scudo::uptr>(P), Align));
      EXPECT_LE(Size, Allocator->getUsableSize(P));
      memset(P, 0xaa, Size);
      checkMemoryTaggingMaybe(Allocator, P, Size, Align);
      Allocator->deallocate(P, Origin, Size);
    }
  }

  Allocator->printStats();
  Allocator->printFragmentationInfo();
}

#define SCUDO_MAKE_BASIC_TEST(SizeLog)                                         \
  SCUDO_TYPED_TEST(ScudoCombinedDeathTest, BasicCombined##SizeLog) {           \
    this->BasicTest(SizeLog);                                                  \
  }

SCUDO_MAKE_BASIC_TEST(0)
SCUDO_MAKE_BASIC_TEST(1)
SCUDO_MAKE_BASIC_TEST(2)
SCUDO_MAKE_BASIC_TEST(3)
SCUDO_MAKE_BASIC_TEST(4)
SCUDO_MAKE_BASIC_TEST(5)
SCUDO_MAKE_BASIC_TEST(6)
SCUDO_MAKE_BASIC_TEST(7)
SCUDO_MAKE_BASIC_TEST(8)
SCUDO_MAKE_BASIC_TEST(9)
SCUDO_MAKE_BASIC_TEST(10)
SCUDO_MAKE_BASIC_TEST(11)
SCUDO_MAKE_BASIC_TEST(12)
SCUDO_MAKE_BASIC_TEST(13)
SCUDO_MAKE_BASIC_TEST(14)
SCUDO_MAKE_BASIC_TEST(15)
SCUDO_MAKE_BASIC_TEST(16)
SCUDO_MAKE_BASIC_TEST(17)
SCUDO_MAKE_BASIC_TEST(18)
SCUDO_MAKE_BASIC_TEST(19)
SCUDO_MAKE_BASIC_TEST(20)

SCUDO_TYPED_TEST(ScudoCombinedTest, ZeroContents) {
  auto *Allocator = this->Allocator.get();

  // Ensure that specifying ZeroContents returns a zero'd out block.
  for (scudo::uptr SizeLog = 0U; SizeLog <= 20U; SizeLog++) {
    for (scudo::uptr Delta = 0U; Delta <= 4U; Delta++) {
      const scudo::uptr Size = (1U << SizeLog) + Delta * 128U;
      void *P = Allocator->allocate(Size, Origin, 1U << MinAlignLog, true);
      EXPECT_NE(P, nullptr);
      for (scudo::uptr I = 0; I < Size; I++)
        ASSERT_EQ((reinterpret_cast<char *>(P))[I], '\0');
      memset(P, 0xaa, Size);
      Allocator->deallocate(P, Origin, Size);
    }
  }
}

SCUDO_TYPED_TEST(ScudoCombinedTest, ZeroFill) {
  auto *Allocator = this->Allocator.get();

  // Ensure that specifying ZeroFill returns a zero'd out block.
  Allocator->setFillContents(scudo::ZeroFill);
  for (scudo::uptr SizeLog = 0U; SizeLog <= 20U; SizeLog++) {
    for (scudo::uptr Delta = 0U; Delta <= 4U; Delta++) {
      const scudo::uptr Size = (1U << SizeLog) + Delta * 128U;
      void *P = Allocator->allocate(Size, Origin, 1U << MinAlignLog, false);
      EXPECT_NE(P, nullptr);
      for (scudo::uptr I = 0; I < Size; I++)
        ASSERT_EQ((reinterpret_cast<char *>(P))[I], '\0');
      memset(P, 0xaa, Size);
      Allocator->deallocate(P, Origin, Size);
    }
  }
}

SCUDO_TYPED_TEST(ScudoCombinedTest, PatternOrZeroFill) {
  auto *Allocator = this->Allocator.get();

  // Ensure that specifying PatternOrZeroFill returns a pattern or zero filled
  // block. The primary allocator only produces pattern filled blocks if MTE
  // is disabled, so we only require pattern filled blocks in that case.
  Allocator->setFillContents(scudo::PatternOrZeroFill);
  for (scudo::uptr SizeLog = 0U; SizeLog <= 20U; SizeLog++) {
    for (scudo::uptr Delta = 0U; Delta <= 4U; Delta++) {
      const scudo::uptr Size = (1U << SizeLog) + Delta * 128U;
      void *P = Allocator->allocate(Size, Origin, 1U << MinAlignLog, false);
      EXPECT_NE(P, nullptr);
      for (scudo::uptr I = 0; I < Size; I++) {
        unsigned char V = (reinterpret_cast<unsigned char *>(P))[I];
        if (isPrimaryAllocation<TestAllocator<TypeParam>>(Size,
                                                          1U << MinAlignLog) &&
            !Allocator->useMemoryTaggingTestOnly())
          ASSERT_EQ(V, scudo::PatternFillByte);
        else
          ASSERT_TRUE(V == scudo::PatternFillByte || V == 0);
      }
      memset(P, 0xaa, Size);
      Allocator->deallocate(P, Origin, Size);
    }
  }
}

SCUDO_TYPED_TEST(ScudoCombinedTest, BlockReuse) {
  auto *Allocator = this->Allocator.get();

  // Verify that a chunk will end up being reused, at some point.
  const scudo::uptr NeedleSize = 1024U;
  void *NeedleP = Allocator->allocate(NeedleSize, Origin);
  Allocator->deallocate(NeedleP, Origin);
  bool Found = false;
  for (scudo::uptr I = 0; I < 1024U && !Found; I++) {
    void *P = Allocator->allocate(NeedleSize, Origin);
    if (Allocator->getHeaderTaggedPointer(P) ==
        Allocator->getHeaderTaggedPointer(NeedleP))
      Found = true;
    Allocator->deallocate(P, Origin);
  }
  EXPECT_TRUE(Found);
}

SCUDO_TYPED_TEST(ScudoCombinedTest, ReallocateLargeIncreasing) {
  auto *Allocator = this->Allocator.get();

  // Reallocate a chunk all the way up to a secondary allocation, verifying that
  // we preserve the data in the process.
  scudo::uptr Size = 16;
  void *P = Allocator->allocate(Size, Origin);
  const char Marker = 'A';
  memset(P, Marker, Size);
  while (Size < TypeParam::Primary::SizeClassMap::MaxSize * 4) {
    void *NewP = Allocator->reallocate(P, Size * 2);
    EXPECT_NE(NewP, nullptr);
    for (scudo::uptr J = 0; J < Size; J++)
      EXPECT_EQ((reinterpret_cast<char *>(NewP))[J], Marker);
    memset(reinterpret_cast<char *>(NewP) + Size, Marker, Size);
    Size *= 2U;
    P = NewP;
  }
  Allocator->deallocate(P, Origin);
}

SCUDO_TYPED_TEST(ScudoCombinedTest, ReallocateLargeDecreasing) {
  auto *Allocator = this->Allocator.get();

  // Reallocate a large chunk all the way down to a byte, verifying that we
  // preserve the data in the process.
  scudo::uptr Size = TypeParam::Primary::SizeClassMap::MaxSize * 2;
  const scudo::uptr DataSize = 2048U;
  void *P = Allocator->allocate(Size, Origin);
  const char Marker = 'A';
  memset(P, Marker, scudo::Min(Size, DataSize));
  while (Size > 1U) {
    Size /= 2U;
    void *NewP = Allocator->reallocate(P, Size);
    EXPECT_NE(NewP, nullptr);
    for (scudo::uptr J = 0; J < scudo::Min(Size, DataSize); J++)
      EXPECT_EQ((reinterpret_cast<char *>(NewP))[J], Marker);
    P = NewP;
  }
  Allocator->deallocate(P, Origin);
}

SCUDO_TYPED_TEST(ScudoCombinedDeathTest, ReallocateSame) {
  auto *Allocator = this->Allocator.get();

  // Check that reallocating a chunk to a slightly smaller or larger size
  // returns the same chunk. This requires that all the sizes we iterate on use
  // the same block size, but that should be the case for MaxSize - 64 with our
  // default class size maps.
  constexpr scudo::uptr InitialSize =
      TypeParam::Primary::SizeClassMap::MaxSize - 64;
  const char Marker = 'A';
  Allocator->setFillContents(scudo::PatternOrZeroFill);

  void *P = Allocator->allocate(InitialSize, Origin);
  scudo::uptr CurrentSize = InitialSize;
  for (scudo::sptr Delta = -32; Delta < 32; Delta += 8) {
    memset(P, Marker, CurrentSize);
    const scudo::uptr NewSize =
        static_cast<scudo::uptr>(static_cast<scudo::sptr>(InitialSize) + Delta);
    void *NewP = Allocator->reallocate(P, NewSize);
    EXPECT_EQ(NewP, P);

    // Verify that existing contents have been preserved.
    for (scudo::uptr I = 0; I < scudo::Min(CurrentSize, NewSize); I++)
      EXPECT_EQ((reinterpret_cast<char *>(NewP))[I], Marker);

    // Verify that new bytes are set according to FillContentsMode.
    for (scudo::uptr I = CurrentSize; I < NewSize; I++) {
      unsigned char V = (reinterpret_cast<unsigned char *>(NewP))[I];
      EXPECT_TRUE(V == scudo::PatternFillByte || V == 0);
    }

    checkMemoryTaggingMaybe(Allocator, NewP, NewSize, 0);
    CurrentSize = NewSize;
  }
  Allocator->deallocate(P, Origin);
}

SCUDO_TYPED_TEST(ScudoCombinedTest, IterateOverChunks) {
  auto *Allocator = this->Allocator.get();
  // Allocates a bunch of chunks, then iterate over all the chunks, ensuring
  // they are the ones we allocated. This requires the allocator to not have any
  // other allocated chunk at this point (eg: won't work with the Quarantine).
  // FIXME: Make it work with UseQuarantine and tagging enabled. Internals of
  // iterateOverChunks reads header by tagged and non-tagger pointers so one of
  // them will fail.
  if (!UseQuarantine) {
    std::vector<void *> V;
    for (scudo::uptr I = 0; I < 64U; I++)
      V.push_back(Allocator->allocate(
          static_cast<scudo::uptr>(std::rand()) %
              (TypeParam::Primary::SizeClassMap::MaxSize / 2U),
          Origin));
    Allocator->disable();
    Allocator->iterateOverChunks(
        0U, static_cast<scudo::uptr>(SCUDO_MMAP_RANGE_SIZE - 1),
        [](uintptr_t Base, UNUSED size_t Size, void *Arg) {
          std::vector<void *> *V = reinterpret_cast<std::vector<void *> *>(Arg);
          void *P = reinterpret_cast<void *>(Base);
          EXPECT_NE(std::find(V->begin(), V->end(), P), V->end());
        },
        reinterpret_cast<void *>(&V));
    Allocator->enable();
    for (auto P : V)
      Allocator->deallocate(P, Origin);
  }
}

SCUDO_TYPED_TEST(ScudoCombinedDeathTest, UseAfterFree) {
  auto *Allocator = this->Allocator.get();

  // Check that use-after-free is detected.
  for (scudo::uptr SizeLog = 0U; SizeLog <= 20U; SizeLog++) {
    const scudo::uptr Size = 1U << SizeLog;
    if (!Allocator->useMemoryTaggingTestOnly())
      continue;
    EXPECT_DEATH(
        {
          disableDebuggerdMaybe();
          void *P = Allocator->allocate(Size, Origin);
          Allocator->deallocate(P, Origin);
          reinterpret_cast<char *>(P)[0] = 'A';
        },
        "");
    EXPECT_DEATH(
        {
          disableDebuggerdMaybe();
          void *P = Allocator->allocate(Size, Origin);
          Allocator->deallocate(P, Origin);
          reinterpret_cast<char *>(P)[Size - 1] = 'A';
        },
        "");
  }
}

SCUDO_TYPED_TEST(ScudoCombinedDeathTest, DisableMemoryTagging) {
  auto *Allocator = this->Allocator.get();

  if (Allocator->useMemoryTaggingTestOnly()) {
    // Check that disabling memory tagging works correctly.
    void *P = Allocator->allocate(2048, Origin);
    EXPECT_DEATH(reinterpret_cast<char *>(P)[2048] = 'A', "");
    scudo::ScopedDisableMemoryTagChecks NoTagChecks;
    Allocator->disableMemoryTagging();
    reinterpret_cast<char *>(P)[2048] = 'A';
    Allocator->deallocate(P, Origin);

    P = Allocator->allocate(2048, Origin);
    EXPECT_EQ(scudo::untagPointer(P), P);
    reinterpret_cast<char *>(P)[2048] = 'A';
    Allocator->deallocate(P, Origin);

    Allocator->releaseToOS(scudo::ReleaseToOS::Force);
  }
}

SCUDO_TYPED_TEST(ScudoCombinedTest, Stats) {
  auto *Allocator = this->Allocator.get();

  scudo::uptr BufferSize = 8192;
  std::vector<char> Buffer(BufferSize);
  scudo::uptr ActualSize = Allocator->getStats(Buffer.data(), BufferSize);
  while (ActualSize > BufferSize) {
    BufferSize = ActualSize + 1024;
    Buffer.resize(BufferSize);
    ActualSize = Allocator->getStats(Buffer.data(), BufferSize);
  }
  std::string Stats(Buffer.begin(), Buffer.end());
  // Basic checks on the contents of the statistics output, which also allows us
  // to verify that we got it all.
  EXPECT_NE(Stats.find("Stats: SizeClassAllocator"), std::string::npos);
  EXPECT_NE(Stats.find("Stats: MapAllocator"), std::string::npos);
  EXPECT_NE(Stats.find("Stats: Quarantine"), std::string::npos);
}

SCUDO_TYPED_TEST_SKIP_THREAD_SAFETY(ScudoCombinedTest, CacheDrain) {
  using AllocatorT = typename BaseT::AllocatorT;
  auto *Allocator = this->Allocator.get();

  std::vector<void *> V;
  for (scudo::uptr I = 0; I < 64U; I++)
    V.push_back(Allocator->allocate(
        static_cast<scudo::uptr>(std::rand()) %
            (TypeParam::Primary::SizeClassMap::MaxSize / 2U),
        Origin));
  for (auto P : V)
    Allocator->deallocate(P, Origin);

  typename AllocatorT::TSDRegistryT::ScopedTSD TSD(
      *Allocator->getTSDRegistry());
  EXPECT_TRUE(!TSD->getCache().isEmpty());
  TSD->getCache().drain();
  EXPECT_TRUE(TSD->getCache().isEmpty());
}

SCUDO_TYPED_TEST_SKIP_THREAD_SAFETY(ScudoCombinedTest, ForceCacheDrain) {
  using AllocatorT = typename BaseT::AllocatorT;
  auto *Allocator = this->Allocator.get();

  std::vector<void *> V;
  for (scudo::uptr I = 0; I < 64U; I++)
    V.push_back(Allocator->allocate(
        static_cast<scudo::uptr>(std::rand()) %
            (TypeParam::Primary::SizeClassMap::MaxSize / 2U),
        Origin));
  for (auto P : V)
    Allocator->deallocate(P, Origin);

  // `ForceAll` will also drain the caches.
  Allocator->releaseToOS(scudo::ReleaseToOS::ForceAll);

  typename AllocatorT::TSDRegistryT::ScopedTSD TSD(
      *Allocator->getTSDRegistry());
  EXPECT_TRUE(TSD->getCache().isEmpty());
  EXPECT_EQ(TSD->getQuarantineCache().getSize(), 0U);
  EXPECT_TRUE(Allocator->getQuarantine()->isEmpty());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, ThreadedCombined) {
  std::mutex Mutex;
  std::condition_variable Cv;
  bool Ready = false;
  auto *Allocator = this->Allocator.get();
  std::thread Threads[32];
  for (scudo::uptr I = 0; I < ARRAY_SIZE(Threads); I++)
    Threads[I] = std::thread([&]() {
      {
        std::unique_lock<std::mutex> Lock(Mutex);
        while (!Ready)
          Cv.wait(Lock);
      }
      std::vector<std::pair<void *, scudo::uptr>> V;
      for (scudo::uptr I = 0; I < 256U; I++) {
        const scudo::uptr Size = static_cast<scudo::uptr>(std::rand()) % 4096U;
        void *P = Allocator->allocate(Size, Origin);
        // A region could have ran out of memory, resulting in a null P.
        if (P)
          V.push_back(std::make_pair(P, Size));
      }

      // Try to interleave pushBlocks(), popBatch() and releaseToOS().
      Allocator->releaseToOS(scudo::ReleaseToOS::Force);

      while (!V.empty()) {
        auto Pair = V.back();
        Allocator->deallocate(Pair.first, Origin, Pair.second);
        V.pop_back();
      }
    });
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    Ready = true;
    Cv.notify_all();
  }
  for (auto &T : Threads)
    T.join();
  Allocator->releaseToOS(scudo::ReleaseToOS::Force);
}

// Test that multiple instantiations of the allocator have not messed up the
// process's signal handlers (GWP-ASan used to do this).
TEST(ScudoCombinedDeathTest, SKIP_ON_FUCHSIA(testSEGV)) {
  const scudo::uptr Size = 4 * scudo::getPageSizeCached();
  scudo::ReservedMemoryT ReservedMemory;
  ASSERT_TRUE(ReservedMemory.create(/*Addr=*/0U, Size, "testSEGV"));
  void *P = reinterpret_cast<void *>(ReservedMemory.getBase());
  ASSERT_NE(P, nullptr);
  EXPECT_DEATH(memset(P, 0xaa, Size), "");
  ReservedMemory.release();
}

struct DeathSizeClassConfig {
  static const scudo::uptr NumBits = 1;
  static const scudo::uptr MinSizeLog = 10;
  static const scudo::uptr MidSizeLog = 10;
  static const scudo::uptr MaxSizeLog = 13;
  static const scudo::u16 MaxNumCachedHint = 8;
  static const scudo::uptr MaxBytesCachedLog = 12;
  static const scudo::uptr SizeDelta = 0;
};

static const scudo::uptr DeathRegionSizeLog = 21U;
struct DeathConfig {
  static const bool MaySupportMemoryTagging = false;
  template <class A> using TSDRegistryT = scudo::TSDRegistrySharedT<A, 1U, 1U>;

  struct Primary {
    // Tiny allocator, its Primary only serves chunks of four sizes.
    using SizeClassMap = scudo::FixedSizeClassMap<DeathSizeClassConfig>;
    static const scudo::uptr RegionSizeLog = DeathRegionSizeLog;
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    typedef scudo::uptr CompactPtrT;
    static const scudo::uptr CompactPtrScale = 0;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
    static const scudo::uptr GroupSizeLog = 18;
  };
  template <typename Config>
  using PrimaryT = scudo::SizeClassAllocator64<Config>;

  struct Secondary {
    template <typename Config>
    using CacheT = scudo::MapAllocatorNoCache<Config>;
  };

  template <typename Config> using SecondaryT = scudo::MapAllocator<Config>;
};

TEST(ScudoCombinedDeathTest, DeathCombined) {
  using AllocatorT = TestAllocator<DeathConfig>;
  auto Allocator = std::unique_ptr<AllocatorT>(new AllocatorT());

  const scudo::uptr Size = 1000U;
  void *P = Allocator->allocate(Size, Origin);
  EXPECT_NE(P, nullptr);

  // Invalid sized deallocation.
  EXPECT_DEATH(Allocator->deallocate(P, Origin, Size + 8U), "");

  // Misaligned pointer. Potentially unused if EXPECT_DEATH isn't available.
  UNUSED void *MisalignedP =
      reinterpret_cast<void *>(reinterpret_cast<scudo::uptr>(P) | 1U);
  EXPECT_DEATH(Allocator->deallocate(MisalignedP, Origin, Size), "");
  EXPECT_DEATH(Allocator->reallocate(MisalignedP, Size * 2U), "");

  // Header corruption.
  scudo::u64 *H =
      reinterpret_cast<scudo::u64 *>(scudo::Chunk::getAtomicHeader(P));
  *H ^= 0x42U;
  EXPECT_DEATH(Allocator->deallocate(P, Origin, Size), "");
  *H ^= 0x420042U;
  EXPECT_DEATH(Allocator->deallocate(P, Origin, Size), "");
  *H ^= 0x420000U;

  // Invalid chunk state.
  Allocator->deallocate(P, Origin, Size);
  EXPECT_DEATH(Allocator->deallocate(P, Origin, Size), "");
  EXPECT_DEATH(Allocator->reallocate(P, Size * 2U), "");
  EXPECT_DEATH(Allocator->getUsableSize(P), "");
}

// Verify that when a region gets full, the allocator will still manage to
// fulfill the allocation through a larger size class.
TEST(ScudoCombinedTest, FullRegion) {
  using AllocatorT = TestAllocator<DeathConfig>;
  auto Allocator = std::unique_ptr<AllocatorT>(new AllocatorT());

  std::vector<void *> V;
  scudo::uptr FailedAllocationsCount = 0;
  for (scudo::uptr ClassId = 1U;
       ClassId <= DeathConfig::Primary::SizeClassMap::LargestClassId;
       ClassId++) {
    const scudo::uptr Size =
        DeathConfig::Primary::SizeClassMap::getSizeByClassId(ClassId);
    // Allocate enough to fill all of the regions above this one.
    const scudo::uptr MaxNumberOfChunks =
        ((1U << DeathRegionSizeLog) / Size) *
        (DeathConfig::Primary::SizeClassMap::LargestClassId - ClassId + 1);
    void *P;
    for (scudo::uptr I = 0; I <= MaxNumberOfChunks; I++) {
      P = Allocator->allocate(Size - 64U, Origin);
      if (!P)
        FailedAllocationsCount++;
      else
        V.push_back(P);
    }
    while (!V.empty()) {
      Allocator->deallocate(V.back(), Origin);
      V.pop_back();
    }
  }
  EXPECT_EQ(FailedAllocationsCount, 0U);
}

// Ensure that releaseToOS can be called prior to any other allocator
// operation without issue.
SCUDO_TYPED_TEST(ScudoCombinedTest, ReleaseToOS) {
  auto *Allocator = this->Allocator.get();
  Allocator->releaseToOS(scudo::ReleaseToOS::Force);
}

SCUDO_TYPED_TEST(ScudoCombinedTest, OddEven) {
  auto *Allocator = this->Allocator.get();
  Allocator->setOption(scudo::Option::MemtagTuning, M_MEMTAG_TUNING_BUFFER_OVERFLOW);

  if (!Allocator->useMemoryTaggingTestOnly())
    return;

  auto CheckOddEven = [](scudo::uptr P1, scudo::uptr P2) {
    scudo::uptr Tag1 = scudo::extractTag(scudo::loadTag(P1));
    scudo::uptr Tag2 = scudo::extractTag(scudo::loadTag(P2));
    EXPECT_NE(Tag1 % 2, Tag2 % 2);
  };

  using SizeClassMap = typename TypeParam::Primary::SizeClassMap;
  for (scudo::uptr ClassId = 1U; ClassId <= SizeClassMap::LargestClassId;
       ClassId++) {
    const scudo::uptr Size = SizeClassMap::getSizeByClassId(ClassId);

    std::set<scudo::uptr> Ptrs;
    bool Found = false;
    for (unsigned I = 0; I != 65536; ++I) {
      scudo::uptr P = scudo::untagPointer(reinterpret_cast<scudo::uptr>(
          Allocator->allocate(Size - scudo::Chunk::getHeaderSize(), Origin)));
      if (Ptrs.count(P - Size)) {
        Found = true;
        CheckOddEven(P, P - Size);
        break;
      }
      if (Ptrs.count(P + Size)) {
        Found = true;
        CheckOddEven(P, P + Size);
        break;
      }
      Ptrs.insert(P);
    }
    EXPECT_TRUE(Found);
  }
}

SCUDO_TYPED_TEST(ScudoCombinedTest, DisableMemInit) {
  auto *Allocator = this->Allocator.get();

  std::vector<void *> Ptrs(65536);

  Allocator->setOption(scudo::Option::ThreadDisableMemInit, 1);

  constexpr scudo::uptr MinAlignLog = FIRST_32_SECOND_64(3U, 4U);

  // Test that if mem-init is disabled on a thread, calloc should still work as
  // expected. This is tricky to ensure when MTE is enabled, so this test tries
  // to exercise the relevant code on our MTE path.
  for (scudo::uptr ClassId = 1U; ClassId <= 8; ClassId++) {
    using SizeClassMap = typename TypeParam::Primary::SizeClassMap;
    const scudo::uptr Size =
        SizeClassMap::getSizeByClassId(ClassId) - scudo::Chunk::getHeaderSize();
    if (Size < 8)
      continue;
    for (unsigned I = 0; I != Ptrs.size(); ++I) {
      Ptrs[I] = Allocator->allocate(Size, Origin);
      memset(Ptrs[I], 0xaa, Size);
    }
    for (unsigned I = 0; I != Ptrs.size(); ++I)
      Allocator->deallocate(Ptrs[I], Origin, Size);
    for (unsigned I = 0; I != Ptrs.size(); ++I) {
      Ptrs[I] = Allocator->allocate(Size - 8, Origin);
      memset(Ptrs[I], 0xbb, Size - 8);
    }
    for (unsigned I = 0; I != Ptrs.size(); ++I)
      Allocator->deallocate(Ptrs[I], Origin, Size - 8);
    for (unsigned I = 0; I != Ptrs.size(); ++I) {
      Ptrs[I] = Allocator->allocate(Size, Origin, 1U << MinAlignLog, true);
      for (scudo::uptr J = 0; J < Size; ++J)
        ASSERT_EQ((reinterpret_cast<char *>(Ptrs[I]))[J], '\0');
    }
  }

  Allocator->setOption(scudo::Option::ThreadDisableMemInit, 0);
}

SCUDO_TYPED_TEST(ScudoCombinedTest, ReallocateInPlaceStress) {
  auto *Allocator = this->Allocator.get();

  // Regression test: make realloc-in-place happen at the very right end of a
  // mapped region.
  constexpr size_t nPtrs = 10000;
  for (scudo::uptr i = 1; i < 32; ++i) {
    scudo::uptr Size = 16 * i - 1;
    std::vector<void *> Ptrs;
    for (size_t i = 0; i < nPtrs; ++i) {
      void *P = Allocator->allocate(Size, Origin);
      P = Allocator->reallocate(P, Size + 1);
      Ptrs.push_back(P);
    }

    for (size_t i = 0; i < nPtrs; ++i)
      Allocator->deallocate(Ptrs[i], Origin);
  }
}

SCUDO_TYPED_TEST(ScudoCombinedTest, RingBufferDefaultDisabled) {
  // The RingBuffer is not initialized until tracking is enabled for the
  // first time.
  auto *Allocator = this->Allocator.get();
  EXPECT_EQ(0u, Allocator->getRingBufferSize());
  EXPECT_EQ(nullptr, Allocator->getRingBufferAddress());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, RingBufferInitOnce) {
  auto *Allocator = this->Allocator.get();
  Allocator->setTrackAllocationStacks(true);

  auto RingBufferSize = Allocator->getRingBufferSize();
  ASSERT_GT(RingBufferSize, 0u);
  auto *RingBufferAddress = Allocator->getRingBufferAddress();
  EXPECT_NE(nullptr, RingBufferAddress);

  // Enable tracking again to verify that the initialization only happens once.
  Allocator->setTrackAllocationStacks(true);
  ASSERT_EQ(RingBufferSize, Allocator->getRingBufferSize());
  EXPECT_EQ(RingBufferAddress, Allocator->getRingBufferAddress());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, RingBufferSize) {
  auto *Allocator = this->Allocator.get();
  Allocator->setTrackAllocationStacks(true);

  auto RingBufferSize = Allocator->getRingBufferSize();
  ASSERT_GT(RingBufferSize, 0u);
  EXPECT_EQ(Allocator->getRingBufferAddress()[RingBufferSize - 1], '\0');
}

SCUDO_TYPED_TEST(ScudoCombinedTest, RingBufferAddress) {
  auto *Allocator = this->Allocator.get();
  Allocator->setTrackAllocationStacks(true);

  auto *RingBufferAddress = Allocator->getRingBufferAddress();
  EXPECT_NE(RingBufferAddress, nullptr);
  EXPECT_EQ(RingBufferAddress, Allocator->getRingBufferAddress());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, StackDepotDefaultDisabled) {
  // The StackDepot is not initialized until tracking is enabled for the
  // first time.
  auto *Allocator = this->Allocator.get();
  EXPECT_EQ(0u, Allocator->getStackDepotSize());
  EXPECT_EQ(nullptr, Allocator->getStackDepotAddress());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, StackDepotInitOnce) {
  auto *Allocator = this->Allocator.get();
  Allocator->setTrackAllocationStacks(true);

  auto StackDepotSize = Allocator->getStackDepotSize();
  EXPECT_GT(StackDepotSize, 0u);
  auto *StackDepotAddress = Allocator->getStackDepotAddress();
  EXPECT_NE(nullptr, StackDepotAddress);

  // Enable tracking again to verify that the initialization only happens once.
  Allocator->setTrackAllocationStacks(true);
  EXPECT_EQ(StackDepotSize, Allocator->getStackDepotSize());
  EXPECT_EQ(StackDepotAddress, Allocator->getStackDepotAddress());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, StackDepotSize) {
  auto *Allocator = this->Allocator.get();
  Allocator->setTrackAllocationStacks(true);

  auto StackDepotSize = Allocator->getStackDepotSize();
  EXPECT_GT(StackDepotSize, 0u);
  EXPECT_EQ(Allocator->getStackDepotAddress()[StackDepotSize - 1], '\0');
}

SCUDO_TYPED_TEST(ScudoCombinedTest, StackDepotAddress) {
  auto *Allocator = this->Allocator.get();
  Allocator->setTrackAllocationStacks(true);

  auto *StackDepotAddress = Allocator->getStackDepotAddress();
  EXPECT_NE(StackDepotAddress, nullptr);
  EXPECT_EQ(StackDepotAddress, Allocator->getStackDepotAddress());
}

SCUDO_TYPED_TEST(ScudoCombinedTest, StackDepot) {
  alignas(scudo::StackDepot) char Buf[sizeof(scudo::StackDepot) +
                                      1024 * sizeof(scudo::atomic_u64) +
                                      1024 * sizeof(scudo::atomic_u32)] = {};
  auto *Depot = reinterpret_cast<scudo::StackDepot *>(Buf);
  Depot->init(1024, 1024);
  ASSERT_TRUE(Depot->isValid(sizeof(Buf)));
  ASSERT_FALSE(Depot->isValid(sizeof(Buf) - 1));
  scudo::uptr Stack[] = {1, 2, 3};
  scudo::u32 Elem = Depot->insert(&Stack[0], &Stack[3]);
  scudo::uptr RingPosPtr = 0;
  scudo::uptr SizePtr = 0;
  ASSERT_TRUE(Depot->find(Elem, &RingPosPtr, &SizePtr));
  ASSERT_EQ(SizePtr, 3u);
  EXPECT_EQ(Depot->at(RingPosPtr), 1u);
  EXPECT_EQ(Depot->at(RingPosPtr + 1), 2u);
  EXPECT_EQ(Depot->at(RingPosPtr + 2), 3u);
}

#if SCUDO_CAN_USE_PRIMARY64
#if SCUDO_TRUSTY

// TrustyConfig is designed for a domain-specific allocator. Add a basic test
// which covers only simple operations and ensure the configuration is able to
// compile.
TEST(ScudoCombinedTest, BasicTrustyConfig) {
  using AllocatorT = scudo::Allocator<scudo::TrustyConfig>;
  auto Allocator = std::unique_ptr<AllocatorT>(new AllocatorT());

  for (scudo::uptr ClassId = 1U;
       ClassId <= scudo::TrustyConfig::SizeClassMap::LargestClassId;
       ClassId++) {
    const scudo::uptr Size =
        scudo::TrustyConfig::SizeClassMap::getSizeByClassId(ClassId);
    void *p = Allocator->allocate(Size - scudo::Chunk::getHeaderSize(), Origin);
    ASSERT_NE(p, nullptr);
    free(p);
  }

  bool UnlockRequired;
  typename AllocatorT::TSDRegistryT::ScopedTSD TSD(
      *Allocator->getTSDRegistry());
  TSD->getCache().drain();

  Allocator->releaseToOS(scudo::ReleaseToOS::Force);
}

#endif
#endif
