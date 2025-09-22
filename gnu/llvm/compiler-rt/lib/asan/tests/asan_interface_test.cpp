//===-- asan_interface_test.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
//===----------------------------------------------------------------------===//
#include "asan_test_utils.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include <sanitizer/allocator_interface.h>
#include <sanitizer/asan_interface.h>
#include <vector>

TEST(AddressSanitizerInterface, GetEstimatedAllocatedSize) {
  EXPECT_EQ(0U, __sanitizer_get_estimated_allocated_size(0));
  const size_t sizes[] = { 1, 30, 1<<30 };
  for (size_t i = 0; i < 3; i++) {
    EXPECT_EQ(sizes[i], __sanitizer_get_estimated_allocated_size(sizes[i]));
  }
}

static const char* kGetAllocatedSizeErrorMsg =
  "attempting to call __sanitizer_get_allocated_size";

TEST(AddressSanitizerInterface, GetAllocatedSizeAndOwnershipTest) {
  const size_t kArraySize = 100;
  char *array = Ident((char*)malloc(kArraySize));
  int *int_ptr = Ident(new int);

  // Allocated memory is owned by allocator. Allocated size should be
  // equal to requested size.
  EXPECT_EQ(true, __sanitizer_get_ownership(array));
  EXPECT_EQ(kArraySize, __sanitizer_get_allocated_size(array));
  EXPECT_EQ(true, __sanitizer_get_ownership(int_ptr));
  EXPECT_EQ(sizeof(int), __sanitizer_get_allocated_size(int_ptr));

  // We cannot call GetAllocatedSize from the memory we didn't map,
  // and from the interior pointers (not returned by previous malloc).
  void *wild_addr = (void*)0x1;
  EXPECT_FALSE(__sanitizer_get_ownership(wild_addr));
  EXPECT_DEATH(__sanitizer_get_allocated_size(wild_addr),
               kGetAllocatedSizeErrorMsg);
  EXPECT_FALSE(__sanitizer_get_ownership(array + kArraySize / 2));
  EXPECT_DEATH(__sanitizer_get_allocated_size(array + kArraySize / 2),
               kGetAllocatedSizeErrorMsg);

  // NULL is not owned, but is a valid argument for
  // __sanitizer_get_allocated_size().
  EXPECT_FALSE(__sanitizer_get_ownership(NULL));
  EXPECT_EQ(0U, __sanitizer_get_allocated_size(NULL));

  // When memory is freed, it's not owned, and call to GetAllocatedSize
  // is forbidden.
  free(array);
  EXPECT_FALSE(__sanitizer_get_ownership(array));
  EXPECT_DEATH(__sanitizer_get_allocated_size(array),
               kGetAllocatedSizeErrorMsg);
  delete int_ptr;

  void *zero_alloc = Ident(malloc(0));
  if (zero_alloc != 0) {
    // If malloc(0) is not null, this pointer is owned and should have valid
    // allocated size.
    EXPECT_TRUE(__sanitizer_get_ownership(zero_alloc));
    // Allocated size is 0 or 1 depending on the allocator used.
    EXPECT_LT(__sanitizer_get_allocated_size(zero_alloc), 2U);
  }
  free(zero_alloc);
}

TEST(AddressSanitizerInterface, GetCurrentAllocatedBytesTest) {
  size_t before_malloc, after_malloc, after_free;
  char *array;
  const size_t kMallocSize = 100;
  before_malloc = __sanitizer_get_current_allocated_bytes();

  array = Ident((char*)malloc(kMallocSize));
  after_malloc = __sanitizer_get_current_allocated_bytes();
  EXPECT_EQ(before_malloc + kMallocSize, after_malloc);

  free(array);
  after_free = __sanitizer_get_current_allocated_bytes();
  EXPECT_EQ(before_malloc, after_free);
}

TEST(AddressSanitizerInterface, GetHeapSizeTest) {
  // ASan allocator does not keep huge chunks in free list, but unmaps them.
  // The chunk should be greater than the quarantine size,
  // otherwise it will be stuck in quarantine instead of being unmapped.
  static const size_t kLargeMallocSize = (1 << 28) + 1;  // 256M
  free(Ident(malloc(kLargeMallocSize)));  // Drain quarantine.
  size_t old_heap_size = __sanitizer_get_heap_size();
  for (int i = 0; i < 3; i++) {
    // fprintf(stderr, "allocating %zu bytes:\n", kLargeMallocSize);
    free(Ident(malloc(kLargeMallocSize)));
    EXPECT_EQ(old_heap_size, __sanitizer_get_heap_size());
  }
}

