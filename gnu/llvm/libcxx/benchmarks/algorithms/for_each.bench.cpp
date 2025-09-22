//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <benchmark/benchmark.h>
#include <deque>

static void bm_deque_for_each(benchmark::State& state) {
  std::deque<char> vec1(state.range(), '1');
  for (auto _ : state) {
    benchmark::DoNotOptimize(vec1);
    benchmark::DoNotOptimize(
        std::for_each(vec1.begin(), vec1.end(), [](char& v) { v = std::clamp(v, (char)10, (char)100); }));
  }
}
BENCHMARK(bm_deque_for_each)->DenseRange(1, 8)->Range(16, 1 << 20);

BENCHMARK_MAIN();
