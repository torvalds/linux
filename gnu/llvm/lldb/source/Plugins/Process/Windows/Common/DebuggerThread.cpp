//===-- DebuggerThread.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DebuggerThread.h"
#include "ExceptionRecord.h"
#include "IDebugDelegate.h"

#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/windows/AutoHandle.h"
#include "lldb/Host/windows/HostProcessWindows.h"
#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/ProcessLauncherWindows.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/Windows/Common/ProcessWindowsLog.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <psapi.h>

#ifndef STATUS_WX86_BREAKPOINT
#define STATUS_WX86_BREAKPOINT 0x4000001FL // For WOW64
#endif

using namespace lldb;
using namespace lldb_private;

DebuggerThread::DebuggerThread(DebugDelegateSP debug_delegate)
    : m_debug_delegate(debug_delegate), m_pid_to_detach(0),
      m_is_shutting_down(false) {
  m_debugging_ended_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

DebuggerThread::~DebuggerThread() { ::CloseHandle(m_debugging_ended_event); }

Status DebuggerThread::DebugLaunch(const ProcessLaunchInfo &launch_info) {
  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "launching '{0}'", launch_info.GetExecutableFile().GetPath());

  Status result;
  llvm::Expected<HostThread> secondary_thread = ThreadLauncher::LaunchThread(
      "lldb.plugin.process-windows.secondary[?]",
      [this, launch_info] { return DebuggerThreadLaunchRoutine(launch_info); });
  if (!secondary_thread) {
    result = Status(secondary_thread.takeError());
    LLDB_LOG(log, "couldn't launch debugger thread. {0}", result);
  }

  return result;
}

Status DebuggerThread::DebugAttach(lldb::pid_t pid,
                                   const ProcessAttachInfo &attach_info) {
  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "attaching to '{0}'", pid);

  Status result;
  llvm::Expected<HostThread> secondary_thread = ThreadLauncher::LaunchThread(
      "lldb.plugin.process-windows.secondary[?]", [this, pid, attach_info] {
        return DebuggerThreadAttachRoutine(pid, attach_info);
      });
  if (!secondary_thread) {
    result = Status(secondary_thread.takeError());
    LLDB_LOG(log, "couldn't attach to process '{0}'. {1}", pid, result);
  }

  return result;
}

lldb::thread_result_t DebuggerThread::DebuggerThreadLaunchRoutine(
    const ProcessLaunchInfo &launch_info) {
  // Grab a shared_ptr reference to this so that we know it won't get deleted
  // until after the thread routine has exited.
  std::shared_ptr<DebuggerThread> this_ref(shared_from_this());

  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "preparing to launch '{0}' on background thread.",
           launch_info.GetExecutableFile().GetPath());

  Status error;
  ProcessLauncherWindows launcher;
  HostProcess process(launcher.LaunchProcess(launch_info, error));
  // If we couldn't create the process, notify waiters immediately.  Otherwise
  // enter the debug loop and wait until we get the create process debug
  // notification.  Note that if the process was created successfully, we can
  // throw away the process handle we got from CreateProcess because Windows
  // will give us another (potentially more useful?) handle when it sends us
  // the CREATE_PROCESS_DEBUG_EVENT.
  if (error.Success())
    DebugLoop();
  else
    m_debug_delegate->OnDebuggerError(error, 0);

  return {};
}

lldb::thread_result_t DebuggerThread::DebuggerThreadAttachRoutine(
    lldb::pid_t pid, const ProcessAttachInfo &attach_info) {
  // Grab a shared_ptr reference to this so that we know it won't get deleted
  // until after the thread routine has exited.
  std::shared_ptr<DebuggerThread> this_ref(shared_from_this());

  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "preparing to attach to process '{0}' on background thread.",
           pid);

  if (!DebugActiveProcess((DWORD)pid)) {
    Status error(::GetLastError(), eErrorTypeWin32);
    m_debug_delegate->OnDebuggerError(error, 0);
    return {};
  }

  // The attach was successful, enter the debug loop.  From here on out, this
  // is no different than a create process operation, so all the same comments
  // in DebugLaunch should apply from this point out.
  DebugLoop();

  return {};
}

