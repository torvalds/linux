//===-- LatencyBenchmarkRunner.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A BenchmarkRunner implementation to measure instruction latencies.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_LATENCY_H
#define LLVM_TOOLS_LLVM_EXEGESIS_LATENCY_H

#include "BenchmarkRunner.h"
#include "Target.h"

namespace llvm {
namespace exegesis {

class LatencyBenchmarkRunner : public BenchmarkRunner {
public:
  LatencyBenchmarkRunner(const LLVMState &State, Benchmark::ModeE Mode,
                         BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
                         Benchmark::ResultAggregationModeE ResultAggMode,
                         ExecutionModeE ExecutionMode,
                         ArrayRef<ValidationEvent> ValCounters,
                         unsigned BenchmarkRepeatCount);
  ~LatencyBenchmarkRunner() override;

private:
  Expected<std::vector<BenchmarkMeasure>>
  runMeasurements(const FunctionExecutor &Executor) const override;

  Benchmark::ResultAggregationModeE ResultAggMode;
  unsigned NumMeasurements;
};
} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_LATENCY_H
