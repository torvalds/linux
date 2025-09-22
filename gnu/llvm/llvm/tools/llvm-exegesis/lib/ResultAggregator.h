//===-- ResultAggregator.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines result aggregators that are used to aggregate the results from
/// multiple full benchmark runs.
///
//===----------------------------------------------------------------------===//

#include "BenchmarkResult.h"

namespace llvm {
namespace exegesis {

class ResultAggregator {
public:
  static std::unique_ptr<ResultAggregator>
  CreateAggregator(Benchmark::RepetitionModeE RepetitionMode);

  virtual void AggregateResults(Benchmark &Result,
                                ArrayRef<Benchmark> OtherResults) const;
  virtual void AggregateMeasurement(BenchmarkMeasure &Measurement,
                                    const BenchmarkMeasure &NewMeasurement,
                                    const Benchmark &Result) const = 0;

  virtual ~ResultAggregator() = default;
};

} // namespace exegesis
} // namespace llvm
