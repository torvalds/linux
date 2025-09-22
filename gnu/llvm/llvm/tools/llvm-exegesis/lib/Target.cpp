//===-- Target.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "Target.h"

#include "LatencyBenchmarkRunner.h"
#include "ParallelSnippetGenerator.h"
#include "PerfHelper.h"
#include "SerialSnippetGenerator.h"
#include "UopsBenchmarkRunner.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/SubtargetFeature.h"

namespace llvm {
namespace exegesis {

cl::OptionCategory Options("llvm-exegesis options");
cl::OptionCategory BenchmarkOptions("llvm-exegesis benchmark options");
cl::OptionCategory AnalysisOptions("llvm-exegesis analysis options");

ExegesisTarget::~ExegesisTarget() {} // anchor.

static ExegesisTarget *FirstTarget = nullptr;

const ExegesisTarget *ExegesisTarget::lookup(Triple TT) {
  for (const ExegesisTarget *T = FirstTarget; T != nullptr; T = T->Next) {
    if (T->matchesArch(TT.getArch()))
      return T;
  }
  return nullptr;
}

Expected<std::unique_ptr<pfm::CounterGroup>>
ExegesisTarget::createCounter(StringRef CounterName, const LLVMState &,
                              ArrayRef<const char *> ValidationCounters,
                              const pid_t ProcessID) const {
  pfm::PerfEvent Event(CounterName);
  if (!Event.valid())
    return make_error<Failure>(Twine("Unable to create counter with name '")
                                   .concat(CounterName)
                                   .concat("'"));

  std::vector<pfm::PerfEvent> ValidationEvents;
  for (const char *ValCounterName : ValidationCounters) {
    ValidationEvents.emplace_back(ValCounterName);
    if (!ValidationEvents.back().valid())
      return make_error<Failure>(
          Twine("Unable to create validation counter with name '")
              .concat(ValCounterName)
              .concat("'"));
  }

  return std::make_unique<pfm::CounterGroup>(
      std::move(Event), std::move(ValidationEvents), ProcessID);
}

void ExegesisTarget::registerTarget(ExegesisTarget *Target) {
  if (FirstTarget == nullptr) {
    FirstTarget = Target;
    return;
  }
  if (Target->Next != nullptr)
    return; // Already registered.
  Target->Next = FirstTarget;
  FirstTarget = Target;
}

std::unique_ptr<SnippetGenerator> ExegesisTarget::createSnippetGenerator(
    Benchmark::ModeE Mode, const LLVMState &State,
    const SnippetGenerator::Options &Opts) const {
  switch (Mode) {
  case Benchmark::Unknown:
    return nullptr;
  case Benchmark::Latency:
    return createSerialSnippetGenerator(State, Opts);
  case Benchmark::Uops:
  case Benchmark::InverseThroughput:
    return createParallelSnippetGenerator(State, Opts);
  }
  return nullptr;
}

Expected<std::unique_ptr<BenchmarkRunner>>
ExegesisTarget::createBenchmarkRunner(
    Benchmark::ModeE Mode, const LLVMState &State,
    BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
    BenchmarkRunner::ExecutionModeE ExecutionMode,
    unsigned BenchmarkRepeatCount, ArrayRef<ValidationEvent> ValidationCounters,
    Benchmark::ResultAggregationModeE ResultAggMode) const {
  PfmCountersInfo PfmCounters = State.getPfmCounters();
  switch (Mode) {
  case Benchmark::Unknown:
    return nullptr;
  case Benchmark::Latency:
  case Benchmark::InverseThroughput:
    if (BenchmarkPhaseSelector == BenchmarkPhaseSelectorE::Measure &&
        !PfmCounters.CycleCounter) {
      const char *ModeName = Mode == Benchmark::Latency
                                 ? "latency"
                                 : "inverse_throughput";
      return make_error<Failure>(
          Twine("can't run '")
              .concat(ModeName)
              .concat(
                  "' mode, sched model does not define a cycle counter. You "
                  "can pass --benchmark-phase=... to skip the actual "
                  "benchmarking or --use-dummy-perf-counters to not query "
                  "the kernel for real event counts."));
    }
    return createLatencyBenchmarkRunner(
        State, Mode, BenchmarkPhaseSelector, ResultAggMode, ExecutionMode,
        ValidationCounters, BenchmarkRepeatCount);
  case Benchmark::Uops:
    if (BenchmarkPhaseSelector == BenchmarkPhaseSelectorE::Measure &&
        !PfmCounters.UopsCounter && !PfmCounters.IssueCounters)
      return make_error<Failure>(
          "can't run 'uops' mode, sched model does not define uops or issue "
          "counters. You can pass --benchmark-phase=... to skip the actual "
          "benchmarking or --use-dummy-perf-counters to not query the kernel "
          "for real event counts.");
    return createUopsBenchmarkRunner(State, BenchmarkPhaseSelector,
                                     ResultAggMode, ExecutionMode,
                                     ValidationCounters);
  }
  return nullptr;
}

std::unique_ptr<SnippetGenerator> ExegesisTarget::createSerialSnippetGenerator(
    const LLVMState &State, const SnippetGenerator::Options &Opts) const {
  return std::make_unique<SerialSnippetGenerator>(State, Opts);
}

std::unique_ptr<SnippetGenerator> ExegesisTarget::createParallelSnippetGenerator(
    const LLVMState &State, const SnippetGenerator::Options &Opts) const {
  return std::make_unique<ParallelSnippetGenerator>(State, Opts);
}

std::unique_ptr<BenchmarkRunner> ExegesisTarget::createLatencyBenchmarkRunner(
    const LLVMState &State, Benchmark::ModeE Mode,
    BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
    Benchmark::ResultAggregationModeE ResultAggMode,
    BenchmarkRunner::ExecutionModeE ExecutionMode,
    ArrayRef<ValidationEvent> ValidationCounters,
    unsigned BenchmarkRepeatCount) const {
  return std::make_unique<LatencyBenchmarkRunner>(
      State, Mode, BenchmarkPhaseSelector, ResultAggMode, ExecutionMode,
      ValidationCounters, BenchmarkRepeatCount);
}

std::unique_ptr<BenchmarkRunner> ExegesisTarget::createUopsBenchmarkRunner(
    const LLVMState &State, BenchmarkPhaseSelectorE BenchmarkPhaseSelector,
    Benchmark::ResultAggregationModeE /*unused*/,
    BenchmarkRunner::ExecutionModeE ExecutionMode,
    ArrayRef<ValidationEvent> ValidationCounters) const {
  return std::make_unique<UopsBenchmarkRunner>(
      State, BenchmarkPhaseSelector, ExecutionMode, ValidationCounters);
}

static_assert(std::is_trivial_v<PfmCountersInfo>,
              "We shouldn't have dynamic initialization here");

const PfmCountersInfo PfmCountersInfo::Default = {nullptr, nullptr, nullptr,
                                                  0u,      nullptr, 0u};
const PfmCountersInfo PfmCountersInfo::Dummy = {
    pfm::PerfEvent::DummyEventString,
    pfm::PerfEvent::DummyEventString,
    nullptr,
    0u,
    nullptr,
    0u};

const PfmCountersInfo &ExegesisTarget::getPfmCounters(StringRef CpuName) const {
  assert(
      is_sorted(CpuPfmCounters,
                [](const CpuAndPfmCounters &LHS, const CpuAndPfmCounters &RHS) {
                  return strcmp(LHS.CpuName, RHS.CpuName) < 0;
                }) &&
      "CpuPfmCounters table is not sorted");

  // Find entry
  auto Found = lower_bound(CpuPfmCounters, CpuName);
  if (Found == CpuPfmCounters.end() || StringRef(Found->CpuName) != CpuName) {
    // Use the default.
    if (!CpuPfmCounters.empty() && CpuPfmCounters.begin()->CpuName[0] == '\0') {
      Found = CpuPfmCounters.begin(); // The target specifies a default.
    } else {
      return PfmCountersInfo::Default; // No default for the target.
    }
  }
  assert(Found->PCI && "Missing counters");
  return *Found->PCI;
}

const PfmCountersInfo &ExegesisTarget::getDummyPfmCounters() const {
  return PfmCountersInfo::Dummy;
}

ExegesisTarget::SavedState::~SavedState() {} // anchor.

namespace {

bool opcodeIsNotAvailable(unsigned, const FeatureBitset &) { return false; }

// Default implementation.
class ExegesisDefaultTarget : public ExegesisTarget {
public:
  ExegesisDefaultTarget() : ExegesisTarget({}, opcodeIsNotAvailable) {}

private:
  std::vector<MCInst> setRegTo(const MCSubtargetInfo &STI, unsigned Reg,
                               const APInt &Value) const override {
    llvm_unreachable("Not yet implemented");
  }

  bool matchesArch(Triple::ArchType Arch) const override {
    llvm_unreachable("never called");
    return false;
  }
};

} // namespace

const ExegesisTarget &ExegesisTarget::getDefault() {
  static ExegesisDefaultTarget Target;
  return Target;
}

} // namespace exegesis
} // namespace llvm
