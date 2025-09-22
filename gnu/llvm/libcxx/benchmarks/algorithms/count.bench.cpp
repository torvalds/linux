//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstring>
#include <random>
#include <vector>

static void bm_vector_bool_count(benchmark::State& state) {
  std::vector<bool> vec1(state.range(), false);

  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::count(vec1.begin(), vec1.end(), true));
  }
}
BENCHMARK(bm_vector_bool_count)->DenseRange(1, 8)->Range(16, 1 << 20);

static void bm_vector_bool_ranges_count(benchmark::State& state) {
  std::vector<bool> vec1(state.range(), false);

  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::ranges::count(vec1.begin(), vec1.end(), true));
  }
}
BENCHMARK(bm_vector_bool_ranges_count)->DenseRange(1, 8)->Range(16, 1 << 20);

BENCHMARK_MAIN();
