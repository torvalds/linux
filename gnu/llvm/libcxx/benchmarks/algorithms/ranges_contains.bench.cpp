//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <benchmark/benchmark.h>
#include <iterator>
#include <vector>

#include "test_iterators.h"

static void bm_contains_char(benchmark::State& state) {
  std::vector<char> a(state.range(), 'a');

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);

    benchmark::DoNotOptimize(std::ranges::contains(a.begin(), a.end(), 'B'));
  }
}
BENCHMARK(bm_contains_char)->RangeMultiplier(16)->Range(16, 16 << 20);

static void bm_contains_int(benchmark::State& state) {
  std::vector<int> a(state.range(), 1);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);

    benchmark::DoNotOptimize(std::ranges::contains(a.begin(), a.end(), 2));
  }
}
BENCHMARK(bm_contains_int)->RangeMultiplier(16)->Range(16, 16 << 20);

static void bm_contains_bool(benchmark::State& state) {
  std::vector<bool> a(state.range(), true);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);

    benchmark::DoNotOptimize(std::ranges::contains(a.begin(), a.end(), false));
  }
}
BENCHMARK(bm_contains_bool)->RangeMultiplier(16)->Range(16, 16 << 20);

BENCHMARK_MAIN();
