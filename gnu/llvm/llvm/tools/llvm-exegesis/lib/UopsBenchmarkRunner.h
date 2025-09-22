//===-- UopsBenchmarkRunner.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A BenchmarkRunner implementation to measure uop decomposition.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_UOPSBENCHMARKRUNNER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_UOPSBENCHMARKRUNNER_H

#include "BenchmarkRunner.h"
#include "Target.h"

namespace llvm {
namespace exegesis {

class UopsBenchmarkRunner : public BenchmarkRunner {
public:
  UopsBenchmarkRunner(const LLVMState &State,
                      BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
                      ExecutionModeE ExecutionMode,
                      ArrayRef<ValidationEvent> ValCounters)
      : BenchmarkRunner(State, Benchmark::Uops, BenchmarkPhaseSelector,
                        ExecutionMode, ValCounters) {}
  ~UopsBenchmarkRunner() override;

  static constexpr const size_t kMinNumDifferentAddresses = 6;

private:
  Expected<std::vector<BenchmarkMeasure>>
  runMeasurements(const FunctionExecutor &Executor) const override;
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_UOPSBENCHMARKRUNNER_H
