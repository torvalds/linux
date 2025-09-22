//===-- ProcessDebugger.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessDebugger.h"

// Windows includes
#include "lldb/Host/windows/windows.h"
#include <psapi.h>

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostNativeProcessBase.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Error.h"

#include "DebuggerThread.h"
#include "ExceptionRecord.h"
#include "ProcessWindowsLog.h"

using namespace lldb;
using namespace lldb_private;

static DWORD ConvertLldbToWinApiProtect(uint32_t protect) {
  // We also can process a read / write permissions here, but if the debugger
  // will make later a write into the allocated memory, it will fail. To get
  // around it is possible inside DoWriteMemory to remember memory permissions,
  // allow write, write and restore permissions, but for now we process only
  // the executable permission.
  //
  // TODO: Process permissions other than executable
  if (protect & ePermissionsExecutable)
    return PAGE_EXECUTE_READWRITE;

  return PAGE_READWRITE;
}

// The Windows page protection bits are NOT independent masks that can be
// bitwise-ORed together.  For example, PAGE_EXECUTE_READ is not (PAGE_EXECUTE
// | PAGE_READ).  To test for an access type, it's necessary to test for any of
// the bits that provide that access type.
static bool IsPageReadable(uint32_t protect) {
  return (protect & PAGE_NOACCESS) == 0;
}

static bool IsPageWritable(uint32_t protect) {
  return (protect & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY |
                     PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
}

static bool IsPageExecutable(uint32_t protect) {
  return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                     PAGE_EXECUTE_WRITECOPY)) != 0;
}

