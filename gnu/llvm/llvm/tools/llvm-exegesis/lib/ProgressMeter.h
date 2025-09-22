//===-- ProgressMeter.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_PROGRESSMETER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_PROGRESSMETER_H

#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <optional>
#include <type_traits>

namespace llvm {
namespace exegesis {

/// Represents `\sum_{i=1..accumulated}{step_i} / accumulated`,
/// where `step_i` is the value passed to the `i`-th call to `step()`,
/// and `accumulated` is the total number of calls to `step()`.
template <typename NumTy, typename DenTy = int> class SimpleMovingAverage {
  NumTy Accumulated = NumTy(0);
  DenTy Steps = 0;

public:
  SimpleMovingAverage() = default;

  SimpleMovingAverage(const SimpleMovingAverage &) = delete;
  SimpleMovingAverage(SimpleMovingAverage &&) = delete;
  SimpleMovingAverage &operator=(const SimpleMovingAverage &) = delete;
  SimpleMovingAverage &operator=(SimpleMovingAverage &&) = delete;

  inline void step(NumTy Quantity) {
    Accumulated += Quantity;
    ++Steps;
  }

  inline NumTy getAccumulated() const { return Accumulated; }

  inline DenTy getNumSteps() const { return Steps; }

  template <typename AvgTy = NumTy>
  inline std::optional<AvgTy> getAverage() const {
    if (Steps == 0)
      return std::nullopt;
    return AvgTy(Accumulated) / Steps;
  }
};

template <typename ClockTypeTy = std::chrono::steady_clock,
          typename = std::enable_if_t<ClockTypeTy::is_steady>>
class ProgressMeter {
public:
  using ClockType = ClockTypeTy;
  using TimePointType = std::chrono::time_point<ClockType>;
  using DurationType = std::chrono::duration<typename ClockType::rep,
                                             typename ClockType::period>;
  using CompetionPercentage = int;
  using Sec = std::chrono::duration<double, std::chrono::seconds::period>;

private:
  raw_ostream &Out;
  const int NumStepsTotal;
  SimpleMovingAverage<DurationType> ElapsedTotal;

public:
  friend class ProgressMeterStep;
  class ProgressMeterStep {
    ProgressMeter *P;
    const TimePointType Begin;

  public:
    inline ProgressMeterStep(ProgressMeter *P_)
        : P(P_), Begin(P ? ProgressMeter<ClockType>::ClockType::now()
                         : TimePointType()) {}

    inline ~ProgressMeterStep() {
      if (!P)
        return;
      const TimePointType End = ProgressMeter<ClockType>::ClockType::now();
      P->step(End - Begin);
    }

    ProgressMeterStep(const ProgressMeterStep &) = delete;
    ProgressMeterStep(ProgressMeterStep &&) = delete;
    ProgressMeterStep &operator=(const ProgressMeterStep &) = delete;
    ProgressMeterStep &operator=(ProgressMeterStep &&) = delete;
  };

  ProgressMeter(int NumStepsTotal_, raw_ostream &out_ = errs())
      : Out(out_), NumStepsTotal(NumStepsTotal_) {
    assert(NumStepsTotal > 0 && "No steps are planned?");
  }

  ProgressMeter(const ProgressMeter &) = delete;
  ProgressMeter(ProgressMeter &&) = delete;
  ProgressMeter &operator=(const ProgressMeter &) = delete;
  ProgressMeter &operator=(ProgressMeter &&) = delete;

private:
  void step(DurationType Elapsed) {
    assert((ElapsedTotal.getNumSteps() < NumStepsTotal) && "Step overflow!");
    assert(Elapsed.count() >= 0 && "Negative time drift detected.");

    auto [OldProgress, OldEta] = eta();
    ElapsedTotal.step(Elapsed);
    auto [NewProgress, NewEta] = eta();

    if (NewProgress < OldProgress + 1)
      return;

    Out << format("Processing... %*d%%", 3, NewProgress);
    if (NewEta) {
      int SecondsTotal = std::ceil(NewEta->count());
      int Seconds = SecondsTotal % 60;
      int MinutesTotal = SecondsTotal / 60;

      Out << format(", ETA %02d:%02d", MinutesTotal, Seconds);
    }
    Out << "\n";
    Out.flush();
  }

  inline std::pair<CompetionPercentage, std::optional<Sec>> eta() const {
    CompetionPercentage Progress =
        (100 * ElapsedTotal.getNumSteps()) / NumStepsTotal;

    std::optional<Sec> ETA;
    if (std::optional<Sec> AverageStepDuration =
            ElapsedTotal.template getAverage<Sec>())
      ETA = (NumStepsTotal - ElapsedTotal.getNumSteps()) * *AverageStepDuration;

    return {Progress, ETA};
  }
};

} // namespace exegesis
} // namespace llvm

#endif
