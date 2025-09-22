//===-- PerfContextSwitchDecoder.cpp --======------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PerfContextSwitchDecoder.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

/// Copied from <linux/perf_event.h> to avoid depending on perf_event.h on
/// non-linux platforms.
/// \{
#define PERF_RECORD_MISC_SWITCH_OUT (1 << 13)

#define PERF_RECORD_LOST 2
#define PERF_RECORD_THROTTLE 5
#define PERF_RECORD_UNTHROTTLE 6
#define PERF_RECORD_LOST_SAMPLES 13
#define PERF_RECORD_SWITCH_CPU_WIDE 15
#define PERF_RECORD_MAX 19

struct perf_event_header {
  uint32_t type;
  uint16_t misc;
  uint16_t size;

  /// \return
  ///   An \a llvm::Error if the record looks obviously wrong, or \a
  ///   llvm::Error::success() otherwise.
  Error SanityCheck() const {
    // The following checks are based on visual inspection of the records and
    // enums in
    // https://elixir.bootlin.com/linux/v4.8/source/include/uapi/linux/perf_event.h
    // See PERF_RECORD_MAX, PERF_RECORD_SWITCH and the data similar records
    // hold.

    // A record of too many uint64_t's or more should mean that the data is
    // wrong
    const uint64_t max_valid_size_bytes = 8000;
    if (size == 0 || size > max_valid_size_bytes)
      return createStringError(
          inconvertibleErrorCode(),
          formatv("A record of {0} bytes was found.", size));

    // We add some numbers to PERF_RECORD_MAX because some systems might have
    // custom records. In any case, we are looking only for abnormal data.
    if (type >= PERF_RECORD_MAX + 100)
      return createStringError(
          inconvertibleErrorCode(),
          formatv("Invalid record type {0} was found.", type));
    return Error::success();
  }

  bool IsContextSwitchRecord() const {
    return type == PERF_RECORD_SWITCH_CPU_WIDE;
  }

  bool IsErrorRecord() const {
    return type == PERF_RECORD_LOST || type == PERF_RECORD_THROTTLE ||
           type == PERF_RECORD_UNTHROTTLE || type == PERF_RECORD_LOST_SAMPLES;
  }
};
/// \}

/// Record found in the perf_event context switch traces. It might contain
/// additional fields in memory, but header.size should have the actual size
/// of the record.
struct PerfContextSwitchRecord {
  struct perf_event_header header;
  uint32_t next_prev_pid;
  uint32_t next_prev_tid;
  uint32_t pid, tid;
  uint64_t time_in_nanos;

  bool IsOut() const { return header.misc & PERF_RECORD_MISC_SWITCH_OUT; }
};

/// Record produced after parsing the raw context switch trace produce by
/// perf_event. A major difference between this struct and
/// PerfContextSwitchRecord is that this one uses tsc instead of nanos.
struct ContextSwitchRecord {
  uint64_t tsc;
  /// Whether the switch is in or out
  bool is_out;
  /// pid = 0 and tid = 0 indicate the swapper or idle process, which normally
  /// runs after a context switch out of a normal user thread.
  lldb::pid_t pid;
  lldb::tid_t tid;

  bool IsOut() const { return is_out; }

  bool IsIn() const { return !is_out; }
};

uint64_t ThreadContinuousExecution::GetLowestKnownTSC() const {
  switch (variant) {
  case Variant::Complete:
    return tscs.complete.start;
  case Variant::OnlyStart:
    return tscs.only_start.start;
  case Variant::OnlyEnd:
    return tscs.only_end.end;
  case Variant::HintedEnd:
    return tscs.hinted_end.start;
  case Variant::HintedStart:
    return tscs.hinted_start.end;
  }
}

uint64_t ThreadContinuousExecution::GetStartTSC() const {
  switch (variant) {
  case Variant::Complete:
    return tscs.complete.start;
  case Variant::OnlyStart:
    return tscs.only_start.start;
  case Variant::OnlyEnd:
    return 0;
  case Variant::HintedEnd:
    return tscs.hinted_end.start;
  case Variant::HintedStart:
    return tscs.hinted_start.hinted_start;
  }
}

