//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "benchmark/benchmark.h"

static void BM_SharedPtrCreateDestroy(benchmark::State& st) {
  while (st.KeepRunning()) {
    auto sp = std::make_shared<int>(42);
    benchmark::DoNotOptimize(sp.get());
  }
}
BENCHMARK(BM_SharedPtrCreateDestroy);

static void BM_SharedPtrIncDecRef(benchmark::State& st) {
  auto sp = std::make_shared<int>(42);
  benchmark::DoNotOptimize(sp.get());
  while (st.KeepRunning()) {
    std::shared_ptr<int> sp2(sp);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_SharedPtrIncDecRef);

static void BM_WeakPtrIncDecRef(benchmark::State& st) {
  auto sp = std::make_shared<int>(42);
  benchmark::DoNotOptimize(sp.get());
  while (st.KeepRunning()) {
    std::weak_ptr<int> wp(sp);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_WeakPtrIncDecRef);

BENCHMARK_MAIN();
