//===-- sanitizer_allocator_test.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
// Tests for sanitizer_allocator.h.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_allocator.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <random>
#include <set>
#include <vector>

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_pthread_wrappers.h"
#include "sanitizer_test_utils.h"

using namespace __sanitizer;

#if defined(__sparcv9)
// FIXME: These tests probably fail because Solaris/sparcv9 uses the full
// 64-bit address space.  Same on Linux/sparc64, so probably a general SPARC
// issue.  Needs more investigation
#  define SKIP_ON_SPARCV9(x) DISABLED_##x
#else
#  define SKIP_ON_SPARCV9(x) x
#endif

// On 64-bit systems with small virtual address spaces (e.g. 39-bit) we can't
// use size class maps with a large number of classes, as that will make the
// SizeClassAllocator64 region size too small (< 2^32).
#if SANITIZER_ANDROID && defined(__aarch64__)
#define ALLOCATOR64_SMALL_SIZE 1
#elif SANITIZER_RISCV64
#define ALLOCATOR64_SMALL_SIZE 1
#else
#define ALLOCATOR64_SMALL_SIZE 0
#endif

// Too slow for debug build
#if !SANITIZER_DEBUG

#if SANITIZER_CAN_USE_ALLOCATOR64
#if SANITIZER_WINDOWS
// On Windows 64-bit there is no easy way to find a large enough fixed address
// space that is always available. Thus, a dynamically allocated address space
// is used instead (i.e. ~(uptr)0).
static const uptr kAllocatorSpace = ~(uptr)0;
static const uptr kAllocatorSize  =  0x8000000000ULL;  // 500G
static const u64 kAddressSpaceSize = 1ULL << 47;
typedef DefaultSizeClassMap SizeClassMap;
#elif SANITIZER_ANDROID && defined(__aarch64__)
static const uptr kAllocatorSpace = 0x3000000000ULL;
static const uptr kAllocatorSize  = 0x2000000000ULL;
static const u64 kAddressSpaceSize = 1ULL << 39;
typedef VeryCompactSizeClassMap SizeClassMap;
#elif SANITIZER_RISCV64
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize = 0x2000000000ULL;  // 128G.
static const u64 kAddressSpaceSize = 1ULL << 38;
typedef VeryDenseSizeClassMap SizeClassMap;
#    elif SANITIZER_APPLE
static const uptr kAllocatorSpace = 0x700000000000ULL;
static const uptr kAllocatorSize  = 0x010000000000ULL;  // 1T.
static const u64 kAddressSpaceSize = 1ULL << 47;
typedef DefaultSizeClassMap SizeClassMap;
#    else
static const uptr kAllocatorSpace = 0x500000000000ULL;
static const uptr kAllocatorSize = 0x010000000000ULL;  // 1T.
static const u64 kAddressSpaceSize = 1ULL << 47;
typedef DefaultSizeClassMap SizeClassMap;
#    endif

template <typename AddressSpaceViewTy>
struct AP64 {  // Allocator Params. Short name for shorter demangled names..
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 16;
  typedef ::SizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceViewTy>
struct AP64Dyn {
  static const uptr kSpaceBeg = ~(uptr)0;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 16;
  typedef ::SizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceViewTy>
struct AP64Compact {
  static const uptr kSpaceBeg = ~(uptr)0;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 16;
  typedef CompactSizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceViewTy>
struct AP64VeryCompact {
  static const uptr kSpaceBeg = ~(uptr)0;
  static const uptr kSpaceSize = 1ULL << 37;
  static const uptr kMetadataSize = 16;
  typedef VeryCompactSizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceViewTy>
struct AP64Dense {
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 16;
  typedef DenseSizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceView>
using Allocator64ASVT = SizeClassAllocator64<AP64<AddressSpaceView>>;
using Allocator64 = Allocator64ASVT<LocalAddressSpaceView>;

template <typename AddressSpaceView>
using Allocator64DynamicASVT = SizeClassAllocator64<AP64Dyn<AddressSpaceView>>;
using Allocator64Dynamic = Allocator64DynamicASVT<LocalAddressSpaceView>;

template <typename AddressSpaceView>
using Allocator64CompactASVT =
    SizeClassAllocator64<AP64Compact<AddressSpaceView>>;
using Allocator64Compact = Allocator64CompactASVT<LocalAddressSpaceView>;

template <typename AddressSpaceView>
using Allocator64VeryCompactASVT =
    SizeClassAllocator64<AP64VeryCompact<AddressSpaceView>>;
using Allocator64VeryCompact =
    Allocator64VeryCompactASVT<LocalAddressSpaceView>;

template <typename AddressSpaceView>
using Allocator64DenseASVT = SizeClassAllocator64<AP64Dense<AddressSpaceView>>;
using Allocator64Dense = Allocator64DenseASVT<LocalAddressSpaceView>;

#elif defined(__mips64)
static const u64 kAddressSpaceSize = 1ULL << 40;
#elif defined(__aarch64__)
static const u64 kAddressSpaceSize = 1ULL << 39;
#elif defined(__s390x__)
static const u64 kAddressSpaceSize = 1ULL << 53;
#elif defined(__s390__)
static const u64 kAddressSpaceSize = 1ULL << 31;
#else
static const u64 kAddressSpaceSize = 1ULL << 32;
#endif

static const uptr kRegionSizeLog = FIRST_32_SECOND_64(20, 24);

template <typename AddressSpaceViewTy>
struct AP32Compact {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = kAddressSpaceSize;
  static const uptr kMetadataSize = 16;
  typedef CompactSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = ::kRegionSizeLog;
  using AddressSpaceView = AddressSpaceViewTy;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
template <typename AddressSpaceView>
using Allocator32CompactASVT =
    SizeClassAllocator32<AP32Compact<AddressSpaceView>>;
using Allocator32Compact = Allocator32CompactASVT<LocalAddressSpaceView>;

template <class SizeClassMap>
void TestSizeClassMap() {
  typedef SizeClassMap SCMap;
  SCMap::Print();
  SCMap::Validate();
}

TEST(SanitizerCommon, DefaultSizeClassMap) {
  TestSizeClassMap<DefaultSizeClassMap>();
}

TEST(SanitizerCommon, CompactSizeClassMap) {
  TestSizeClassMap<CompactSizeClassMap>();
}

TEST(SanitizerCommon, VeryCompactSizeClassMap) {
  TestSizeClassMap<VeryCompactSizeClassMap>();
}

TEST(SanitizerCommon, InternalSizeClassMap) {
  TestSizeClassMap<InternalSizeClassMap>();
}

TEST(SanitizerCommon, DenseSizeClassMap) {
  TestSizeClassMap<VeryCompactSizeClassMap>();
}

template <class Allocator>
void TestSizeClassAllocator(uptr premapped_heap = 0) {
  Allocator *a = new Allocator;
  a->Init(kReleaseToOSIntervalNever, premapped_heap);
  typename Allocator::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);

