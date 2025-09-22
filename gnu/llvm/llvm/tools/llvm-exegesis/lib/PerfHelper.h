//===-- PerfHelper.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Helpers for measuring perf events.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_PERFHELPER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_PERFHELPER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <functional>
#include <memory>

#ifdef _MSC_VER
typedef int pid_t;
#else
#include <sys/types.h>
#endif // _MSC_VER

struct perf_event_attr;

namespace llvm {
namespace exegesis {
namespace pfm {

// Returns true on error.
bool pfmInitialize();
void pfmTerminate();

// Retrieves the encoding for the event described by pfm_event_string.
// NOTE: pfm_initialize() must be called before creating PerfEvent objects.
class PerfEvent {
public:
  // Dummy event that does not require access to counters (for tests).
  static const char *const DummyEventString;

  // http://perfmon2.sourceforge.net/manv4/libpfm.html
  // Events are expressed as strings. e.g. "INSTRUCTION_RETIRED"
  explicit PerfEvent(StringRef PfmEventString);

  PerfEvent(const PerfEvent &) = delete;
  PerfEvent(PerfEvent &&other);
  ~PerfEvent();

  // The pfm_event_string passed at construction time.
  StringRef name() const;

  // Whether the event was successfully created.
  bool valid() const;

  // The encoded event to be passed to the Kernel.
  const perf_event_attr *attribute() const;

  // The fully qualified name for the event.
  // e.g. "snb_ep::INSTRUCTION_RETIRED:e=0:i=0:c=0:t=0:u=1:k=0:mg=0:mh=1"
  StringRef getPfmEventString() const;

protected:
  PerfEvent() = default;
  std::string EventString;
  std::string FullQualifiedEventString;
  perf_event_attr *Attr;

private:
  void initRealEvent(StringRef PfmEventString);
};

// Represents a single event that has been configured in the Linux perf
// subsystem.
class ConfiguredEvent {
public:
  ConfiguredEvent(PerfEvent &&EventToConfigure);

  void initRealEvent(const pid_t ProcessID, const int GroupFD = -1);
  Expected<SmallVector<int64_t>> readOrError(StringRef FunctionBytes) const;
  int getFileDescriptor() const { return FileDescriptor; }
  bool isDummyEvent() const {
    return Event.name() == PerfEvent::DummyEventString;
  }

  ConfiguredEvent(const ConfiguredEvent &) = delete;
  ConfiguredEvent(ConfiguredEvent &&other) = default;

  ~ConfiguredEvent();

private:
  PerfEvent Event;
  int FileDescriptor = -1;
};

// Consists of a counter measuring a specific event and associated validation
// counters measuring execution conditions. All counters in a group are part
// of a single event group and are thus scheduled on and off the CPU as a single
// unit.
class CounterGroup {
public:
  // event: the PerfEvent to measure.
  explicit CounterGroup(PerfEvent &&event, std::vector<PerfEvent> &&ValEvents,
                        pid_t ProcessID = 0);

  CounterGroup(const CounterGroup &) = delete;
  CounterGroup(CounterGroup &&other) = default;

  virtual ~CounterGroup() = default;

  /// Starts the measurement of the event.
  virtual void start();

  /// Stops the measurement of the event.
  void stop();

  /// Returns the current value of the counter or error if it cannot be read.
  /// FunctionBytes: The benchmark function being executed.
  /// This is used to filter out the measurements to ensure they are only
  /// within the benchmarked code.
  /// If empty (or not specified), then no filtering will be done.
  /// Not all counters choose to use this.
  virtual Expected<SmallVector<int64_t, 4>>
  readOrError(StringRef FunctionBytes = StringRef()) const;

  virtual Expected<SmallVector<int64_t>> readValidationCountersOrError() const;

  virtual int numValues() const;

  int getFileDescriptor() const { return EventCounter.getFileDescriptor(); }

protected:
  ConfiguredEvent EventCounter;
  bool IsDummyEvent;
  std::vector<ConfiguredEvent> ValidationEventCounters;

private:
  void initRealEvent(pid_t ProcessID);
};

} // namespace pfm
} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_PERFHELPER_H
