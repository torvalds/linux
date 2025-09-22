//===-- HistoryThread.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_HISTORYTHREAD_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_HISTORYTHREAD_H

#include <mutex>

#include "lldb/Core/UserSettingsController.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/StackFrameList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class HistoryThread HistoryThread.h "HistoryThread.h"
/// A thread object representing a backtrace from a previous point in the
/// process execution
///
/// This subclass of Thread is used to provide a backtrace from earlier in
/// process execution.  It is given a backtrace list of pc addresses and it
/// will create stack frames for them.

class HistoryThread : public lldb_private::Thread {
public:
  HistoryThread(lldb_private::Process &process, lldb::tid_t tid,
                std::vector<lldb::addr_t> pcs,
                bool pcs_are_call_addresses = false);

  ~HistoryThread() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(StackFrame *frame) override;

  void RefreshStateAfterStop() override {}

  bool CalculateStopInfo() override { return false; }

  void SetExtendedBacktraceToken(uint64_t token) override {
    m_extended_unwind_token = token;
  }

  uint64_t GetExtendedBacktraceToken() override {
    return m_extended_unwind_token;
  }

  const char *GetQueueName() override { return m_queue_name.c_str(); }

  void SetQueueName(const char *name) override { m_queue_name = name; }

  lldb::queue_id_t GetQueueID() override { return m_queue_id; }

  void SetQueueID(lldb::queue_id_t queue) override { m_queue_id = queue; }

  const char *GetThreadName() { return m_thread_name.c_str(); }

  uint32_t GetExtendedBacktraceOriginatingIndexID() override;

  void SetThreadName(const char *name) { m_thread_name = name; }

  const char *GetName() override { return m_thread_name.c_str(); }

  void SetName(const char *name) override { m_thread_name = name; }

protected:
  virtual lldb::StackFrameListSP GetStackFrameList();

  mutable std::mutex m_framelist_mutex;
  lldb::StackFrameListSP m_framelist;
  std::vector<lldb::addr_t> m_pcs;

  uint64_t m_extended_unwind_token;
  std::string m_queue_name;
  std::string m_thread_name;
  lldb::tid_t m_originating_unique_thread_id;
  lldb::queue_id_t m_queue_id;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_HISTORYTHREAD_H
