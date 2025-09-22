//===-- ResultAggregator.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ResultAggregator.h"

namespace llvm {
namespace exegesis {

class DefaultResultAggregator : public ResultAggregator {
  void AggregateResults(Benchmark &Result,
                        ArrayRef<Benchmark> OtherResults) const override{};
  void AggregateMeasurement(BenchmarkMeasure &Measurement,
                            const BenchmarkMeasure &NewMeasurement,
                            const Benchmark &Result) const override{};
};

class MinimumResultAggregator : public ResultAggregator {
  void AggregateMeasurement(BenchmarkMeasure &Measurement,
                            const BenchmarkMeasure &NewMeasurement,
                            const Benchmark &Result) const override;
};

void MinimumResultAggregator::AggregateMeasurement(
    BenchmarkMeasure &Measurement, const BenchmarkMeasure &NewMeasurement,
    const Benchmark &Result) const {
  Measurement.PerInstructionValue = std::min(
      Measurement.PerInstructionValue, NewMeasurement.PerInstructionValue);
  Measurement.PerSnippetValue =
      std::min(Measurement.PerSnippetValue, NewMeasurement.PerSnippetValue);
  Measurement.RawValue =
      std::min(Measurement.RawValue, NewMeasurement.RawValue);
}

class MiddleHalfResultAggregator : public ResultAggregator {
  void AggregateMeasurement(BenchmarkMeasure &Measurement,
                            const BenchmarkMeasure &NewMeasurement,
                            const Benchmark &Result) const override;
};

void MiddleHalfResultAggregator::AggregateMeasurement(
    BenchmarkMeasure &Measurement, const BenchmarkMeasure &NewMeasurement,
    const Benchmark &Result) const {
  Measurement.RawValue = NewMeasurement.RawValue - Measurement.RawValue;
  Measurement.PerInstructionValue = Measurement.RawValue;
  Measurement.PerInstructionValue /= Result.MinInstructions;
  Measurement.PerSnippetValue = Measurement.RawValue;
  Measurement.PerSnippetValue /=
      std::ceil(Result.MinInstructions /
                static_cast<double>(Result.Key.Instructions.size()));
}

void ResultAggregator::AggregateResults(
    Benchmark &Result, ArrayRef<Benchmark> OtherResults) const {
  for (const Benchmark &OtherResult : OtherResults) {
    append_range(Result.AssembledSnippet, OtherResult.AssembledSnippet);

    if (OtherResult.Measurements.empty())
      continue;

    assert(OtherResult.Measurements.size() == Result.Measurements.size() &&
           "Expected to have an identical number of measurements");

    for (auto I : zip(Result.Measurements, OtherResult.Measurements)) {
      BenchmarkMeasure &Measurement = std::get<0>(I);
      const BenchmarkMeasure &NewMeasurement = std::get<1>(I);

      assert(Measurement.Key == NewMeasurement.Key &&
             "Expected measurements to be symmetric");

      AggregateMeasurement(Measurement, NewMeasurement, Result);
    }
  }
}

std::unique_ptr<ResultAggregator>
ResultAggregator::CreateAggregator(Benchmark::RepetitionModeE RepetitionMode) {
  switch (RepetitionMode) {
  case Benchmark::RepetitionModeE::Duplicate:
  case Benchmark::RepetitionModeE::Loop:
    return std::make_unique<DefaultResultAggregator>();
  case Benchmark::RepetitionModeE::AggregateMin:
    return std::make_unique<MinimumResultAggregator>();
  case Benchmark::RepetitionModeE::MiddleHalfDuplicate:
  case Benchmark::RepetitionModeE::MiddleHalfLoop:
    return std::make_unique<MiddleHalfResultAggregator>();
  }
  llvm_unreachable("Unknown Benchmark::RepetitionModeE enum");
}

} // namespace exegesis
} // namespace llvm
