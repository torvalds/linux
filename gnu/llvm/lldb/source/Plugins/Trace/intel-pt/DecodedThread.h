//===-- DecodedThread.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_DECODEDTHREAD_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_DECODEDTHREAD_H

#include "intel-pt.h"
#include "lldb/Target/Trace.h"
#include "lldb/Utility/TraceIntelPTGDBRemotePackets.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <deque>
#include <optional>
#include <utility>
#include <variant>

namespace lldb_private {
namespace trace_intel_pt {

/// Class for representing a libipt decoding error.
class IntelPTError : public llvm::ErrorInfo<IntelPTError> {
public:
  static char ID;

  /// \param[in] libipt_error_code
  ///     Negative number returned by libipt when decoding the trace and
  ///     signaling errors.
  ///
  /// \param[in] address
  ///     Optional instruction address. When decoding an individual instruction,
  ///     its address might be available in the \a pt_insn object, and should be
  ///     passed to this constructor. Other errors don't have an associated
  ///     address.
  IntelPTError(int libipt_error_code,
               lldb::addr_t address = LLDB_INVALID_ADDRESS);

  std::error_code convertToErrorCode() const override {
    return llvm::errc::not_supported;
  }

  int GetLibiptErrorCode() const { return m_libipt_error_code; }

  void log(llvm::raw_ostream &OS) const override;

private:
  int m_libipt_error_code;
  lldb::addr_t m_address;
};

/// \class DecodedThread
/// Class holding the instructions and function call hierarchy obtained from
/// decoding a trace, as well as a position cursor used when reverse debugging
/// the trace.
///
/// Each decoded thread contains a cursor to the current position the user is
/// stopped at. See \a Trace::GetCursorPosition for more information.
class DecodedThread : public std::enable_shared_from_this<DecodedThread> {
public:
  using TSC = uint64_t;

  /// A structure that represents a maximal range of trace items associated to
  /// the same TSC value.
  struct TSCRange {
    TSC tsc;
    /// Number of trace items in this range.
    uint64_t items_count;
    /// Index of the first trace item in this range.
    uint64_t first_item_index;

    /// \return
    ///   \b true if and only if the given \p item_index is covered by this
    ///   range.
    bool InRange(uint64_t item_index) const;
  };

  /// A structure that represents a maximal range of trace items associated to
  /// the same non-interpolated timestamps in nanoseconds.
  struct NanosecondsRange {
    /// The nanoseconds value for this range.
    uint64_t nanos;
    /// The corresponding TSC value for this range.
    TSC tsc;
    /// A nullable pointer to the next range.
    NanosecondsRange *next_range;
    /// Number of trace items in this range.
    uint64_t items_count;
    /// Index of the first trace item in this range.
    uint64_t first_item_index;

    /// Calculate an interpolated timestamp in nanoseconds for the given item
    /// index. It's guaranteed that two different item indices will produce
    /// different interpolated values.
    ///
    /// \param[in] item_index
    ///   The index of the item whose timestamp will be estimated. It has to be
    ///   part of this range.
    ///
    /// \param[in] beginning_of_time_nanos
    ///   The timestamp at which tracing started.
    ///
    /// \param[in] tsc_conversion
    ///   The tsc -> nanos conversion utility
    ///
    /// \return
    ///   An interpolated timestamp value for the given trace item.
    double
    GetInterpolatedTime(uint64_t item_index, uint64_t beginning_of_time_nanos,
                        const LinuxPerfZeroTscConversion &tsc_conversion) const;

    /// \return
    ///   \b true if and only if the given \p item_index is covered by this
    ///   range.
    bool InRange(uint64_t item_index) const;
  };

  // Struct holding counts for events
  struct EventsStats {
    /// A count for each individual event kind. We use an unordered map instead
    /// of a DenseMap because DenseMap can't understand enums.
    ///
    /// Note: We can't use DenseMap because lldb::TraceEvent is not
    /// automatically handled correctly by DenseMap. We'd need to implement a
    /// custom DenseMapInfo struct for TraceEvent and that's a bit too much for
    /// such a simple structure.
    std::unordered_map<lldb::TraceEvent, uint64_t> events_counts;
    uint64_t total_count = 0;

    void RecordEvent(lldb::TraceEvent event);
  };

  // Struct holding counts for errors
  struct ErrorStats {
    /// The following counters are mutually exclusive
    /// \{
    uint64_t other_errors = 0;
    uint64_t fatal_errors = 0;
    // libipt error -> count
    llvm::DenseMap<const char *, uint64_t> libipt_errors;
    /// \}

    uint64_t GetTotalCount() const;

    void RecordError(int libipt_error_code);

    void RecordError(bool fatal);
  };

  DecodedThread(
      lldb::ThreadSP thread_sp,
      const std::optional<LinuxPerfZeroTscConversion> &tsc_conversion);

  /// Get the total number of instruction, errors and events from the decoded
  /// trace.
  uint64_t GetItemsCount() const;

  /// \return
  ///   The error associated with a given trace item.
  llvm::StringRef GetErrorByIndex(uint64_t item_index) const;

  /// \return
  ///   The trace item kind given an item index.
  lldb::TraceItemKind GetItemKindByIndex(uint64_t item_index) const;

  /// \return
  ///   The underlying event type for the given trace item index.
  lldb::TraceEvent GetEventByIndex(int item_index) const;

