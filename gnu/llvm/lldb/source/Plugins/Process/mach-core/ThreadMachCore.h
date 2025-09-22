//===-- ThreadMachCore.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MACH_CORE_THREADMACHCORE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MACH_CORE_THREADMACHCORE_H

#include <string>

#include "lldb/Target/Thread.h"

class ProcessMachCore;

class ThreadMachCore : public lldb_private::Thread {
public:
  ThreadMachCore(lldb_private::Process &process, lldb::tid_t tid,
                 uint32_t objfile_lc_thread_idx);

  ~ThreadMachCore() override;

  void RefreshStateAfterStop() override;

  const char *GetName() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(lldb_private::StackFrame *frame) override;

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

protected:
  friend class ProcessMachCore;

  // Member variables.
  std::string m_thread_name;
  std::string m_dispatch_queue_name;
  lldb::addr_t m_thread_dispatch_qaddr;
  lldb::RegisterContextSP m_thread_reg_ctx_sp;
  uint32_t m_objfile_lc_thread_idx;

  // Protected member functions.
  bool CalculateStopInfo() override;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MACH_CORE_THREADMACHCORE_H
