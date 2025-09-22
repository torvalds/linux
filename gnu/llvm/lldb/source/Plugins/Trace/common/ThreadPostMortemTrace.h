//===-- ThreadPostMortemTrace.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPOSTMORTEMTRACE_H
#define LLDB_TARGET_THREADPOSTMORTEMTRACE_H

#include "lldb/Target/Thread.h"
#include <optional>

namespace lldb_private {

/// \class ThreadPostMortemTrace ThreadPostMortemTrace.h
///
/// Thread implementation used for representing threads gotten from trace
/// session files, which are similar to threads from core files.
///
class ThreadPostMortemTrace : public Thread {
public:
  /// \param[in] process
  ///     The process who owns this thread.
  ///
  /// \param[in] tid
  ///     The tid of this thread.
  ///
  /// \param[in] trace_file
  ///     The file that contains the list of instructions that were traced when
  ///     this thread was being executed.
  ThreadPostMortemTrace(Process &process, lldb::tid_t tid,
                        const std::optional<FileSpec> &trace_file)
      : Thread(process, tid), m_trace_file(trace_file) {}

  void RefreshStateAfterStop() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(StackFrame *frame) override;

  /// \return
  ///   The trace file of this thread.
  const std::optional<FileSpec> &GetTraceFile() const;

protected:
  bool CalculateStopInfo() override;

  lldb::RegisterContextSP m_thread_reg_ctx_sp;

private:
  std::optional<FileSpec> m_trace_file;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPOSTMORTEMTRACE_H
