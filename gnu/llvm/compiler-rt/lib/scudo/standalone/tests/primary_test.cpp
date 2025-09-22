//===-- primary_test.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "allocator_config.h"
#include "allocator_config_wrapper.h"
#include "condition_variable.h"
#include "primary32.h"
#include "primary64.h"
#include "size_class_map.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <stdlib.h>
#include <thread>
#include <vector>

// Note that with small enough regions, the SizeClassAllocator64 also works on
// 32-bit architectures. It's not something we want to encourage, but we still
// should ensure the tests pass.

template <typename SizeClassMapT> struct TestConfig1 {
  static const bool MaySupportMemoryTagging = false;
  template <typename> using TSDRegistryT = void;
  template <typename> using PrimaryT = void;
  template <typename> using SecondaryT = void;

  struct Primary {
    using SizeClassMap = SizeClassMapT;
    static const scudo::uptr RegionSizeLog = 18U;
    static const scudo::uptr GroupSizeLog = 18U;
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    typedef scudo::uptr CompactPtrT;
    static const scudo::uptr CompactPtrScale = 0;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
  };
};

template <typename SizeClassMapT> struct TestConfig2 {
  static const bool MaySupportMemoryTagging = false;
  template <typename> using TSDRegistryT = void;
  template <typename> using PrimaryT = void;
  template <typename> using SecondaryT = void;

  struct Primary {
    using SizeClassMap = SizeClassMapT;
#if defined(__mips__)
    // Unable to allocate greater size on QEMU-user.
    static const scudo::uptr RegionSizeLog = 23U;
#else
    static const scudo::uptr RegionSizeLog = 24U;
#endif
    static const scudo::uptr GroupSizeLog = 20U;
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    typedef scudo::uptr CompactPtrT;
    static const scudo::uptr CompactPtrScale = 0;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
  };
};

template <typename SizeClassMapT> struct TestConfig3 {
  static const bool MaySupportMemoryTagging = true;
  template <typename> using TSDRegistryT = void;
  template <typename> using PrimaryT = void;
  template <typename> using SecondaryT = void;

  struct Primary {
    using SizeClassMap = SizeClassMapT;
#if defined(__mips__)
    // Unable to allocate greater size on QEMU-user.
    static const scudo::uptr RegionSizeLog = 23U;
#else
    static const scudo::uptr RegionSizeLog = 24U;
#endif
    static const scudo::uptr GroupSizeLog = 20U;
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    typedef scudo::uptr CompactPtrT;
    static const scudo::uptr CompactPtrScale = 0;
    static const bool EnableContiguousRegions = false;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
  };
};

template <typename SizeClassMapT> struct TestConfig4 {
  static const bool MaySupportMemoryTagging = true;
  template <typename> using TSDRegistryT = void;
  template <typename> using PrimaryT = void;
  template <typename> using SecondaryT = void;

  struct Primary {
    using SizeClassMap = SizeClassMapT;
#if defined(__mips__)
    // Unable to allocate greater size on QEMU-user.
    static const scudo::uptr RegionSizeLog = 23U;
#else
    static const scudo::uptr RegionSizeLog = 24U;
#endif
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    static const scudo::uptr CompactPtrScale = 3U;
    static const scudo::uptr GroupSizeLog = 20U;
    typedef scudo::u32 CompactPtrT;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
  };
};

// This is the only test config that enables the condition variable.
template <typename SizeClassMapT> struct TestConfig5 {
  static const bool MaySupportMemoryTagging = true;
  template <typename> using TSDRegistryT = void;
  template <typename> using PrimaryT = void;
  template <typename> using SecondaryT = void;

  struct Primary {
    using SizeClassMap = SizeClassMapT;
#if defined(__mips__)
    // Unable to allocate greater size on QEMU-user.
    static const scudo::uptr RegionSizeLog = 23U;
#else
    static const scudo::uptr RegionSizeLog = 24U;
#endif
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    static const scudo::uptr CompactPtrScale = SCUDO_MIN_ALIGNMENT_LOG;
    static const scudo::uptr GroupSizeLog = 18U;
    typedef scudo::u32 CompactPtrT;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
#if SCUDO_LINUX
    using ConditionVariableT = scudo::ConditionVariableLinux;
#else
    using ConditionVariableT = scudo::ConditionVariableDummy;
#endif
  };
};

template <template <typename> class BaseConfig, typename SizeClassMapT>
struct Config : public BaseConfig<SizeClassMapT> {};