Status DebuggerThread::StopDebugging(bool terminate) {
  Status error;

  lldb::pid_t pid = m_process.GetProcessId();

  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "terminate = {0}, inferior={1}.", terminate, pid);

  // Set m_is_shutting_down to true if it was false.  Return if it was already
  // true.
  bool expected = false;
  if (!m_is_shutting_down.compare_exchange_strong(expected, true))
    return error;

  // Make a copy of the process, since the termination sequence will reset
  // DebuggerThread's internal copy and it needs to remain open for the Wait
  // operation.
  HostProcess process_copy = m_process;
  lldb::process_t handle = m_process.GetNativeProcess().GetSystemHandle();

  if (terminate) {
    if (handle != nullptr && handle != LLDB_INVALID_PROCESS) {
      // Initiate the termination before continuing the exception, so that the
      // next debug event we get is the exit process event, and not some other
      // event.
      BOOL terminate_suceeded = TerminateProcess(handle, 0);
      LLDB_LOG(log,
               "calling TerminateProcess({0}, 0) (inferior={1}), success={2}",
               handle, pid, terminate_suceeded);
    } else {
      LLDB_LOG(log,
               "NOT calling TerminateProcess because the inferior is not valid "
               "({0}, 0) (inferior={1})",
               handle, pid);
    }
  }

  // If we're stuck waiting for an exception to continue (e.g. the user is at a
  // breakpoint messing around in the debugger), continue it now.  But only
  // AFTER calling TerminateProcess to make sure that the very next call to
  // WaitForDebugEvent is an exit process event.
  if (m_active_exception.get()) {
    LLDB_LOG(log, "masking active exception");
    ContinueAsyncException(ExceptionResult::MaskException);
  }

  if (!terminate) {
    // Indicate that we want to detach.
    m_pid_to_detach = GetProcess().GetProcessId();

    // Force a fresh break so that the detach can happen from the debugger
    // thread.
    if (!::DebugBreakProcess(
            GetProcess().GetNativeProcess().GetSystemHandle())) {
      error.SetError(::GetLastError(), eErrorTypeWin32);
    }
  }

  LLDB_LOG(log, "waiting for detach from process {0} to complete.", pid);

  DWORD wait_result = WaitForSingleObject(m_debugging_ended_event, 5000);
  if (wait_result != WAIT_OBJECT_0) {
    error.SetError(GetLastError(), eErrorTypeWin32);
    LLDB_LOG(log, "error: WaitForSingleObject({0}, 5000) returned {1}",
             m_debugging_ended_event, wait_result);
  } else
    LLDB_LOG(log, "detach from process {0} completed successfully.", pid);

  if (!error.Success()) {
    LLDB_LOG(log, "encountered an error while trying to stop process {0}. {1}",
             pid, error);
  }
  return error;
}

void DebuggerThread::ContinueAsyncException(ExceptionResult result) {
  if (!m_active_exception.get())
    return;

  Log *log = GetLog(WindowsLog::Process | WindowsLog::Exception);
  LLDB_LOG(log, "broadcasting for inferior process {0}.",
           m_process.GetProcessId());

  m_active_exception.reset();
  m_exception_pred.SetValue(result, eBroadcastAlways);
}

void DebuggerThread::FreeProcessHandles() {
  m_process = HostProcess();
  m_main_thread = HostThread();
  if (m_image_file) {
    ::CloseHandle(m_image_file);
    m_image_file = nullptr;
  }
}

