//===-- NativeThreadWindows.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeThreadWindows.h"
#include "NativeProcessWindows.h"

#include "lldb/Host/HostThread.h"
#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "lldb/lldb-forward.h"

using namespace lldb;
using namespace lldb_private;

NativeThreadWindows::NativeThreadWindows(NativeProcessWindows &process,
                                         const HostThread &thread)
    : NativeThreadProtocol(process, thread.GetNativeThread().GetThreadId()),
      m_stop_info(), m_stop_description(), m_host_thread(thread) {
  m_reg_context_up =
      (NativeRegisterContextWindows::CreateHostNativeRegisterContextWindows(
          process.GetArchitecture(), *this));
}

Status NativeThreadWindows::DoStop() {
  if (m_state != eStateStopped) {
    DWORD previous_suspend_count =
        ::SuspendThread(m_host_thread.GetNativeThread().GetSystemHandle());
    if (previous_suspend_count == (DWORD)-1)
      return Status(::GetLastError(), eErrorTypeWin32);

    m_state = eStateStopped;
  }
  return Status();
}

Status NativeThreadWindows::DoResume(lldb::StateType resume_state) {
  StateType current_state = GetState();
  if (resume_state == current_state)
    return Status();

  if (resume_state == eStateStepping) {
    Log *log = GetLog(LLDBLog::Thread);

    uint32_t flags_index =
        GetRegisterContext().ConvertRegisterKindToRegisterNumber(
            eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS);
    uint64_t flags_value =
        GetRegisterContext().ReadRegisterAsUnsigned(flags_index, 0);
    NativeProcessProtocol &process = GetProcess();
    const ArchSpec &arch = process.GetArchitecture();
    switch (arch.GetMachine()) {
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      flags_value |= 0x100; // Set the trap flag on the CPU
      break;
    case llvm::Triple::aarch64:
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      flags_value |= 0x200000; // The SS bit in PState
      break;
    default:
      LLDB_LOG(log, "single stepping unsupported on this architecture");
      break;
    }
    GetRegisterContext().WriteRegisterFromUnsigned(flags_index, flags_value);
  }

  if (resume_state == eStateStepping || resume_state == eStateRunning) {
    DWORD previous_suspend_count = 0;
    HANDLE thread_handle = m_host_thread.GetNativeThread().GetSystemHandle();
    do {
      // ResumeThread returns -1 on error, or the thread's *previous* suspend
      // count on success. This means that the return value is 1 when the thread
      // was restarted. Note that DWORD is an unsigned int, so we need to
      // explicitly compare with -1.
      previous_suspend_count = ::ResumeThread(thread_handle);

      if (previous_suspend_count == (DWORD)-1)
        return Status(::GetLastError(), eErrorTypeWin32);

    } while (previous_suspend_count > 1);
    m_state = eStateRunning;
  }

  return Status();
}

std::string NativeThreadWindows::GetName() {
  if (!m_name.empty())
    return m_name;

  // Name is not a property of the Windows thread. Create one with the
  // process's.
  NativeProcessProtocol &process = GetProcess();
  ProcessInstanceInfo process_info;
  if (Host::GetProcessInfo(process.GetID(), process_info)) {
    std::string process_name(process_info.GetName());
    m_name = process_name;
  }
  return m_name;
}

void NativeThreadWindows::SetStopReason(ThreadStopInfo stop_info,
                                        std::string description) {
  m_state = eStateStopped;
  m_stop_info = stop_info;
  m_stop_description = description;
}

bool NativeThreadWindows::GetStopReason(ThreadStopInfo &stop_info,
                                        std::string &description) {
  Log *log = GetLog(LLDBLog::Thread);

  switch (m_state) {
  case eStateStopped:
  case eStateCrashed:
  case eStateExited:
  case eStateSuspended:
  case eStateUnloaded:
    stop_info = m_stop_info;
    description = m_stop_description;
    return true;

  case eStateInvalid:
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
  case eStateDetached:
    if (log) {
      log->Printf("NativeThreadWindows::%s tid %" PRIu64
                  " in state %s cannot answer stop reason",
                  __FUNCTION__, GetID(), StateAsCString(m_state));
    }
    return false;
  }
  llvm_unreachable("unhandled StateType!");
}

Status NativeThreadWindows::SetWatchpoint(lldb::addr_t addr, size_t size,
                                          uint32_t watch_flags, bool hardware) {
  if (!hardware)
    return Status("not implemented");
  if (m_state == eStateLaunching)
    return Status();
  Status error = RemoveWatchpoint(addr);
  if (error.Fail())
    return error;
  uint32_t wp_index =
      m_reg_context_up->SetHardwareWatchpoint(addr, size, watch_flags);
  if (wp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware watchpoint failed.");
  m_watchpoint_index_map.insert({addr, wp_index});
  return Status();
}

Status NativeThreadWindows::RemoveWatchpoint(lldb::addr_t addr) {
  auto wp = m_watchpoint_index_map.find(addr);
  if (wp == m_watchpoint_index_map.end())
    return Status();
  uint32_t wp_index = wp->second;
  m_watchpoint_index_map.erase(wp);
  if (m_reg_context_up->ClearHardwareWatchpoint(wp_index))
    return Status();
  return Status("Clearing hardware watchpoint failed.");
}

Status NativeThreadWindows::SetHardwareBreakpoint(lldb::addr_t addr,
                                                  size_t size) {
  return Status("unimplemented.");
}

Status NativeThreadWindows::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  return Status("unimplemented.");
}