#if !defined(__NetBSD__)
static const size_t kManyThreadsMallocSizes[] = {5, 1UL<<10, 1UL<<14, 357};
static const size_t kManyThreadsIterations = 250;
static const size_t kManyThreadsNumThreads =
  (SANITIZER_WORDSIZE == 32) ? 40 : 200;

static void *ManyThreadsWithStatsWorker(void *arg) {
  (void)arg;
  for (size_t iter = 0; iter < kManyThreadsIterations; iter++) {
    for (size_t size_index = 0; size_index < 4; size_index++) {
      free(Ident(malloc(kManyThreadsMallocSizes[size_index])));
    }
  }
  // Just one large allocation.
  free(Ident(malloc(1 << 20)));
  return 0;
}

TEST(AddressSanitizerInterface, ManyThreadsWithStatsStressTest) {
  size_t before_test, after_test, i;
  pthread_t threads[kManyThreadsNumThreads];
  before_test = __sanitizer_get_current_allocated_bytes();
  for (i = 0; i < kManyThreadsNumThreads; i++) {
    PTHREAD_CREATE(&threads[i], 0,
                   (void* (*)(void *x))ManyThreadsWithStatsWorker, (void*)i);
  }
  for (i = 0; i < kManyThreadsNumThreads; i++) {
    PTHREAD_JOIN(threads[i], 0);
  }
  after_test = __sanitizer_get_current_allocated_bytes();
  // ASan stats also reflect memory usage of internal ASan RTL structs,
  // so we can't check for equality here.
  EXPECT_LT(after_test, before_test + (1UL<<20));
}
#endif

static void DoDoubleFree() {
  int *x = Ident(new int);
  delete Ident(x);
  delete Ident(x);
}

static void MyDeathCallback() {
  fprintf(stderr, "MyDeathCallback\n");
  fflush(0);  // On Windows, stderr doesn't flush on crash.
}

TEST(AddressSanitizerInterface, DeathCallbackTest) {
  __asan_set_death_callback(MyDeathCallback);
  EXPECT_DEATH(DoDoubleFree(), "MyDeathCallback");
  __asan_set_death_callback(NULL);
}

#define GOOD_ACCESS(ptr, offset)  \
    EXPECT_FALSE(__asan_address_is_poisoned(ptr + offset))

#define BAD_ACCESS(ptr, offset) \
    EXPECT_TRUE(__asan_address_is_poisoned(ptr + offset))

static const char* kUseAfterPoisonErrorMessage = "use-after-poison";

TEST(AddressSanitizerInterface, SimplePoisonMemoryRegionTest) {
  char *array = Ident((char*)malloc(120));
  // poison array[40..80)
  __asan_poison_memory_region(array + 40, 40);
  GOOD_ACCESS(array, 39);
  GOOD_ACCESS(array, 80);
  BAD_ACCESS(array, 40);
  BAD_ACCESS(array, 60);
  BAD_ACCESS(array, 79);
  EXPECT_DEATH(Ident(array[40]), kUseAfterPoisonErrorMessage);
  __asan_unpoison_memory_region(array + 40, 40);
  // access previously poisoned memory.
  GOOD_ACCESS(array, 40);
  GOOD_ACCESS(array, 79);
  free(array);
}

TEST(AddressSanitizerInterface, OverlappingPoisonMemoryRegionTest) {
  char *array = Ident((char*)malloc(120));
  // Poison [0..40) and [80..120)
  __asan_poison_memory_region(array, 40);
  __asan_poison_memory_region(array + 80, 40);
  BAD_ACCESS(array, 20);
  GOOD_ACCESS(array, 60);
  BAD_ACCESS(array, 100);
  // Poison whole array - [0..120)
  __asan_poison_memory_region(array, 120);
  BAD_ACCESS(array, 60);
  // Unpoison [24..96)
  __asan_unpoison_memory_region(array + 24, 72);
  BAD_ACCESS(array, 23);
  GOOD_ACCESS(array, 24);
  GOOD_ACCESS(array, 60);
  GOOD_ACCESS(array, 95);
  BAD_ACCESS(array, 96);
  free(array);
}

TEST(AddressSanitizerInterface, PushAndPopWithPoisoningTest) {
  // Vector of capacity 20
  char *vec = Ident((char*)malloc(20));
  __asan_poison_memory_region(vec, 20);
  for (size_t i = 0; i < 7; i++) {
    // Simulate push_back.
    __asan_unpoison_memory_region(vec + i, 1);
    GOOD_ACCESS(vec, i);
    BAD_ACCESS(vec, i + 1);
  }
  for (size_t i = 7; i > 0; i--) {
    // Simulate pop_back.
    __asan_poison_memory_region(vec + i - 1, 1);
    BAD_ACCESS(vec, i - 1);
    if (i > 1) GOOD_ACCESS(vec, i - 2);
  }
  free(vec);
}

