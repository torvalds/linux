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
#include <deque>
#include <random>
#include <vector>

template <class Container>
static void bm_find(benchmark::State& state) {
  using T = Container::value_type;

  Container vec1(state.range(), '1');
  std::mt19937_64 rng(std::random_device{}());

  for (auto _ : state) {
    auto idx  = rng() % vec1.size();
    vec1[idx] = '2';
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::find(vec1.begin(), vec1.end(), T('2')));
    vec1[idx] = '1';
  }
}
BENCHMARK(bm_find<std::vector<char>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_find<std::vector<short>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_find<std::vector<int>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_find<std::deque<char>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_find<std::deque<short>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_find<std::deque<int>>)->DenseRange(1, 8)->Range(16, 1 << 20);

template <class Container>
static void bm_ranges_find(benchmark::State& state) {
  using T = Container::value_type;

  Container vec1(state.range(), '1');
  std::mt19937_64 rng(std::random_device{}());

  for (auto _ : state) {
    auto idx  = rng() % vec1.size();
    vec1[idx] = '2';
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::ranges::find(vec1, T('2')));
    vec1[idx] = '1';
  }
}
BENCHMARK(bm_ranges_find<std::vector<char>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_ranges_find<std::vector<short>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_ranges_find<std::vector<int>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_ranges_find<std::deque<char>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_ranges_find<std::deque<short>>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_ranges_find<std::deque<int>>)->DenseRange(1, 8)->Range(16, 1 << 20);

static void bm_vector_bool_find(benchmark::State& state) {
  std::vector<bool> vec1(state.range(), false);
  std::mt19937_64 rng(std::random_device{}());

  for (auto _ : state) {
    auto idx  = rng() % vec1.size();
    vec1[idx] = true;
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::find(vec1.begin(), vec1.end(), true));
    vec1[idx] = false;
  }
}
BENCHMARK(bm_vector_bool_find)->DenseRange(1, 8)->Range(16, 1 << 20);

static void bm_vector_bool_ranges_find(benchmark::State& state) {
  std::vector<bool> vec1(state.range(), false);
  std::mt19937_64 rng(std::random_device{}());

  for (auto _ : state) {
    auto idx  = rng() % vec1.size();
    vec1[idx] = true;
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::ranges::find(vec1, true));
    vec1[idx] = false;
  }
}
BENCHMARK(bm_vector_bool_ranges_find)->DenseRange(1, 8)->Range(16, 1 << 20);

BENCHMARK_MAIN();
