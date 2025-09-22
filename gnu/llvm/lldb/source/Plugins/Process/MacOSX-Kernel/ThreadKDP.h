//===-- ThreadKDP.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_THREADKDP_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_THREADKDP_H

#include <string>

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"

class ProcessKDP;

class ThreadKDP : public lldb_private::Thread {
public:
  ThreadKDP(lldb_private::Process &process, lldb::tid_t tid);

  ~ThreadKDP() override;

  void RefreshStateAfterStop() override;

  const char *GetName() override;

  const char *GetQueueName() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(lldb_private::StackFrame *frame) override;

  void Dump(lldb_private::Log *log, uint32_t index);

  static bool ThreadIDIsValid(lldb::tid_t thread);

  bool ShouldStop(bool &step_more);

  const char *GetBasicInfoAsString();

  void SetName(const char *name) override {
    if (name && name[0])
      m_thread_name.assign(name);
    else
      m_thread_name.clear();
  }

  lldb::addr_t GetThreadDispatchQAddr() { return m_thread_dispatch_qaddr; }

  void SetThreadDispatchQAddr(lldb::addr_t thread_dispatch_qaddr) {
    m_thread_dispatch_qaddr = thread_dispatch_qaddr;
  }

  void SetStopInfoFrom_KDP_EXCEPTION(
      const lldb_private::DataExtractor &exc_reply_packet);

protected:
  friend class ProcessKDP;

  // Member variables.
  std::string m_thread_name;
  std::string m_dispatch_queue_name;
  lldb::addr_t m_thread_dispatch_qaddr;
  lldb::StopInfoSP m_cached_stop_info_sp;
  // Protected member functions.
  bool CalculateStopInfo() override;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_THREADKDP_H
