//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <list>
#include <memory_resource>

#include "benchmark/benchmark.h"

static void bm_list(benchmark::State& state) {
  char buffer[16384];
  std::pmr::monotonic_buffer_resource resource(buffer, sizeof(buffer));
  for (auto _ : state) {
    std::pmr::list<int> l(&resource);
    for (int64_t i = 0; i != state.range(); ++i) {
      l.push_back(1);
      benchmark::DoNotOptimize(l);
    }
    resource.release();
  }
}
BENCHMARK(bm_list)->Range(1, 2048);

BENCHMARK_MAIN();
