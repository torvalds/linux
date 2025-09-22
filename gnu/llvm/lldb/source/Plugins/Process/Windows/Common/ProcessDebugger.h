//===-- ProcessDebugger.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ProcessDebugger_h_
#define liblldb_ProcessDebugger_h_

#include "lldb/Host/windows/windows.h"

#include "lldb/Utility/Status.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Mutex.h"

#include "ForwardDecl.h"
#include <map>
#include <set>

namespace lldb_private {

class HostProcess;
class HostThread;
class ProcessLaunchInfo;
class ProcessAttachInfo;

class ProcessWindowsData {
public:
  ProcessWindowsData(bool stop_at_entry) : m_stop_at_entry(stop_at_entry) {
    m_initial_stop_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
  }

  ~ProcessWindowsData() { ::CloseHandle(m_initial_stop_event); }

  Status m_launch_error;
  DebuggerThreadSP m_debugger;
  // StopInfoSP m_pending_stop_info;
  HANDLE m_initial_stop_event = nullptr;
  bool m_initial_stop_received = false;
  bool m_stop_at_entry;
  std::map<lldb::tid_t, lldb::ThreadSP> m_new_threads;
  std::set<lldb::tid_t> m_exited_threads;
};

class ProcessDebugger {

public:
  virtual ~ProcessDebugger();

  virtual void OnExitProcess(uint32_t exit_code);
  virtual void OnDebuggerConnected(lldb::addr_t image_base);
  virtual ExceptionResult OnDebugException(bool first_chance,
                                           const ExceptionRecord &record);
  virtual void OnCreateThread(const HostThread &thread);
  virtual void OnExitThread(lldb::tid_t thread_id, uint32_t exit_code);
  virtual void OnLoadDll(const ModuleSpec &module_spec,
                         lldb::addr_t module_addr);
  virtual void OnUnloadDll(lldb::addr_t module_addr);
  virtual void OnDebugString(const std::string &string);
  virtual void OnDebuggerError(const Status &error, uint32_t type);

protected:
  Status DetachProcess();

  Status LaunchProcess(ProcessLaunchInfo &launch_info,
                       DebugDelegateSP delegate);

  Status AttachProcess(lldb::pid_t pid, const ProcessAttachInfo &attach_info,
                       DebugDelegateSP delegate);

  Status DestroyProcess(lldb::StateType process_state);

  Status HaltProcess(bool &caused_stop);

  Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                             MemoryRegionInfo &range_info);

  Status ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    size_t &bytes_read);

  Status WriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                     size_t &bytes_written);

  Status AllocateMemory(size_t size, uint32_t permissions, lldb::addr_t &addr);

  Status DeallocateMemory(lldb::addr_t addr);

  lldb::pid_t GetDebuggedProcessId() const;

  Status WaitForDebuggerConnection(DebuggerThreadSP debugger,
                                   HostProcess &process);

protected:
  llvm::sys::Mutex m_mutex;
  std::unique_ptr<ProcessWindowsData> m_session_data;
};

} // namespace lldb_private

#endif // #ifndef liblldb_ProcessDebugger_h_