  static const uptr sizes[] = {
    1, 16,  30, 40, 100, 1000, 10000,
    50000, 60000, 100000, 120000, 300000, 500000, 1000000, 2000000
  };

  std::vector<void *> allocated;

  uptr last_total_allocated = 0;
  for (int i = 0; i < 3; i++) {
    // Allocate a bunch of chunks.
    for (uptr s = 0; s < ARRAY_SIZE(sizes); s++) {
      uptr size = sizes[s];
      if (!a->CanAllocate(size, 1)) continue;
      // printf("s = %ld\n", size);
      uptr n_iter = std::max((uptr)6, 4000000 / size);
      // fprintf(stderr, "size: %ld iter: %ld\n", size, n_iter);
      for (uptr i = 0; i < n_iter; i++) {
        uptr class_id0 = Allocator::SizeClassMapT::ClassID(size);
        char *x = (char*)cache.Allocate(a, class_id0);
        x[0] = 0;
        x[size - 1] = 0;
        x[size / 2] = 0;
        allocated.push_back(x);
        CHECK_EQ(x, a->GetBlockBegin(x));
        CHECK_EQ(x, a->GetBlockBegin(x + size - 1));
        CHECK(a->PointerIsMine(x));
        CHECK(a->PointerIsMine(x + size - 1));
        CHECK(a->PointerIsMine(x + size / 2));
        CHECK_GE(a->GetActuallyAllocatedSize(x), size);
        uptr class_id = a->GetSizeClass(x);
        CHECK_EQ(class_id, Allocator::SizeClassMapT::ClassID(size));
        uptr *metadata = reinterpret_cast<uptr*>(a->GetMetaData(x));
        metadata[0] = reinterpret_cast<uptr>(x) + 1;
        metadata[1] = 0xABCD;
      }
    }
    // Deallocate all.
    for (uptr i = 0; i < allocated.size(); i++) {
      void *x = allocated[i];
      uptr *metadata = reinterpret_cast<uptr*>(a->GetMetaData(x));
      CHECK_EQ(metadata[0], reinterpret_cast<uptr>(x) + 1);
      CHECK_EQ(metadata[1], 0xABCD);
      cache.Deallocate(a, a->GetSizeClass(x), x);
    }
    allocated.clear();
    uptr total_allocated = a->TotalMemoryUsed();
    if (last_total_allocated == 0)
      last_total_allocated = total_allocated;
    CHECK_EQ(last_total_allocated, total_allocated);
  }

  // Check that GetBlockBegin never crashes.
  for (uptr x = 0, step = kAddressSpaceSize / 100000;
       x < kAddressSpaceSize - step; x += step)
    if (a->PointerIsMine(reinterpret_cast<void *>(x)))
      Ident(a->GetBlockBegin(reinterpret_cast<void *>(x)));

  a->TestOnlyUnmap();
  delete a;
}

#if SANITIZER_CAN_USE_ALLOCATOR64

// Allocates kAllocatorSize aligned bytes on construction and frees it on
// destruction.
class ScopedPremappedHeap {
 public:
  ScopedPremappedHeap() {
    BasePtr = MmapNoReserveOrDie(2 * kAllocatorSize, "preallocated heap");
    AlignedAddr = RoundUpTo(reinterpret_cast<uptr>(BasePtr), kAllocatorSize);
  }

  ~ScopedPremappedHeap() { UnmapOrDie(BasePtr, kAllocatorSize); }

  uptr Addr() { return AlignedAddr; }

 private:
  void *BasePtr;
  uptr AlignedAddr;
};

// These tests can fail on Windows if memory is somewhat full and lit happens
// to run them all at the same time. FIXME: Make them not flaky and reenable.
#if !SANITIZER_WINDOWS
TEST(SanitizerCommon, SizeClassAllocator64) {
  TestSizeClassAllocator<Allocator64>();
}

TEST(SanitizerCommon, SizeClassAllocator64Dynamic) {
  TestSizeClassAllocator<Allocator64Dynamic>();
}

#if !ALLOCATOR64_SMALL_SIZE
// Android only has 39-bit address space, so mapping 2 * kAllocatorSize
// sometimes fails.
TEST(SanitizerCommon, SizeClassAllocator64DynamicPremapped) {
  ScopedPremappedHeap h;
  TestSizeClassAllocator<Allocator64Dynamic>(h.Addr());
}

TEST(SanitizerCommon, SizeClassAllocator64Compact) {
  TestSizeClassAllocator<Allocator64Compact>();
}

TEST(SanitizerCommon, SizeClassAllocator64Dense) {
  TestSizeClassAllocator<Allocator64Dense>();
}
#endif

TEST(SanitizerCommon, SizeClassAllocator64VeryCompact) {
  TestSizeClassAllocator<Allocator64VeryCompact>();
}
#endif
#endif

TEST(SanitizerCommon, SizeClassAllocator32Compact) {
  TestSizeClassAllocator<Allocator32Compact>();
}

template <typename AddressSpaceViewTy>
struct AP32SeparateBatches {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = kAddressSpaceSize;
  static const uptr kMetadataSize = 16;
  typedef DefaultSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = ::kRegionSizeLog;
  using AddressSpaceView = AddressSpaceViewTy;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags =
      SizeClassAllocator32FlagMasks::kUseSeparateSizeClassForBatch;
};
template <typename AddressSpaceView>
using Allocator32SeparateBatchesASVT =
    SizeClassAllocator32<AP32SeparateBatches<AddressSpaceView>>;
using Allocator32SeparateBatches =
    Allocator32SeparateBatchesASVT<LocalAddressSpaceView>;

TEST(SanitizerCommon, SizeClassAllocator32SeparateBatches) {
  TestSizeClassAllocator<Allocator32SeparateBatches>();
}

template <class Allocator>
void SizeClassAllocatorMetadataStress(uptr premapped_heap = 0) {
  Allocator *a = new Allocator;
  a->Init(kReleaseToOSIntervalNever, premapped_heap);
  typename Allocator::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);

  const uptr kNumAllocs = 1 << 13;
  void *allocated[kNumAllocs];
  void *meta[kNumAllocs];
  for (uptr i = 0; i < kNumAllocs; i++) {
    void *x = cache.Allocate(a, 1 + i % (Allocator::kNumClasses - 1));
    allocated[i] = x;
    meta[i] = a->GetMetaData(x);
  }
  // Get Metadata kNumAllocs^2 times.
  for (uptr i = 0; i < kNumAllocs * kNumAllocs; i++) {
    uptr idx = i % kNumAllocs;
    void *m = a->GetMetaData(allocated[idx]);
    EXPECT_EQ(m, meta[idx]);
  }
  for (uptr i = 0; i < kNumAllocs; i++) {
    cache.Deallocate(a, 1 + i % (Allocator::kNumClasses - 1), allocated[i]);
  }