  /// Get the most recent CPU id before or at the given trace item index.
  ///
  /// \param[in] item_index
  ///   The trace item index to compare with.
  ///
  /// \return
  ///   The requested cpu id, or \a LLDB_INVALID_CPU_ID if not available.
  lldb::cpu_id_t GetCPUByIndex(uint64_t item_index) const;

  /// \return
  ///   The PSB offset associated with the given item index.
  lldb::addr_t GetSyncPointOffsetByIndex(uint64_t item_index) const;

  /// Get a maximal range of trace items that include the given \p item_index
  /// that have the same TSC value.
  ///
  /// \param[in] item_index
  ///   The trace item index to compare with.
  ///
  /// \return
  ///   The requested TSC range, or \a std::nullopt if not available.
  std::optional<DecodedThread::TSCRange>
  GetTSCRangeByIndex(uint64_t item_index) const;

  /// Get a maximal range of trace items that include the given \p item_index
  /// that have the same nanoseconds timestamp without interpolation.
  ///
  /// \param[in] item_index
  ///   The trace item index to compare with.
  ///
  /// \return
  ///   The requested nanoseconds range, or \a std::nullopt if not available.
  std::optional<DecodedThread::NanosecondsRange>
  GetNanosecondsRangeByIndex(uint64_t item_index);

  /// \return
  ///     The load address of the instruction at the given index.
  lldb::addr_t GetInstructionLoadAddress(uint64_t item_index) const;

  /// \return
  ///     The number of instructions in this trace (not trace items).
  uint64_t GetTotalInstructionCount() const;

  /// Return an object with statistics of the trace events that happened.
  ///
  /// \return
  ///   The stats object of all the events.
  const EventsStats &GetEventsStats() const;

  /// Return an object with statistics of the trace errors that happened.
  ///
  /// \return
  ///   The stats object of all the events.
  const ErrorStats &GetErrorStats() const;

  /// The approximate size in bytes used by this instance,
  /// including all the already decoded instructions.
  size_t CalculateApproximateMemoryUsage() const;

  lldb::ThreadSP GetThread();

  /// Notify this object that a new tsc has been seen.
  /// If this a new TSC, an event will be created.
  void NotifyTsc(TSC tsc);

  /// Notify this object that a CPU has been seen.
  /// If this a new CPU, an event will be created.
  void NotifyCPU(lldb::cpu_id_t cpu_id);

  /// Notify this object that a new PSB has been seen.
  void NotifySyncPoint(lldb::addr_t psb_offset);

  /// Append a decoding error.
  void AppendError(const IntelPTError &error);

  /// Append a custom decoding.
  ///
  /// \param[in] error
  ///   The error message.
  ///
  /// \param[in] fatal
  ///   If \b true, then the whole decoded thread should be discarded because a
  ///   fatal anomaly has been found.
  void AppendCustomError(llvm::StringRef error, bool fatal = false);

  /// Append an event.
  void AppendEvent(lldb::TraceEvent);

  /// Append an instruction.
  void AppendInstruction(const pt_insn &insn);

private:
  /// When adding new members to this class, make sure
  /// to update \a CalculateApproximateMemoryUsage() accordingly.
  lldb::ThreadSP m_thread_sp;

  using TraceItemStorage =
      std::variant<std::string, lldb::TraceEvent, lldb::addr_t>;

  /// Create a new trace item.
  ///
  /// \return
  ///   The index of the new item.
  template <typename Data>
  DecodedThread::TraceItemStorage &CreateNewTraceItem(lldb::TraceItemKind kind,
                                                      Data &&data);

  /// Most of the trace data is stored here.
  std::deque<TraceItemStorage> m_item_data;

  /// This map contains the TSCs of the decoded trace items. It maps
  /// `item index -> TSC`, where `item index` is the first index
  /// at which the mapped TSC first appears. We use this representation because
  /// TSCs are sporadic and we can think of them as ranges.
  std::map<uint64_t, TSCRange> m_tscs;
  /// This is the chronologically last TSC that has been added.
  std::optional<std::map<uint64_t, TSCRange>::iterator> m_last_tsc =
      std::nullopt;
  /// This map contains the non-interpolated nanoseconds timestamps of the
  /// decoded trace items. It maps `item index -> nanoseconds`, where `item
  /// index` is the first index at which the mapped nanoseconds first appears.
  /// We use this representation because timestamps are sporadic and we think of
  /// them as ranges.
  std::map<uint64_t, NanosecondsRange> m_nanoseconds;
  std::optional<std::map<uint64_t, NanosecondsRange>::iterator>
      m_last_nanoseconds = std::nullopt;

  // The cpu information is stored as a map. It maps `item index -> CPU`.
  // A CPU is associated with the next instructions that follow until the next
  // cpu is seen.
  std::map<uint64_t, lldb::cpu_id_t> m_cpus;
  /// This is the chronologically last CPU ID.
  std::optional<uint64_t> m_last_cpu;

  // The PSB offsets are stored as a map. It maps `item index -> psb offset`.
  llvm::DenseMap<uint64_t, lldb::addr_t> m_psb_offsets;

  /// TSC -> nanos conversion utility.
  std::optional<LinuxPerfZeroTscConversion> m_tsc_conversion;

  /// Statistics of all tracing errors.
  ErrorStats m_error_stats;

  /// Statistics of all tracing events.
  EventsStats m_events_stats;
  /// Total amount of time spent decoding.
  std::chrono::milliseconds m_total_decoding_time{0};

  /// Total number of instructions in the trace.
  uint64_t m_insn_count = 0;
};

using DecodedThreadSP = std::shared_ptr<DecodedThread>;

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_DECODEDTHREAD_H