uint64_t ThreadContinuousExecution::GetEndTSC() const {
  switch (variant) {
  case Variant::Complete:
    return tscs.complete.end;
  case Variant::OnlyStart:
    return std::numeric_limits<uint64_t>::max();
  case Variant::OnlyEnd:
    return tscs.only_end.end;
  case Variant::HintedEnd:
    return tscs.hinted_end.hinted_end;
  case Variant::HintedStart:
    return tscs.hinted_start.end;
  }
}

ThreadContinuousExecution ThreadContinuousExecution::CreateCompleteExecution(
    lldb::cpu_id_t cpu_id, lldb::tid_t tid, lldb::pid_t pid, uint64_t start,
    uint64_t end) {
  ThreadContinuousExecution o(cpu_id, tid, pid);
  o.variant = Variant::Complete;
  o.tscs.complete.start = start;
  o.tscs.complete.end = end;
  return o;
}

ThreadContinuousExecution ThreadContinuousExecution::CreateHintedStartExecution(
    lldb::cpu_id_t cpu_id, lldb::tid_t tid, lldb::pid_t pid,
    uint64_t hinted_start, uint64_t end) {
  ThreadContinuousExecution o(cpu_id, tid, pid);
  o.variant = Variant::HintedStart;
  o.tscs.hinted_start.hinted_start = hinted_start;
  o.tscs.hinted_start.end = end;
  return o;
}

ThreadContinuousExecution ThreadContinuousExecution::CreateHintedEndExecution(
    lldb::cpu_id_t cpu_id, lldb::tid_t tid, lldb::pid_t pid, uint64_t start,
    uint64_t hinted_end) {
  ThreadContinuousExecution o(cpu_id, tid, pid);
  o.variant = Variant::HintedEnd;
  o.tscs.hinted_end.start = start;
  o.tscs.hinted_end.hinted_end = hinted_end;
  return o;
}

ThreadContinuousExecution ThreadContinuousExecution::CreateOnlyEndExecution(
    lldb::cpu_id_t cpu_id, lldb::tid_t tid, lldb::pid_t pid, uint64_t end) {
  ThreadContinuousExecution o(cpu_id, tid, pid);
  o.variant = Variant::OnlyEnd;
  o.tscs.only_end.end = end;
  return o;
}

ThreadContinuousExecution ThreadContinuousExecution::CreateOnlyStartExecution(
    lldb::cpu_id_t cpu_id, lldb::tid_t tid, lldb::pid_t pid, uint64_t start) {
  ThreadContinuousExecution o(cpu_id, tid, pid);
  o.variant = Variant::OnlyStart;
  o.tscs.only_start.start = start;
  return o;
}

static Error RecoverExecutionsFromConsecutiveRecords(
    cpu_id_t cpu_id, const LinuxPerfZeroTscConversion &tsc_conversion,
    const ContextSwitchRecord &current_record,
    const std::optional<ContextSwitchRecord> &prev_record,
    std::function<void(const ThreadContinuousExecution &execution)>
        on_new_execution) {
  if (!prev_record) {
    if (current_record.IsOut()) {
      on_new_execution(ThreadContinuousExecution::CreateOnlyEndExecution(
          cpu_id, current_record.tid, current_record.pid, current_record.tsc));
    }
    // The 'in' case will be handled later when we try to look for its end
    return Error::success();
  }

  const ContextSwitchRecord &prev = *prev_record;
  if (prev.tsc >= current_record.tsc)
    return createStringError(
        inconvertibleErrorCode(),
        formatv("A context switch record doesn't happen after the previous "
                "record. Previous TSC= {0}, current TSC = {1}.",
                prev.tsc, current_record.tsc));

  if (current_record.IsIn() && prev.IsIn()) {
    // We found two consecutive ins, which means that we didn't capture
    // the end of the previous execution.
    on_new_execution(ThreadContinuousExecution::CreateHintedEndExecution(
        cpu_id, prev.tid, prev.pid, prev.tsc, current_record.tsc - 1));
  } else if (current_record.IsOut() && prev.IsOut()) {
    // We found two consecutive outs, that means that we didn't capture
    // the beginning of the current execution.
    on_new_execution(ThreadContinuousExecution::CreateHintedStartExecution(
        cpu_id, current_record.tid, current_record.pid, prev.tsc + 1,
        current_record.tsc));
  } else if (current_record.IsOut() && prev.IsIn()) {
    if (current_record.pid == prev.pid && current_record.tid == prev.tid) {
      /// A complete execution
      on_new_execution(ThreadContinuousExecution::CreateCompleteExecution(
          cpu_id, current_record.tid, current_record.pid, prev.tsc,
          current_record.tsc));
    } else {
      // An out after the in of a different thread. The first one doesn't
      // have an end, and the second one doesn't have a start.
      on_new_execution(ThreadContinuousExecution::CreateHintedEndExecution(
          cpu_id, prev.tid, prev.pid, prev.tsc, current_record.tsc - 1));
      on_new_execution(ThreadContinuousExecution::CreateHintedStartExecution(
          cpu_id, current_record.tid, current_record.pid, prev.tsc + 1,
          current_record.tsc));
    }
  }
  return Error::success();
}