template <template <typename> class BaseConfig, typename SizeClassMapT>
struct SizeClassAllocator
    : public scudo::SizeClassAllocator64<
          scudo::PrimaryConfig<Config<BaseConfig, SizeClassMapT>>> {};
template <typename SizeClassMapT>
struct SizeClassAllocator<TestConfig1, SizeClassMapT>
    : public scudo::SizeClassAllocator32<
          scudo::PrimaryConfig<Config<TestConfig1, SizeClassMapT>>> {};

template <template <typename> class BaseConfig, typename SizeClassMapT>
struct TestAllocator : public SizeClassAllocator<BaseConfig, SizeClassMapT> {
  ~TestAllocator() {
    this->verifyAllBlocksAreReleasedTestOnly();
    this->unmapTestOnly();
  }

  void *operator new(size_t size) {
    void *p = nullptr;
    EXPECT_EQ(0, posix_memalign(&p, alignof(TestAllocator), size));
    return p;
  }

  void operator delete(void *ptr) { free(ptr); }
};

template <template <typename> class BaseConfig>
struct ScudoPrimaryTest : public Test {};

#if SCUDO_FUCHSIA
#define SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                              \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig2)                            \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig3)
#else
#define SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                              \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig1)                            \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig2)                            \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig3)                            \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig4)                            \
  SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TestConfig5)
#endif

#define SCUDO_TYPED_TEST_TYPE(FIXTURE, NAME, TYPE)                             \
  using FIXTURE##NAME##_##TYPE = FIXTURE##NAME<TYPE>;                          \
  TEST_F(FIXTURE##NAME##_##TYPE, NAME) { FIXTURE##NAME<TYPE>::Run(); }

#define SCUDO_TYPED_TEST(FIXTURE, NAME)                                        \
  template <template <typename> class TypeParam>                               \
  struct FIXTURE##NAME : public FIXTURE<TypeParam> {                           \
    void Run();                                                                \
  };                                                                           \
  SCUDO_TYPED_TEST_ALL_TYPES(FIXTURE, NAME)                                    \
  template <template <typename> class TypeParam>                               \
  void FIXTURE##NAME<TypeParam>::Run()

SCUDO_TYPED_TEST(ScudoPrimaryTest, BasicPrimary) {
  using Primary = TestAllocator<TypeParam, scudo::DefaultSizeClassMap>;
  std::unique_ptr<Primary> Allocator(new Primary);
  Allocator->init(/*ReleaseToOsInterval=*/-1);
  typename Primary::CacheT Cache;
  Cache.init(nullptr, Allocator.get());
  const scudo::uptr NumberOfAllocations = 32U;
  for (scudo::uptr I = 0; I <= 16U; I++) {
    const scudo::uptr Size = 1UL << I;
    if (!Primary::canAllocate(Size))
      continue;
    const scudo::uptr ClassId = Primary::SizeClassMap::getClassIdBySize(Size);
    void *Pointers[NumberOfAllocations];
    for (scudo::uptr J = 0; J < NumberOfAllocations; J++) {
      void *P = Cache.allocate(ClassId);
      memset(P, 'B', Size);
      Pointers[J] = P;
    }
    for (scudo::uptr J = 0; J < NumberOfAllocations; J++)
      Cache.deallocate(ClassId, Pointers[J]);
  }
  Cache.destroy(nullptr);
  Allocator->releaseToOS(scudo::ReleaseToOS::Force);
  scudo::ScopedString Str;
  Allocator->getStats(&Str);
  Str.output();
}

struct SmallRegionsConfig {
  static const bool MaySupportMemoryTagging = false;
  template <typename> using TSDRegistryT = void;
  template <typename> using PrimaryT = void;
  template <typename> using SecondaryT = void;

  struct Primary {
    using SizeClassMap = scudo::DefaultSizeClassMap;
    static const scudo::uptr RegionSizeLog = 21U;
    static const scudo::s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const scudo::s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    typedef scudo::uptr CompactPtrT;
    static const scudo::uptr CompactPtrScale = 0;
    static const bool EnableRandomOffset = true;
    static const scudo::uptr MapSizeIncrement = 1UL << 18;
    static const scudo::uptr GroupSizeLog = 20U;
  };
};

