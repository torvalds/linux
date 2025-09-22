//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>

#include "benchmark/benchmark.h"
#include "test_iterators.h"

static void BM_lexicographical_compare_three_way_slow_path(benchmark::State& state) {
  auto size = state.range(0);
  std::vector<int> v1;
  v1.resize(size);
  // v2 is identical except for the last value.
  // This means, that `lexicographical_compare_three_way` actually has to
  // compare the complete vector and cannot bail out early.
  std::vector<int> v2 = v1;
  v2.back() += 1;
  int* b1 = v1.data();
  int* e1 = b1 + v1.size();
  int* b2 = v2.data();
  int* e2 = b2 + v2.size();

  for (auto _ : state) {
    auto cmp = std::compare_three_way();
    benchmark::DoNotOptimize(std::__lexicographical_compare_three_way_slow_path(b1, e1, b2, e2, cmp));
  }
}

BENCHMARK(BM_lexicographical_compare_three_way_slow_path)->RangeMultiplier(4)->Range(1, 1 << 20);

static void BM_lexicographical_compare_three_way_fast_path(benchmark::State& state) {
  auto size = state.range(0);
  std::vector<int> v1;
  v1.resize(size);
  // v2 is identical except for the last value.
  // This means, that `lexicographical_compare_three_way` actually has to
  // compare the complete vector and cannot bail out early.
  std::vector<int> v2 = v1;
  v2.back() += 1;
  int* b1 = v1.data();
  int* e1 = b1 + v1.size();
  int* b2 = v2.data();
  int* e2 = b2 + v2.size();

  for (auto _ : state) {
    auto cmp = std::compare_three_way();
    benchmark::DoNotOptimize(std::__lexicographical_compare_three_way_fast_path(b1, e1, b2, e2, cmp));
  }
}

BENCHMARK(BM_lexicographical_compare_three_way_fast_path)->RangeMultiplier(4)->Range(1, 1 << 20);

template <class IteratorT>
static void BM_lexicographical_compare_three_way(benchmark::State& state) {
  auto size = state.range(0);
  std::vector<int> v1;
  v1.resize(size);
  // v2 is identical except for the last value.
  // This means, that `lexicographical_compare_three_way` actually has to
  // compare the complete vector and cannot bail out early.
  std::vector<int> v2 = v1;
  v2.back() += 1;
  auto b1 = IteratorT{v1.data()};
  auto e1 = IteratorT{v1.data() + v1.size()};
  auto b2 = IteratorT{v2.data()};
  auto e2 = IteratorT{v2.data() + v2.size()};

  for (auto _ : state) {
    benchmark::DoNotOptimize(std::lexicographical_compare_three_way(b1, e1, b2, e2));
  }
}

// Type alias to make sure the `*` does not appear in the benchmark name.
// A `*` would confuse the Python test runner running this google benchmark.
using IntPtr = int*;

// `lexicographical_compare_three_way` has a fast path for random access iterators.
BENCHMARK_TEMPLATE(BM_lexicographical_compare_three_way, IntPtr)->RangeMultiplier(4)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_lexicographical_compare_three_way, random_access_iterator<IntPtr>)
    ->RangeMultiplier(4)
    ->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_lexicographical_compare_three_way, cpp17_input_iterator<IntPtr>)
    ->RangeMultiplier(4)
    ->Range(1, 1 << 20);

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  benchmark::RunSpecifiedBenchmarks();
}