  a->TestOnlyUnmap();
  delete a;
}

#if SANITIZER_CAN_USE_ALLOCATOR64
// These tests can fail on Windows if memory is somewhat full and lit happens
// to run them all at the same time. FIXME: Make them not flaky and reenable.
#if !SANITIZER_WINDOWS
TEST(SanitizerCommon, SizeClassAllocator64MetadataStress) {
  SizeClassAllocatorMetadataStress<Allocator64>();
}

TEST(SanitizerCommon, SizeClassAllocator64DynamicMetadataStress) {
  SizeClassAllocatorMetadataStress<Allocator64Dynamic>();
}

#if !ALLOCATOR64_SMALL_SIZE
TEST(SanitizerCommon, SizeClassAllocator64DynamicPremappedMetadataStress) {
  ScopedPremappedHeap h;
  SizeClassAllocatorMetadataStress<Allocator64Dynamic>(h.Addr());
}

TEST(SanitizerCommon, SizeClassAllocator64CompactMetadataStress) {
  SizeClassAllocatorMetadataStress<Allocator64Compact>();
}
#endif

#endif
#endif  // SANITIZER_CAN_USE_ALLOCATOR64
TEST(SanitizerCommon, SizeClassAllocator32CompactMetadataStress) {
  SizeClassAllocatorMetadataStress<Allocator32Compact>();
}

template <class Allocator>
void SizeClassAllocatorGetBlockBeginStress(u64 TotalSize,
                                           uptr premapped_heap = 0) {
  Allocator *a = new Allocator;
  a->Init(kReleaseToOSIntervalNever, premapped_heap);
  typename Allocator::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);

  uptr max_size_class = Allocator::SizeClassMapT::kLargestClassID;
  uptr size = Allocator::SizeClassMapT::Size(max_size_class);
  // Make sure we correctly compute GetBlockBegin() w/o overflow.
  for (size_t i = 0; i <= TotalSize / size; i++) {
    void *x = cache.Allocate(a, max_size_class);
    void *beg = a->GetBlockBegin(x);
    // if ((i & (i - 1)) == 0)
    //   fprintf(stderr, "[%zd] %p %p\n", i, x, beg);
    EXPECT_EQ(x, beg);
  }

  a->TestOnlyUnmap();
  delete a;
}

#if SANITIZER_CAN_USE_ALLOCATOR64
// These tests can fail on Windows if memory is somewhat full and lit happens
// to run them all at the same time. FIXME: Make them not flaky and reenable.
#if !SANITIZER_WINDOWS
TEST(SanitizerCommon, SizeClassAllocator64GetBlockBegin) {
  SizeClassAllocatorGetBlockBeginStress<Allocator64>(
      1ULL << (SANITIZER_ANDROID ? 31 : 33));
}
TEST(SanitizerCommon, SizeClassAllocator64DynamicGetBlockBegin) {
  SizeClassAllocatorGetBlockBeginStress<Allocator64Dynamic>(
      1ULL << (SANITIZER_ANDROID ? 31 : 33));
}
#if !ALLOCATOR64_SMALL_SIZE
TEST(SanitizerCommon, SizeClassAllocator64DynamicPremappedGetBlockBegin) {
  ScopedPremappedHeap h;
  SizeClassAllocatorGetBlockBeginStress<Allocator64Dynamic>(
      1ULL << (SANITIZER_ANDROID ? 31 : 33), h.Addr());
}
TEST(SanitizerCommon, SizeClassAllocator64CompactGetBlockBegin) {
  SizeClassAllocatorGetBlockBeginStress<Allocator64Compact>(1ULL << 33);
}
#endif
TEST(SanitizerCommon, SizeClassAllocator64VeryCompactGetBlockBegin) {
  // Does not have > 4Gb for each class.
  SizeClassAllocatorGetBlockBeginStress<Allocator64VeryCompact>(1ULL << 31);
}
TEST(SanitizerCommon, SizeClassAllocator32CompactGetBlockBegin) {
  SizeClassAllocatorGetBlockBeginStress<Allocator32Compact>(1ULL << 33);
}
#endif
#endif  // SANITIZER_CAN_USE_ALLOCATOR64

struct TestMapUnmapCallback {
  static int map_count, map_secondary_count, unmap_count;
  void OnMap(uptr p, uptr size) const { map_count++; }
  void OnMapSecondary(uptr p, uptr size, uptr user_begin,
                      uptr user_size) const {
    map_secondary_count++;
  }
  void OnUnmap(uptr p, uptr size) const { unmap_count++; }

  static void Reset() { map_count = map_secondary_count = unmap_count = 0; }
};
int TestMapUnmapCallback::map_count;
int TestMapUnmapCallback::map_secondary_count;
int TestMapUnmapCallback::unmap_count;

#if SANITIZER_CAN_USE_ALLOCATOR64
// These tests can fail on Windows if memory is somewhat full and lit happens
// to run them all at the same time. FIXME: Make them not flaky and reenable.
#if !SANITIZER_WINDOWS

template <typename AddressSpaceViewTy = LocalAddressSpaceView>
struct AP64WithCallback {
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 16;
  typedef ::SizeClassMap SizeClassMap;
  typedef TestMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

TEST(SanitizerCommon, SizeClassAllocator64MapUnmapCallback) {
  TestMapUnmapCallback::Reset();
  typedef SizeClassAllocator64<AP64WithCallback<>> Allocator64WithCallBack;
  Allocator64WithCallBack *a = new Allocator64WithCallBack;
  a->Init(kReleaseToOSIntervalNever);
  EXPECT_EQ(TestMapUnmapCallback::map_count, 1);  // Allocator state.
  EXPECT_EQ(TestMapUnmapCallback::map_secondary_count, 0);
  typename Allocator64WithCallBack::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);
  AllocatorStats stats;
  stats.Init();
  const size_t kNumChunks = 128;
  uint32_t chunks[kNumChunks];
  a->GetFromAllocator(&stats, 30, chunks, kNumChunks);
  // State + alloc + metadata + freearray.
  EXPECT_EQ(TestMapUnmapCallback::map_count, 4);
  EXPECT_EQ(TestMapUnmapCallback::map_secondary_count, 0);
  a->TestOnlyUnmap();
  EXPECT_EQ(TestMapUnmapCallback::unmap_count, 1);  // The whole thing.
  delete a;
}
#endif
#endif

template <typename AddressSpaceViewTy = LocalAddressSpaceView>
struct AP32WithCallback {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = kAddressSpaceSize;
  static const uptr kMetadataSize = 16;
  typedef CompactSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = ::kRegionSizeLog;
  using AddressSpaceView = AddressSpaceViewTy;
  typedef TestMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};

TEST(SanitizerCommon, SizeClassAllocator32MapUnmapCallback) {
  TestMapUnmapCallback::Reset();
  typedef SizeClassAllocator32<AP32WithCallback<>> Allocator32WithCallBack;
  Allocator32WithCallBack *a = new Allocator32WithCallBack;
  a->Init(kReleaseToOSIntervalNever);
  EXPECT_EQ(TestMapUnmapCallback::map_count, 0);
  EXPECT_EQ(TestMapUnmapCallback::map_secondary_count, 0);
  Allocator32WithCallBack::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);
  AllocatorStats stats;
  stats.Init();
  a->AllocateBatch(&stats, &cache, 32);
  EXPECT_EQ(TestMapUnmapCallback::map_count, 1);
  EXPECT_EQ(TestMapUnmapCallback::map_secondary_count, 0);
  a->TestOnlyUnmap();
  EXPECT_EQ(TestMapUnmapCallback::unmap_count, 1);
  delete a;
}

TEST(SanitizerCommon, LargeMmapAllocatorMapUnmapCallback) {
  TestMapUnmapCallback::Reset();
  LargeMmapAllocator<TestMapUnmapCallback> a;
  a.Init();
  AllocatorStats stats;
  stats.Init();
  void *x = a.Allocate(&stats, 1 << 20, 1);
  EXPECT_EQ(TestMapUnmapCallback::map_count, 0);
  EXPECT_EQ(TestMapUnmapCallback::map_secondary_count, 1);
  a.Deallocate(&stats, x);
  EXPECT_EQ(TestMapUnmapCallback::unmap_count, 1);
}

// Don't test OOM conditions on Win64 because it causes other tests on the same
// machine to OOM.
#if SANITIZER_CAN_USE_ALLOCATOR64 && !SANITIZER_WINDOWS64
TEST(SanitizerCommon, SizeClassAllocator64Overflow) {
  Allocator64 a;
  a.Init(kReleaseToOSIntervalNever);
  Allocator64::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);
  AllocatorStats stats;
  stats.Init();