#if !defined(ASAN_SHADOW_SCALE) || ASAN_SHADOW_SCALE == 3
// Make sure that each aligned block of size "2^granularity" doesn't have
// "true" value before "false" value.
static void MakeShadowValid(bool *shadow, int length, int granularity) {
  bool can_be_poisoned = true;
  for (int i = length - 1; i >= 0; i--) {
    if (!shadow[i])
      can_be_poisoned = false;
    if (!can_be_poisoned)
      shadow[i] = false;
    if (i % (1 << granularity) == 0) {
      can_be_poisoned = true;
    }
  }
}

TEST(AddressSanitizerInterface, PoisoningStressTest) {
  const size_t kSize = 24;
  bool expected[kSize];
  char *arr = Ident((char*)malloc(kSize));
  for (size_t l1 = 0; l1 < kSize; l1++) {
    for (size_t s1 = 1; l1 + s1 <= kSize; s1++) {
      for (size_t l2 = 0; l2 < kSize; l2++) {
        for (size_t s2 = 1; l2 + s2 <= kSize; s2++) {
          // Poison [l1, l1+s1), [l2, l2+s2) and check result.
          __asan_unpoison_memory_region(arr, kSize);
          __asan_poison_memory_region(arr + l1, s1);
          __asan_poison_memory_region(arr + l2, s2);
          memset(expected, false, kSize);
          memset(expected + l1, true, s1);
          MakeShadowValid(expected, kSize, /*granularity*/ 3);
          memset(expected + l2, true, s2);
          MakeShadowValid(expected, kSize, /*granularity*/ 3);
          for (size_t i = 0; i < kSize; i++) {
            ASSERT_EQ(expected[i], __asan_address_is_poisoned(arr + i));
          }
          // Unpoison [l1, l1+s1) and [l2, l2+s2) and check result.
          __asan_poison_memory_region(arr, kSize);
          __asan_unpoison_memory_region(arr + l1, s1);
          __asan_unpoison_memory_region(arr + l2, s2);
          memset(expected, true, kSize);
          memset(expected + l1, false, s1);
          MakeShadowValid(expected, kSize, /*granularity*/ 3);
          memset(expected + l2, false, s2);
          MakeShadowValid(expected, kSize, /*granularity*/ 3);
          for (size_t i = 0; i < kSize; i++) {
            ASSERT_EQ(expected[i], __asan_address_is_poisoned(arr + i));
          }
        }
      }
    }
  }
  free(arr);
}
#endif  // !defined(ASAN_SHADOW_SCALE) || ASAN_SHADOW_SCALE == 3

TEST(AddressSanitizerInterface, GlobalRedzones) {
  GOOD_ACCESS(glob1, 1 - 1);
  GOOD_ACCESS(glob2, 2 - 1);
  GOOD_ACCESS(glob3, 3 - 1);
  GOOD_ACCESS(glob4, 4 - 1);
  GOOD_ACCESS(glob5, 5 - 1);
  GOOD_ACCESS(glob6, 6 - 1);
  GOOD_ACCESS(glob7, 7 - 1);
  GOOD_ACCESS(glob8, 8 - 1);
  GOOD_ACCESS(glob9, 9 - 1);
  GOOD_ACCESS(glob10, 10 - 1);
  GOOD_ACCESS(glob11, 11 - 1);
  GOOD_ACCESS(glob12, 12 - 1);
  GOOD_ACCESS(glob13, 13 - 1);
  GOOD_ACCESS(glob14, 14 - 1);
  GOOD_ACCESS(glob15, 15 - 1);
  GOOD_ACCESS(glob16, 16 - 1);
  GOOD_ACCESS(glob17, 17 - 1);
  GOOD_ACCESS(glob1000, 1000 - 1);
  GOOD_ACCESS(glob10000, 10000 - 1);
  GOOD_ACCESS(glob100000, 100000 - 1);

  BAD_ACCESS(glob1, 1);
  BAD_ACCESS(glob2, 2);
  BAD_ACCESS(glob3, 3);
  BAD_ACCESS(glob4, 4);
  BAD_ACCESS(glob5, 5);
  BAD_ACCESS(glob6, 6);
  BAD_ACCESS(glob7, 7);
  BAD_ACCESS(glob8, 8);
  BAD_ACCESS(glob9, 9);
  BAD_ACCESS(glob10, 10);
  BAD_ACCESS(glob11, 11);
  BAD_ACCESS(glob12, 12);
  BAD_ACCESS(glob13, 13);
  BAD_ACCESS(glob14, 14);
  BAD_ACCESS(glob15, 15);
  BAD_ACCESS(glob16, 16);
  BAD_ACCESS(glob17, 17);
  BAD_ACCESS(glob1000, 1000);
  BAD_ACCESS(glob1000, 1100);  // Redzone is at least 101 bytes.
  BAD_ACCESS(glob10000, 10000);
  BAD_ACCESS(glob10000, 11000);  // Redzone is at least 1001 bytes.
  BAD_ACCESS(glob100000, 100000);
  BAD_ACCESS(glob100000, 110000);  // Redzone is at least 10001 bytes.
}

