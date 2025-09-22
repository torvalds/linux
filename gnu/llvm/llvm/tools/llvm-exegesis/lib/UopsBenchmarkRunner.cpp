//===-- UopsBenchmarkRunner.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UopsBenchmarkRunner.h"

#include "Target.h"

namespace llvm {
namespace exegesis {

UopsBenchmarkRunner::~UopsBenchmarkRunner() = default;

Expected<std::vector<BenchmarkMeasure>>
UopsBenchmarkRunner::runMeasurements(const FunctionExecutor &Executor) const {
  std::vector<BenchmarkMeasure> Result;
  const PfmCountersInfo &PCI = State.getPfmCounters();

  SmallVector<const char *> ValCountersToRun;
  Error ValCounterErr = getValidationCountersToRun(ValCountersToRun);
  if (ValCounterErr)
    return std::move(ValCounterErr);

  // Uops per port.
  for (const auto *IssueCounter = PCI.IssueCounters,
                  *IssueCounterEnd = PCI.IssueCounters + PCI.NumIssueCounters;
       IssueCounter != IssueCounterEnd; ++IssueCounter) {
    SmallVector<int64_t> ValCounterPortValues(ValCountersToRun.size(), -1);
    if (!IssueCounter->Counter)
      continue;
    auto ExpectedCounterValue = Executor.runAndSample(
        IssueCounter->Counter, ValCountersToRun, ValCounterPortValues);
    if (!ExpectedCounterValue)
      return ExpectedCounterValue.takeError();

    std::map<ValidationEvent, int64_t> ValidationInfo;
    for (size_t I = 0; I < ValidationCounters.size(); ++I)
      ValidationInfo[ValidationCounters[I]] = ValCounterPortValues[I];

    Result.push_back(BenchmarkMeasure::Create(
        IssueCounter->ProcResName, (*ExpectedCounterValue)[0], ValidationInfo));
  }
  // NumMicroOps.
  if (const char *const UopsCounter = PCI.UopsCounter) {
    SmallVector<int64_t> ValCounterUopsValues(ValCountersToRun.size(), -1);
    auto ExpectedCounterValue = Executor.runAndSample(
        UopsCounter, ValCountersToRun, ValCounterUopsValues);
    if (!ExpectedCounterValue)
      return ExpectedCounterValue.takeError();

    std::map<ValidationEvent, int64_t> ValidationInfo;
    for (size_t I = 0; I < ValidationCounters.size(); ++I)
      ValidationInfo[ValidationCounters[I]] = ValCounterUopsValues[I];

    Result.push_back(BenchmarkMeasure::Create(
        "NumMicroOps", (*ExpectedCounterValue)[0], ValidationInfo));
  }
  return std::move(Result);
}

} // namespace exegesis
} // namespace llvm