  const size_t kNumChunks = 128;
  uint32_t chunks[kNumChunks];
  bool allocation_failed = false;
  for (int i = 0; i < 1000000; i++) {
    uptr class_id = a.kNumClasses - 1;
    if (!a.GetFromAllocator(&stats, class_id, chunks, kNumChunks)) {
      allocation_failed = true;
      break;
    }
  }
  EXPECT_EQ(allocation_failed, true);

  a.TestOnlyUnmap();
}
#endif

TEST(SanitizerCommon, LargeMmapAllocator) {
  LargeMmapAllocator<NoOpMapUnmapCallback> a;
  a.Init();
  AllocatorStats stats;
  stats.Init();

  static const int kNumAllocs = 1000;
  char *allocated[kNumAllocs];
  static const uptr size = 4000;
  // Allocate some.
  for (int i = 0; i < kNumAllocs; i++) {
    allocated[i] = (char *)a.Allocate(&stats, size, 1);
    CHECK(a.PointerIsMine(allocated[i]));
  }
  // Deallocate all.
  CHECK_GT(a.TotalMemoryUsed(), size * kNumAllocs);
  for (int i = 0; i < kNumAllocs; i++) {
    char *p = allocated[i];
    CHECK(a.PointerIsMine(p));
    a.Deallocate(&stats, p);
  }
  // Check that non left.
  CHECK_EQ(a.TotalMemoryUsed(), 0);

  // Allocate some more, also add metadata.
  for (int i = 0; i < kNumAllocs; i++) {
    char *x = (char *)a.Allocate(&stats, size, 1);
    CHECK_GE(a.GetActuallyAllocatedSize(x), size);
    uptr *meta = reinterpret_cast<uptr*>(a.GetMetaData(x));
    *meta = i;
    allocated[i] = x;
  }
  for (int i = 0; i < kNumAllocs * kNumAllocs; i++) {
    char *p = allocated[i % kNumAllocs];
    CHECK(a.PointerIsMine(p));
    CHECK(a.PointerIsMine(p + 2000));
  }
  CHECK_GT(a.TotalMemoryUsed(), size * kNumAllocs);
  // Deallocate all in reverse order.
  for (int i = 0; i < kNumAllocs; i++) {
    int idx = kNumAllocs - i - 1;
    char *p = allocated[idx];
    uptr *meta = reinterpret_cast<uptr*>(a.GetMetaData(p));
    CHECK_EQ(*meta, idx);
    CHECK(a.PointerIsMine(p));
    a.Deallocate(&stats, p);
  }
  CHECK_EQ(a.TotalMemoryUsed(), 0);

  // Test alignments. Test with 512MB alignment on x64 non-Windows machines.
  // Windows doesn't overcommit, and many machines do not have 51.2GB of swap.
  uptr max_alignment =
      (SANITIZER_WORDSIZE == 64 && !SANITIZER_WINDOWS) ? (1 << 28) : (1 << 24);
  for (uptr alignment = 8; alignment <= max_alignment; alignment *= 2) {
    const uptr kNumAlignedAllocs = 100;
    for (uptr i = 0; i < kNumAlignedAllocs; i++) {
      uptr size = ((i % 10) + 1) * 4096;
      char *p = allocated[i] = (char *)a.Allocate(&stats, size, alignment);
      CHECK_EQ(p, a.GetBlockBegin(p));
      CHECK_EQ(p, a.GetBlockBegin(p + size - 1));
      CHECK_EQ(p, a.GetBlockBegin(p + size / 2));
      CHECK_EQ(0, (uptr)allocated[i] % alignment);
      p[0] = p[size - 1] = 0;
    }
    for (uptr i = 0; i < kNumAlignedAllocs; i++) {
      a.Deallocate(&stats, allocated[i]);
    }
  }

  // Regression test for boundary condition in GetBlockBegin().
  uptr page_size = GetPageSizeCached();
  char *p = (char *)a.Allocate(&stats, page_size, 1);
  CHECK_EQ(p, a.GetBlockBegin(p));
  CHECK_EQ(p, (char *)a.GetBlockBegin(p + page_size - 1));
  CHECK_NE(p, (char *)a.GetBlockBegin(p + page_size));
  a.Deallocate(&stats, p);
}

template <class PrimaryAllocator>
void TestCombinedAllocator(uptr premapped_heap = 0) {
  typedef CombinedAllocator<PrimaryAllocator> Allocator;
  Allocator *a = new Allocator;
  a->Init(kReleaseToOSIntervalNever, premapped_heap);
  std::mt19937 r;

  typename Allocator::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  a->InitCache(&cache);

  EXPECT_EQ(a->Allocate(&cache, -1, 1), (void*)0);
  EXPECT_EQ(a->Allocate(&cache, -1, 1024), (void*)0);
  EXPECT_EQ(a->Allocate(&cache, (uptr)-1 - 1024, 1), (void*)0);
  EXPECT_EQ(a->Allocate(&cache, (uptr)-1 - 1024, 1024), (void*)0);
  EXPECT_EQ(a->Allocate(&cache, (uptr)-1 - 1023, 1024), (void*)0);
  EXPECT_EQ(a->Allocate(&cache, -1, 1), (void*)0);

  const uptr kNumAllocs = 100000;
  const uptr kNumIter = 10;
  for (uptr iter = 0; iter < kNumIter; iter++) {
    std::vector<void*> allocated;
    for (uptr i = 0; i < kNumAllocs; i++) {
      uptr size = (i % (1 << 14)) + 1;
      if ((i % 1024) == 0)
        size = 1 << (10 + (i % 14));
      void *x = a->Allocate(&cache, size, 1);
      uptr *meta = reinterpret_cast<uptr*>(a->GetMetaData(x));
      CHECK_EQ(*meta, 0);
      *meta = size;
      allocated.push_back(x);
    }

    std::shuffle(allocated.begin(), allocated.end(), r);

    // Test ForEachChunk(...)
    {
      std::set<void *> reported_chunks;
      auto cb = [](uptr chunk, void *arg) {
        auto reported_chunks_ptr = reinterpret_cast<std::set<void *> *>(arg);
        auto pair =
            reported_chunks_ptr->insert(reinterpret_cast<void *>(chunk));
        // Check chunk is never reported more than once.
        ASSERT_TRUE(pair.second);
      };
      a->ForEachChunk(cb, reinterpret_cast<void *>(&reported_chunks));
      for (const auto &allocated_ptr : allocated) {
        ASSERT_NE(reported_chunks.find(allocated_ptr), reported_chunks.end());
      }
    }

    for (uptr i = 0; i < kNumAllocs; i++) {
      void *x = allocated[i];
      uptr *meta = reinterpret_cast<uptr*>(a->GetMetaData(x));
      CHECK_NE(*meta, 0);
      CHECK(a->PointerIsMine(x));
      *meta = 0;
      a->Deallocate(&cache, x);
    }
    allocated.clear();
    a->SwallowCache(&cache);
  }
  a->DestroyCache(&cache);
  a->TestOnlyUnmap();
}

#if SANITIZER_CAN_USE_ALLOCATOR64
TEST(SanitizerCommon, CombinedAllocator64) {
  TestCombinedAllocator<Allocator64>();
}

TEST(SanitizerCommon, CombinedAllocator64Dynamic) {
  TestCombinedAllocator<Allocator64Dynamic>();
}

#if !ALLOCATOR64_SMALL_SIZE
#if !SANITIZER_WINDOWS
// Windows fails to map 1TB, so disable this test.
TEST(SanitizerCommon, CombinedAllocator64DynamicPremapped) {
  ScopedPremappedHeap h;
  TestCombinedAllocator<Allocator64Dynamic>(h.Addr());
}
#endif

TEST(SanitizerCommon, CombinedAllocator64Compact) {
  TestCombinedAllocator<Allocator64Compact>();
}
#endif

TEST(SanitizerCommon, CombinedAllocator64VeryCompact) {
  TestCombinedAllocator<Allocator64VeryCompact>();
}
#endif

TEST(SanitizerCommon, SKIP_ON_SPARCV9(CombinedAllocator32Compact)) {
  TestCombinedAllocator<Allocator32Compact>();
}

template <class Allocator>
void TestSizeClassAllocatorLocalCache(uptr premapped_heap = 0) {
  using AllocatorCache = typename Allocator::AllocatorCache;
  AllocatorCache cache;
  Allocator *a = new Allocator();

  a->Init(kReleaseToOSIntervalNever, premapped_heap);
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);