// The 64-bit SizeClassAllocator can be easily OOM'd with small region sizes.
// For the 32-bit one, it requires actually exhausting memory, so we skip it.
TEST(ScudoPrimaryTest, Primary64OOM) {
  using Primary =
      scudo::SizeClassAllocator64<scudo::PrimaryConfig<SmallRegionsConfig>>;
  Primary Allocator;
  Allocator.init(/*ReleaseToOsInterval=*/-1);
  typename Primary::CacheT Cache;
  scudo::GlobalStats Stats;
  Stats.init();
  Cache.init(&Stats, &Allocator);
  bool AllocationFailed = false;
  std::vector<void *> Blocks;
  const scudo::uptr ClassId = Primary::SizeClassMap::LargestClassId;
  const scudo::uptr Size = Primary::getSizeByClassId(ClassId);
  const scudo::u16 MaxCachedBlockCount = Primary::CacheT::getMaxCached(Size);

  for (scudo::uptr I = 0; I < 10000U; I++) {
    for (scudo::uptr J = 0; J < MaxCachedBlockCount; ++J) {
      void *Ptr = Cache.allocate(ClassId);
      if (Ptr == nullptr) {
        AllocationFailed = true;
        break;
      }
      memset(Ptr, 'B', Size);
      Blocks.push_back(Ptr);
    }
  }

  for (auto *Ptr : Blocks)
    Cache.deallocate(ClassId, Ptr);

  Cache.destroy(nullptr);
  Allocator.releaseToOS(scudo::ReleaseToOS::Force);
  scudo::ScopedString Str;
  Allocator.getStats(&Str);
  Str.output();
  EXPECT_EQ(AllocationFailed, true);
  Allocator.unmapTestOnly();
}

SCUDO_TYPED_TEST(ScudoPrimaryTest, PrimaryIterate) {
  using Primary = TestAllocator<TypeParam, scudo::DefaultSizeClassMap>;
  std::unique_ptr<Primary> Allocator(new Primary);
  Allocator->init(/*ReleaseToOsInterval=*/-1);
  typename Primary::CacheT Cache;
  Cache.init(nullptr, Allocator.get());
  std::vector<std::pair<scudo::uptr, void *>> V;
  for (scudo::uptr I = 0; I < 64U; I++) {
    const scudo::uptr Size =
        static_cast<scudo::uptr>(std::rand()) % Primary::SizeClassMap::MaxSize;
    const scudo::uptr ClassId = Primary::SizeClassMap::getClassIdBySize(Size);
    void *P = Cache.allocate(ClassId);
    V.push_back(std::make_pair(ClassId, P));
  }
  scudo::uptr Found = 0;
  auto Lambda = [&V, &Found](scudo::uptr Block) {
    for (const auto &Pair : V) {
      if (Pair.second == reinterpret_cast<void *>(Block))
        Found++;
    }
  };
  Allocator->disable();
  Allocator->iterateOverBlocks(Lambda);
  Allocator->enable();
  EXPECT_EQ(Found, V.size());
  while (!V.empty()) {
    auto Pair = V.back();
    Cache.deallocate(Pair.first, Pair.second);
    V.pop_back();
  }
  Cache.destroy(nullptr);
  Allocator->releaseToOS(scudo::ReleaseToOS::Force);
  scudo::ScopedString Str;
  Allocator->getStats(&Str);
  Str.output();
}

SCUDO_TYPED_TEST(ScudoPrimaryTest, PrimaryThreaded) {
  using Primary = TestAllocator<TypeParam, scudo::Config::Primary::SizeClassMap>;
  std::unique_ptr<Primary> Allocator(new Primary);
  Allocator->init(/*ReleaseToOsInterval=*/-1);
  std::mutex Mutex;
  std::condition_variable Cv;
  bool Ready = false;
  std::thread Threads[32];
  for (scudo::uptr I = 0; I < ARRAY_SIZE(Threads); I++) {
    Threads[I] = std::thread([&]() {
      static thread_local typename Primary::CacheT Cache;
      Cache.init(nullptr, Allocator.get());
      std::vector<std::pair<scudo::uptr, void *>> V;
      {
        std::unique_lock<std::mutex> Lock(Mutex);
        while (!Ready)
          Cv.wait(Lock);
      }
      for (scudo::uptr I = 0; I < 256U; I++) {
        const scudo::uptr Size = static_cast<scudo::uptr>(std::rand()) %
                                 Primary::SizeClassMap::MaxSize / 4;
        const scudo::uptr ClassId =
            Primary::SizeClassMap::getClassIdBySize(Size);
        void *P = Cache.allocate(ClassId);
        if (P)
          V.push_back(std::make_pair(ClassId, P));
      }

      // Try to interleave pushBlocks(), popBlocks() and releaseToOS().
      Allocator->releaseToOS(scudo::ReleaseToOS::Force);

      while (!V.empty()) {
        auto Pair = V.back();
        Cache.deallocate(Pair.first, Pair.second);
        V.pop_back();
        // This increases the chance of having non-full TransferBatches and it
        // will jump into the code path of merging TransferBatches.
        if (std::rand() % 8 == 0)
          Cache.drain();
      }
      Cache.destroy(nullptr);
    });
  }
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    Ready = true;
    Cv.notify_all();
  }
  for (auto &T : Threads)
    T.join();
  Allocator->releaseToOS(scudo::ReleaseToOS::Force);
  scudo::ScopedString Str;
  Allocator->getStats(&Str);
  Allocator->getFragmentationInfo(&Str);
  Str.output();
}

