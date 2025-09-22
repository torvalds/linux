//===-- NativeThreadOpenBSD.h ---------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeThreadOpenBSD_H_
#define liblldb_NativeThreadOpenBSD_H_

#include "lldb/Host/common/NativeThreadProtocol.h"

#include <csignal>
#include <map>
#include <string>

namespace lldb_private {
namespace process_openbsd {

class NativeProcessOpenBSD;

class NativeThreadOpenBSD : public NativeThreadProtocol {
  friend class NativeProcessOpenBSD;

public:
  NativeThreadOpenBSD(NativeProcessOpenBSD &process, lldb::tid_t tid);

  // ---------------------------------------------------------------------
  // NativeThreadProtocol Interface
  // ---------------------------------------------------------------------
  std::string GetName() override;

  lldb::StateType GetState() override;

  bool GetStopReason(ThreadStopInfo &stop_info,
                     std::string &description) override;

  NativeRegisterContext& GetRegisterContext() override;

  // OpenBSD does not expose hardware debug registers to userland
  // so these functions will just return a Status error.
  Status SetHardwareBreakpoint(lldb::addr_t addr, size_t size) override;
  Status RemoveHardwareBreakpoint(lldb::addr_t addr) override;

  // Similarly, until software watchpoints are implemented in lldb,
  // these functions will just return a Status error.
  Status SetWatchpoint(lldb::addr_t addr, size_t size, uint32_t watch_flags,
                       bool hardware) override;
  Status RemoveWatchpoint(lldb::addr_t addr) override;

private:
  // ---------------------------------------------------------------------
  // Interface for friend classes
  // ---------------------------------------------------------------------

  void SetStoppedBySignal(uint32_t signo, const siginfo_t *info = nullptr);
  void SetStoppedByBreakpoint();
  void SetStoppedByTrace();
  void SetStoppedByExec();
  void SetStopped();
  void SetRunning();
  void SetStepping();

  // ---------------------------------------------------------------------
  // Member Variables
  // ---------------------------------------------------------------------
  lldb::StateType m_state;
  ThreadStopInfo m_stop_info;
  std::unique_ptr<NativeRegisterContext> m_reg_context_up;
  std::string m_stop_description;
};

typedef std::shared_ptr<NativeThreadOpenBSD> NativeThreadOpenBSDSP;
} // namespace process_openbsd
} // namespace lldb_private

#endif // #ifndef liblldb_NativeThreadOpenBSD_H_