  const uptr kNumAllocs = 10000;
  const int kNumIter = 100;
  uptr saved_total = 0;
  for (int class_id = 1; class_id <= 5; class_id++) {
    for (int it = 0; it < kNumIter; it++) {
      void *allocated[kNumAllocs];
      for (uptr i = 0; i < kNumAllocs; i++) {
        allocated[i] = cache.Allocate(a, class_id);
      }
      for (uptr i = 0; i < kNumAllocs; i++) {
        cache.Deallocate(a, class_id, allocated[i]);
      }
      cache.Drain(a);
      uptr total_allocated = a->TotalMemoryUsed();
      if (it)
        CHECK_EQ(saved_total, total_allocated);
      saved_total = total_allocated;
    }
  }

  a->TestOnlyUnmap();
  delete a;
}

#if SANITIZER_CAN_USE_ALLOCATOR64
// These tests can fail on Windows if memory is somewhat full and lit happens
// to run them all at the same time. FIXME: Make them not flaky and reenable.
#if !SANITIZER_WINDOWS
TEST(SanitizerCommon, SizeClassAllocator64LocalCache) {
  TestSizeClassAllocatorLocalCache<Allocator64>();
}

TEST(SanitizerCommon, SizeClassAllocator64DynamicLocalCache) {
  TestSizeClassAllocatorLocalCache<Allocator64Dynamic>();
}

#if !ALLOCATOR64_SMALL_SIZE
TEST(SanitizerCommon, SizeClassAllocator64DynamicPremappedLocalCache) {
  ScopedPremappedHeap h;
  TestSizeClassAllocatorLocalCache<Allocator64Dynamic>(h.Addr());
}

TEST(SanitizerCommon, SizeClassAllocator64CompactLocalCache) {
  TestSizeClassAllocatorLocalCache<Allocator64Compact>();
}
#endif
TEST(SanitizerCommon, SizeClassAllocator64VeryCompactLocalCache) {
  TestSizeClassAllocatorLocalCache<Allocator64VeryCompact>();
}
#endif
#endif

TEST(SanitizerCommon, SizeClassAllocator32CompactLocalCache) {
  TestSizeClassAllocatorLocalCache<Allocator32Compact>();
}

#if SANITIZER_CAN_USE_ALLOCATOR64
typedef Allocator64::AllocatorCache AllocatorCache;
static AllocatorCache static_allocator_cache;

void *AllocatorLeakTestWorker(void *arg) {
  typedef AllocatorCache::Allocator Allocator;
  Allocator *a = (Allocator*)(arg);
  static_allocator_cache.Allocate(a, 10);
  static_allocator_cache.Drain(a);
  return 0;
}

TEST(SanitizerCommon, AllocatorLeakTest) {
  typedef AllocatorCache::Allocator Allocator;
  Allocator a;
  a.Init(kReleaseToOSIntervalNever);
  uptr total_used_memory = 0;
  for (int i = 0; i < 100; i++) {
    pthread_t t;
    PTHREAD_CREATE(&t, 0, AllocatorLeakTestWorker, &a);
    PTHREAD_JOIN(t, 0);
    if (i == 0)
      total_used_memory = a.TotalMemoryUsed();
    EXPECT_EQ(a.TotalMemoryUsed(), total_used_memory);
  }

  a.TestOnlyUnmap();
}

// Struct which is allocated to pass info to new threads.  The new thread frees
// it.
struct NewThreadParams {
  AllocatorCache *thread_cache;
  AllocatorCache::Allocator *allocator;
  uptr class_id;
};

// Called in a new thread.  Just frees its argument.
static void *DeallocNewThreadWorker(void *arg) {
  NewThreadParams *params = reinterpret_cast<NewThreadParams*>(arg);
  params->thread_cache->Deallocate(params->allocator, params->class_id, params);
  return NULL;
}

