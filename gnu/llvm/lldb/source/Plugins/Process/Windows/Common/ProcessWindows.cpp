//===-- ProcessWindows.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessWindows.h"

// Windows includes
#include "lldb/Host/windows/windows.h"
#include <psapi.h>

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostNativeProcessBase.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/State.h"

#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include "DebuggerThread.h"
#include "ExceptionRecord.h"
#include "ForwardDecl.h"
#include "LocalDebugDelegate.h"
#include "ProcessWindowsLog.h"
#include "TargetThreadWindows.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ProcessWindows, ProcessWindowsCommon)

namespace {
std::string GetProcessExecutableName(HANDLE process_handle) {
  std::vector<wchar_t> file_name;
  DWORD file_name_size = MAX_PATH; // first guess, not an absolute limit
  DWORD copied = 0;
  do {
    file_name_size *= 2;
    file_name.resize(file_name_size);
    copied = ::GetModuleFileNameExW(process_handle, NULL, file_name.data(),
                                    file_name_size);
  } while (copied >= file_name_size);
  file_name.resize(copied);
  std::string result;
  llvm::convertWideToUTF8(file_name.data(), result);
  return result;
}

std::string GetProcessExecutableName(DWORD pid) {
  std::string file_name;
  HANDLE process_handle =
      ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (process_handle != NULL) {
    file_name = GetProcessExecutableName(process_handle);
    ::CloseHandle(process_handle);
  }
  return file_name;
}
} // anonymous namespace

namespace lldb_private {

ProcessSP ProcessWindows::CreateInstance(lldb::TargetSP target_sp,
                                         lldb::ListenerSP listener_sp,
                                         const FileSpec *,
                                         bool can_connect) {
  return ProcessSP(new ProcessWindows(target_sp, listener_sp));
}

static bool ShouldUseLLDBServer() {
  llvm::StringRef use_lldb_server = ::getenv("LLDB_USE_LLDB_SERVER");
  return use_lldb_server.equals_insensitive("on") ||
         use_lldb_server.equals_insensitive("yes") ||
         use_lldb_server.equals_insensitive("1") ||
         use_lldb_server.equals_insensitive("true");
}

void ProcessWindows::Initialize() {
  if (!ShouldUseLLDBServer()) {
    static llvm::once_flag g_once_flag;

    llvm::call_once(g_once_flag, []() {
      PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                    GetPluginDescriptionStatic(),
                                    CreateInstance);
    });
  }
}

void ProcessWindows::Terminate() {}

llvm::StringRef ProcessWindows::GetPluginDescriptionStatic() {
  return "Process plugin for Windows";
}

// Constructors and destructors.

ProcessWindows::ProcessWindows(lldb::TargetSP target_sp,
                               lldb::ListenerSP listener_sp)
    : lldb_private::Process(target_sp, listener_sp),
      m_watchpoint_ids(
          RegisterContextWindows::GetNumHardwareBreakpointSlots(),
          LLDB_INVALID_BREAK_ID) {}

ProcessWindows::~ProcessWindows() {}

size_t ProcessWindows::GetSTDOUT(char *buf, size_t buf_size, Status &error) {
  error.SetErrorString("GetSTDOUT unsupported on Windows");
  return 0;
}

size_t ProcessWindows::GetSTDERR(char *buf, size_t buf_size, Status &error) {
  error.SetErrorString("GetSTDERR unsupported on Windows");
  return 0;
}

size_t ProcessWindows::PutSTDIN(const char *buf, size_t buf_size,
                                Status &error) {
  error.SetErrorString("PutSTDIN unsupported on Windows");
  return 0;
}

Status ProcessWindows::EnableBreakpointSite(BreakpointSite *bp_site) {
  if (bp_site->HardwareRequired())
    return Status("Hardware breakpoints are not supported.");

  Log *log = GetLog(WindowsLog::Breakpoints);
  LLDB_LOG(log, "bp_site = {0:x}, id={1}, addr={2:x}", bp_site,
           bp_site->GetID(), bp_site->GetLoadAddress());

  Status error = EnableSoftwareBreakpoint(bp_site);
  if (!error.Success())
    LLDB_LOG(log, "error: {0}", error);
  return error;
}

