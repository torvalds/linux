//===-- DebuggerThread.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Windows_DebuggerThread_H_
#define liblldb_Plugins_Process_Windows_DebuggerThread_H_

#include <atomic>
#include <memory>

#include "ForwardDecl.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/Predicate.h"

namespace lldb_private {

// DebuggerThread
//
// Debugs a single process, notifying listeners as appropriate when interesting
// things occur.
class DebuggerThread : public std::enable_shared_from_this<DebuggerThread> {
public:
  DebuggerThread(DebugDelegateSP debug_delegate);
  virtual ~DebuggerThread();

  Status DebugLaunch(const ProcessLaunchInfo &launch_info);
  Status DebugAttach(lldb::pid_t pid, const ProcessAttachInfo &attach_info);

  HostProcess GetProcess() const { return m_process; }
  HostThread GetMainThread() const { return m_main_thread; }
  std::weak_ptr<ExceptionRecord> GetActiveException() {
    return m_active_exception;
  }

  Status StopDebugging(bool terminate);

  void ContinueAsyncException(ExceptionResult result);

private:
  void FreeProcessHandles();
  void DebugLoop();
  ExceptionResult HandleExceptionEvent(const EXCEPTION_DEBUG_INFO &info,
                                       DWORD thread_id);
  DWORD HandleCreateThreadEvent(const CREATE_THREAD_DEBUG_INFO &info,
                                DWORD thread_id);
  DWORD HandleCreateProcessEvent(const CREATE_PROCESS_DEBUG_INFO &info,
                                 DWORD thread_id);
  DWORD HandleExitThreadEvent(const EXIT_THREAD_DEBUG_INFO &info,
                              DWORD thread_id);
  DWORD HandleExitProcessEvent(const EXIT_PROCESS_DEBUG_INFO &info,
                               DWORD thread_id);
  DWORD HandleLoadDllEvent(const LOAD_DLL_DEBUG_INFO &info, DWORD thread_id);
  DWORD HandleUnloadDllEvent(const UNLOAD_DLL_DEBUG_INFO &info,
                             DWORD thread_id);
  DWORD HandleODSEvent(const OUTPUT_DEBUG_STRING_INFO &info, DWORD thread_id);
  DWORD HandleRipEvent(const RIP_INFO &info, DWORD thread_id);

  DebugDelegateSP m_debug_delegate;

  HostProcess m_process;    // The process being debugged.
  HostThread m_main_thread; // The main thread of the inferior.

  // The image file of the process being debugged.
  HANDLE m_image_file = nullptr;

  // The current exception waiting to be handled
  ExceptionRecordSP m_active_exception;

  // A predicate which gets signalled when an exception is finished processing
  // and the debug loop can be continued.
  Predicate<ExceptionResult> m_exception_pred;

  // An event which gets signalled by the debugger thread when it exits the
  // debugger loop and is detached from the inferior.
  HANDLE m_debugging_ended_event = nullptr;

  // Signals the loop to detach from the process (specified by pid).
  std::atomic<DWORD> m_pid_to_detach;

  // Signals the debug loop to stop processing certain types of events that
  // block shutdown.
  std::atomic<bool> m_is_shutting_down;

  // Indicates we've detached from the inferior process and the debug loop can
  // exit.
  bool m_detached = false;

  lldb::thread_result_t
  DebuggerThreadLaunchRoutine(const ProcessLaunchInfo &launch_info);
  lldb::thread_result_t
  DebuggerThreadAttachRoutine(lldb::pid_t pid,
                              const ProcessAttachInfo &launch_info);
};
}
#endif
