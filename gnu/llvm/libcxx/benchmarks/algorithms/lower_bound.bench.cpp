//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include "common.h"

namespace {
template <class ValueType>
struct LowerBound {
  size_t Quantity;

  mutable std::mt19937_64 rng{std::random_device{}()};

  void run(benchmark::State& state) const {
    runOpOnCopies<ValueType>(state, Quantity, Order::Ascending, BatchSize::CountBatch, [&](auto& Copy) {
      auto result = std::lower_bound(Copy.begin(), Copy.end(), Copy[rng() % Copy.size()]);
      benchmark::DoNotOptimize(result);
    });
  }

  std::string name() const { return "BM_LowerBound" + ValueType::name() + "_" + std::to_string(Quantity); }
};
} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  makeCartesianProductBenchmark<LowerBound, AllValueTypes>(Quantities);
  benchmark::RunSpecifiedBenchmarks();
}