Status ProcessWindows::DisableBreakpointSite(BreakpointSite *bp_site) {
  Log *log = GetLog(WindowsLog::Breakpoints);
  LLDB_LOG(log, "bp_site = {0:x}, id={1}, addr={2:x}", bp_site,
           bp_site->GetID(), bp_site->GetLoadAddress());

  Status error = DisableSoftwareBreakpoint(bp_site);

  if (!error.Success())
    LLDB_LOG(log, "error: {0}", error);
  return error;
}

Status ProcessWindows::DoDetach(bool keep_stopped) {
  Status error;
  Log *log = GetLog(WindowsLog::Process);
  StateType private_state = GetPrivateState();
  if (private_state != eStateExited && private_state != eStateDetached) {
    error = DetachProcess();
    if (error.Success())
      SetPrivateState(eStateDetached);
    else
      LLDB_LOG(log, "Detaching process error: {0}", error);
  } else {
    error.SetErrorStringWithFormatv("error: process {0} in state = {1}, but "
                                    "cannot detach it in this state.",
                                    GetID(), private_state);
    LLDB_LOG(log, "error: {0}", error);
  }
  return error;
}

Status ProcessWindows::DoLaunch(Module *exe_module,
                                ProcessLaunchInfo &launch_info) {
  Status error;
  DebugDelegateSP delegate(new LocalDebugDelegate(shared_from_this()));
  error = LaunchProcess(launch_info, delegate);
  if (error.Success())
    SetID(launch_info.GetProcessID());
  return error;
}

Status
ProcessWindows::DoAttachToProcessWithID(lldb::pid_t pid,
                                        const ProcessAttachInfo &attach_info) {
  DebugDelegateSP delegate(new LocalDebugDelegate(shared_from_this()));
  Status error = AttachProcess(pid, attach_info, delegate);
  if (error.Success())
    SetID(GetDebuggedProcessId());
  return error;
}

Status ProcessWindows::DoResume() {
  Log *log = GetLog(WindowsLog::Process);
  llvm::sys::ScopedLock lock(m_mutex);
  Status error;

  StateType private_state = GetPrivateState();
  if (private_state == eStateStopped || private_state == eStateCrashed) {
    LLDB_LOG(log, "process {0} is in state {1}.  Resuming...",
             m_session_data->m_debugger->GetProcess().GetProcessId(),
             GetPrivateState());

    LLDB_LOG(log, "resuming {0} threads.", m_thread_list.GetSize());

    bool failed = false;
    for (uint32_t i = 0; i < m_thread_list.GetSize(); ++i) {
      auto thread = std::static_pointer_cast<TargetThreadWindows>(
          m_thread_list.GetThreadAtIndex(i));
      Status result = thread->DoResume();
      if (result.Fail()) {
        failed = true;
        LLDB_LOG(
            log,
            "Trying to resume thread at index {0}, but failed with error {1}.",
            i, result);
      }
    }

    if (failed) {
      error.SetErrorString("ProcessWindows::DoResume failed");
    } else {
      SetPrivateState(eStateRunning);
    }

    ExceptionRecordSP active_exception =
        m_session_data->m_debugger->GetActiveException().lock();
    if (active_exception) {
      // Resume the process and continue processing debug events.  Mask the
      // exception so that from the process's view, there is no indication that
      // anything happened.
      m_session_data->m_debugger->ContinueAsyncException(
          ExceptionResult::MaskException);
    }
  } else {
    LLDB_LOG(log, "error: process {0} is in state {1}.  Returning...",
             m_session_data->m_debugger->GetProcess().GetProcessId(),
             GetPrivateState());
  }
  return error;
}

