//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This benchmark compares the performance of std::mutex and std::shared_mutex in contended scenarios.
// it's meant to establish a baseline overhead for std::shared_mutex and std::mutex, and to help inform decisions about
// which mutex to use when selecting a mutex type for a given use case.

#include <atomic>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <thread>

#include "benchmark/benchmark.h"

int global_value = 42;
std::mutex m;
std::shared_mutex sm;

static void BM_shared_mutex(benchmark::State& state) {
  for (auto _ : state) {
    std::shared_lock<std::shared_mutex> lock(sm);
    benchmark::DoNotOptimize(global_value);
  }
}

static void BM_mutex(benchmark::State& state) {
  for (auto _ : state) {
    std::lock_guard<std::mutex> lock(m);
    benchmark::DoNotOptimize(global_value);
  }
}

BENCHMARK(BM_shared_mutex)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(32);
BENCHMARK(BM_mutex)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(32);

BENCHMARK_MAIN();
