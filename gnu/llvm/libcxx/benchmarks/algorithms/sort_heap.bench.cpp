//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>

#include "common.h"

namespace {
template <class ValueType>
struct SortHeap {
  size_t Quantity;

  void run(benchmark::State& state) const {
    runOpOnCopies<ValueType>(state, Quantity, Order::Heap, BatchSize::CountElements, [](auto& Copy) {
      std::sort_heap(Copy.begin(), Copy.end());
    });
  }

  std::string name() const { return "BM_SortHeap" + ValueType::name() + "_" + std::to_string(Quantity); };
};
} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  makeCartesianProductBenchmark<SortHeap, AllValueTypes>(Quantities);
  benchmark::RunSpecifiedBenchmarks();
}
