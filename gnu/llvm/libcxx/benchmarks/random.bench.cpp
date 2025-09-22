//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <random>

#include "benchmark/benchmark.h"

constexpr std::size_t MAX_BUFFER_LEN = 256;
constexpr std::size_t MAX_SEED_LEN   = 16;

static void BM_SeedSeq_Generate(benchmark::State& state) {
  std::array<std::uint32_t, MAX_BUFFER_LEN> buffer;
  std::array<std::uint32_t, MAX_SEED_LEN> seeds;
  {
    std::random_device rd;
    std::generate(std::begin(seeds), std::begin(seeds) + state.range(0), [&]() { return rd(); });
  }
  std::seed_seq seed(std::begin(seeds), std::begin(seeds) + state.range(0));
  for (auto _ : state) {
    seed.generate(std::begin(buffer), std::begin(buffer) + state.range(1));
  }
}
BENCHMARK(BM_SeedSeq_Generate)->Ranges({{1, MAX_SEED_LEN}, {1, MAX_BUFFER_LEN}});

BENCHMARK_MAIN();