Status ProcessWindows::DoDestroy() {
  StateType private_state = GetPrivateState();
  return DestroyProcess(private_state);
}

Status ProcessWindows::DoHalt(bool &caused_stop) {
  StateType state = GetPrivateState();
  if (state != eStateStopped)
    return HaltProcess(caused_stop);
  caused_stop = false;
  return Status();
}

void ProcessWindows::DidLaunch() {
  ArchSpec arch_spec;
  DidAttach(arch_spec);
}

void ProcessWindows::DidAttach(ArchSpec &arch_spec) {
  llvm::sys::ScopedLock lock(m_mutex);

  // The initial stop won't broadcast the state change event, so account for
  // that here.
  if (m_session_data && GetPrivateState() == eStateStopped &&
      m_session_data->m_stop_at_entry)
    RefreshStateAfterStop();
}

static void
DumpAdditionalExceptionInformation(llvm::raw_ostream &stream,
                                   const ExceptionRecordSP &exception) {
  // Decode additional exception information for specific exception types based
  // on
  // https://docs.microsoft.com/en-us/windows/desktop/api/winnt/ns-winnt-_exception_record

  const int addr_min_width = 2 + 8; // "0x" + 4 address bytes

  const std::vector<ULONG_PTR> &args = exception->GetExceptionArguments();
  switch (exception->GetExceptionCode()) {
  case EXCEPTION_ACCESS_VIOLATION: {
    if (args.size() < 2)
      break;

    stream << ": ";
    const int access_violation_code = args[0];
    const lldb::addr_t access_violation_address = args[1];
    switch (access_violation_code) {
    case 0:
      stream << "Access violation reading";
      break;
    case 1:
      stream << "Access violation writing";
      break;
    case 8:
      stream << "User-mode data execution prevention (DEP) violation at";
      break;
    default:
      stream << "Unknown access violation (code " << access_violation_code
             << ") at";
      break;
    }
    stream << " location "
           << llvm::format_hex(access_violation_address, addr_min_width);
    break;
  }
  case EXCEPTION_IN_PAGE_ERROR: {
    if (args.size() < 3)
      break;

    stream << ": ";
    const int page_load_error_code = args[0];
    const lldb::addr_t page_load_error_address = args[1];
    const DWORD underlying_code = args[2];
    switch (page_load_error_code) {
    case 0:
      stream << "In page error reading";
      break;
    case 1:
      stream << "In page error writing";
      break;
    case 8:
      stream << "User-mode data execution prevention (DEP) violation at";
      break;
    default:
      stream << "Unknown page loading error (code " << page_load_error_code
             << ") at";
      break;
    }
    stream << " location "
           << llvm::format_hex(page_load_error_address, addr_min_width)
           << " (status code " << llvm::format_hex(underlying_code, 8) << ")";
    break;
  }
  }
}

