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

#include "test_iterators.h"
#include <vector>

static void bm_ends_with_contiguous_iter(benchmark::State& state) {
  std::vector<int> a(state.range(), 1);
  std::vector<int> p(state.range(), 1);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(p);

    auto begin1 = contiguous_iterator(a.data());
    auto end1   = contiguous_iterator(a.data() + a.size());
    auto begin2 = contiguous_iterator(p.data());
    auto end2   = contiguous_iterator(p.data() + p.size());

    benchmark::DoNotOptimize(std::ranges::ends_with(begin1, end1, begin2, end2));
  }
}
BENCHMARK(bm_ends_with_contiguous_iter)->RangeMultiplier(16)->Range(16, 16 << 20);

static void bm_ends_with_random_iter(benchmark::State& state) {
  std::vector<int> a(state.range(), 1);
  std::vector<int> p(state.range(), 1);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(p);

    auto begin1 = random_access_iterator(a.begin());
    auto end1   = random_access_iterator(a.end());
    auto begin2 = random_access_iterator(p.begin());
    auto end2   = random_access_iterator(p.end());

    benchmark::DoNotOptimize(std::ranges::ends_with(begin1, end1, begin2, end2));
  }
}
BENCHMARK(bm_ends_with_random_iter)->RangeMultiplier(16)->Range(16, 16 << 20);

static void bm_ends_with_bidirectional_iter(benchmark::State& state) {
  std::vector<int> a(state.range(), 1);
  std::vector<int> p(state.range(), 1);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(p);

    auto begin1 = bidirectional_iterator(a.begin());
    auto end1   = bidirectional_iterator(a.end());
    auto begin2 = bidirectional_iterator(p.begin());
    auto end2   = bidirectional_iterator(p.end());

    benchmark::DoNotOptimize(std::ranges::ends_with(begin1, end1, begin2, end2));
  }
}
BENCHMARK(bm_ends_with_bidirectional_iter)->RangeMultiplier(16)->Range(16, 16 << 20);

static void bm_ends_with_forward_iter(benchmark::State& state) {
  std::vector<int> a(state.range(), 1);
  std::vector<int> p(state.range(), 1);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(p);

    auto begin1 = forward_iterator(a.begin());
    auto end1   = forward_iterator(a.end());
    auto begin2 = forward_iterator(p.begin());
    auto end2   = forward_iterator(p.end());

    benchmark::DoNotOptimize(std::ranges::ends_with(begin1, end1, begin2, end2));
  }
}
BENCHMARK(bm_ends_with_forward_iter)->RangeMultiplier(16)->Range(16, 16 << 20);

static void bm_ends_with_forward_iter_with_size_optimization(benchmark::State& state) {
  std::vector<int> a(state.range(), 1);
  std::vector<int> p(state.range(), 1);
  p.push_back(2);

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(p);

    auto begin1 = forward_iterator(a.begin());
    auto end1   = forward_iterator(a.end());
    auto begin2 = forward_iterator(p.begin());
    auto end2   = forward_iterator(p.end());

    benchmark::DoNotOptimize(std::ranges::ends_with(begin1, end1, begin2, end2));
  }
}
BENCHMARK(bm_ends_with_forward_iter_with_size_optimization)->RangeMultiplier(16)->Range(16, 16 << 20);

BENCHMARK_MAIN();