namespace lldb_private {

ProcessDebugger::~ProcessDebugger() {}

lldb::pid_t ProcessDebugger::GetDebuggedProcessId() const {
  if (m_session_data)
    return m_session_data->m_debugger->GetProcess().GetProcessId();
  return LLDB_INVALID_PROCESS_ID;
}

Status ProcessDebugger::DetachProcess() {
  Log *log = GetLog(WindowsLog::Process);
  DebuggerThreadSP debugger_thread;
  {
    // Acquire the lock only long enough to get the DebuggerThread.
    // StopDebugging() will trigger a call back into ProcessDebugger which will
    // also acquire the lock.  Thus we have to release the lock before calling
    // StopDebugging().
    llvm::sys::ScopedLock lock(m_mutex);

    if (!m_session_data) {
      LLDB_LOG(log, "there is no active session.");
      return Status();
    }

    debugger_thread = m_session_data->m_debugger;
  }

  Status error;

  LLDB_LOG(log, "detaching from process {0}.",
           debugger_thread->GetProcess().GetNativeProcess().GetSystemHandle());
  error = debugger_thread->StopDebugging(false);

  // By the time StopDebugging returns, there is no more debugger thread, so
  // we can be assured that no other thread will race for the session data.
  m_session_data.reset();

  return error;
}

Status ProcessDebugger::LaunchProcess(ProcessLaunchInfo &launch_info,
                                      DebugDelegateSP delegate) {
  // Even though m_session_data is accessed here, it is before a debugger
  // thread has been kicked off.  So there's no race conditions, and it
  // shouldn't be necessary to acquire the mutex.

  Log *log = GetLog(WindowsLog::Process);
  Status result;

  FileSpec working_dir = launch_info.GetWorkingDirectory();
  namespace fs = llvm::sys::fs;
  if (working_dir) {
    FileSystem::Instance().Resolve(working_dir);
    if (!FileSystem::Instance().IsDirectory(working_dir)) {
      result.SetErrorStringWithFormat("No such file or directory: %s",
                                      working_dir.GetPath().c_str());
      return result;
    }
  }

  if (!launch_info.GetFlags().Test(eLaunchFlagDebug)) {
    StreamString stream;
    stream.Printf("ProcessDebugger unable to launch '%s'.  ProcessDebugger can "
                  "only be used for debug launches.",
                  launch_info.GetExecutableFile().GetPath().c_str());
    std::string message = stream.GetString().str();
    result.SetErrorString(message.c_str());

    LLDB_LOG(log, "error: {0}", message);
    return result;
  }

  bool stop_at_entry = launch_info.GetFlags().Test(eLaunchFlagStopAtEntry);
  m_session_data.reset(new ProcessWindowsData(stop_at_entry));
  m_session_data->m_debugger.reset(new DebuggerThread(delegate));
  DebuggerThreadSP debugger = m_session_data->m_debugger;

  // Kick off the DebugLaunch asynchronously and wait for it to complete.
  result = debugger->DebugLaunch(launch_info);
  if (result.Fail()) {
    LLDB_LOG(log, "failed launching '{0}'. {1}",
             launch_info.GetExecutableFile().GetPath(), result);
    return result;
  }

  HostProcess process;
  Status error = WaitForDebuggerConnection(debugger, process);
  if (error.Fail()) {
    LLDB_LOG(log, "failed launching '{0}'. {1}",
             launch_info.GetExecutableFile().GetPath(), error);
    return error;
  }

  LLDB_LOG(log, "successfully launched '{0}'",
           launch_info.GetExecutableFile().GetPath());

  // We've hit the initial stop.  If eLaunchFlagsStopAtEntry was specified, the
  // private state should already be set to eStateStopped as a result of
  // hitting the initial breakpoint.  If it was not set, the breakpoint should
  // have already been resumed from and the private state should already be
  // eStateRunning.
  launch_info.SetProcessID(process.GetProcessId());

  return result;
}

Status ProcessDebugger::AttachProcess(lldb::pid_t pid,
                                      const ProcessAttachInfo &attach_info,
                                      DebugDelegateSP delegate) {
  Log *log = GetLog(WindowsLog::Process);
  m_session_data.reset(
      new ProcessWindowsData(!attach_info.GetContinueOnceAttached()));
  DebuggerThreadSP debugger(new DebuggerThread(delegate));

  m_session_data->m_debugger = debugger;

  DWORD process_id = static_cast<DWORD>(pid);
  Status error = debugger->DebugAttach(process_id, attach_info);
  if (error.Fail()) {
    LLDB_LOG(
        log,
        "encountered an error occurred initiating the asynchronous attach. {0}",
        error);
    return error;
  }

  HostProcess process;
  error = WaitForDebuggerConnection(debugger, process);
  if (error.Fail()) {
    LLDB_LOG(log,
             "encountered an error waiting for the debugger to connect. {0}",
             error);
    return error;
  }

  LLDB_LOG(log, "successfully attached to process with pid={0}", process_id);

  // We've hit the initial stop.  If eLaunchFlagsStopAtEntry was specified, the
  // private state should already be set to eStateStopped as a result of
  // hitting the initial breakpoint.  If it was not set, the breakpoint should
  // have already been resumed from and the private state should already be
  // eStateRunning.

  return error;
}

Status ProcessDebugger::DestroyProcess(const lldb::StateType state) {
  Log *log = GetLog(WindowsLog::Process);
  DebuggerThreadSP debugger_thread;
  {
    // Acquire this lock inside an inner scope, only long enough to get the
    // DebuggerThread. StopDebugging() will trigger a call back into
    // ProcessDebugger which will acquire the lock again, so we need to not
    // deadlock.
    llvm::sys::ScopedLock lock(m_mutex);

    if (!m_session_data) {
      LLDB_LOG(log, "warning: state = {0}, but there is no active session.",
               state);
      return Status();
    }

    debugger_thread = m_session_data->m_debugger;
  }

  if (state == eStateExited || state == eStateDetached) {
    LLDB_LOG(log, "warning: cannot destroy process {0} while state = {1}.",
             GetDebuggedProcessId(), state);
    return Status();
  }

  LLDB_LOG(log, "Shutting down process {0}.",
           debugger_thread->GetProcess().GetNativeProcess().GetSystemHandle());
  auto error = debugger_thread->StopDebugging(true);

  // By the time StopDebugging returns, there is no more debugger thread, so
  // we can be assured that no other thread will race for the session data.
  m_session_data.reset();

  return error;
}

Status ProcessDebugger::HaltProcess(bool &caused_stop) {
  Log *log = GetLog(WindowsLog::Process);
  Status error;
  llvm::sys::ScopedLock lock(m_mutex);
  caused_stop = ::DebugBreakProcess(m_session_data->m_debugger->GetProcess()
                                        .GetNativeProcess()
                                        .GetSystemHandle());
  if (!caused_stop) {
    error.SetError(::GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "DebugBreakProcess failed with error {0}", error);
  }

  return error;
}

Status ProcessDebugger::ReadMemory(lldb::addr_t vm_addr, void *buf, size_t size,
                                   size_t &bytes_read) {
  Status error;
  bytes_read = 0;
  Log *log = GetLog(WindowsLog::Memory);
  llvm::sys::ScopedLock lock(m_mutex);

  if (!m_session_data) {
    error.SetErrorString(
        "cannot read, there is no active debugger connection.");
    LLDB_LOG(log, "error: {0}", error);
    return error;
  }

  LLDB_LOG(log, "attempting to read {0} bytes from address {1:x}", size,
           vm_addr);

  HostProcess process = m_session_data->m_debugger->GetProcess();
  void *addr = reinterpret_cast<void *>(vm_addr);
  SIZE_T num_of_bytes_read = 0;
  if (!::ReadProcessMemory(process.GetNativeProcess().GetSystemHandle(), addr,
                           buf, size, &num_of_bytes_read)) {
    error.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "reading failed with error: {0}", error);
  } else {
    bytes_read = num_of_bytes_read;
  }
  return error;
}

