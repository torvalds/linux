//===-- IntelPTThreadTraceCollection.h ------------------------ -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_IntelPTPerThreadTraceCollection_H_
#define liblldb_IntelPTPerThreadTraceCollection_H_

#include "IntelPTSingleBufferTrace.h"
#include <optional>

namespace lldb_private {
namespace process_linux {

/// Manages a list of thread traces.
class IntelPTThreadTraceCollection {
public:
  IntelPTThreadTraceCollection() {}

  /// Dispose of all traces
  void Clear();

  /// \return
  ///   \b true if and only if this instance of tracing the provided \p tid.
  bool TracesThread(lldb::tid_t tid) const;

  /// \return
  ///   The total sum of the intel pt trace buffer sizes used by this
  ///   collection.
  size_t GetTotalBufferSize() const;

  /// Execute the provided callback on each thread that is being traced.
  ///
  /// \param[in] callback.tid
  ///   The id of the thread that is being traced.
  ///
  /// \param[in] callback.core_trace
  ///   The single-buffer trace instance for the given core.
  void ForEachThread(std::function<void(lldb::tid_t tid,
                                        IntelPTSingleBufferTrace &thread_trace)>
                         callback);

  llvm::Expected<IntelPTSingleBufferTrace &> GetTracedThread(lldb::tid_t tid);

  /// Start tracing the thread given by its \p tid.
  ///
  /// \return
  ///   An error if the operation failed.
  llvm::Error TraceStart(lldb::tid_t tid,
                         const TraceIntelPTStartRequest &request);

  /// Stop tracing the thread given by its \p tid.
  ///
  /// \return
  ///   An error if the given thread is not being traced or tracing couldn't be
  ///   stopped.
  llvm::Error TraceStop(lldb::tid_t tid);

  size_t GetTracedThreadsCount() const;

  /// \copydoc IntelPTProcessTrace::TryGetBinaryData()
  llvm::Expected<std::optional<std::vector<uint8_t>>>
  TryGetBinaryData(const TraceGetBinaryDataRequest &request);

private:
  llvm::DenseMap<lldb::tid_t, IntelPTSingleBufferTrace> m_thread_traces;
  /// Total actual thread buffer size in bytes
  size_t m_total_buffer_size = 0;
};

} // namespace process_linux
} // namespace lldb_private

#endif // liblldb_IntelPTPerThreadTraceCollection_H_