// The allocator cache is supposed to be POD and zero initialized.  We should be
// able to call Deallocate on a zeroed cache, and it will self-initialize.
TEST(Allocator, AllocatorCacheDeallocNewThread) {
  AllocatorCache::Allocator allocator;
  allocator.Init(kReleaseToOSIntervalNever);
  AllocatorCache main_cache;
  AllocatorCache child_cache;
  memset(&main_cache, 0, sizeof(main_cache));
  memset(&child_cache, 0, sizeof(child_cache));

  uptr class_id = DefaultSizeClassMap::ClassID(sizeof(NewThreadParams));
  NewThreadParams *params = reinterpret_cast<NewThreadParams*>(
      main_cache.Allocate(&allocator, class_id));
  params->thread_cache = &child_cache;
  params->allocator = &allocator;
  params->class_id = class_id;
  pthread_t t;
  PTHREAD_CREATE(&t, 0, DeallocNewThreadWorker, params);
  PTHREAD_JOIN(t, 0);

  allocator.TestOnlyUnmap();
}
#endif

TEST(Allocator, Basic) {
  char *p = (char*)InternalAlloc(10);
  EXPECT_NE(p, (char*)0);
  char *p2 = (char*)InternalAlloc(20);
  EXPECT_NE(p2, (char*)0);
  EXPECT_NE(p2, p);
  InternalFree(p);
  InternalFree(p2);
}

TEST(Allocator, Stress) {
  const int kCount = 1000;
  char *ptrs[kCount];
  unsigned rnd = 42;
  for (int i = 0; i < kCount; i++) {
    uptr sz = my_rand_r(&rnd) % 1000;
    char *p = (char*)InternalAlloc(sz);
    EXPECT_NE(p, (char*)0);
    ptrs[i] = p;
  }
  for (int i = 0; i < kCount; i++) {
    InternalFree(ptrs[i]);
  }
}

TEST(Allocator, LargeAlloc) {
  void *p = InternalAlloc(10 << 20);
  InternalFree(p);
}

TEST(Allocator, ScopedBuffer) {
  const int kSize = 512;
  {
    InternalMmapVector<int> int_buf(kSize);
    EXPECT_EQ((uptr)kSize, int_buf.size());
  }
  InternalMmapVector<char> char_buf(kSize);
  EXPECT_EQ((uptr)kSize, char_buf.size());
  internal_memset(char_buf.data(), 'c', kSize);
  for (int i = 0; i < kSize; i++) {
    EXPECT_EQ('c', char_buf[i]);
  }
}

void IterationTestCallback(uptr chunk, void *arg) {
  reinterpret_cast<std::set<uptr> *>(arg)->insert(chunk);
}

template <class Allocator>
void TestSizeClassAllocatorIteration(uptr premapped_heap = 0) {
  Allocator *a = new Allocator;
  a->Init(kReleaseToOSIntervalNever, premapped_heap);
  typename Allocator::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);

  static const uptr sizes[] = {1, 16, 30, 40, 100, 1000, 10000,
    50000, 60000, 100000, 120000, 300000, 500000, 1000000, 2000000};

  std::vector<void *> allocated;

  // Allocate a bunch of chunks.
  for (uptr s = 0; s < ARRAY_SIZE(sizes); s++) {
    uptr size = sizes[s];
    if (!a->CanAllocate(size, 1)) continue;
    // printf("s = %ld\n", size);
    uptr n_iter = std::max((uptr)6, 80000 / size);
    // fprintf(stderr, "size: %ld iter: %ld\n", size, n_iter);
    for (uptr j = 0; j < n_iter; j++) {
      uptr class_id0 = Allocator::SizeClassMapT::ClassID(size);
      void *x = cache.Allocate(a, class_id0);
      allocated.push_back(x);
    }
  }

  std::set<uptr> reported_chunks;
  a->ForceLock();
  a->ForEachChunk(IterationTestCallback, &reported_chunks);
  a->ForceUnlock();

  for (uptr i = 0; i < allocated.size(); i++) {
    // Don't use EXPECT_NE. Reporting the first mismatch is enough.
    ASSERT_NE(reported_chunks.find(reinterpret_cast<uptr>(allocated[i])),
              reported_chunks.end());
  }

  a->TestOnlyUnmap();
  delete a;
}

#if SANITIZER_CAN_USE_ALLOCATOR64
// These tests can fail on Windows if memory is somewhat full and lit happens
// to run them all at the same time. FIXME: Make them not flaky and reenable.
#if !SANITIZER_WINDOWS
TEST(SanitizerCommon, SizeClassAllocator64Iteration) {
  TestSizeClassAllocatorIteration<Allocator64>();
}
TEST(SanitizerCommon, SizeClassAllocator64DynamicIteration) {
  TestSizeClassAllocatorIteration<Allocator64Dynamic>();
}
#if !ALLOCATOR64_SMALL_SIZE
TEST(SanitizerCommon, SizeClassAllocator64DynamicPremappedIteration) {
  ScopedPremappedHeap h;
  TestSizeClassAllocatorIteration<Allocator64Dynamic>(h.Addr());
}
#endif
#endif
#endif

TEST(SanitizerCommon, SKIP_ON_SPARCV9(SizeClassAllocator32Iteration)) {
  TestSizeClassAllocatorIteration<Allocator32Compact>();
}

TEST(SanitizerCommon, LargeMmapAllocatorIteration) {
  LargeMmapAllocator<NoOpMapUnmapCallback> a;
  a.Init();
  AllocatorStats stats;
  stats.Init();

  static const uptr kNumAllocs = 1000;
  char *allocated[kNumAllocs];
  static const uptr size = 40;
  // Allocate some.
  for (uptr i = 0; i < kNumAllocs; i++)
    allocated[i] = (char *)a.Allocate(&stats, size, 1);

  std::set<uptr> reported_chunks;
  a.ForceLock();
  a.ForEachChunk(IterationTestCallback, &reported_chunks);
  a.ForceUnlock();

  for (uptr i = 0; i < kNumAllocs; i++) {
    // Don't use EXPECT_NE. Reporting the first mismatch is enough.
    ASSERT_NE(reported_chunks.find(reinterpret_cast<uptr>(allocated[i])),
              reported_chunks.end());
  }
  for (uptr i = 0; i < kNumAllocs; i++)
    a.Deallocate(&stats, allocated[i]);
}

TEST(SanitizerCommon, LargeMmapAllocatorBlockBegin) {
  LargeMmapAllocator<NoOpMapUnmapCallback> a;
  a.Init();
  AllocatorStats stats;
  stats.Init();

  static const uptr kNumAllocs = 1024;
  static const uptr kNumExpectedFalseLookups = 10000000;
  char *allocated[kNumAllocs];
  static const uptr size = 4096;
  // Allocate some.
  for (uptr i = 0; i < kNumAllocs; i++) {
    allocated[i] = (char *)a.Allocate(&stats, size, 1);
  }

  a.ForceLock();
  for (uptr i = 0; i < kNumAllocs  * kNumAllocs; i++) {
    // if ((i & (i - 1)) == 0) fprintf(stderr, "[%zd]\n", i);
    char *p1 = allocated[i % kNumAllocs];
    EXPECT_EQ(p1, a.GetBlockBeginFastLocked(p1));
    EXPECT_EQ(p1, a.GetBlockBeginFastLocked(p1 + size / 2));
    EXPECT_EQ(p1, a.GetBlockBeginFastLocked(p1 + size - 1));
    EXPECT_EQ(p1, a.GetBlockBeginFastLocked(p1 - 100));
  }

  for (uptr i = 0; i < kNumExpectedFalseLookups; i++) {
    void *p = reinterpret_cast<void *>(i % 1024);
    EXPECT_EQ((void *)0, a.GetBlockBeginFastLocked(p));
    p = reinterpret_cast<void *>(~0L - (i % 1024));
    EXPECT_EQ((void *)0, a.GetBlockBeginFastLocked(p));
  }
  a.ForceUnlock();

  for (uptr i = 0; i < kNumAllocs; i++)
    a.Deallocate(&stats, allocated[i]);
}


