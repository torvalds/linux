//===-- NativeThreadNetBSD.h ---------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeThreadNetBSD_H_
#define liblldb_NativeThreadNetBSD_H_

#include "lldb/Host/common/NativeThreadProtocol.h"

#include "Plugins/Process/NetBSD/NativeRegisterContextNetBSD.h"

#include <csignal>
#include <map>
#include <string>

namespace lldb_private {
namespace process_netbsd {

class NativeProcessNetBSD;

class NativeThreadNetBSD : public NativeThreadProtocol {
  friend class NativeProcessNetBSD;

public:
  NativeThreadNetBSD(NativeProcessNetBSD &process, lldb::tid_t tid);

  // NativeThreadProtocol Interface
  std::string GetName() override;

  lldb::StateType GetState() override;

  bool GetStopReason(ThreadStopInfo &stop_info,
                     std::string &description) override;

  NativeRegisterContextNetBSD &GetRegisterContext() override;

  Status SetWatchpoint(lldb::addr_t addr, size_t size, uint32_t watch_flags,
                       bool hardware) override;

  Status RemoveWatchpoint(lldb::addr_t addr) override;

  Status SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;

  Status RemoveHardwareBreakpoint(lldb::addr_t addr) override;

private:
  // Interface for friend classes

  Status Resume();
  Status SingleStep();
  Status Suspend();

  void SetStoppedBySignal(uint32_t signo, const siginfo_t *info = nullptr);
  void SetStoppedByBreakpoint();
  void SetStoppedByTrace();
  void SetStoppedByExec();
  void SetStoppedByWatchpoint(uint32_t wp_index);
  void SetStoppedByFork(lldb::pid_t child_pid, lldb::tid_t child_tid);
  void SetStoppedByVFork(lldb::pid_t child_pid, lldb::tid_t child_tid);
  void SetStoppedByVForkDone();
  void SetStoppedWithNoReason();
  void SetStopped();
  void SetRunning();
  void SetStepping();

  llvm::Error CopyWatchpointsFrom(NativeThreadNetBSD& source);

  // Member Variables
  lldb::StateType m_state;
  ThreadStopInfo m_stop_info;
  std::unique_ptr<NativeRegisterContextNetBSD> m_reg_context_up;
  std::string m_stop_description;
  using WatchpointIndexMap = std::map<lldb::addr_t, uint32_t>;
  WatchpointIndexMap m_watchpoint_index_map;
  WatchpointIndexMap m_hw_break_index_map;
};

typedef std::shared_ptr<NativeThreadNetBSD> NativeThreadNetBSDSP;
} // namespace process_netbsd
} // namespace lldb_private

#endif // #ifndef liblldb_NativeThreadNetBSD_H_