void ProcessWindows::RefreshStateAfterStop() {
  Log *log = GetLog(WindowsLog::Exception);
  llvm::sys::ScopedLock lock(m_mutex);

  if (!m_session_data) {
    LLDB_LOG(log, "no active session.  Returning...");
    return;
  }

  m_thread_list.RefreshStateAfterStop();

  std::weak_ptr<ExceptionRecord> exception_record =
      m_session_data->m_debugger->GetActiveException();
  ExceptionRecordSP active_exception = exception_record.lock();
  if (!active_exception) {
    LLDB_LOG(log,
             "there is no active exception in process {0}.  Why is the "
             "process stopped?",
             m_session_data->m_debugger->GetProcess().GetProcessId());
    return;
  }

  StopInfoSP stop_info;
  m_thread_list.SetSelectedThreadByID(active_exception->GetThreadID());
  ThreadSP stop_thread = m_thread_list.GetSelectedThread();
  if (!stop_thread)
    return;

  switch (active_exception->GetExceptionCode()) {
  case EXCEPTION_SINGLE_STEP: {
    RegisterContextSP register_context = stop_thread->GetRegisterContext();
    const uint64_t pc = register_context->GetPC();
    BreakpointSiteSP site(GetBreakpointSiteList().FindByAddress(pc));
    if (site && site->ValidForThisThread(*stop_thread)) {
      LLDB_LOG(log,
               "Single-stepped onto a breakpoint in process {0} at "
               "address {1:x} with breakpoint site {2}",
               m_session_data->m_debugger->GetProcess().GetProcessId(), pc,
               site->GetID());
      stop_info = StopInfo::CreateStopReasonWithBreakpointSiteID(*stop_thread,
                                                                 site->GetID());
      stop_thread->SetStopInfo(stop_info);

      return;
    }

    auto *reg_ctx = static_cast<RegisterContextWindows *>(
        stop_thread->GetRegisterContext().get());
    uint32_t slot_id = reg_ctx->GetTriggeredHardwareBreakpointSlotId();
    if (slot_id != LLDB_INVALID_INDEX32) {
      int id = m_watchpoint_ids[slot_id];
      LLDB_LOG(log,
               "Single-stepped onto a watchpoint in process {0} at address "
               "{1:x} with watchpoint {2}",
               m_session_data->m_debugger->GetProcess().GetProcessId(), pc, id);

      stop_info = StopInfo::CreateStopReasonWithWatchpointID(*stop_thread, id);
      stop_thread->SetStopInfo(stop_info);

      return;
    }

    LLDB_LOG(log, "single stepping thread {0}", stop_thread->GetID());
    stop_info = StopInfo::CreateStopReasonToTrace(*stop_thread);
    stop_thread->SetStopInfo(stop_info);

    return;
  }

  case EXCEPTION_BREAKPOINT: {
    RegisterContextSP register_context = stop_thread->GetRegisterContext();

    int breakpoint_size = 1;
    switch (GetTarget().GetArchitecture().GetMachine()) {
    case llvm::Triple::aarch64:
      breakpoint_size = 4;
      break;

    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      breakpoint_size = 2;
      break;

    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      breakpoint_size = 1;
      break;

    default:
      LLDB_LOG(log, "Unknown breakpoint size for architecture");
      break;
    }

    // The current PC is AFTER the BP opcode, on all architectures.
    uint64_t pc = register_context->GetPC() - breakpoint_size;

    BreakpointSiteSP site(GetBreakpointSiteList().FindByAddress(pc));
    if (site) {
      LLDB_LOG(log,
               "detected breakpoint in process {0} at address {1:x} with "
               "breakpoint site {2}",
               m_session_data->m_debugger->GetProcess().GetProcessId(), pc,
               site->GetID());

      if (site->ValidForThisThread(*stop_thread)) {
        LLDB_LOG(log,
                 "Breakpoint site {0} is valid for this thread ({1:x}), "
                 "creating stop info.",
                 site->GetID(), stop_thread->GetID());

        stop_info = StopInfo::CreateStopReasonWithBreakpointSiteID(
            *stop_thread, site->GetID());
        register_context->SetPC(pc);
      } else {
        LLDB_LOG(log,
                 "Breakpoint site {0} is not valid for this thread, "
                 "creating empty stop info.",
                 site->GetID());
      }
      stop_thread->SetStopInfo(stop_info);
      return;
    } else {
      // The thread hit a hard-coded breakpoint like an `int 3` or
      // `__debugbreak()`.
      LLDB_LOG(log,
               "No breakpoint site matches for this thread. __debugbreak()?  "
               "Creating stop info with the exception.");
      // FALLTHROUGH:  We'll treat this as a generic exception record in the
      // default case.
      [[fallthrough]];
    }
  }

  default: {
    std::string desc;
    llvm::raw_string_ostream desc_stream(desc);
    desc_stream << "Exception "
                << llvm::format_hex(active_exception->GetExceptionCode(), 8)
                << " encountered at address "
                << llvm::format_hex(active_exception->GetExceptionAddress(), 8);
    DumpAdditionalExceptionInformation(desc_stream, active_exception);

    stop_info = StopInfo::CreateStopReasonWithException(
        *stop_thread, desc_stream.str().c_str());
    stop_thread->SetStopInfo(stop_info);
    LLDB_LOG(log, "{0}", desc_stream.str());
    return;
  }
  }
}

