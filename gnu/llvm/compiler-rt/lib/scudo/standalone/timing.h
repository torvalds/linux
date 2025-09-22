//===-- timing.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TIMING_H_
#define SCUDO_TIMING_H_

#include "common.h"
#include "mutex.h"
#include "string_utils.h"
#include "thread_annotations.h"

#include <inttypes.h>
#include <string.h>

namespace scudo {

class TimingManager;

// A simple timer for evaluating execution time of code snippets. It can be used
// along with TimingManager or standalone.
class Timer {
public:
  // The use of Timer without binding to a TimingManager is supposed to do the
  // timer logging manually. Otherwise, TimingManager will do the logging stuff
  // for you.
  Timer() = default;
  Timer(Timer &&Other)
      : StartTime(0), AccTime(Other.AccTime), Manager(Other.Manager),
        HandleId(Other.HandleId) {
    Other.Manager = nullptr;
  }

  Timer(const Timer &) = delete;

  ~Timer();

  void start() {
    CHECK_EQ(StartTime, 0U);
    StartTime = getMonotonicTime();
  }
  void stop() {
    AccTime += getMonotonicTime() - StartTime;
    StartTime = 0;
  }
  u64 getAccumulatedTime() const { return AccTime; }

  // Unset the bound TimingManager so that we don't report the data back. This
  // is useful if we only want to track subset of certain scope events.
  void ignore() {
    StartTime = 0;
    AccTime = 0;
    Manager = nullptr;
  }

protected:
  friend class TimingManager;
  Timer(TimingManager &Manager, u32 HandleId)
      : Manager(&Manager), HandleId(HandleId) {}

  u64 StartTime = 0;
  u64 AccTime = 0;
  TimingManager *Manager = nullptr;
  u32 HandleId;
};

// A RAII-style wrapper for easy scope execution measurement. Note that in order
// not to take additional space for the message like `Name`. It only works with
// TimingManager.
class ScopedTimer : public Timer {
public:
  ScopedTimer(TimingManager &Manager, const char *Name);
  ScopedTimer(TimingManager &Manager, const Timer &Nest, const char *Name);
  ~ScopedTimer() { stop(); }
};

// In Scudo, the execution time of single run of code snippets may not be
// useful, we are more interested in the average time from several runs.
// TimingManager lets the registered timer report their data and reports the
// average execution time for each timer periodically.
class TimingManager {
public:
  TimingManager(u32 PrintingInterval = DefaultPrintingInterval)
      : PrintingInterval(PrintingInterval) {}
  ~TimingManager() {
    if (NumAllocatedTimers != 0)
      printAll();
  }

  Timer getOrCreateTimer(const char *Name) EXCLUDES(Mutex) {
    ScopedLock L(Mutex);

    CHECK_LT(strlen(Name), MaxLenOfTimerName);
    for (u32 I = 0; I < NumAllocatedTimers; ++I) {
      if (strncmp(Name, Timers[I].Name, MaxLenOfTimerName) == 0)
        return Timer(*this, I);
    }

    CHECK_LT(NumAllocatedTimers, MaxNumberOfTimers);
    strncpy(Timers[NumAllocatedTimers].Name, Name, MaxLenOfTimerName);
    TimerRecords[NumAllocatedTimers].AccumulatedTime = 0;
    TimerRecords[NumAllocatedTimers].Occurrence = 0;
    TimerRecords[NumAllocatedTimers].MaxTime = 0;
    return Timer(*this, NumAllocatedTimers++);
  }

  // Add a sub-Timer associated with another Timer. This is used when we want to
  // detail the execution time in the scope of a Timer.
  // For example,
  //   void Foo() {
  //     // T1 records the time spent in both first and second tasks.
  //     ScopedTimer T1(getTimingManager(), "Task1");
  //     {
  //       // T2 records the time spent in first task
  //       ScopedTimer T2(getTimingManager, T1, "Task2");
  //       // Do first task.
  //     }
  //     // Do second task.
  //   }
  //
  // The report will show proper indents to indicate the nested relation like,
  //   -- Average Operation Time -- -- Name (# of Calls) --
  //             10.0(ns)            Task1 (1)
  //              5.0(ns)              Task2 (1)
  Timer nest(const Timer &T, const char *Name) EXCLUDES(Mutex) {
    CHECK_EQ(T.Manager, this);
    Timer Nesting = getOrCreateTimer(Name);

    ScopedLock L(Mutex);
    CHECK_NE(Nesting.HandleId, T.HandleId);
    Timers[Nesting.HandleId].Nesting = T.HandleId;
    return Nesting;
  }

