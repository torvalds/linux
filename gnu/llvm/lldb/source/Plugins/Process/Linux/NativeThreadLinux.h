//===-- NativeThreadLinux.h ----------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeThreadLinux_H_
#define liblldb_NativeThreadLinux_H_

#include "Plugins/Process/Linux/NativeRegisterContextLinux.h"
#include "Plugins/Process/Linux/SingleStepCheck.h"
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/lldb-private-forward.h"

#include "llvm/ADT/StringRef.h"

#include <csignal>
#include <map>
#include <memory>
#include <string>

namespace lldb_private {
namespace process_linux {

class NativeProcessLinux;

class NativeThreadLinux : public NativeThreadProtocol {
  friend class NativeProcessLinux;

public:
  NativeThreadLinux(NativeProcessLinux &process, lldb::tid_t tid);

  // NativeThreadProtocol Interface
  std::string GetName() override;

  lldb::StateType GetState() override;

  bool GetStopReason(ThreadStopInfo &stop_info,
                     std::string &description) override;

  NativeRegisterContextLinux &GetRegisterContext() override {
    return *m_reg_context_up;
  }

  Status SetWatchpoint(lldb::addr_t addr, size_t size, uint32_t watch_flags,
                       bool hardware) override;

  Status RemoveWatchpoint(lldb::addr_t addr) override;

  Status SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;

  Status RemoveHardwareBreakpoint(lldb::addr_t addr) override;

  NativeProcessLinux &GetProcess();

  const NativeProcessLinux &GetProcess() const;

  llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
  GetSiginfo() const override;

private:
  // Interface for friend classes

  /// Resumes the thread.  If \p signo is anything but
  /// LLDB_INVALID_SIGNAL_NUMBER, deliver that signal to the thread.
  Status Resume(uint32_t signo);

  /// Single steps the thread.  If \p signo is anything but
  /// LLDB_INVALID_SIGNAL_NUMBER, deliver that signal to the thread.
  Status SingleStep(uint32_t signo);

  void SetStoppedBySignal(uint32_t signo, const siginfo_t *info = nullptr);

  /// Return true if the thread is stopped.
  /// If stopped by a signal, indicate the signo in the signo argument.
  /// Otherwise, return LLDB_INVALID_SIGNAL_NUMBER.
  bool IsStopped(int *signo);

  void SetStoppedByExec();

  void SetStoppedByBreakpoint();

  void SetStoppedByWatchpoint(uint32_t wp_index);

  bool IsStoppedAtBreakpoint();

  bool IsStoppedAtWatchpoint();

  void SetStoppedByTrace();

  void SetStoppedByFork(bool is_vfork, lldb::pid_t child_pid);

  void SetStoppedByVForkDone();

  void SetStoppedWithNoReason();

  void SetStoppedByProcessorTrace(llvm::StringRef description);

  void SetExited();

  Status RequestStop();

  // Private interface
  void MaybeLogStateChange(lldb::StateType new_state);

  void SetStopped();

  /// Extend m_stop_description with logical and allocation tag values.
  /// If there is an error along the way just add the information we were able
  /// to get.
  void AnnotateSyncTagCheckFault(lldb::addr_t fault_addr);

  // Member Variables
  lldb::StateType m_state;
  ThreadStopInfo m_stop_info;
  std::unique_ptr<NativeRegisterContextLinux> m_reg_context_up;
  std::string m_stop_description;
  using WatchpointIndexMap = std::map<lldb::addr_t, uint32_t>;
  WatchpointIndexMap m_watchpoint_index_map;
  WatchpointIndexMap m_hw_break_index_map;
  std::unique_ptr<SingleStepWorkaround> m_step_workaround;
};
} // namespace process_linux
} // namespace lldb_private

#endif // #ifndef liblldb_NativeThreadLinux_H_