Status ProcessDebugger::WriteMemory(lldb::addr_t vm_addr, const void *buf,
                                    size_t size, size_t &bytes_written) {
  Status error;
  bytes_written = 0;
  Log *log = GetLog(WindowsLog::Memory);
  llvm::sys::ScopedLock lock(m_mutex);
  LLDB_LOG(log, "attempting to write {0} bytes into address {1:x}", size,
           vm_addr);

  if (!m_session_data) {
    error.SetErrorString(
        "cannot write, there is no active debugger connection.");
    LLDB_LOG(log, "error: {0}", error);
    return error;
  }

  HostProcess process = m_session_data->m_debugger->GetProcess();
  void *addr = reinterpret_cast<void *>(vm_addr);
  SIZE_T num_of_bytes_written = 0;
  lldb::process_t handle = process.GetNativeProcess().GetSystemHandle();
  if (::WriteProcessMemory(handle, addr, buf, size, &num_of_bytes_written)) {
    FlushInstructionCache(handle, addr, num_of_bytes_written);
    bytes_written = num_of_bytes_written;
  } else {
    error.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "writing failed with error: {0}", error);
  }
  return error;
}

Status ProcessDebugger::AllocateMemory(size_t size, uint32_t permissions,
                                       lldb::addr_t &addr) {
  Status error;
  addr = LLDB_INVALID_ADDRESS;
  Log *log = GetLog(WindowsLog::Memory);
  llvm::sys::ScopedLock lock(m_mutex);
  LLDB_LOG(log, "attempting to allocate {0} bytes with permissions {1}", size,
           permissions);

  if (!m_session_data) {
    error.SetErrorString(
        "cannot allocate, there is no active debugger connection");
    LLDB_LOG(log, "error: {0}", error);
    return error;
  }

  HostProcess process = m_session_data->m_debugger->GetProcess();
  lldb::process_t handle = process.GetNativeProcess().GetSystemHandle();
  auto protect = ConvertLldbToWinApiProtect(permissions);
  auto result = ::VirtualAllocEx(handle, nullptr, size, MEM_COMMIT, protect);
  if (!result) {
    error.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "allocating failed with error: {0}", error);
  } else {
    addr = reinterpret_cast<addr_t>(result);
  }
  return error;
}

Status ProcessDebugger::DeallocateMemory(lldb::addr_t vm_addr) {
  Status result;

  Log *log = GetLog(WindowsLog::Memory);
  llvm::sys::ScopedLock lock(m_mutex);
  LLDB_LOG(log, "attempting to deallocate bytes at address {0}", vm_addr);

  if (!m_session_data) {
    result.SetErrorString(
        "cannot deallocate, there is no active debugger connection");
    LLDB_LOG(log, "error: {0}", result);
    return result;
  }

  HostProcess process = m_session_data->m_debugger->GetProcess();
  lldb::process_t handle = process.GetNativeProcess().GetSystemHandle();
  if (!::VirtualFreeEx(handle, reinterpret_cast<LPVOID>(vm_addr), 0,
                       MEM_RELEASE)) {
    result.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "deallocating failed with error: {0}", result);
  }

  return result;
}

