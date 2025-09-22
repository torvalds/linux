//===-- PerfHelper.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PerfHelper.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#ifdef HAVE_LIBPFM
#include <perfmon/perf_event.h>
#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_perf_event.h>
#endif

#include <cassert>
#include <cstddef>
#include <errno.h>  // for erno
#include <string.h> // for strerror()

namespace llvm {
namespace exegesis {
namespace pfm {

#ifdef HAVE_LIBPFM
static bool isPfmError(int Code) { return Code != PFM_SUCCESS; }
#endif

bool pfmInitialize() {
#ifdef HAVE_LIBPFM
  return isPfmError(pfm_initialize());
#else
  return true;
#endif
}

void pfmTerminate() {
#ifdef HAVE_LIBPFM
  pfm_terminate();
#endif
}

// Performance counters may be unavailable for a number of reasons (such as
// kernel.perf_event_paranoid restriction or CPU being unknown to libpfm).
//
// Dummy event can be specified to skip interaction with real performance
// counters while still passing control to the generated code snippet.
const char *const PerfEvent::DummyEventString = "not-really-an-event";

PerfEvent::~PerfEvent() {
#ifdef HAVE_LIBPFM
  delete Attr;
  ;
#endif
}

PerfEvent::PerfEvent(PerfEvent &&Other)
    : EventString(std::move(Other.EventString)),
      FullQualifiedEventString(std::move(Other.FullQualifiedEventString)),
      Attr(Other.Attr) {
  Other.Attr = nullptr;
}

PerfEvent::PerfEvent(StringRef PfmEventString)
    : EventString(PfmEventString.str()), Attr(nullptr) {
  if (PfmEventString != DummyEventString)
    initRealEvent(PfmEventString);
  else
    FullQualifiedEventString = PfmEventString;
}

void PerfEvent::initRealEvent(StringRef PfmEventString) {
#ifdef HAVE_LIBPFM
  char *Fstr = nullptr;
  pfm_perf_encode_arg_t Arg = {};
  Attr = new perf_event_attr();
  Arg.attr = Attr;
  Arg.fstr = &Fstr;
  Arg.size = sizeof(pfm_perf_encode_arg_t);
  const int Result = pfm_get_os_event_encoding(EventString.c_str(), PFM_PLM3,
                                               PFM_OS_PERF_EVENT, &Arg);
  if (isPfmError(Result)) {
    // We don't know beforehand which counters are available (e.g. 6 uops ports
    // on Sandybridge but 8 on Haswell) so we report the missing counter without
    // crashing.
    errs() << pfm_strerror(Result) << " - cannot create event " << EventString
           << "\n";
  }
  if (Fstr) {
    FullQualifiedEventString = Fstr;
    free(Fstr);
  }
#endif
}

StringRef PerfEvent::name() const { return EventString; }

bool PerfEvent::valid() const { return !FullQualifiedEventString.empty(); }

const perf_event_attr *PerfEvent::attribute() const { return Attr; }

StringRef PerfEvent::getPfmEventString() const {
  return FullQualifiedEventString;
}

ConfiguredEvent::ConfiguredEvent(PerfEvent &&EventToConfigure)
    : Event(std::move(EventToConfigure)) {
  assert(Event.valid());
}

#ifdef HAVE_LIBPFM
void ConfiguredEvent::initRealEvent(const pid_t ProcessID, const int GroupFD) {
  const int CPU = -1;
  const uint32_t Flags = 0;
  perf_event_attr AttrCopy = *Event.attribute();
  FileDescriptor = perf_event_open(&AttrCopy, ProcessID, CPU, GroupFD, Flags);
  if (FileDescriptor == -1) {
    errs() << "Unable to open event. ERRNO: " << strerror(errno)
           << ". Make sure your kernel allows user "
              "space perf monitoring.\nYou may want to try:\n$ sudo sh "
              "-c 'echo -1 > /proc/sys/kernel/perf_event_paranoid'.\n"
           << "If you are debugging and just want to execute the snippet "
              "without actually reading performance counters, "
              "pass --use-dummy-perf-counters command line option.\n";
  }
  assert(FileDescriptor != -1 && "Unable to open event");
}

Expected<SmallVector<int64_t>>
ConfiguredEvent::readOrError(StringRef /*unused*/) const {
  int64_t Count = 0;
  ssize_t ReadSize = ::read(FileDescriptor, &Count, sizeof(Count));

  if (ReadSize != sizeof(Count))
    return make_error<StringError>("Failed to read event counter",
                                   errc::io_error);

  SmallVector<int64_t, 1> Result;
  Result.push_back(Count);
  return Result;
}

ConfiguredEvent::~ConfiguredEvent() { close(FileDescriptor); }
#else
void ConfiguredEvent::initRealEvent(pid_t ProcessID, const int GroupFD) {}

Expected<SmallVector<int64_t>>
ConfiguredEvent::readOrError(StringRef /*unused*/) const {
  return make_error<StringError>("Not implemented",
                                 errc::function_not_supported);
}

ConfiguredEvent::~ConfiguredEvent() = default;
#endif // HAVE_LIBPFM

CounterGroup::CounterGroup(PerfEvent &&E, std::vector<PerfEvent> &&ValEvents,
                           pid_t ProcessID)
    : EventCounter(std::move(E)) {
  IsDummyEvent = EventCounter.isDummyEvent();

  for (auto &&ValEvent : ValEvents)
    ValidationEventCounters.emplace_back(std::move(ValEvent));

  if (!IsDummyEvent)
    initRealEvent(ProcessID);
}

#ifdef HAVE_LIBPFM
void CounterGroup::initRealEvent(pid_t ProcessID) {
  EventCounter.initRealEvent(ProcessID);

  for (auto &ValCounter : ValidationEventCounters)
    ValCounter.initRealEvent(ProcessID, getFileDescriptor());
}

void CounterGroup::start() {
  if (!IsDummyEvent)
    ioctl(getFileDescriptor(), PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
}

void CounterGroup::stop() {
  if (!IsDummyEvent)
    ioctl(getFileDescriptor(), PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
}

Expected<SmallVector<int64_t, 4>>
CounterGroup::readOrError(StringRef FunctionBytes) const {
  if (!IsDummyEvent)
    return EventCounter.readOrError(FunctionBytes);
  else
    return SmallVector<int64_t, 1>(1, 42);
}

Expected<SmallVector<int64_t>>
CounterGroup::readValidationCountersOrError() const {
  SmallVector<int64_t, 4> Result;
  for (const auto &ValCounter : ValidationEventCounters) {
    Expected<SmallVector<int64_t>> ValueOrError =
        ValCounter.readOrError(StringRef());

    if (!ValueOrError)
      return ValueOrError.takeError();

    // Reading a validation counter will only return a single value, so it is
    // safe to only append the first value here. Also assert that this is true.
    assert(ValueOrError->size() == 1 &&
           "Validation counters should only return a single value");
    Result.push_back((*ValueOrError)[0]);
  }
  return Result;
}

int CounterGroup::numValues() const { return 1; }
#else

void CounterGroup::initRealEvent(pid_t ProcessID) {}

void CounterGroup::start() {}

void CounterGroup::stop() {}

Expected<SmallVector<int64_t, 4>>
CounterGroup::readOrError(StringRef /*unused*/) const {
  if (IsDummyEvent) {
    SmallVector<int64_t, 4> Result;
    Result.push_back(42);
    return Result;
  }
  return make_error<StringError>("Not implemented", errc::io_error);
}

Expected<SmallVector<int64_t>>
CounterGroup::readValidationCountersOrError() const {
  return SmallVector<int64_t>(0);
}

int CounterGroup::numValues() const { return 1; }

#endif

} // namespace pfm
} // namespace exegesis
} // namespace llvm
