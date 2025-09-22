//===-- NativeThreadWindows.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeThreadWindows_h_
#define liblldb_NativeThreadWindows_h_

#include "lldb/Host/HostThread.h"
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/lldb-private-forward.h"

#include "NativeRegisterContextWindows.h"

namespace lldb_private {

class NativeProcessWindows;

class NativeThreadWindows : public NativeThreadProtocol {
public:
  NativeThreadWindows(NativeProcessWindows &process, const HostThread &thread);

  ~NativeThreadWindows() {}

  Status DoStop();
  Status DoResume(lldb::StateType resume_state);

  std::string GetName() override;

  lldb::StateType GetState() override { return m_state; }

  NativeRegisterContextWindows &GetRegisterContext() override {
    return *m_reg_context_up;
  }

  bool GetStopReason(ThreadStopInfo &stop_info,
                     std::string &description) override;

  Status SetWatchpoint(lldb::addr_t addr, size_t size, uint32_t watch_flags,
                       bool hardware) override;

  Status RemoveWatchpoint(lldb::addr_t addr) override;

  Status SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;

  Status RemoveHardwareBreakpoint(lldb::addr_t addr) override;

  void SetStopReason(ThreadStopInfo stop_info, std::string description);

  const HostThread &GetHostThread() { return m_host_thread; }

protected:
  lldb::StateType m_state = lldb::StateType::eStateInvalid;
  std::string m_name;
  ThreadStopInfo m_stop_info;
  std::string m_stop_description;
  std::unique_ptr<NativeRegisterContextWindows> m_reg_context_up;
  // Cache address and index of the watchpoints and hardware breakpoints since
  // the register context does not.
  using IndexMap = std::map<lldb::addr_t, uint32_t>;
  IndexMap m_watchpoint_index_map;
  IndexMap m_hw_breakpoint_index_map;
  HostThread m_host_thread;
};
} // namespace lldb_private

#endif // #ifndef liblldb_NativeThreadWindows_h_