// Don't test OOM conditions on Win64 because it causes other tests on the same
// machine to OOM.
#if SANITIZER_CAN_USE_ALLOCATOR64 && !SANITIZER_WINDOWS64 && !ALLOCATOR64_SMALL_SIZE
typedef __sanitizer::SizeClassMap<2, 22, 22, 34, 128, 16> SpecialSizeClassMap;
template <typename AddressSpaceViewTy = LocalAddressSpaceView>
struct AP64_SpecialSizeClassMap {
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef SpecialSizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

// Regression test for out-of-memory condition in PopulateFreeList().
TEST(SanitizerCommon, SizeClassAllocator64PopulateFreeListOOM) {
  // In a world where regions are small and chunks are huge...
  typedef SizeClassAllocator64<AP64_SpecialSizeClassMap<>> SpecialAllocator64;
  const uptr kRegionSize =
      kAllocatorSize / SpecialSizeClassMap::kNumClassesRounded;
  SpecialAllocator64 *a = new SpecialAllocator64;
  a->Init(kReleaseToOSIntervalNever);
  SpecialAllocator64::AllocatorCache cache;
  memset(&cache, 0, sizeof(cache));
  cache.Init(0);

  // ...one man is on a mission to overflow a region with a series of
  // successive allocations.

  const uptr kClassID = 24;
  const uptr kAllocationSize = SpecialSizeClassMap::Size(kClassID);
  ASSERT_LT(2 * kAllocationSize, kRegionSize);
  ASSERT_GT(3 * kAllocationSize, kRegionSize);
  EXPECT_NE(cache.Allocate(a, kClassID), nullptr);
  EXPECT_NE(cache.Allocate(a, kClassID), nullptr);
  EXPECT_EQ(cache.Allocate(a, kClassID), nullptr);

  const uptr Class2 = 21;
  const uptr Size2 = SpecialSizeClassMap::Size(Class2);
  ASSERT_EQ(Size2 * 8, kRegionSize);
  char *p[7];
  for (int i = 0; i < 7; i++) {
    p[i] = (char*)cache.Allocate(a, Class2);
    EXPECT_NE(p[i], nullptr);
    fprintf(stderr, "p[%d] %p s = %lx\n", i, (void*)p[i], Size2);
    p[i][Size2 - 1] = 42;
    if (i) ASSERT_LT(p[i - 1], p[i]);
  }
  EXPECT_EQ(cache.Allocate(a, Class2), nullptr);
  cache.Deallocate(a, Class2, p[0]);
  cache.Drain(a);
  ASSERT_EQ(p[6][Size2 - 1], 42);
  a->TestOnlyUnmap();
  delete a;
}

#endif

#if SANITIZER_CAN_USE_ALLOCATOR64

class NoMemoryMapper {
 public:
  uptr last_request_buffer_size = 0;

  u64 *MapPackedCounterArrayBuffer(uptr buffer_size) {
    last_request_buffer_size = buffer_size * sizeof(u64);
    return nullptr;
  }
};

class RedZoneMemoryMapper {
 public:
  RedZoneMemoryMapper() {
    const auto page_size = GetPageSize();
    buffer = MmapOrDie(3ULL * page_size, "");
    MprotectNoAccess(reinterpret_cast<uptr>(buffer), page_size);
    MprotectNoAccess(reinterpret_cast<uptr>(buffer) + page_size * 2, page_size);
  }
  ~RedZoneMemoryMapper() { UnmapOrDie(buffer, 3 * GetPageSize()); }

  u64 *MapPackedCounterArrayBuffer(uptr buffer_size) {
    buffer_size *= sizeof(u64);
    const auto page_size = GetPageSize();
    CHECK_EQ(buffer_size, page_size);
    u64 *p =
        reinterpret_cast<u64 *>(reinterpret_cast<uptr>(buffer) + page_size);
    memset(p, 0, page_size);
    return p;
  }

 private:
  void *buffer;
};

TEST(SanitizerCommon, SizeClassAllocator64PackedCounterArray) {
  NoMemoryMapper no_memory_mapper;
  for (int i = 0; i < 64; i++) {
    // Various valid counter's max values packed into one word.
    Allocator64::PackedCounterArray counters_2n(1, 1ULL << i,
                                                &no_memory_mapper);
    EXPECT_EQ(8ULL, no_memory_mapper.last_request_buffer_size);

    // Check the "all bit set" values too.
    Allocator64::PackedCounterArray counters_2n1_1(1, ~0ULL >> i,
                                                   &no_memory_mapper);
    EXPECT_EQ(8ULL, no_memory_mapper.last_request_buffer_size);

    // Verify the packing ratio, the counter is expected to be packed into the
    // closest power of 2 bits.
    Allocator64::PackedCounterArray counters(64, 1ULL << i, &no_memory_mapper);
    EXPECT_EQ(8ULL * RoundUpToPowerOfTwo(i + 1),
              no_memory_mapper.last_request_buffer_size);
  }

  RedZoneMemoryMapper memory_mapper;
  // Go through 1, 2, 4, 8, .. 64 bits per counter.
  for (int i = 0; i < 7; i++) {
    // Make sure counters request one memory page for the buffer.
    const u64 kNumCounters = (GetPageSize() / 8) * (64 >> i);
    Allocator64::PackedCounterArray counters(
        kNumCounters, 1ULL << ((1 << i) - 1), &memory_mapper);
    counters.Inc(0);
    for (u64 c = 1; c < kNumCounters - 1; c++) {
      ASSERT_EQ(0ULL, counters.Get(c));
      counters.Inc(c);
      ASSERT_EQ(1ULL, counters.Get(c - 1));
    }
    ASSERT_EQ(0ULL, counters.Get(kNumCounters - 1));
    counters.Inc(kNumCounters - 1);

    if (i > 0) {
      counters.IncRange(0, kNumCounters - 1);
      for (u64 c = 0; c < kNumCounters; c++)
        ASSERT_EQ(2ULL, counters.Get(c));
    }
  }
}

class RangeRecorder {
 public:
  std::string reported_pages;