bool ProcessWindows::CanDebug(lldb::TargetSP target_sp,
                              bool plugin_specified_by_name) {
  if (plugin_specified_by_name)
    return true;

  // For now we are just making sure the file exists for a given module
  ModuleSP exe_module_sp(target_sp->GetExecutableModule());
  if (exe_module_sp.get())
    return FileSystem::Instance().Exists(exe_module_sp->GetFileSpec());
  // However, if there is no executable module, we return true since we might
  // be preparing to attach.
  return true;
}

bool ProcessWindows::DoUpdateThreadList(ThreadList &old_thread_list,
                                        ThreadList &new_thread_list) {
  Log *log = GetLog(WindowsLog::Thread);
  // Add all the threads that were previously running and for which we did not
  // detect a thread exited event.
  int new_size = 0;
  int continued_threads = 0;
  int exited_threads = 0;
  int new_threads = 0;

  for (ThreadSP old_thread : old_thread_list.Threads()) {
    lldb::tid_t old_thread_id = old_thread->GetID();
    auto exited_thread_iter =
        m_session_data->m_exited_threads.find(old_thread_id);
    if (exited_thread_iter == m_session_data->m_exited_threads.end()) {
      new_thread_list.AddThread(old_thread);
      ++new_size;
      ++continued_threads;
      LLDB_LOGV(log, "Thread {0} was running and is still running.",
                old_thread_id);
    } else {
      LLDB_LOGV(log, "Thread {0} was running and has exited.", old_thread_id);
      ++exited_threads;
    }
  }

  // Also add all the threads that are new since the last time we broke into
  // the debugger.
  for (const auto &thread_info : m_session_data->m_new_threads) {
    new_thread_list.AddThread(thread_info.second);
    ++new_size;
    ++new_threads;
    LLDB_LOGV(log, "Thread {0} is new since last update.", thread_info.first);
  }

  LLDB_LOG(log, "{0} new threads, {1} old threads, {2} exited threads.",
           new_threads, continued_threads, exited_threads);

  m_session_data->m_new_threads.clear();
  m_session_data->m_exited_threads.clear();

  return new_size > 0;
}

bool ProcessWindows::IsAlive() {
  StateType state = GetPrivateState();
  switch (state) {
  case eStateCrashed:
  case eStateDetached:
  case eStateUnloaded:
  case eStateExited:
  case eStateInvalid:
    return false;
  default:
    return true;
  }
}

ArchSpec ProcessWindows::GetSystemArchitecture() {
  return HostInfo::GetArchitecture();
}

size_t ProcessWindows::DoReadMemory(lldb::addr_t vm_addr, void *buf,
                                    size_t size, Status &error) {
  size_t bytes_read = 0;
  error = ProcessDebugger::ReadMemory(vm_addr, buf, size, bytes_read);
  return bytes_read;
}

size_t ProcessWindows::DoWriteMemory(lldb::addr_t vm_addr, const void *buf,
                                     size_t size, Status &error) {
  size_t bytes_written = 0;
  error = ProcessDebugger::WriteMemory(vm_addr, buf, size, bytes_written);
  return bytes_written;
}

lldb::addr_t ProcessWindows::DoAllocateMemory(size_t size, uint32_t permissions,
                                              Status &error) {
  lldb::addr_t vm_addr = LLDB_INVALID_ADDRESS;
  error = ProcessDebugger::AllocateMemory(size, permissions, vm_addr);
  return vm_addr;
}