// Through a simple allocation that spans two pages, verify that releaseToOS
// actually releases some bytes (at least one page worth). This is a regression
// test for an error in how the release criteria were computed.
SCUDO_TYPED_TEST(ScudoPrimaryTest, ReleaseToOS) {
  using Primary = TestAllocator<TypeParam, scudo::DefaultSizeClassMap>;
  std::unique_ptr<Primary> Allocator(new Primary);
  Allocator->init(/*ReleaseToOsInterval=*/-1);
  typename Primary::CacheT Cache;
  Cache.init(nullptr, Allocator.get());
  const scudo::uptr Size = scudo::getPageSizeCached() * 2;
  EXPECT_TRUE(Primary::canAllocate(Size));
  const scudo::uptr ClassId = Primary::SizeClassMap::getClassIdBySize(Size);
  void *P = Cache.allocate(ClassId);
  EXPECT_NE(P, nullptr);
  Cache.deallocate(ClassId, P);
  Cache.destroy(nullptr);
  EXPECT_GT(Allocator->releaseToOS(scudo::ReleaseToOS::ForceAll), 0U);
}

SCUDO_TYPED_TEST(ScudoPrimaryTest, MemoryGroup) {
  using Primary = TestAllocator<TypeParam, scudo::DefaultSizeClassMap>;
  std::unique_ptr<Primary> Allocator(new Primary);
  Allocator->init(/*ReleaseToOsInterval=*/-1);
  typename Primary::CacheT Cache;
  Cache.init(nullptr, Allocator.get());
  const scudo::uptr Size = 32U;
  const scudo::uptr ClassId = Primary::SizeClassMap::getClassIdBySize(Size);

  // We will allocate 4 times the group size memory and release all of them. We
  // expect the free blocks will be classified with groups. Then we will
  // allocate the same amount of memory as group size and expect the blocks will
  // have the max address difference smaller or equal to 2 times the group size.
  // Note that it isn't necessary to be in the range of single group size
  // because the way we get the group id is doing compact pointer shifting.
  // According to configuration, the compact pointer may not align to group
  // size. As a result, the blocks can cross two groups at most.
  const scudo::uptr GroupSizeMem = (1ULL << Primary::GroupSizeLog);
  const scudo::uptr PeakAllocationMem = 4 * GroupSizeMem;
  const scudo::uptr PeakNumberOfAllocations = PeakAllocationMem / Size;
  const scudo::uptr FinalNumberOfAllocations = GroupSizeMem / Size;
  std::vector<scudo::uptr> Blocks;
  std::mt19937 R;

  for (scudo::uptr I = 0; I < PeakNumberOfAllocations; ++I)
    Blocks.push_back(reinterpret_cast<scudo::uptr>(Cache.allocate(ClassId)));

  std::shuffle(Blocks.begin(), Blocks.end(), R);

  // Release all the allocated blocks, including those held by local cache.
  while (!Blocks.empty()) {
    Cache.deallocate(ClassId, reinterpret_cast<void *>(Blocks.back()));
    Blocks.pop_back();
  }
  Cache.drain();

  for (scudo::uptr I = 0; I < FinalNumberOfAllocations; ++I)
    Blocks.push_back(reinterpret_cast<scudo::uptr>(Cache.allocate(ClassId)));

  EXPECT_LE(*std::max_element(Blocks.begin(), Blocks.end()) -
                *std::min_element(Blocks.begin(), Blocks.end()),
            GroupSizeMem * 2);

  while (!Blocks.empty()) {
    Cache.deallocate(ClassId, reinterpret_cast<void *>(Blocks.back()));
    Blocks.pop_back();
  }
  Cache.drain();
}