void DebuggerThread::DebugLoop() {
  Log *log = GetLog(WindowsLog::Event);
  DEBUG_EVENT dbe = {};
  bool should_debug = true;
  LLDB_LOGV(log, "Entering WaitForDebugEvent loop");
  while (should_debug) {
    LLDB_LOGV(log, "Calling WaitForDebugEvent");
    BOOL wait_result = WaitForDebugEvent(&dbe, INFINITE);
    if (wait_result) {
      DWORD continue_status = DBG_CONTINUE;
      switch (dbe.dwDebugEventCode) {
      default:
        llvm_unreachable("Unhandle debug event code!");
      case EXCEPTION_DEBUG_EVENT: {
        ExceptionResult status =
            HandleExceptionEvent(dbe.u.Exception, dbe.dwThreadId);

        if (status == ExceptionResult::MaskException)
          continue_status = DBG_CONTINUE;
        else if (status == ExceptionResult::SendToApplication)
          continue_status = DBG_EXCEPTION_NOT_HANDLED;

        break;
      }
      case CREATE_THREAD_DEBUG_EVENT:
        continue_status =
            HandleCreateThreadEvent(dbe.u.CreateThread, dbe.dwThreadId);
        break;
      case CREATE_PROCESS_DEBUG_EVENT:
        continue_status =
            HandleCreateProcessEvent(dbe.u.CreateProcessInfo, dbe.dwThreadId);
        break;
      case EXIT_THREAD_DEBUG_EVENT:
        continue_status =
            HandleExitThreadEvent(dbe.u.ExitThread, dbe.dwThreadId);
        break;
      case EXIT_PROCESS_DEBUG_EVENT:
        continue_status =
            HandleExitProcessEvent(dbe.u.ExitProcess, dbe.dwThreadId);
        should_debug = false;
        break;
      case LOAD_DLL_DEBUG_EVENT:
        continue_status = HandleLoadDllEvent(dbe.u.LoadDll, dbe.dwThreadId);
        break;
      case UNLOAD_DLL_DEBUG_EVENT:
        continue_status = HandleUnloadDllEvent(dbe.u.UnloadDll, dbe.dwThreadId);
        break;
      case OUTPUT_DEBUG_STRING_EVENT:
        continue_status = HandleODSEvent(dbe.u.DebugString, dbe.dwThreadId);
        break;
      case RIP_EVENT:
        continue_status = HandleRipEvent(dbe.u.RipInfo, dbe.dwThreadId);
        if (dbe.u.RipInfo.dwType == SLE_ERROR)
          should_debug = false;
        break;
      }

      LLDB_LOGV(log, "calling ContinueDebugEvent({0}, {1}, {2}) on thread {3}.",
                dbe.dwProcessId, dbe.dwThreadId, continue_status,
                ::GetCurrentThreadId());

      ::ContinueDebugEvent(dbe.dwProcessId, dbe.dwThreadId, continue_status);

      if (m_detached) {
        should_debug = false;
      }
    } else {
      LLDB_LOG(log, "returned FALSE from WaitForDebugEvent.  Error = {0}",
               ::GetLastError());

      should_debug = false;
    }
  }
  FreeProcessHandles();

  LLDB_LOG(log, "WaitForDebugEvent loop completed, exiting.");
  ::SetEvent(m_debugging_ended_event);
}

ExceptionResult
DebuggerThread::HandleExceptionEvent(const EXCEPTION_DEBUG_INFO &info,
                                     DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event | WindowsLog::Exception);
  if (m_is_shutting_down) {
    // A breakpoint that occurs while `m_pid_to_detach` is non-zero is a magic
    // exception that
    // we use simply to wake up the DebuggerThread so that we can close out the
    // debug loop.
    if (m_pid_to_detach != 0 &&
        (info.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT ||
         info.ExceptionRecord.ExceptionCode == STATUS_WX86_BREAKPOINT)) {
      LLDB_LOG(log, "Breakpoint exception is cue to detach from process {0:x}",
               m_pid_to_detach.load());
      ::DebugActiveProcessStop(m_pid_to_detach);
      m_detached = true;
    }

    // Don't perform any blocking operations while we're shutting down.  That
    // will cause TerminateProcess -> WaitForSingleObject to time out.
    return ExceptionResult::SendToApplication;
  }

  bool first_chance = (info.dwFirstChance != 0);

  m_active_exception.reset(
      new ExceptionRecord(info.ExceptionRecord, thread_id));
  LLDB_LOG(log, "encountered {0} chance exception {1:x} on thread {2:x}",
           first_chance ? "first" : "second",
           info.ExceptionRecord.ExceptionCode, thread_id);

  ExceptionResult result =
      m_debug_delegate->OnDebugException(first_chance, *m_active_exception);
  m_exception_pred.SetValue(result, eBroadcastNever);

  LLDB_LOG(log, "waiting for ExceptionPred != BreakInDebugger");
  result = *m_exception_pred.WaitForValueNotEqualTo(
      ExceptionResult::BreakInDebugger);

  LLDB_LOG(log, "got ExceptionPred = {0}", (int)m_exception_pred.GetValue());
  return result;
}

