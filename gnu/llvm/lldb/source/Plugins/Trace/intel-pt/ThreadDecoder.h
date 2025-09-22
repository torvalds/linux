//===-- ThreadDecoder.h --======---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_THREAD_DECODER_H
#define LLDB_SOURCE_PLUGINS_TRACE_THREAD_DECODER_H

#include "DecodedThread.h"
#include "forward-declarations.h"
#include "intel-pt.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include <optional>

namespace lldb_private {
namespace trace_intel_pt {

/// Class that handles the decoding of a thread and caches the result.
class ThreadDecoder {
public:
  /// \param[in] thread_sp
  ///     The thread whose intel pt trace buffer will be decoded.
  ///
  /// \param[in] trace
  ///     The main Trace object who owns this decoder and its data.
  ThreadDecoder(const lldb::ThreadSP &thread_sp, TraceIntelPT &trace);

  /// Decode the thread and store the result internally, to avoid
  /// recomputations.
  ///
  /// \return
  ///     A \a DecodedThread instance.
  llvm::Expected<DecodedThreadSP> Decode();

  /// \return
  ///     The lowest TSC value in this trace if available, \a std::nullopt if
  ///     the trace is empty or the trace contains no timing information, or an
  ///     \a llvm::Error if it was not possible to set up the decoder.
  llvm::Expected<std::optional<uint64_t>> FindLowestTSC();

  ThreadDecoder(const ThreadDecoder &other) = delete;
  ThreadDecoder &operator=(const ThreadDecoder &other) = delete;

private:
  llvm::Expected<DecodedThreadSP> DoDecode();

  lldb::ThreadSP m_thread_sp;
  TraceIntelPT &m_trace;
  std::optional<DecodedThreadSP> m_decoded_thread;
};

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_THREAD_DECODER_H