Expected<std::vector<ThreadContinuousExecution>>
lldb_private::trace_intel_pt::DecodePerfContextSwitchTrace(
    ArrayRef<uint8_t> data, cpu_id_t cpu_id,
    const LinuxPerfZeroTscConversion &tsc_conversion) {

  std::vector<ThreadContinuousExecution> executions;

  // This offset is used to create the error message in case of failures.
  size_t offset = 0;

  auto do_decode = [&]() -> Error {
    std::optional<ContextSwitchRecord> prev_record;
    while (offset < data.size()) {
      const perf_event_header &perf_record =
          *reinterpret_cast<const perf_event_header *>(data.data() + offset);
      if (Error err = perf_record.SanityCheck())
        return err;

      if (perf_record.IsContextSwitchRecord()) {
        const PerfContextSwitchRecord &context_switch_record =
            *reinterpret_cast<const PerfContextSwitchRecord *>(data.data() +
                                                               offset);
        ContextSwitchRecord record{
            tsc_conversion.ToTSC(context_switch_record.time_in_nanos),
            context_switch_record.IsOut(),
            static_cast<lldb::pid_t>(context_switch_record.pid),
            static_cast<lldb::tid_t>(context_switch_record.tid)};

        if (Error err = RecoverExecutionsFromConsecutiveRecords(
                cpu_id, tsc_conversion, record, prev_record,
                [&](const ThreadContinuousExecution &execution) {
                  executions.push_back(execution);
                }))
          return err;

        prev_record = record;
      }
      offset += perf_record.size;
    }

    // We might have an incomplete last record
    if (prev_record && prev_record->IsIn())
      executions.push_back(ThreadContinuousExecution::CreateOnlyStartExecution(
          cpu_id, prev_record->tid, prev_record->pid, prev_record->tsc));
    return Error::success();
  };

  if (Error err = do_decode())
    return createStringError(inconvertibleErrorCode(),
                             formatv("Malformed perf context switch trace for "
                                     "cpu {0} at offset {1}. {2}",
                                     cpu_id, offset, toString(std::move(err))));

  return executions;
}

Expected<std::vector<uint8_t>>
lldb_private::trace_intel_pt::FilterProcessesFromContextSwitchTrace(
    llvm::ArrayRef<uint8_t> data, const std::set<lldb::pid_t> &pids) {
  size_t offset = 0;
  std::vector<uint8_t> out_data;

  while (offset < data.size()) {
    const perf_event_header &perf_record =
        *reinterpret_cast<const perf_event_header *>(data.data() + offset);
    if (Error err = perf_record.SanityCheck())
      return std::move(err);
    bool should_copy = false;
    if (perf_record.IsContextSwitchRecord()) {
      const PerfContextSwitchRecord &context_switch_record =
          *reinterpret_cast<const PerfContextSwitchRecord *>(data.data() +
                                                             offset);
      if (pids.count(context_switch_record.pid))
        should_copy = true;
    } else if (perf_record.IsErrorRecord()) {
      should_copy = true;
    }

    if (should_copy) {
      for (size_t i = 0; i < perf_record.size; i++) {
        out_data.push_back(data[offset + i]);
      }
    }

    offset += perf_record.size;
  }
  return out_data;
}
