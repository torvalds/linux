//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>
#include <system_error>

#include "benchmark/benchmark.h"

static void BM_SystemErrorWithMessage(benchmark::State& state) {
  for (auto _ : state) {
    std::error_code ec{};
    benchmark::DoNotOptimize(std::system_error{ec, ""});
  }
}
BENCHMARK(BM_SystemErrorWithMessage);

static void BM_SystemErrorWithoutMessage(benchmark::State& state) {
  for (auto _ : state) {
    std::error_code ec{};
    benchmark::DoNotOptimize(std::system_error{ec});
  }
}
BENCHMARK(BM_SystemErrorWithoutMessage);

BENCHMARK_MAIN();
