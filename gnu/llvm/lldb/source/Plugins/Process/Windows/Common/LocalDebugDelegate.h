//===-- LocalDebugDelegate.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Windows_LocalDebugDelegate_H_
#define liblldb_Plugins_Process_Windows_LocalDebugDelegate_H_

#include <memory>

#include "IDebugDelegate.h"

#include "lldb/lldb-forward.h"

namespace lldb_private {

class ProcessWindows;
typedef std::shared_ptr<ProcessWindows> ProcessWindowsSP;

// LocalDebugDelegate
//
// LocalDebugDelegate creates a connection between a ProcessWindows and the
// debug driver.  This serves to decouple ProcessWindows from the debug
// driver.  It would be possible to get a similar decoupling by just having
// ProcessWindows implement this interface directly.  There are two reasons
// why we don't do this:
//
// 1) In the future when we add support for local debugging through LLGS, and we
//    go through the Native*Protocol interface, it is likely we will need the
//    additional flexibility provided by this sort of adapter pattern.
// 2) LLDB holds a shared_ptr to the ProcessWindows, and our driver thread
//    needs access to it as well.  To avoid a race condition, we want to make
//    sure that we're also holding onto a shared_ptr.
//    lldb_private::Process supports enable_shared_from_this, but that gives us
//    a ProcessSP (which is exactly what we are trying to decouple from the
//    driver), so this adapter serves as a way to transparently hold the
//    ProcessSP while still keeping it decoupled from the driver.
class LocalDebugDelegate : public IDebugDelegate {
public:
  explicit LocalDebugDelegate(lldb::ProcessWP process);

  void OnExitProcess(uint32_t exit_code) override;
  void OnDebuggerConnected(lldb::addr_t image_base) override;
  ExceptionResult OnDebugException(bool first_chance,
                                   const ExceptionRecord &record) override;
  void OnCreateThread(const HostThread &thread) override;
  void OnExitThread(lldb::tid_t thread_id, uint32_t exit_code) override;
  void OnLoadDll(const lldb_private::ModuleSpec &module_spec,
                 lldb::addr_t module_addr) override;
  void OnUnloadDll(lldb::addr_t module_addr) override;
  void OnDebugString(const std::string &message) override;
  void OnDebuggerError(const Status &error, uint32_t type) override;

private:
  ProcessWindowsSP GetProcessPointer();

  lldb::ProcessWP m_process;
};
}

#endif
