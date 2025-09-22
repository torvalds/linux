//===-- TraceIntelPTGDBRemotePackets.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_TRACEINTELPTGDBREMOTEPACKETS_H
#define LLDB_UTILITY_TRACEINTELPTGDBREMOTEPACKETS_H

#include "lldb/Utility/TraceGDBRemotePackets.h"

#include "llvm/Support/JSON.h"

#include <chrono>
#include <optional>

/// See docs/lldb-gdb-remote.txt for more information.
///
/// Do not use system-dependent types, like size_t, because they might cause
/// issues when compiling on arm.
namespace lldb_private {

// List of data kinds used by jLLDBGetState and jLLDBGetBinaryData.
struct IntelPTDataKinds {
  static const char *kProcFsCpuInfo;
  static const char *kIptTrace;
  static const char *kPerfContextSwitchTrace;
};

/// jLLDBTraceStart gdb-remote packet
/// \{
struct TraceIntelPTStartRequest : TraceStartRequest {
  /// Size in bytes to use for each thread's trace buffer.
  uint64_t ipt_trace_size;

  /// Whether to enable TSC
  bool enable_tsc;

  /// PSB packet period
  std::optional<uint64_t> psb_period;

  /// Required when doing "process tracing".
  ///
  /// Limit in bytes on all the thread traces started by this "process trace"
  /// instance. When a thread is about to be traced and the limit would be hit,
  /// then a "tracing" stop event is triggered.
  std::optional<uint64_t> process_buffer_size_limit;

  /// Whether to have a trace buffer per thread or per cpu cpu.
  std::optional<bool> per_cpu_tracing;

  /// Disable the cgroup filtering that is automatically applied in per cpu
  /// mode.
  std::optional<bool> disable_cgroup_filtering;

  bool IsPerCpuTracing() const;
};

bool fromJSON(const llvm::json::Value &value, TraceIntelPTStartRequest &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceIntelPTStartRequest &packet);
/// \}

/// Helper structure to help parse long numbers that can't
/// be easily represented by a JSON number that is compatible with
/// Javascript (52 bits) or that can also be represented as hex.
///
/// \{
struct JSONUINT64 {
  uint64_t value;
};

llvm::json::Value toJSON(const JSONUINT64 &uint64, bool hex);

bool fromJSON(const llvm::json::Value &value, JSONUINT64 &uint64,
              llvm::json::Path path);
/// \}

/// jLLDBTraceGetState gdb-remote packet
/// \{

/// TSC to wall time conversion values defined in the Linux perf_event_open API
/// when the capibilities cap_user_time and cap_user_time_zero are set. See the
/// See the documentation of `time_zero` in
/// https://man7.org/linux/man-pages/man2/perf_event_open.2.html for more
/// information.
struct LinuxPerfZeroTscConversion {
  /// Convert TSC value to nanosecond wall time. The beginning of time (0
  /// nanoseconds) is defined by the kernel at boot time and has no particularly
  /// useful meaning. On the other hand, this value is constant for an entire
  /// trace session.
  /// See 'time_zero' section of
  /// https://man7.org/linux/man-pages/man2/perf_event_open.2.html
  ///
  /// \param[in] tsc
  ///   The TSC value to be converted.
  ///
  /// \return
  ///   Nanosecond wall time.
  uint64_t ToNanos(uint64_t tsc) const;

  uint64_t ToTSC(uint64_t nanos) const;

  uint32_t time_mult;
  uint16_t time_shift;
  JSONUINT64 time_zero;
};

struct TraceIntelPTGetStateResponse : TraceGetStateResponse {
  /// The TSC to wall time conversion if it exists, otherwise \b nullptr.
  std::optional<LinuxPerfZeroTscConversion> tsc_perf_zero_conversion;
  bool using_cgroup_filtering = false;
};

bool fromJSON(const llvm::json::Value &value,
              LinuxPerfZeroTscConversion &packet, llvm::json::Path path);

llvm::json::Value toJSON(const LinuxPerfZeroTscConversion &packet);

bool fromJSON(const llvm::json::Value &value,
              TraceIntelPTGetStateResponse &packet, llvm::json::Path path);

llvm::json::Value toJSON(const TraceIntelPTGetStateResponse &packet);
/// \}

} // namespace lldb_private

#endif // LLDB_UTILITY_TRACEINTELPTGDBREMOTEPACKETS_H
