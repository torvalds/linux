//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <array>
#include <charconv>
#include <random>

#include "benchmark/benchmark.h"
#include "test_macros.h"

static const std::array<unsigned, 1000> input = [] {
  std::mt19937 generator;
  std::uniform_int_distribution<unsigned> distribution(0, std::numeric_limits<unsigned>::max());
  std::array<unsigned, 1000> result;
  std::generate_n(result.begin(), result.size(), [&] { return distribution(generator); });
  return result;
}();

static void BM_to_chars_good(benchmark::State& state) {
  char buffer[128];
  int base = state.range(0);
  while (state.KeepRunningBatch(input.size()))
    for (auto value : input)
      benchmark::DoNotOptimize(std::to_chars(buffer, &buffer[128], value, base));
}
BENCHMARK(BM_to_chars_good)->DenseRange(2, 36, 1);

static void BM_to_chars_bad(benchmark::State& state) {
  char buffer[128];
  int base = state.range(0);
  struct sample {
    unsigned size;
    unsigned value;
  };
  std::array<sample, 1000> data;
  // Assume the failure occurs, on average, halfway during the conversion.
  std::transform(input.begin(), input.end(), data.begin(), [&](unsigned value) {
    std::to_chars_result result = std::to_chars(buffer, &buffer[128], value, base);
    return sample{unsigned((result.ptr - buffer) / 2), value};
  });

  while (state.KeepRunningBatch(data.size()))
    for (auto element : data)
      benchmark::DoNotOptimize(std::to_chars(buffer, &buffer[element.size], element.value, base));
}
BENCHMARK(BM_to_chars_bad)->DenseRange(2, 36, 1);

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  benchmark::RunSpecifiedBenchmarks();
}
