#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unordered_set>
#include <vector>

#include "benchmark/benchmark.h"

#include "ContainerBenchmarks.h"
#include "GenerateInput.h"
#include "test_macros.h"

using namespace ContainerBenchmarks;

constexpr std::size_t TestNumInputs = 1024;

template <class _Size>
inline TEST_ALWAYS_INLINE _Size loadword(const void* __p) {
  _Size __r;
  std::memcpy(&__r, __p, sizeof(__r));
  return __r;
}

inline TEST_ALWAYS_INLINE std::size_t rotate_by_at_least_1(std::size_t __val, int __shift) {
  return (__val >> __shift) | (__val << (64 - __shift));
}

inline TEST_ALWAYS_INLINE std::size_t hash_len_16(std::size_t __u, std::size_t __v) {
  const std::size_t __mul = 0x9ddfea08eb382d69ULL;
  std::size_t __a         = (__u ^ __v) * __mul;
  __a ^= (__a >> 47);
  std::size_t __b = (__v ^ __a) * __mul;
  __b ^= (__b >> 47);
  __b *= __mul;
  return __b;
}

template <std::size_t _Len>
inline TEST_ALWAYS_INLINE std::size_t hash_len_0_to_8(const char* __s) {
  static_assert(_Len == 4 || _Len == 8, "");
  const uint64_t __a = loadword<uint32_t>(__s);
  const uint64_t __b = loadword<uint32_t>(__s + _Len - 4);
  return hash_len_16(_Len + (__a << 3), __b);
}

struct UInt32Hash {
  UInt32Hash() = default;
  inline TEST_ALWAYS_INLINE std::size_t operator()(uint32_t data) const {
    return hash_len_0_to_8<4>(reinterpret_cast<const char*>(&data));
  }
};

struct UInt64Hash {
  UInt64Hash() = default;
  inline TEST_ALWAYS_INLINE std::size_t operator()(uint64_t data) const {
    return hash_len_0_to_8<8>(reinterpret_cast<const char*>(&data));
  }
};

struct UInt128Hash {
  UInt128Hash() = default;
  inline TEST_ALWAYS_INLINE std::size_t operator()(__uint128_t data) const {
    const __uint128_t __mask = static_cast<std::size_t>(-1);
    const std::size_t __a    = (std::size_t)(data & __mask);
    const std::size_t __b    = (std::size_t)((data & (__mask << 64)) >> 64);
    return hash_len_16(__a, rotate_by_at_least_1(__b + 16, 16)) ^ __b;
  }
};

struct UInt32Hash2 {
  UInt32Hash2() = default;
  inline TEST_ALWAYS_INLINE std::size_t operator()(uint32_t data) const {
    const uint32_t __m = 0x5bd1e995;
    const uint32_t __r = 24;
    uint32_t __h       = 4;
    uint32_t __k       = data;
    __k *= __m;
    __k ^= __k >> __r;
    __k *= __m;
    __h *= __m;
    __h ^= __k;
    __h ^= __h >> 13;
    __h *= __m;
    __h ^= __h >> 15;
    return __h;
  }
};

struct UInt64Hash2 {
  UInt64Hash2() = default;
  inline TEST_ALWAYS_INLINE std::size_t operator()(uint64_t data) const {
    return hash_len_0_to_8<8>(reinterpret_cast<const char*>(&data));
  }
};

// The sole purpose of this comparator is to be used in BM_Rehash, where
// we need something slow enough to be easily noticable in benchmark results.
// The default implementation of operator== for strings seems to be a little
// too fast for that specific benchmark to reliably show a noticeable
// improvement, but unoptimized bytewise comparison fits just right.
// Early return is there just for convenience, since we only compare strings
// of equal length in BM_Rehash.
struct SlowStringEq {
  SlowStringEq() = default;
  inline TEST_ALWAYS_INLINE bool operator()(const std::string& lhs, const std::string& rhs) const {
    if (lhs.size() != rhs.size())
      return false;

    bool eq = true;
    for (size_t i = 0; i < lhs.size(); ++i) {
      eq &= lhs[i] == rhs[i];
    }
    return eq;
  }
};

//----------------------------------------------------------------------------//
//                               BM_Hash
// ---------------------------------------------------------------------------//

template <class HashFn, class GenInputs>
void BM_Hash(benchmark::State& st, HashFn fn, GenInputs gen) {
  auto in               = gen(st.range(0));
  const auto end        = in.data() + in.size();
  std::size_t last_hash = 0;
  benchmark::DoNotOptimize(&last_hash);
  while (st.KeepRunning()) {
    for (auto it = in.data(); it != end; ++it) {
      benchmark::DoNotOptimize(last_hash += fn(*it));
    }
    benchmark::ClobberMemory();
  }
}

