//===-- TraceGDBRemotePackets.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_TRACEGDBREMOTEPACKETS_H
#define LLDB_UTILITY_TRACEGDBREMOTEPACKETS_H

#include "llvm/Support/JSON.h"

#include <chrono>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"

/// See docs/lldb-gdb-remote.txt for more information.
namespace lldb_private {

/// jLLDBTraceSupported gdb-remote packet
/// \{
struct TraceSupportedResponse {
  /// The name of the technology, e.g. intel-pt or arm-coresight.
  ///
  /// In order for a Trace plug-in (see \a lldb_private::Trace.h) to support the
  /// trace technology given by this struct, it should match its name with this
  /// field.
  std::string name;
  /// The description for the technology.
  std::string description;
};

bool fromJSON(const llvm::json::Value &value, TraceSupportedResponse &info,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceSupportedResponse &packet);
/// \}

/// jLLDBTraceStart gdb-remote packet
/// \{
struct TraceStartRequest {
  /// Tracing technology name, e.g. intel-pt, arm-coresight.
  std::string type;

  /// If \a std::nullopt, then this starts tracing the whole process. Otherwise,
  /// only tracing for the specified threads is enabled.
  std::optional<std::vector<lldb::tid_t>> tids;

  /// \return
  ///     \b true if \a tids is \a std::nullopt, i.e. whole process tracing.
  bool IsProcessTracing() const;
};

bool fromJSON(const llvm::json::Value &value, TraceStartRequest &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceStartRequest &packet);
/// \}

/// jLLDBTraceStop gdb-remote packet
/// \{
struct TraceStopRequest {
  TraceStopRequest() = default;

  TraceStopRequest(llvm::StringRef type, const std::vector<lldb::tid_t> &tids);

  TraceStopRequest(llvm::StringRef type) : type(type){};

  bool IsProcessTracing() const;

  /// Tracing technology name, e.g. intel-pt, arm-coresight.
  std::string type;
  /// If \a std::nullopt, then this stops tracing the whole process. Otherwise,
  /// only tracing for the specified threads is stopped.
  std::optional<std::vector<lldb::tid_t>> tids;
};

bool fromJSON(const llvm::json::Value &value, TraceStopRequest &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceStopRequest &packet);
///}

/// jLLDBTraceGetState gdb-remote packet
/// \{
struct TraceGetStateRequest {
  /// Tracing technology name, e.g. intel-pt, arm-coresight.
  std::string type;
};

bool fromJSON(const llvm::json::Value &value, TraceGetStateRequest &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceGetStateRequest &packet);

struct TraceBinaryData {
  /// Identifier of data to fetch with jLLDBTraceGetBinaryData.
  std::string kind;
  /// Size in bytes for this data.
  uint64_t size;
};

bool fromJSON(const llvm::json::Value &value, TraceBinaryData &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceBinaryData &packet);

struct TraceThreadState {
  lldb::tid_t tid;
  /// List of binary data objects for this thread.
  std::vector<TraceBinaryData> binary_data;
};

bool fromJSON(const llvm::json::Value &value, TraceThreadState &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceThreadState &packet);

struct TraceCpuState {
  lldb::cpu_id_t id;
  /// List of binary data objects for this core.
  std::vector<TraceBinaryData> binary_data;
};

bool fromJSON(const llvm::json::Value &value, TraceCpuState &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceCpuState &packet);

struct TraceGetStateResponse {
  std::vector<TraceThreadState> traced_threads;
  std::vector<TraceBinaryData> process_binary_data;
  std::optional<std::vector<TraceCpuState>> cpus;
  std::optional<std::vector<std::string>> warnings;

  void AddWarning(llvm::StringRef warning);
};

bool fromJSON(const llvm::json::Value &value, TraceGetStateResponse &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const TraceGetStateResponse &packet);
/// \}

/// jLLDBTraceGetBinaryData gdb-remote packet
/// \{
struct TraceGetBinaryDataRequest {
  /// Tracing technology name, e.g. intel-pt, arm-coresight.
  std::string type;
  /// Identifier for the data.
  std::string kind;
  /// Optional tid if the data is related to a thread.
  std::optional<lldb::tid_t> tid;
  /// Optional core id if the data is related to a cpu core.
  std::optional<lldb::cpu_id_t> cpu_id;
};

bool fromJSON(const llvm::json::Value &value,
              lldb_private::TraceGetBinaryDataRequest &packet,
              llvm::json::Path path);

llvm::json::Value toJSON(const lldb_private::TraceGetBinaryDataRequest &packet);
/// \}

} // namespace lldb_private

#endif // LLDB_UTILITY_TRACEGDBREMOTEPACKETS_H