Status ProcessWindows::DoDeallocateMemory(lldb::addr_t ptr) {
  return ProcessDebugger::DeallocateMemory(ptr);
}

Status ProcessWindows::DoGetMemoryRegionInfo(lldb::addr_t vm_addr,
                                             MemoryRegionInfo &info) {
  return ProcessDebugger::GetMemoryRegionInfo(vm_addr, info);
}

lldb::addr_t ProcessWindows::GetImageInfoAddress() {
  Target &target = GetTarget();
  ObjectFile *obj_file = target.GetExecutableModule()->GetObjectFile();
  Address addr = obj_file->GetImageInfoAddress(&target);
  if (addr.IsValid())
    return addr.GetLoadAddress(&target);
  else
    return LLDB_INVALID_ADDRESS;
}

DynamicLoaderWindowsDYLD *ProcessWindows::GetDynamicLoader() {
  if (m_dyld_up.get() == NULL)
    m_dyld_up.reset(DynamicLoader::FindPlugin(
        this, DynamicLoaderWindowsDYLD::GetPluginNameStatic()));
  return static_cast<DynamicLoaderWindowsDYLD *>(m_dyld_up.get());
}

void ProcessWindows::OnExitProcess(uint32_t exit_code) {
  // No need to acquire the lock since m_session_data isn't accessed.
  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "Process {0} exited with code {1}", GetID(), exit_code);

  TargetSP target = CalculateTarget();
  if (target) {
    ModuleSP executable_module = target->GetExecutableModule();
    ModuleList unloaded_modules;
    unloaded_modules.Append(executable_module);
    target->ModulesDidUnload(unloaded_modules, true);
  }

  SetProcessExitStatus(GetID(), true, 0, exit_code);
  SetPrivateState(eStateExited);

  ProcessDebugger::OnExitProcess(exit_code);
}

void ProcessWindows::OnDebuggerConnected(lldb::addr_t image_base) {
  DebuggerThreadSP debugger = m_session_data->m_debugger;
  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "Debugger connected to process {0}.  Image base = {1:x}",
           debugger->GetProcess().GetProcessId(), image_base);

  ModuleSP module;
  // During attach, we won't have the executable module, so find it now.
  const DWORD pid = debugger->GetProcess().GetProcessId();
  const std::string file_name = GetProcessExecutableName(pid);
  if (file_name.empty()) {
    return;
  }

  FileSpec executable_file(file_name);
  FileSystem::Instance().Resolve(executable_file);
  ModuleSpec module_spec(executable_file);
  Status error;
  module =
      GetTarget().GetOrCreateModule(module_spec, true /* notify */, &error);
  if (!module) {
    return;
  }

  GetTarget().SetExecutableModule(module, eLoadDependentsNo);

  if (auto dyld = GetDynamicLoader())
    dyld->OnLoadModule(module, ModuleSpec(), image_base);

  // Add the main executable module to the list of pending module loads.  We
  // can't call GetTarget().ModulesDidLoad() here because we still haven't
  // returned from DoLaunch() / DoAttach() yet so the target may not have set
  // the process instance to `this` yet.
  llvm::sys::ScopedLock lock(m_mutex);

  const HostThread &host_main_thread = debugger->GetMainThread();
  ThreadSP main_thread =
      std::make_shared<TargetThreadWindows>(*this, host_main_thread);

  tid_t id = host_main_thread.GetNativeThread().GetThreadId();
  main_thread->SetID(id);

  m_session_data->m_new_threads[id] = main_thread;
}

