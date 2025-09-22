//===-- LatencyBenchmarkRunner.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LatencyBenchmarkRunner.h"

#include "BenchmarkRunner.h"
#include "Target.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cmath>

#define DEBUG_TYPE "exegesis-latency-benchmarkrunner"

namespace llvm {
namespace exegesis {

LatencyBenchmarkRunner::LatencyBenchmarkRunner(
    const LLVMState &State, Benchmark::ModeE Mode,
    BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
    Benchmark::ResultAggregationModeE ResultAgg, ExecutionModeE ExecutionMode,
    ArrayRef<ValidationEvent> ValCounters, unsigned BenchmarkRepeatCount)
    : BenchmarkRunner(State, Mode, BenchmarkPhaseSelector, ExecutionMode,
                      ValCounters) {
  assert((Mode == Benchmark::Latency || Mode == Benchmark::InverseThroughput) &&
         "invalid mode");
  ResultAggMode = ResultAgg;
  NumMeasurements = BenchmarkRepeatCount;
}

LatencyBenchmarkRunner::~LatencyBenchmarkRunner() = default;

static double computeVariance(const SmallVector<int64_t, 4> &Values) {
  if (Values.empty())
    return 0.0;
  double Sum = std::accumulate(Values.begin(), Values.end(), 0.0);

  const double Mean = Sum / Values.size();
  double Ret = 0;
  for (const auto &V : Values) {
    double Delta = V - Mean;
    Ret += Delta * Delta;
  }
  return Ret / Values.size();
}

static int64_t findMin(const SmallVector<int64_t, 4> &Values) {
  if (Values.empty())
    return 0;
  return *llvm::min_element(Values);
}

static int64_t findMax(const SmallVector<int64_t, 4> &Values) {
  if (Values.empty())
    return 0;
  return *llvm::max_element(Values);
}

static int64_t findMean(const SmallVector<int64_t, 4> &Values) {
  if (Values.empty())
    return 0;
  return std::accumulate(Values.begin(), Values.end(), 0.0) /
         static_cast<double>(Values.size());
}

Expected<std::vector<BenchmarkMeasure>> LatencyBenchmarkRunner::runMeasurements(
    const FunctionExecutor &Executor) const {
  // Cycle measurements include some overhead from the kernel. Repeat the
  // measure several times and return the aggregated value, as specified by
  // ResultAggMode.
  SmallVector<int64_t, 4> AccumulatedValues;
  double MinVariance = std::numeric_limits<double>::infinity();
  const PfmCountersInfo &PCI = State.getPfmCounters();
  const char *CounterName = PCI.CycleCounter;

  SmallVector<const char *> ValCountersToRun;
  Error ValCounterErr = getValidationCountersToRun(ValCountersToRun);
  if (ValCounterErr)
    return std::move(ValCounterErr);

  SmallVector<int64_t> ValCounterValues(ValCountersToRun.size(), 0);
  // Values count for each run.
  int ValuesCount = 0;
  for (size_t I = 0; I < NumMeasurements; ++I) {
    SmallVector<int64_t> IterationValCounterValues(ValCountersToRun.size(), -1);
    auto ExpectedCounterValues = Executor.runAndSample(
        CounterName, ValCountersToRun, IterationValCounterValues);
    if (!ExpectedCounterValues)
      return ExpectedCounterValues.takeError();
    ValuesCount = ExpectedCounterValues.get().size();
    if (ValuesCount == 1) {
      LLVM_DEBUG(dbgs() << "Latency value: " << ExpectedCounterValues.get()[0]
                        << "\n");
      AccumulatedValues.push_back(ExpectedCounterValues.get()[0]);
    } else {
      // We'll keep the reading with lowest variance (ie., most stable)
      double Variance = computeVariance(*ExpectedCounterValues);
      if (MinVariance > Variance) {
        AccumulatedValues = std::move(ExpectedCounterValues.get());
        MinVariance = Variance;
      }
    }

    for (size_t I = 0; I < ValCounterValues.size(); ++I) {
      LLVM_DEBUG(dbgs() << getValidationEventName(ValidationCounters[I]) << ": "
                        << IterationValCounterValues[I] << "\n");
      ValCounterValues[I] += IterationValCounterValues[I];
    }
  }

  std::map<ValidationEvent, int64_t> ValidationInfo;
  for (size_t I = 0; I < ValidationCounters.size(); ++I)
    ValidationInfo[ValidationCounters[I]] = ValCounterValues[I];

  std::string ModeName;
  switch (Mode) {
  case Benchmark::Latency:
    ModeName = "latency";
    break;
  case Benchmark::InverseThroughput:
    ModeName = "inverse_throughput";
    break;
  default:
    break;
  }

  switch (ResultAggMode) {
  case Benchmark::MinVariance: {
    if (ValuesCount == 1)
      errs() << "Each sample only has one value. result-aggregation-mode "
                "of min-variance is probably non-sensical\n";
    std::vector<BenchmarkMeasure> Result;
    Result.reserve(AccumulatedValues.size());
    for (const int64_t Value : AccumulatedValues)
      Result.push_back(
          BenchmarkMeasure::Create(ModeName, Value, ValidationInfo));
    return std::move(Result);
  }
  case Benchmark::Min: {
    std::vector<BenchmarkMeasure> Result;
    Result.push_back(BenchmarkMeasure::Create(
        ModeName, findMin(AccumulatedValues), ValidationInfo));
    return std::move(Result);
  }
  case Benchmark::Max: {
    std::vector<BenchmarkMeasure> Result;
    Result.push_back(BenchmarkMeasure::Create(
        ModeName, findMax(AccumulatedValues), ValidationInfo));
    return std::move(Result);
  }
  case Benchmark::Mean: {
    std::vector<BenchmarkMeasure> Result;
    Result.push_back(BenchmarkMeasure::Create(
        ModeName, findMean(AccumulatedValues), ValidationInfo));
    return std::move(Result);
  }
  }
  return make_error<Failure>(Twine("Unexpected benchmark mode(")
                                 .concat(std::to_string(Mode))
                                 .concat(" and unexpected ResultAggMode ")
                                 .concat(std::to_string(ResultAggMode)));
}

} // namespace exegesis
} // namespace llvm