  RangeRecorder()
      : page_size_scaled_log(
            Log2(GetPageSizeCached() >> Allocator64::kCompactPtrScale)),
        last_page_reported(0) {}

  void ReleasePageRangeToOS(u32 class_id, u32 from, u32 to) {
    from >>= page_size_scaled_log;
    to >>= page_size_scaled_log;
    ASSERT_LT(from, to);
    if (!reported_pages.empty())
      ASSERT_LT(last_page_reported, from);
    reported_pages.append(from - last_page_reported, '.');
    reported_pages.append(to - from, 'x');
    last_page_reported = to;
  }

 private:
  const uptr page_size_scaled_log;
  u32 last_page_reported;
};

TEST(SanitizerCommon, SizeClassAllocator64FreePagesRangeTracker) {
  typedef Allocator64::FreePagesRangeTracker<RangeRecorder> RangeTracker;

  // 'x' denotes a page to be released, '.' denotes a page to be kept around.
  const char* test_cases[] = {
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

  for (auto test_case : test_cases) {
    RangeRecorder range_recorder;
    RangeTracker tracker(&range_recorder, 1);
    for (int i = 0; test_case[i] != 0; i++)
      tracker.NextPage(test_case[i] == 'x');
    tracker.Done();
    // Strip trailing '.'-pages before comparing the results as they are not
    // going to be reported to range_recorder anyway.
    const char* last_x = strrchr(test_case, 'x');
    std::string expected(
        test_case,
        last_x == nullptr ? 0 : (last_x - test_case + 1));
    EXPECT_STREQ(expected.c_str(), range_recorder.reported_pages.c_str());
  }
}

class ReleasedPagesTrackingMemoryMapper {
 public:
  std::set<u32> reported_pages;
  std::vector<u64> buffer;

  u64 *MapPackedCounterArrayBuffer(uptr buffer_size) {
    reported_pages.clear();
    buffer.assign(buffer_size, 0);
    return buffer.data();
  }
  void ReleasePageRangeToOS(u32 class_id, u32 from, u32 to) {
    uptr page_size_scaled =
        GetPageSizeCached() >> Allocator64::kCompactPtrScale;
    for (u32 i = from; i < to; i += page_size_scaled)
      reported_pages.insert(i);
  }
};

template <class Allocator>
void TestReleaseFreeMemoryToOS() {
  ReleasedPagesTrackingMemoryMapper memory_mapper;
  const uptr kAllocatedPagesCount = 1024;
  const uptr page_size = GetPageSizeCached();
  const uptr page_size_scaled = page_size >> Allocator::kCompactPtrScale;
  std::mt19937 r;
  uint32_t rnd_state = 42;

  for (uptr class_id = 1; class_id <= Allocator::SizeClassMapT::kLargestClassID;
      class_id++) {
    const uptr chunk_size = Allocator::SizeClassMapT::Size(class_id);
    const uptr chunk_size_scaled = chunk_size >> Allocator::kCompactPtrScale;
    const uptr max_chunks =
        kAllocatedPagesCount * GetPageSizeCached() / chunk_size;

    // Generate the random free list.
    std::vector<u32> free_array;
    bool in_free_range = false;
    uptr current_range_end = 0;
    for (uptr i = 0; i < max_chunks; i++) {
      if (i == current_range_end) {
        in_free_range = (my_rand_r(&rnd_state) & 1U) == 1;
        current_range_end += my_rand_r(&rnd_state) % 100 + 1;
      }
      if (in_free_range)
        free_array.push_back(i * chunk_size_scaled);
    }
    if (free_array.empty())
      continue;
    // Shuffle free_list to verify that ReleaseFreeMemoryToOS does not depend on
    // the list ordering.
    std::shuffle(free_array.begin(), free_array.end(), r);

    Allocator::ReleaseFreeMemoryToOS(&free_array[0], free_array.size(),
                                     chunk_size, kAllocatedPagesCount,
                                     &memory_mapper, class_id);

    // Verify that there are no released pages touched by used chunks and all
    // ranges of free chunks big enough to contain the entire memory pages had
    // these pages released.
    uptr verified_released_pages = 0;
    std::set<u32> free_chunks(free_array.begin(), free_array.end());

    u32 current_chunk = 0;
    in_free_range = false;
    u32 current_free_range_start = 0;
    for (uptr i = 0; i <= max_chunks; i++) {
      bool is_free_chunk = free_chunks.find(current_chunk) != free_chunks.end();

      if (is_free_chunk) {
        if (!in_free_range) {
          in_free_range = true;
          current_free_range_start = current_chunk;
        }
      } else {
        // Verify that this used chunk does not touch any released page.
        for (uptr i_page = current_chunk / page_size_scaled;
             i_page <= (current_chunk + chunk_size_scaled - 1) /
                       page_size_scaled;
             i_page++) {
          bool page_released =
              memory_mapper.reported_pages.find(i_page * page_size_scaled) !=
              memory_mapper.reported_pages.end();
          ASSERT_EQ(false, page_released);
        }

        if (in_free_range) {
          in_free_range = false;
          // Verify that all entire memory pages covered by this range of free
          // chunks were released.
          u32 page = RoundUpTo(current_free_range_start, page_size_scaled);
          while (page + page_size_scaled <= current_chunk) {
            bool page_released =
                memory_mapper.reported_pages.find(page) !=
                memory_mapper.reported_pages.end();
            ASSERT_EQ(true, page_released);
            verified_released_pages++;
            page += page_size_scaled;
          }
        }
      }

      current_chunk += chunk_size_scaled;
    }

    ASSERT_EQ(memory_mapper.reported_pages.size(), verified_released_pages);
  }
}

TEST(SanitizerCommon, SizeClassAllocator64ReleaseFreeMemoryToOS) {
  TestReleaseFreeMemoryToOS<Allocator64>();
}

#if !ALLOCATOR64_SMALL_SIZE
TEST(SanitizerCommon, SizeClassAllocator64CompactReleaseFreeMemoryToOS) {
  TestReleaseFreeMemoryToOS<Allocator64Compact>();
}

TEST(SanitizerCommon, SizeClassAllocator64VeryCompactReleaseFreeMemoryToOS) {
  TestReleaseFreeMemoryToOS<Allocator64VeryCompact>();
}
#endif  // !ALLOCATOR64_SMALL_SIZE

#endif  // SANITIZER_CAN_USE_ALLOCATOR64

TEST(SanitizerCommon, LowLevelAllocatorShouldRoundUpSizeOnAlloc) {
  // When allocating a memory block slightly bigger than a memory page and
  // LowLevelAllocator calls MmapOrDie for the internal buffer, it should round
  // the size up to the page size, so that subsequent calls to the allocator
  // can use the remaining space in the last allocated page.
  static LowLevelAllocator allocator;
  char *ptr1 = (char *)allocator.Allocate(GetPageSizeCached() + 16);
  char *ptr2 = (char *)allocator.Allocate(16);
  EXPECT_EQ(ptr2, ptr1 + GetPageSizeCached() + 16);
}

#endif  // #if !SANITIZER_DEBUG