ExceptionResult
ProcessWindows::OnDebugException(bool first_chance,
                                 const ExceptionRecord &record) {
  Log *log = GetLog(WindowsLog::Exception);
  llvm::sys::ScopedLock lock(m_mutex);

  // FIXME: Without this check, occasionally when running the test suite there
  // is
  // an issue where m_session_data can be null.  It's not clear how this could
  // happen but it only surfaces while running the test suite.  In order to
  // properly diagnose this, we probably need to first figure allow the test
  // suite to print out full lldb logs, and then add logging to the process
  // plugin.
  if (!m_session_data) {
    LLDB_LOG(log,
             "Debugger thread reported exception {0:x} at address {1:x}, "
             "but there is no session.",
             record.GetExceptionCode(), record.GetExceptionAddress());
    return ExceptionResult::SendToApplication;
  }

  if (!first_chance) {
    // Not any second chance exception is an application crash by definition.
    // It may be an expression evaluation crash.
    SetPrivateState(eStateStopped);
  }

  ExceptionResult result = ExceptionResult::SendToApplication;
  switch (record.GetExceptionCode()) {
  case EXCEPTION_BREAKPOINT:
    // Handle breakpoints at the first chance.
    result = ExceptionResult::BreakInDebugger;

    if (!m_session_data->m_initial_stop_received) {
      LLDB_LOG(
          log,
          "Hit loader breakpoint at address {0:x}, setting initial stop event.",
          record.GetExceptionAddress());
      m_session_data->m_initial_stop_received = true;
      ::SetEvent(m_session_data->m_initial_stop_event);
    } else {
      LLDB_LOG(log, "Hit non-loader breakpoint at address {0:x}.",
               record.GetExceptionAddress());
    }
    SetPrivateState(eStateStopped);
    break;
  case EXCEPTION_SINGLE_STEP:
    result = ExceptionResult::BreakInDebugger;
    SetPrivateState(eStateStopped);
    break;
  default:
    LLDB_LOG(log,
             "Debugger thread reported exception {0:x} at address {1:x} "
             "(first_chance={2})",
             record.GetExceptionCode(), record.GetExceptionAddress(),
             first_chance);
    // For non-breakpoints, give the application a chance to handle the
    // exception first.
    if (first_chance)
      result = ExceptionResult::SendToApplication;
    else
      result = ExceptionResult::BreakInDebugger;
  }

  return result;
}

void ProcessWindows::OnCreateThread(const HostThread &new_thread) {
  llvm::sys::ScopedLock lock(m_mutex);

  ThreadSP thread = std::make_shared<TargetThreadWindows>(*this, new_thread);

  const HostNativeThread &native_new_thread = new_thread.GetNativeThread();
  tid_t id = native_new_thread.GetThreadId();
  thread->SetID(id);

  m_session_data->m_new_threads[id] = thread;

  for (const std::map<int, WatchpointInfo>::value_type &p : m_watchpoints) {
    auto *reg_ctx = static_cast<RegisterContextWindows *>(
        thread->GetRegisterContext().get());
    reg_ctx->AddHardwareBreakpoint(p.second.slot_id, p.second.address,
                                   p.second.size, p.second.read,
                                   p.second.write);
  }
}

void ProcessWindows::OnExitThread(lldb::tid_t thread_id, uint32_t exit_code) {
  llvm::sys::ScopedLock lock(m_mutex);

  // On a forced termination, we may get exit thread events after the session
  // data has been cleaned up.
  if (!m_session_data)
    return;

  // A thread may have started and exited before the debugger stopped allowing a
  // refresh.
  // Just remove it from the new threads list in that case.
  auto iter = m_session_data->m_new_threads.find(thread_id);
  if (iter != m_session_data->m_new_threads.end())
    m_session_data->m_new_threads.erase(iter);
  else
    m_session_data->m_exited_threads.insert(thread_id);
}

void ProcessWindows::OnLoadDll(const ModuleSpec &module_spec,
                               lldb::addr_t module_addr) {
  if (auto dyld = GetDynamicLoader())
    dyld->OnLoadModule(nullptr, module_spec, module_addr);
}

void ProcessWindows::OnUnloadDll(lldb::addr_t module_addr) {
  if (auto dyld = GetDynamicLoader())
    dyld->OnUnloadModule(module_addr);
}

void ProcessWindows::OnDebugString(const std::string &string) {}

