//===-- sanitizer_common_test.cpp -----------------------------------------===//
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
#include <algorithm>

// This ensures that including both internal sanitizer_common headers
// and the interface headers does not lead to compilation failures.
// Both may be included in unit tests, where googletest transitively
// pulls in sanitizer interface headers.
// The headers are specifically included using relative paths,
// because a compiler may use a different mismatching version
// of sanitizer headers.
#include "../../../include/sanitizer/asan_interface.h"
#include "../../../include/sanitizer/msan_interface.h"
#include "../../../include/sanitizer/tsan_interface.h"
#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_pthread_wrappers.h"

namespace __sanitizer {

static bool IsSorted(const uptr *array, uptr n) {
  for (uptr i = 1; i < n; i++) {
    if (array[i] < array[i - 1]) return false;
  }
  return true;
}

TEST(SanitizerCommon, SortTest) {
  uptr array[100];
  uptr n = 100;
  // Already sorted.
  for (uptr i = 0; i < n; i++) {
    array[i] = i;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // Reverse order.
  for (uptr i = 0; i < n; i++) {
    array[i] = n - 1 - i;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // Mixed order.
  for (uptr i = 0; i < n; i++) {
    array[i] = (i % 2 == 0) ? i : n - 1 - i;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // All equal.
  for (uptr i = 0; i < n; i++) {
    array[i] = 42;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // All but one sorted.
  for (uptr i = 0; i < n - 1; i++) {
    array[i] = i;
  }
  array[n - 1] = 42;
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // Minimal case - sort three elements.
  array[0] = 1;
  array[1] = 0;
  Sort(array, 2);
  EXPECT_TRUE(IsSorted(array, 2));
}

TEST(SanitizerCommon, MmapAlignedOrDieOnFatalError) {
  uptr PageSize = GetPageSizeCached();
  for (uptr size = 1; size <= 32; size *= 2) {
    for (uptr alignment = 1; alignment <= 32; alignment *= 2) {
      for (int iter = 0; iter < 100; iter++) {
        uptr res = (uptr)MmapAlignedOrDieOnFatalError(
            size * PageSize, alignment * PageSize, "MmapAlignedOrDieTest");
        EXPECT_EQ(0U, res % (alignment * PageSize));
        internal_memset((void*)res, 1, size * PageSize);
        UnmapOrDie((void*)res, size * PageSize);
      }
    }
  }
}

TEST(SanitizerCommon, Mprotect) {
  uptr PageSize = GetPageSizeCached();
  u8 *mem = reinterpret_cast<u8 *>(MmapOrDie(PageSize, "MprotectTest"));
  for (u8 *p = mem; p < mem + PageSize; ++p) ++(*p);

  MprotectReadOnly(reinterpret_cast<uptr>(mem), PageSize);
  for (u8 *p = mem; p < mem + PageSize; ++p) EXPECT_EQ(1u, *p);
  EXPECT_DEATH(++mem[0], "");
  EXPECT_DEATH(++mem[PageSize / 2], "");
  EXPECT_DEATH(++mem[PageSize - 1], "");

  MprotectNoAccess(reinterpret_cast<uptr>(mem), PageSize);
  volatile u8 t;
  (void)t;
  EXPECT_DEATH(t = mem[0], "");
  EXPECT_DEATH(t = mem[PageSize / 2], "");
  EXPECT_DEATH(t = mem[PageSize - 1], "");
}

TEST(SanitizerCommon, InternalMmapVectorRoundUpCapacity) {
  InternalMmapVector<uptr> v;
  v.reserve(1);
  CHECK_EQ(v.capacity(), GetPageSizeCached() / sizeof(uptr));
}

TEST(SanitizerCommon, InternalMmapVectorReize) {
  InternalMmapVector<uptr> v;
  CHECK_EQ(0U, v.size());
  CHECK_GE(v.capacity(), v.size());

  v.reserve(1000);
  CHECK_EQ(0U, v.size());
  CHECK_GE(v.capacity(), 1000U);

  v.resize(10000);
  CHECK_EQ(10000U, v.size());
  CHECK_GE(v.capacity(), v.size());
  uptr cap = v.capacity();

  v.resize(100);
  CHECK_EQ(100U, v.size());
  CHECK_EQ(v.capacity(), cap);

  v.reserve(10);
  CHECK_EQ(100U, v.size());
  CHECK_EQ(v.capacity(), cap);
}

TEST(SanitizerCommon, InternalMmapVector) {
  InternalMmapVector<uptr> vector;
  for (uptr i = 0; i < 100; i++) {
    EXPECT_EQ(i, vector.size());
    vector.push_back(i);
  }
  for (uptr i = 0; i < 100; i++) {
    EXPECT_EQ(i, vector[i]);
  }
  for (int i = 99; i >= 0; i--) {
    EXPECT_EQ((uptr)i, vector.back());
    vector.pop_back();
    EXPECT_EQ((uptr)i, vector.size());
  }
  InternalMmapVector<uptr> empty_vector;
  CHECK_EQ(empty_vector.capacity(), 0U);
  CHECK_EQ(0U, empty_vector.size());
}

TEST(SanitizerCommon, InternalMmapVectorEq) {
  InternalMmapVector<uptr> vector1;
  InternalMmapVector<uptr> vector2;
  for (uptr i = 0; i < 100; i++) {
    vector1.push_back(i);
    vector2.push_back(i);
  }
  EXPECT_TRUE(vector1 == vector2);
  EXPECT_FALSE(vector1 != vector2);

  vector1.push_back(1);
  EXPECT_FALSE(vector1 == vector2);
  EXPECT_TRUE(vector1 != vector2);

  vector2.push_back(1);
  EXPECT_TRUE(vector1 == vector2);
  EXPECT_FALSE(vector1 != vector2);

  vector1[55] = 1;
  EXPECT_FALSE(vector1 == vector2);
  EXPECT_TRUE(vector1 != vector2);
}

TEST(SanitizerCommon, InternalMmapVectorSwap) {
  InternalMmapVector<uptr> vector1;
  InternalMmapVector<uptr> vector2;
  InternalMmapVector<uptr> vector3;
  InternalMmapVector<uptr> vector4;
  for (uptr i = 0; i < 100; i++) {
    vector1.push_back(i);
    vector2.push_back(i);
    vector3.push_back(-i);
    vector4.push_back(-i);
  }
  EXPECT_NE(vector2, vector3);
  EXPECT_NE(vector1, vector4);
  vector1.swap(vector3);
  EXPECT_EQ(vector2, vector3);
  EXPECT_EQ(vector1, vector4);
}

void TestThreadInfo(bool main) {
  uptr stk_addr = 0;
  uptr stk_size = 0;
  uptr tls_addr = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(main, &stk_addr, &stk_size, &tls_addr, &tls_size);

  int stack_var;
  EXPECT_NE(stk_addr, (uptr)0);
  EXPECT_NE(stk_size, (uptr)0);
  EXPECT_GT((uptr)&stack_var, stk_addr);
  EXPECT_LT((uptr)&stack_var, stk_addr + stk_size);

#if SANITIZER_LINUX && defined(__x86_64__)
  static __thread int thread_var;
  EXPECT_NE(tls_addr, (uptr)0);
  EXPECT_NE(tls_size, (uptr)0);
  EXPECT_GT((uptr)&thread_var, tls_addr);
  EXPECT_LT((uptr)&thread_var, tls_addr + tls_size);

  // Ensure that tls and stack do not intersect.
  uptr tls_end = tls_addr + tls_size;
  EXPECT_TRUE(tls_addr < stk_addr || tls_addr >= stk_addr + stk_size);
  EXPECT_TRUE(tls_end  < stk_addr || tls_end  >=  stk_addr + stk_size);
  EXPECT_TRUE((tls_addr < stk_addr) == (tls_end  < stk_addr));
#endif
}

static void *WorkerThread(void *arg) {
  TestThreadInfo(false);
  return 0;
}

TEST(SanitizerCommon, ThreadStackTlsMain) {
  InitTlsSize();
  TestThreadInfo(true);
}

TEST(SanitizerCommon, ThreadStackTlsWorker) {
  InitTlsSize();
  pthread_t t;
  PTHREAD_CREATE(&t, 0, WorkerThread, 0);
  PTHREAD_JOIN(t, 0);
}

bool UptrLess(uptr a, uptr b) {
  return a < b;
}

TEST(SanitizerCommon, InternalLowerBound) {
  std::vector<int> arr = {1, 3, 5, 7, 11};

  EXPECT_EQ(0u, InternalLowerBound(arr, 0));
  EXPECT_EQ(0u, InternalLowerBound(arr, 1));
  EXPECT_EQ(1u, InternalLowerBound(arr, 2));
  EXPECT_EQ(1u, InternalLowerBound(arr, 3));
  EXPECT_EQ(2u, InternalLowerBound(arr, 4));
  EXPECT_EQ(2u, InternalLowerBound(arr, 5));
  EXPECT_EQ(3u, InternalLowerBound(arr, 6));
  EXPECT_EQ(3u, InternalLowerBound(arr, 7));
  EXPECT_EQ(4u, InternalLowerBound(arr, 8));
  EXPECT_EQ(4u, InternalLowerBound(arr, 9));
  EXPECT_EQ(4u, InternalLowerBound(arr, 10));
  EXPECT_EQ(4u, InternalLowerBound(arr, 11));
  EXPECT_EQ(5u, InternalLowerBound(arr, 12));
}

TEST(SanitizerCommon, InternalLowerBoundVsStdLowerBound) {
  std::vector<int> data;
  auto create_item = [] (size_t i, size_t j) {
    auto v = i * 10000 + j;
    return ((v << 6) + (v >> 6) + 0x9e3779b9) % 100;
  };
  for (size_t i = 0; i < 1000; ++i) {
    data.resize(i);
    for (size_t j = 0; j < i; ++j) {
      data[j] = create_item(i, j);
    }

    std::sort(data.begin(), data.end());

    for (size_t j = 0; j < i; ++j) {
      int val = create_item(i, j);
      for (auto to_find : {val - 1, val, val + 1}) {
        uptr expected =
            std::lower_bound(data.begin(), data.end(), to_find) - data.begin();
        EXPECT_EQ(expected,
                  InternalLowerBound(data, to_find, std::less<int>()));
      }
    }
  }
}

class SortAndDedupTest : public ::testing::TestWithParam<std::vector<int>> {};

TEST_P(SortAndDedupTest, SortAndDedup) {
  std::vector<int> v_std = GetParam();
  std::sort(v_std.begin(), v_std.end());
  v_std.erase(std::unique(v_std.begin(), v_std.end()), v_std.end());

  std::vector<int> v = GetParam();
  SortAndDedup(v);

  EXPECT_EQ(v_std, v);
}

const std::vector<int> kSortAndDedupTests[] = {
    {},
    {1},
    {1, 1},
    {1, 1, 1},
    {1, 2, 3},
    {3, 2, 1},
    {1, 2, 2, 3},
    {3, 3, 2, 1, 2},
    {3, 3, 2, 1, 2},
    {1, 2, 1, 1, 2, 1, 1, 1, 2, 2},
    {1, 3, 3, 2, 3, 1, 3, 1, 4, 4, 2, 1, 4, 1, 1, 2, 2},
};
INSTANTIATE_TEST_SUITE_P(SortAndDedupTest, SortAndDedupTest,
                         ::testing::ValuesIn(kSortAndDedupTests));

#if SANITIZER_LINUX && !SANITIZER_ANDROID
TEST(SanitizerCommon, FindPathToBinary) {
  char *true_path = FindPathToBinary("true");
  EXPECT_NE((char*)0, internal_strstr(true_path, "/bin/true"));
  InternalFree(true_path);
  EXPECT_EQ(0, FindPathToBinary("unexisting_binary.ergjeorj"));
}
#elif SANITIZER_WINDOWS
TEST(SanitizerCommon, FindPathToBinary) {
  // ntdll.dll should be on PATH in all supported test environments on all
  // supported Windows versions.
  char *ntdll_path = FindPathToBinary("ntdll.dll");
  EXPECT_NE((char*)0, internal_strstr(ntdll_path, "ntdll.dll"));
  InternalFree(ntdll_path);
  EXPECT_EQ(0, FindPathToBinary("unexisting_binary.ergjeorj"));
}
#endif

TEST(SanitizerCommon, StripPathPrefix) {
  EXPECT_EQ(0, StripPathPrefix(0, "prefix"));
  EXPECT_STREQ("foo", StripPathPrefix("foo", 0));
  EXPECT_STREQ("dir/file.cc",
               StripPathPrefix("/usr/lib/dir/file.cc", "/usr/lib/"));
  EXPECT_STREQ("/file.cc", StripPathPrefix("/usr/myroot/file.cc", "/myroot"));
  EXPECT_STREQ("file.h", StripPathPrefix("/usr/lib/./file.h", "/usr/lib/"));
}

TEST(SanitizerCommon, RemoveANSIEscapeSequencesFromString) {
  RemoveANSIEscapeSequencesFromString(nullptr);
  const char *buffs[22] = {
    "Default",                                "Default",
    "\033[95mLight magenta",                  "Light magenta",
    "\033[30mBlack\033[32mGreen\033[90mGray", "BlackGreenGray",
    "\033[106mLight cyan \033[107mWhite ",    "Light cyan White ",
    "\033[31mHello\033[0m World",             "Hello World",
    "\033[38;5;82mHello \033[38;5;198mWorld", "Hello World",
    "123[653456789012",                       "123[653456789012",
    "Normal \033[5mBlink \033[25mNormal",     "Normal Blink Normal",
    "\033[106m\033[107m",                     "",
    "",                                       "",
    " ",                                      " ",
  };

  for (size_t i = 0; i < ARRAY_SIZE(buffs); i+=2) {
    char *buffer_copy = internal_strdup(buffs[i]);
    RemoveANSIEscapeSequencesFromString(buffer_copy);
    EXPECT_STREQ(buffer_copy, buffs[i+1]);
    InternalFree(buffer_copy);
  }
}

TEST(SanitizerCommon, InternalScopedStringAppend) {
  InternalScopedString str;
  EXPECT_EQ(0U, str.length());
  EXPECT_STREQ("", str.data());

  str.Append("");
  EXPECT_EQ(0U, str.length());
  EXPECT_STREQ("", str.data());

  str.Append("foo");
  EXPECT_EQ(3U, str.length());
  EXPECT_STREQ("foo", str.data());

  str.Append("");
  EXPECT_EQ(3U, str.length());
  EXPECT_STREQ("foo", str.data());

  str.Append("123\000456");
  EXPECT_EQ(6U, str.length());
  EXPECT_STREQ("foo123", str.data());
}

TEST(SanitizerCommon, InternalScopedStringAppendF) {
  InternalScopedString str;
  EXPECT_EQ(0U, str.length());
  EXPECT_STREQ("", str.data());

  str.AppendF("foo");
  EXPECT_EQ(3U, str.length());
  EXPECT_STREQ("foo", str.data());

  int x = 1234;
  str.AppendF("%d", x);
  EXPECT_EQ(7U, str.length());
  EXPECT_STREQ("foo1234", str.data());

  str.AppendF("%d", x);
  EXPECT_EQ(11U, str.length());
  EXPECT_STREQ("foo12341234", str.data());

  str.clear();
  EXPECT_EQ(0U, str.length());
  EXPECT_STREQ("", str.data());
}

TEST(SanitizerCommon, InternalScopedStringLarge) {
  InternalScopedString str;
  std::string expected;
  for (int i = 0; i < 1000; ++i) {
    std::string append(i, 'a' + i % 26);
    expected += append;
    str.AppendF("%s", append.c_str());
    EXPECT_EQ(expected, str.data());
  }
}

TEST(SanitizerCommon, InternalScopedStringLargeFormat) {
  InternalScopedString str;
  std::string expected;
  for (int i = 0; i < 1000; ++i) {
    std::string append(i, 'a' + i % 26);
    expected += append;
    str.AppendF("%s", append.c_str());
    EXPECT_EQ(expected, str.data());
  }
}

#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_APPLE || SANITIZER_IOS
TEST(SanitizerCommon, GetRandom) {
  u8 buffer_1[32], buffer_2[32];
  for (bool blocking : { false, true }) {
    EXPECT_FALSE(GetRandom(nullptr, 32, blocking));
    EXPECT_FALSE(GetRandom(buffer_1, 0, blocking));
    EXPECT_FALSE(GetRandom(buffer_1, 512, blocking));
    EXPECT_EQ(ARRAY_SIZE(buffer_1), ARRAY_SIZE(buffer_2));
    for (uptr size = 4; size <= ARRAY_SIZE(buffer_1); size += 4) {
      for (uptr i = 0; i < 100; i++) {
        EXPECT_TRUE(GetRandom(buffer_1, size, blocking));
        EXPECT_TRUE(GetRandom(buffer_2, size, blocking));
        EXPECT_NE(internal_memcmp(buffer_1, buffer_2, size), 0);
      }
    }
  }
}
#endif

TEST(SanitizerCommon, ReservedAddressRangeInit) {
  uptr init_size = 0xffff;
  ReservedAddressRange address_range;
  uptr res = address_range.Init(init_size);
  CHECK_NE(res, (void*)-1);
  UnmapOrDie((void*)res, init_size);
  // Should be able to map into the same space now.
  ReservedAddressRange address_range2;
  uptr res2 = address_range2.Init(init_size, nullptr, res);
  CHECK_EQ(res, res2);

  // TODO(flowerhack): Once this is switched to the "real" implementation
  // (rather than passing through to MmapNoAccess*), enforce and test "no
  // double initializations allowed"
}

TEST(SanitizerCommon, ReservedAddressRangeMap) {
  constexpr uptr init_size = 0xffff;
  ReservedAddressRange address_range;
  uptr res = address_range.Init(init_size);
  CHECK_NE(res, (void*) -1);

  // Valid mappings should succeed.
  CHECK_EQ(res, address_range.Map(res, init_size));

  // Valid mappings should be readable.
  unsigned char buffer[init_size];
  memcpy(buffer, reinterpret_cast<void *>(res), init_size);

  // TODO(flowerhack): Once this is switched to the "real" implementation, make
  // sure you can only mmap into offsets in the Init range.
}

TEST(SanitizerCommon, ReservedAddressRangeUnmap) {
  uptr PageSize = GetPageSizeCached();
  uptr init_size = PageSize * 8;
  ReservedAddressRange address_range;
  uptr base_addr = address_range.Init(init_size);
  CHECK_NE(base_addr, (void*)-1);
  CHECK_EQ(base_addr, address_range.Map(base_addr, init_size));

  // Unmapping the entire range should succeed.
  address_range.Unmap(base_addr, init_size);

  // Map a new range.
  base_addr = address_range.Init(init_size);
  CHECK_EQ(base_addr, address_range.Map(base_addr, init_size));

  // Windows doesn't allow partial unmappings.
  #if !SANITIZER_WINDOWS

  // Unmapping at the beginning should succeed.
  address_range.Unmap(base_addr, PageSize);

  // Unmapping at the end should succeed.
  uptr new_start = reinterpret_cast<uptr>(address_range.base()) +
                   address_range.size() - PageSize;
  address_range.Unmap(new_start, PageSize);

  #endif

  // Unmapping in the middle of the ReservedAddressRange should fail.
  EXPECT_DEATH(address_range.Unmap(base_addr + (PageSize * 2), PageSize), ".*");
}

TEST(SanitizerCommon, ReadBinaryNameCached) {
  char buf[256];
  EXPECT_NE((uptr)0, ReadBinaryNameCached(buf, sizeof(buf)));
}

}  // namespace __sanitizer
