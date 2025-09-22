//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <benchmark/benchmark.h>
#include <vector>

static void bm_equal_iter(benchmark::State& state) {
  std::vector<char> vec1(state.range(), '1');
  std::vector<char> vec2(state.range(), '1');
  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(vec2);
    benchmark::DoNotOptimize(std::equal(vec1.begin(), vec1.end(), vec2.begin()));
  }
}
BENCHMARK(bm_equal_iter)->DenseRange(1, 8)->Range(16, 1 << 20);

static void bm_equal(benchmark::State& state) {
  std::vector<char> vec1(state.range(), '1');
  std::vector<char> vec2(state.range(), '1');
  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(vec2);
    benchmark::DoNotOptimize(std::equal(vec1.begin(), vec1.end(), vec2.begin(), vec2.end()));
  }
}
BENCHMARK(bm_equal)->DenseRange(1, 8)->Range(16, 1 << 20);

static void bm_ranges_equal(benchmark::State& state) {
  std::vector<char> vec1(state.range(), '1');
  std::vector<char> vec2(state.range(), '1');
  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(vec2);
    benchmark::DoNotOptimize(std::ranges::equal(vec1, vec2));
  }
}
BENCHMARK(bm_ranges_equal)->DenseRange(1, 8)->Range(16, 1 << 20);

BENCHMARK_MAIN();