TEST(AddressSanitizerInterface, PoisonedRegion) {
  size_t rz = 16;
  for (size_t size = 1; size <= 64; size++) {
    char *p = new char[size];
    for (size_t beg = 0; beg < size + rz; beg++) {
      for (size_t end = beg; end < size + rz; end++) {
        void *first_poisoned = __asan_region_is_poisoned(p + beg, end - beg);
        if (beg == end) {
          EXPECT_FALSE(first_poisoned);
        } else if (beg < size && end <= size) {
          EXPECT_FALSE(first_poisoned);
        } else if (beg >= size) {
          EXPECT_EQ(p + beg, first_poisoned);
        } else {
          EXPECT_GT(end, size);
          EXPECT_EQ(p + size, first_poisoned);
        }
      }
    }
    delete [] p;
  }
}

// This is a performance benchmark for manual runs.
// asan's memset interceptor calls mem_is_zero for the entire shadow region.
// the profile should look like this:
//     89.10%   [.] __memset_sse2
//     10.50%   [.] __sanitizer::mem_is_zero
// I.e. mem_is_zero should consume ~ SHADOW_GRANULARITY less CPU cycles
// than memset itself.
TEST(AddressSanitizerInterface, DISABLED_StressLargeMemset) {
  size_t size = 1 << 20;
  char *x = new char[size];
  for (int i = 0; i < 100000; i++)
    Ident(memset)(x, 0, size);
  delete [] x;
}

// Same here, but we run memset with small sizes.
TEST(AddressSanitizerInterface, DISABLED_StressSmallMemset) {
  size_t size = 32;
  char *x = new char[size];
  for (int i = 0; i < 100000000; i++)
    Ident(memset)(x, 0, size);
  delete [] x;
}
static const char *kInvalidPoisonMessage = "invalid-poison-memory-range";
static const char *kInvalidUnpoisonMessage = "invalid-unpoison-memory-range";

TEST(AddressSanitizerInterface, DISABLED_InvalidPoisonAndUnpoisonCallsTest) {
  char *array = Ident((char*)malloc(120));
  __asan_unpoison_memory_region(array, 120);
  // Try to unpoison not owned memory
  EXPECT_DEATH(__asan_unpoison_memory_region(array, 121),
               kInvalidUnpoisonMessage);
  EXPECT_DEATH(__asan_unpoison_memory_region(array - 1, 120),
               kInvalidUnpoisonMessage);

  __asan_poison_memory_region(array, 120);
  // Try to poison not owned memory.
  EXPECT_DEATH(__asan_poison_memory_region(array, 121), kInvalidPoisonMessage);
  EXPECT_DEATH(__asan_poison_memory_region(array - 1, 120),
               kInvalidPoisonMessage);
  free(array);
}

TEST(AddressSanitizerInterface, GetOwnershipStressTest) {
  std::vector<char *> pointers;
  std::vector<size_t> sizes;
  const size_t kNumMallocs = 1 << 9;
  for (size_t i = 0; i < kNumMallocs; i++) {
    size_t size = i * 100 + 1;
    pointers.push_back((char*)malloc(size));
    sizes.push_back(size);
  }
  for (size_t i = 0; i < 4000000; i++) {
    EXPECT_FALSE(__sanitizer_get_ownership(&pointers));
    EXPECT_FALSE(__sanitizer_get_ownership((void*)0x1234));
    size_t idx = i % kNumMallocs;
    EXPECT_TRUE(__sanitizer_get_ownership(pointers[idx]));
    EXPECT_EQ(sizes[idx], __sanitizer_get_allocated_size(pointers[idx]));
  }
  for (size_t i = 0, n = pointers.size(); i < n; i++)
    free(pointers[i]);
}

TEST(AddressSanitizerInterface, HandleNoReturnTest) {
  char array[40];
  __asan_poison_memory_region(array, sizeof(array));
  BAD_ACCESS(array, 20);
  __asan_handle_no_return();
  // Fake stack does not need to be unpoisoned.
  if (__asan_get_current_fake_stack())
    return;
  // It unpoisons the whole thread stack.
  GOOD_ACCESS(array, 20);
}