DWORD
DebuggerThread::HandleCreateThreadEvent(const CREATE_THREAD_DEBUG_INFO &info,
                                        DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event | WindowsLog::Thread);
  LLDB_LOG(log, "Thread {0} spawned in process {1}", thread_id,
           m_process.GetProcessId());
  HostThread thread(info.hThread);
  thread.GetNativeThread().SetOwnsHandle(false);
  m_debug_delegate->OnCreateThread(thread);
  return DBG_CONTINUE;
}

DWORD
DebuggerThread::HandleCreateProcessEvent(const CREATE_PROCESS_DEBUG_INFO &info,
                                         DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event | WindowsLog::Process);
  uint32_t process_id = ::GetProcessId(info.hProcess);

  LLDB_LOG(log, "process {0} spawned", process_id);

  std::string thread_name;
  llvm::raw_string_ostream name_stream(thread_name);
  name_stream << "lldb.plugin.process-windows.secondary[" << process_id << "]";
  name_stream.flush();
  llvm::set_thread_name(thread_name);

  // info.hProcess and info.hThread are closed automatically by Windows when
  // EXIT_PROCESS_DEBUG_EVENT is received.
  m_process = HostProcess(info.hProcess);
  ((HostProcessWindows &)m_process.GetNativeProcess()).SetOwnsHandle(false);
  m_main_thread = HostThread(info.hThread);
  m_main_thread.GetNativeThread().SetOwnsHandle(false);
  m_image_file = info.hFile;

  lldb::addr_t load_addr = reinterpret_cast<lldb::addr_t>(info.lpBaseOfImage);
  m_debug_delegate->OnDebuggerConnected(load_addr);

  return DBG_CONTINUE;
}

DWORD
DebuggerThread::HandleExitThreadEvent(const EXIT_THREAD_DEBUG_INFO &info,
                                      DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event | WindowsLog::Thread);
  LLDB_LOG(log, "Thread {0} exited with code {1} in process {2}", thread_id,
           info.dwExitCode, m_process.GetProcessId());
  m_debug_delegate->OnExitThread(thread_id, info.dwExitCode);
  return DBG_CONTINUE;
}

DWORD
DebuggerThread::HandleExitProcessEvent(const EXIT_PROCESS_DEBUG_INFO &info,
                                       DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event | WindowsLog::Thread);
  LLDB_LOG(log, "process {0} exited with code {1}", m_process.GetProcessId(),
           info.dwExitCode);

  m_debug_delegate->OnExitProcess(info.dwExitCode);

  return DBG_CONTINUE;
}

static std::optional<std::string> GetFileNameFromHandleFallback(HANDLE hFile) {
  // Check that file is not empty as we cannot map a file with zero length.
  DWORD dwFileSizeHi = 0;
  DWORD dwFileSizeLo = ::GetFileSize(hFile, &dwFileSizeHi);
  if (dwFileSizeLo == 0 && dwFileSizeHi == 0)
    return std::nullopt;

  AutoHandle filemap(
      ::CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 1, NULL), nullptr);
  if (!filemap.IsValid())
    return std::nullopt;

  auto view_deleter = [](void *pMem) { ::UnmapViewOfFile(pMem); };
  std::unique_ptr<void, decltype(view_deleter)> pMem(
      ::MapViewOfFile(filemap.get(), FILE_MAP_READ, 0, 0, 1), view_deleter);
  if (!pMem)
    return std::nullopt;

  std::array<wchar_t, MAX_PATH + 1> mapped_filename;
  if (!::GetMappedFileNameW(::GetCurrentProcess(), pMem.get(),
                            mapped_filename.data(), mapped_filename.size()))
    return std::nullopt;

  // A series of null-terminated strings, plus an additional null character
  std::array<wchar_t, 512> drive_strings;
  drive_strings[0] = L'\0';
  if (!::GetLogicalDriveStringsW(drive_strings.size(), drive_strings.data()))
    return std::nullopt;

  std::array<wchar_t, 3> drive = {L"_:"};
  for (const wchar_t *it = drive_strings.data(); *it != L'\0';
       it += wcslen(it) + 1) {
    // Copy the drive letter to the template string
    drive[0] = it[0];
    std::array<wchar_t, MAX_PATH> device_name;
    if (::QueryDosDeviceW(drive.data(), device_name.data(),
                          device_name.size())) {
      size_t device_name_len = wcslen(device_name.data());
      if (device_name_len < mapped_filename.size()) {
        bool match = _wcsnicmp(mapped_filename.data(), device_name.data(),
                               device_name_len) == 0;
        if (match && mapped_filename[device_name_len] == L'\\') {
          // Replace device path with its drive letter
          std::wstring rebuilt_path(drive.data());
          rebuilt_path.append(&mapped_filename[device_name_len]);
          std::string path_utf8;
          llvm::convertWideToUTF8(rebuilt_path, path_utf8);
          return path_utf8;
        }
      }
    }
  }
  return std::nullopt;
}