void ProcessWindows::OnDebuggerError(const Status &error, uint32_t type) {
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
    LLDB_LOG(
        log,
        "Error {0} occurred launching the process before the initial stop. {1}",
        error.GetError(), error);
    return;
  }
}

std::optional<uint32_t> ProcessWindows::GetWatchpointSlotCount() {
  return RegisterContextWindows::GetNumHardwareBreakpointSlots();
}

Status ProcessWindows::EnableWatchpoint(WatchpointSP wp_sp, bool notify) {
  Status error;

  if (wp_sp->IsEnabled()) {
    wp_sp->SetEnabled(true, notify);
    return error;
  }

  WatchpointInfo info;
  for (info.slot_id = 0;
       info.slot_id < RegisterContextWindows::GetNumHardwareBreakpointSlots();
       info.slot_id++)
    if (m_watchpoint_ids[info.slot_id] == LLDB_INVALID_BREAK_ID)
      break;
  if (info.slot_id == RegisterContextWindows::GetNumHardwareBreakpointSlots()) {
    error.SetErrorStringWithFormat("Can't find free slot for watchpoint %i",
                                   wp_sp->GetID());
    return error;
  }
  info.address = wp_sp->GetLoadAddress();
  info.size = wp_sp->GetByteSize();
  info.read = wp_sp->WatchpointRead();
  info.write = wp_sp->WatchpointWrite() || wp_sp->WatchpointModify();

  for (unsigned i = 0U; i < m_thread_list.GetSize(); i++) {
    Thread *thread = m_thread_list.GetThreadAtIndex(i).get();
    auto *reg_ctx = static_cast<RegisterContextWindows *>(
        thread->GetRegisterContext().get());
    if (!reg_ctx->AddHardwareBreakpoint(info.slot_id, info.address, info.size,
                                        info.read, info.write)) {
      error.SetErrorStringWithFormat(
          "Can't enable watchpoint %i on thread 0x%llx", wp_sp->GetID(),
          thread->GetID());
      break;
    }
  }
  if (error.Fail()) {
    for (unsigned i = 0U; i < m_thread_list.GetSize(); i++) {
      Thread *thread = m_thread_list.GetThreadAtIndex(i).get();
      auto *reg_ctx = static_cast<RegisterContextWindows *>(
          thread->GetRegisterContext().get());
      reg_ctx->RemoveHardwareBreakpoint(info.slot_id);
    }
    return error;
  }

  m_watchpoints[wp_sp->GetID()] = info;
  m_watchpoint_ids[info.slot_id] = wp_sp->GetID();

  wp_sp->SetEnabled(true, notify);

  return error;
}

Status ProcessWindows::DisableWatchpoint(WatchpointSP wp_sp, bool notify) {
  Status error;

  if (!wp_sp->IsEnabled()) {
    wp_sp->SetEnabled(false, notify);
    return error;
  }

  auto it = m_watchpoints.find(wp_sp->GetID());
  if (it == m_watchpoints.end()) {
    error.SetErrorStringWithFormat("Info about watchpoint %i is not found",
                                   wp_sp->GetID());
    return error;
  }

  for (unsigned i = 0U; i < m_thread_list.GetSize(); i++) {
    Thread *thread = m_thread_list.GetThreadAtIndex(i).get();
    auto *reg_ctx = static_cast<RegisterContextWindows *>(
        thread->GetRegisterContext().get());
    if (!reg_ctx->RemoveHardwareBreakpoint(it->second.slot_id)) {
      error.SetErrorStringWithFormat(
          "Can't disable watchpoint %i on thread 0x%llx", wp_sp->GetID(),
          thread->GetID());
      break;
    }
  }
  if (error.Fail())
    return error;

  m_watchpoint_ids[it->second.slot_id] = LLDB_INVALID_BREAK_ID;
  m_watchpoints.erase(it);

  wp_sp->SetEnabled(false, notify);

  return error;
}
} // namespace lldb_private
