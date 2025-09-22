//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <deque>
#include <ranges>

#include "benchmark/benchmark.h"

namespace {
void run_sizes(auto benchmark) {
  benchmark->Arg(0)
      ->Arg(1)
      ->Arg(2)
      ->Arg(64)
      ->Arg(512)
      ->Arg(1024)
      ->Arg(4000)
      ->Arg(4096)
      ->Arg(5500)
      ->Arg(64000)
      ->Arg(65536)
      ->Arg(70000);
}

void BM_join_view_in_vectors(benchmark::State& state) {
  auto size = state.range(0);
  std::vector<std::vector<int>> input(size, std::vector<int>(32));
  std::ranges::fill(input | std::views::join, 10);
  std::vector<int> output;
  output.resize(size * 32);

  for (auto _ : state) {
    benchmark::DoNotOptimize(input);
    benchmark::DoNotOptimize(output);
    std::ranges::copy(input | std::views::join, output.begin());
  }
}
BENCHMARK(BM_join_view_in_vectors)->Apply(run_sizes);

void BM_join_view_out_vectors(benchmark::State& state) {
  auto size = state.range(0);
  std::vector<std::vector<int>> output(size, std::vector<int>(32));
  std::vector<int> input;
  input.resize(size * 32);
  std::ranges::fill(input, 10);

  for (auto _ : state) {
    benchmark::DoNotOptimize(output);
    benchmark::DoNotOptimize(input);
    std::ranges::copy(input, (output | std::views::join).begin());
  }
}
BENCHMARK(BM_join_view_out_vectors)->Apply(run_sizes);

void BM_join_view_deques(benchmark::State& state) {
  auto size = state.range(0);
  std::deque<std::deque<int>> deque(size, std::deque<int>(32));
  std::ranges::fill(deque | std::views::join, 10);
  std::vector<int> output;
  output.resize(size * 32);

  for (auto _ : state) {
    benchmark::DoNotOptimize(deque);
    benchmark::DoNotOptimize(output);
    std::ranges::copy(deque | std::views::join, output.begin());
  }
}
BENCHMARK(BM_join_view_deques)->Apply(run_sizes);
} // namespace

BENCHMARK_MAIN();