Status ProcessDebugger::GetMemoryRegionInfo(lldb::addr_t vm_addr,
                                            MemoryRegionInfo &info) {
  Log *log = GetLog(WindowsLog::Memory);
  Status error;
  llvm::sys::ScopedLock lock(m_mutex);
  info.Clear();

  if (!m_session_data) {
    error.SetErrorString(
        "GetMemoryRegionInfo called with no debugging session.");
    LLDB_LOG(log, "error: {0}", error);
    return error;
  }
  HostProcess process = m_session_data->m_debugger->GetProcess();
  lldb::process_t handle = process.GetNativeProcess().GetSystemHandle();
  if (handle == nullptr || handle == LLDB_INVALID_PROCESS) {
    error.SetErrorString(
        "GetMemoryRegionInfo called with an invalid target process.");
    LLDB_LOG(log, "error: {0}", error);
    return error;
  }

  LLDB_LOG(log, "getting info for address {0:x}", vm_addr);

  void *addr = reinterpret_cast<void *>(vm_addr);
  MEMORY_BASIC_INFORMATION mem_info = {};
  SIZE_T result = ::VirtualQueryEx(handle, addr, &mem_info, sizeof(mem_info));
  if (result == 0) {
    DWORD last_error = ::GetLastError();
    if (last_error == ERROR_INVALID_PARAMETER) {
      // ERROR_INVALID_PARAMETER is returned if VirtualQueryEx is called with
      // an address past the highest accessible address. We should return a
      // range from the vm_addr to LLDB_INVALID_ADDRESS
      info.GetRange().SetRangeBase(vm_addr);
      info.GetRange().SetRangeEnd(LLDB_INVALID_ADDRESS);
      info.SetReadable(MemoryRegionInfo::eNo);
      info.SetExecutable(MemoryRegionInfo::eNo);
      info.SetWritable(MemoryRegionInfo::eNo);
      info.SetMapped(MemoryRegionInfo::eNo);
      return error;
    } else {
      error.SetError(last_error, eErrorTypeWin32);
      LLDB_LOG(log,
               "VirtualQueryEx returned error {0} while getting memory "
               "region info for address {1:x}",
               error, vm_addr);
      return error;
    }
  }

  // Protect bits are only valid for MEM_COMMIT regions.
  if (mem_info.State == MEM_COMMIT) {
    const bool readable = IsPageReadable(mem_info.Protect);
    const bool executable = IsPageExecutable(mem_info.Protect);
    const bool writable = IsPageWritable(mem_info.Protect);
    info.SetReadable(readable ? MemoryRegionInfo::eYes : MemoryRegionInfo::eNo);
    info.SetExecutable(executable ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
    info.SetWritable(writable ? MemoryRegionInfo::eYes : MemoryRegionInfo::eNo);
  } else {
    info.SetReadable(MemoryRegionInfo::eNo);
    info.SetExecutable(MemoryRegionInfo::eNo);
    info.SetWritable(MemoryRegionInfo::eNo);
  }

  // AllocationBase is defined for MEM_COMMIT and MEM_RESERVE but not MEM_FREE.
  if (mem_info.State != MEM_FREE) {
    info.GetRange().SetRangeBase(
        reinterpret_cast<addr_t>(mem_info.BaseAddress));
    info.GetRange().SetRangeEnd(reinterpret_cast<addr_t>(mem_info.BaseAddress) +
                                mem_info.RegionSize);
    info.SetMapped(MemoryRegionInfo::eYes);
  } else {
    // In the unmapped case we need to return the distance to the next block of
    // memory. VirtualQueryEx nearly does that except that it gives the
    // distance from the start of the page containing vm_addr.
    SYSTEM_INFO data;
    ::GetSystemInfo(&data);
    DWORD page_offset = vm_addr % data.dwPageSize;
    info.GetRange().SetRangeBase(vm_addr);
    info.GetRange().SetByteSize(mem_info.RegionSize - page_offset);
    info.SetMapped(MemoryRegionInfo::eNo);
  }

  LLDB_LOGV(log,
            "Memory region info for address {0}: readable={1}, "
            "executable={2}, writable={3}",
            vm_addr, info.GetReadable(), info.GetExecutable(),
            info.GetWritable());
  return error;
}

void ProcessDebugger::OnExitProcess(uint32_t exit_code) {
  // If the process exits before any initial stop then notify the debugger
  // of the error otherwise WaitForDebuggerConnection() will be blocked.
  // An example of this issue is when a process fails to load a dependent DLL.
  if (m_session_data && !m_session_data->m_initial_stop_received) {
    Status error(exit_code, eErrorTypeWin32);
    OnDebuggerError(error, 0);
  }
}

void ProcessDebugger::OnDebuggerConnected(lldb::addr_t image_base) {}

ExceptionResult
ProcessDebugger::OnDebugException(bool first_chance,
                                  const ExceptionRecord &record) {
  Log *log = GetLog(WindowsLog::Exception);
  llvm::sys::ScopedLock lock(m_mutex);
  // FIXME: Without this check, occasionally when running the test suite
  // there is an issue where m_session_data can be null.  It's not clear how
  // this could happen but it only surfaces while running the test suite.  In
  // order to properly diagnose this, we probably need to first figure allow the
  // test suite to print out full lldb logs, and then add logging to the process
  // plugin.
  if (!m_session_data) {
    LLDB_LOG(log,
             "Debugger thread reported exception {0:x} at address {1:x}, but "
             "there is no session.",
             record.GetExceptionCode(), record.GetExceptionAddress());
    return ExceptionResult::SendToApplication;
  }

  ExceptionResult result = ExceptionResult::SendToApplication;
  if ((record.GetExceptionCode() == EXCEPTION_BREAKPOINT ||
       record.GetExceptionCode() ==
           0x4000001FL /*WOW64 STATUS_WX86_BREAKPOINT*/) &&
      !m_session_data->m_initial_stop_received) {
    // Handle breakpoints at the first chance.
    result = ExceptionResult::BreakInDebugger;
    LLDB_LOG(
        log,
        "Hit loader breakpoint at address {0:x}, setting initial stop event.",
        record.GetExceptionAddress());
    m_session_data->m_initial_stop_received = true;
    ::SetEvent(m_session_data->m_initial_stop_event);
  }
  return result;
}

void ProcessDebugger::OnCreateThread(const HostThread &thread) {
  // Do nothing by default
}

void ProcessDebugger::OnExitThread(lldb::tid_t thread_id, uint32_t exit_code) {
  // Do nothing by default
}

void ProcessDebugger::OnLoadDll(const ModuleSpec &module_spec,
                                lldb::addr_t module_addr) {
  // Do nothing by default
}

void ProcessDebugger::OnUnloadDll(lldb::addr_t module_addr) {
  // Do nothing by default
}

void ProcessDebugger::OnDebugString(const std::string &string) {}

void ProcessDebugger::OnDebuggerError(const Status &error, uint32_t type) {
  llvm::sys::ScopedLock lock(m_mutex);
  Log *log = GetLog(WindowsLog::Process);

  if (m_session_data->m_initial_stop_received) {
    // This happened while debugging.  Do we shutdown the debugging session,
    // try to continue, or do something else?
    LLDB_LOG(log,
             "Error {0} occurred during debugging.  Unexpected behavior "
             "may result.  {1}",
             error.GetError(), error);
  } else {
    // If we haven't actually launched the process yet, this was an error
    // launching the process.  Set the internal error and signal the initial
    // stop event so that the DoLaunch method wakes up and returns a failure.
    m_session_data->m_launch_error = error;
    ::SetEvent(m_session_data->m_initial_stop_event);
    LLDB_LOG(log,
             "Error {0} occurred launching the process before the initial "
             "stop. {1}",
             error.GetError(), error);
    return;
  }
}

Status ProcessDebugger::WaitForDebuggerConnection(DebuggerThreadSP debugger,
                                                  HostProcess &process) {
  Status result;
  Log *log = GetLog(WindowsLog::Process | WindowsLog::Breakpoints);
  LLDB_LOG(log, "Waiting for loader breakpoint.");

  // Block this function until we receive the initial stop from the process.
  if (::WaitForSingleObject(m_session_data->m_initial_stop_event, INFINITE) ==
      WAIT_OBJECT_0) {
    LLDB_LOG(log, "hit loader breakpoint, returning.");

    process = debugger->GetProcess();
    return m_session_data->m_launch_error;
  } else
    return Status(::GetLastError(), eErrorTypeWin32);
}

} // namespace lldb_private