DWORD
DebuggerThread::HandleLoadDllEvent(const LOAD_DLL_DEBUG_INFO &info,
                                   DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event);
  if (info.hFile == nullptr) {
    // Not sure what this is, so just ignore it.
    LLDB_LOG(log, "Warning: Inferior {0} has a NULL file handle, returning...",
             m_process.GetProcessId());
    return DBG_CONTINUE;
  }

  auto on_load_dll = [&](llvm::StringRef path) {
    FileSpec file_spec(path);
    ModuleSpec module_spec(file_spec);
    lldb::addr_t load_addr = reinterpret_cast<lldb::addr_t>(info.lpBaseOfDll);

    LLDB_LOG(log, "Inferior {0} - DLL '{1}' loaded at address {2:x}...",
             m_process.GetProcessId(), path, info.lpBaseOfDll);

    m_debug_delegate->OnLoadDll(module_spec, load_addr);
  };

  std::vector<wchar_t> buffer(1);
  DWORD required_size =
      GetFinalPathNameByHandleW(info.hFile, &buffer[0], 0, VOLUME_NAME_DOS);
  if (required_size > 0) {
    buffer.resize(required_size + 1);
    required_size = GetFinalPathNameByHandleW(info.hFile, &buffer[0],
                                              required_size, VOLUME_NAME_DOS);
    std::string path_str_utf8;
    llvm::convertWideToUTF8(buffer.data(), path_str_utf8);
    llvm::StringRef path_str = path_str_utf8;
    const char *path = path_str.data();
    if (path_str.starts_with("\\\\?\\"))
      path += 4;

    on_load_dll(path);
  } else if (std::optional<std::string> path =
                 GetFileNameFromHandleFallback(info.hFile)) {
    on_load_dll(*path);
  } else {
    LLDB_LOG(
        log,
        "Inferior {0} - Error {1} occurred calling GetFinalPathNameByHandle",
        m_process.GetProcessId(), ::GetLastError());
  }
  // Windows does not automatically close info.hFile, so we need to do it.
  ::CloseHandle(info.hFile);
  return DBG_CONTINUE;
}

DWORD
DebuggerThread::HandleUnloadDllEvent(const UNLOAD_DLL_DEBUG_INFO &info,
                                     DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event);
  LLDB_LOG(log, "process {0} unloading DLL at addr {1:x}.",
           m_process.GetProcessId(), info.lpBaseOfDll);

  m_debug_delegate->OnUnloadDll(
      reinterpret_cast<lldb::addr_t>(info.lpBaseOfDll));
  return DBG_CONTINUE;
}

DWORD
DebuggerThread::HandleODSEvent(const OUTPUT_DEBUG_STRING_INFO &info,
                               DWORD thread_id) {
  return DBG_CONTINUE;
}

DWORD
DebuggerThread::HandleRipEvent(const RIP_INFO &info, DWORD thread_id) {
  Log *log = GetLog(WindowsLog::Event);
  LLDB_LOG(log, "encountered error {0} (type={1}) in process {2} thread {3}",
           info.dwError, info.dwType, m_process.GetProcessId(), thread_id);

  Status error(info.dwError, eErrorTypeWin32);
  m_debug_delegate->OnDebuggerError(error, info.dwType);

  return DBG_CONTINUE;
}
