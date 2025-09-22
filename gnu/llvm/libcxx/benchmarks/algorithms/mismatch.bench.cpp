//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <benchmark/benchmark.h>
#include <random>

void BenchmarkSizes(benchmark::internal::Benchmark* Benchmark) {
  Benchmark->DenseRange(1, 8);
  for (size_t i = 16; i != 1 << 20; i *= 2) {
    Benchmark->Arg(i - 1);
    Benchmark->Arg(i);
    Benchmark->Arg(i + 1);
  }
}

// TODO: Look into benchmarking aligned and unaligned memory explicitly
// (currently things happen to be aligned because they are malloced that way)
template <class T>
static void bm_mismatch(benchmark::State& state) {
  std::vector<T> vec1(state.range(), '1');
  std::vector<T> vec2(state.range(), '1');
  std::mt19937_64 rng(std::random_device{}());

  vec1.back() = '2';
  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::mismatch(vec1.begin(), vec1.end(), vec2.begin()));
  }
}
BENCHMARK(bm_mismatch<char>)->Apply(BenchmarkSizes);
BENCHMARK(bm_mismatch<short>)->Apply(BenchmarkSizes);
BENCHMARK(bm_mismatch<int>)->Apply(BenchmarkSizes);

template <class T>
static void bm_mismatch_two_range_overload(benchmark::State& state) {
  std::vector<T> vec1(state.range(), '1');
  std::vector<T> vec2(state.range(), '1');
  std::mt19937_64 rng(std::random_device{}());

  vec1.back() = '2';
  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(std::mismatch(vec1.begin(), vec1.end(), vec2.begin(), vec2.end()));
  }
}
BENCHMARK(bm_mismatch_two_range_overload<char>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_mismatch_two_range_overload<short>)->DenseRange(1, 8)->Range(16, 1 << 20);
BENCHMARK(bm_mismatch_two_range_overload<int>)->DenseRange(1, 8)->Range(16, 1 << 20);

BENCHMARK_MAIN();