  void report(const Timer &T) EXCLUDES(Mutex) {
    ScopedLock L(Mutex);

    const u32 HandleId = T.HandleId;
    CHECK_LT(HandleId, MaxNumberOfTimers);
    u64 AccTime = T.getAccumulatedTime();
    TimerRecords[HandleId].AccumulatedTime += AccTime;
    if (AccTime > TimerRecords[HandleId].MaxTime) {
      TimerRecords[HandleId].MaxTime = AccTime;
    }
    ++TimerRecords[HandleId].Occurrence;
    ++NumEventsReported;
    if (NumEventsReported % PrintingInterval == 0) {
      ScopedString Str;
      getAllImpl(Str);
      Str.output();
    }
  }

  void printAll() EXCLUDES(Mutex) {
    ScopedString Str;
    getAll(Str);
    Str.output();
  }

  void getAll(ScopedString &Str) EXCLUDES(Mutex) {
    ScopedLock L(Mutex);
    getAllImpl(Str);
  }

private:
  void getAllImpl(ScopedString &Str) REQUIRES(Mutex) {
    static char AvgHeader[] = "-- Average Operation Time --";
    static char MaxHeader[] = "-- Maximum Operation Time --";
    static char NameHeader[] = "-- Name (# of Calls) --";
    Str.append("%-15s %-15s %-15s\n", AvgHeader, MaxHeader, NameHeader);

    for (u32 I = 0; I < NumAllocatedTimers; ++I) {
      if (Timers[I].Nesting != MaxNumberOfTimers)
        continue;
      getImpl(Str, I);
    }
  }

  void getImpl(ScopedString &Str, const u32 HandleId, const u32 ExtraIndent = 0)
      REQUIRES(Mutex) {
    const u64 AccumulatedTime = TimerRecords[HandleId].AccumulatedTime;
    const u64 Occurrence = TimerRecords[HandleId].Occurrence;
    const u64 Integral = Occurrence == 0 ? 0 : AccumulatedTime / Occurrence;
    // Only keep single digit of fraction is enough and it enables easier layout
    // maintenance.
    const u64 Fraction =
        Occurrence == 0 ? 0
                        : ((AccumulatedTime % Occurrence) * 10) / Occurrence;

    // Average time.
    Str.append("%14" PRId64 ".%" PRId64 "(ns) %-8s", Integral, Fraction, " ");

    // Maximum time.
    Str.append("%16" PRId64 "(ns) %-11s", TimerRecords[HandleId].MaxTime, " ");

    // Name and num occurrences.
    for (u32 I = 0; I < ExtraIndent; ++I)
      Str.append("%s", "  ");
    Str.append("%s (%" PRId64 ")\n", Timers[HandleId].Name, Occurrence);

    for (u32 I = 0; I < NumAllocatedTimers; ++I)
      if (Timers[I].Nesting == HandleId)
        getImpl(Str, I, ExtraIndent + 1);
  }

  // Instead of maintaining pages for timer registration, a static buffer is
  // sufficient for most use cases in Scudo.
  static constexpr u32 MaxNumberOfTimers = 50;
  static constexpr u32 MaxLenOfTimerName = 50;
  static constexpr u32 DefaultPrintingInterval = 100;

  struct Record {
    u64 AccumulatedTime = 0;
    u64 Occurrence = 0;
    u64 MaxTime = 0;
  };

  struct TimerInfo {
    char Name[MaxLenOfTimerName + 1];
    u32 Nesting = MaxNumberOfTimers;
  };

  HybridMutex Mutex;
  // The frequency of proactively dumping the timer statistics. For example, the
  // default setting is to dump the statistics every 100 reported events.
  u32 PrintingInterval GUARDED_BY(Mutex);
  u64 NumEventsReported GUARDED_BY(Mutex) = 0;
  u32 NumAllocatedTimers GUARDED_BY(Mutex) = 0;
  TimerInfo Timers[MaxNumberOfTimers] GUARDED_BY(Mutex);
  Record TimerRecords[MaxNumberOfTimers] GUARDED_BY(Mutex);
};

} // namespace scudo

#endif // SCUDO_TIMING_H_
