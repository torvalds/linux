//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <benchmark/benchmark.h>
#include <exception>

void bm_make_exception_ptr(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::make_exception_ptr(42));
  }
}
BENCHMARK(bm_make_exception_ptr)->ThreadRange(1, 8);

BENCHMARK_MAIN();