BENCHMARK_CAPTURE(BM_Hash, uint32_random_std_hash, std::hash<uint32_t>{}, getRandomIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_Hash, uint32_random_custom_hash, UInt32Hash{}, getRandomIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_Hash, uint32_top_std_hash, std::hash<uint32_t>{}, getSortedTopBitsIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_Hash, uint32_top_custom_hash, UInt32Hash{}, getSortedTopBitsIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

//----------------------------------------------------------------------------//
//                       BM_InsertValue
// ---------------------------------------------------------------------------//

// Sorted Ascending //
BENCHMARK_CAPTURE(
    BM_InsertValue, unordered_set_uint32, std::unordered_set<uint32_t>{}, getRandomIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_InsertValue, unordered_set_uint32_sorted, std::unordered_set<uint32_t>{}, getSortedIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

// Top Bytes //
BENCHMARK_CAPTURE(BM_InsertValue,
                  unordered_set_top_bits_uint32,
                  std::unordered_set<uint32_t>{},
                  getSortedTopBitsIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_InsertValueRehash,
                  unordered_set_top_bits_uint32,
                  std::unordered_set<uint32_t, UInt32Hash>{},
                  getSortedTopBitsIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

// String //
BENCHMARK_CAPTURE(BM_InsertValue, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_InsertValueRehash, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

// Prefixed String //
BENCHMARK_CAPTURE(
    BM_InsertValue, unordered_set_prefixed_string, std::unordered_set<std::string>{}, getPrefixedRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_InsertValueRehash,
                  unordered_set_prefixed_string,
                  std::unordered_set<std::string>{},
                  getPrefixedRandomStringInputs)
    ->Arg(TestNumInputs);

//----------------------------------------------------------------------------//
//                         BM_Find
// ---------------------------------------------------------------------------//

// Random //
BENCHMARK_CAPTURE(
    BM_Find, unordered_set_random_uint64, std::unordered_set<uint64_t>{}, getRandomIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash,
                  unordered_set_random_uint64,
                  std::unordered_set<uint64_t, UInt64Hash>{},
                  getRandomIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

// Sorted //
BENCHMARK_CAPTURE(
    BM_Find, unordered_set_sorted_uint64, std::unordered_set<uint64_t>{}, getSortedIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash,
                  unordered_set_sorted_uint64,
                  std::unordered_set<uint64_t, UInt64Hash>{},
                  getSortedIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

// Sorted //
BENCHMARK_CAPTURE(BM_Find,
                  unordered_set_sorted_uint128,
                  std::unordered_set<__uint128_t, UInt128Hash>{},
                  getSortedTopBitsIntegerInputs<__uint128_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash,
                  unordered_set_sorted_uint128,
                  std::unordered_set<__uint128_t, UInt128Hash>{},
                  getSortedTopBitsIntegerInputs<__uint128_t>)
    ->Arg(TestNumInputs);

// Sorted //
BENCHMARK_CAPTURE(
    BM_Find, unordered_set_sorted_uint32, std::unordered_set<uint32_t>{}, getSortedIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash,
                  unordered_set_sorted_uint32,
                  std::unordered_set<uint32_t, UInt32Hash2>{},
                  getSortedIntegerInputs<uint32_t>)
    ->Arg(TestNumInputs);

// Sorted Ascending //
BENCHMARK_CAPTURE(
    BM_Find, unordered_set_sorted_large_uint64, std::unordered_set<uint64_t>{}, getSortedLargeIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash,
                  unordered_set_sorted_large_uint64,
                  std::unordered_set<uint64_t, UInt64Hash>{},
                  getSortedLargeIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

// Top Bits //
BENCHMARK_CAPTURE(
    BM_Find, unordered_set_top_bits_uint64, std::unordered_set<uint64_t>{}, getSortedTopBitsIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash,
                  unordered_set_top_bits_uint64,
                  std::unordered_set<uint64_t, UInt64Hash>{},
                  getSortedTopBitsIntegerInputs<uint64_t>)
    ->Arg(TestNumInputs);

// String //
BENCHMARK_CAPTURE(BM_Find, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_FindRehash, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

// Prefixed String //
BENCHMARK_CAPTURE(
    BM_Find, unordered_set_prefixed_string, std::unordered_set<std::string>{}, getPrefixedRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_FindRehash, unordered_set_prefixed_string, std::unordered_set<std::string>{}, getPrefixedRandomStringInputs)
    ->Arg(TestNumInputs);

//----------------------------------------------------------------------------//
//                         BM_Rehash
// ---------------------------------------------------------------------------//

BENCHMARK_CAPTURE(BM_Rehash,
                  unordered_set_string_arg,
                  std::unordered_set<std::string, std::hash<std::string>, SlowStringEq>{},
                  getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_Rehash, unordered_set_int_arg, std::unordered_set<int>{}, getRandomIntegerInputs<int>)
    ->Arg(TestNumInputs);

//----------------------------------------------------------------------------//
//                         BM_Compare
// ---------------------------------------------------------------------------//

BENCHMARK_CAPTURE(
    BM_Compare_same_container, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_Compare_same_container, unordered_set_int, std::unordered_set<int>{}, getRandomIntegerInputs<int>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_Compare_different_containers, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_Compare_different_containers, unordered_set_int, std::unordered_set<int>{}, getRandomIntegerInputs<int>)
    ->Arg(TestNumInputs);

///////////////////////////////////////////////////////////////////////////////
BENCHMARK_CAPTURE(BM_InsertDuplicate, unordered_set_int, std::unordered_set<int>{}, getRandomIntegerInputs<int>)
    ->Arg(TestNumInputs);
BENCHMARK_CAPTURE(BM_InsertDuplicate, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_EmplaceDuplicate, unordered_set_int, std::unordered_set<int>{}, getRandomIntegerInputs<int>)
    ->Arg(TestNumInputs);
BENCHMARK_CAPTURE(BM_EmplaceDuplicate, unordered_set_string, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_InsertDuplicate, unordered_set_int_insert_arg, std::unordered_set<int>{}, getRandomIntegerInputs<int>)
    ->Arg(TestNumInputs);
BENCHMARK_CAPTURE(
    BM_InsertDuplicate, unordered_set_string_insert_arg, std::unordered_set<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_EmplaceDuplicate, unordered_set_int_insert_arg, std::unordered_set<int>{}, getRandomIntegerInputs<unsigned>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(
    BM_EmplaceDuplicate, unordered_set_string_arg, std::unordered_set<std::string>{}, getRandomCStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_MAIN();
