//===-- ProcessKDP.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdlib>

#include <memory>
#include <mutex>

#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/common/TCPSocket.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionGroupString.h"
#include "lldb/Interpreter/OptionGroupUInt64.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StringExtractor.h"
#include "lldb/Utility/UUID.h"

#include "llvm/Support/Threading.h"

#define USEC_PER_SEC 1000000

#include "Plugins/DynamicLoader/Darwin-Kernel/DynamicLoaderDarwinKernel.h"
#include "Plugins/DynamicLoader/Static/DynamicLoaderStatic.h"
#include "ProcessKDP.h"
#include "ProcessKDPLog.h"
#include "ThreadKDP.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ProcessKDP, ProcessMacOSXKernel)

namespace {

#define LLDB_PROPERTIES_processkdp
#include "ProcessKDPProperties.inc"

enum {
#define LLDB_PROPERTIES_processkdp
#include "ProcessKDPPropertiesEnum.inc"
};

class PluginProperties : public Properties {
public:
  static llvm::StringRef GetSettingName() {
    return ProcessKDP::GetPluginNameStatic();
  }

  PluginProperties() : Properties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
    m_collection_sp->Initialize(g_processkdp_properties);
  }

  ~PluginProperties() override = default;

  uint64_t GetPacketTimeout() {
    const uint32_t idx = ePropertyKDPPacketTimeout;
    return GetPropertyAtIndexAs<uint64_t>(
        idx, g_processkdp_properties[idx].default_uint_value);
  }
};

} // namespace

static PluginProperties &GetGlobalPluginProperties() {
  static PluginProperties g_settings;
  return g_settings;
}

static const lldb::tid_t g_kernel_tid = 1;

llvm::StringRef ProcessKDP::GetPluginDescriptionStatic() {
  return "KDP Remote protocol based debugging plug-in for darwin kernel "
         "debugging.";
}

void ProcessKDP::Terminate() {
  PluginManager::UnregisterPlugin(ProcessKDP::CreateInstance);
}

lldb::ProcessSP ProcessKDP::CreateInstance(TargetSP target_sp,
                                           ListenerSP listener_sp,
                                           const FileSpec *crash_file_path,
                                           bool can_connect) {
  lldb::ProcessSP process_sp;
  if (crash_file_path == NULL)
    process_sp = std::make_shared<ProcessKDP>(target_sp, listener_sp);
  return process_sp;
}

bool ProcessKDP::CanDebug(TargetSP target_sp, bool plugin_specified_by_name) {
  if (plugin_specified_by_name)
    return true;

  // For now we are just making sure the file exists for a given module
  Module *exe_module = target_sp->GetExecutableModulePointer();
  if (exe_module) {
    const llvm::Triple &triple_ref = target_sp->GetArchitecture().GetTriple();
    switch (triple_ref.getOS()) {
    case llvm::Triple::Darwin: // Should use "macosx" for desktop and "ios" for
                               // iOS, but accept darwin just in case
    case llvm::Triple::MacOSX: // For desktop targets
    case llvm::Triple::IOS:    // For arm targets
    case llvm::Triple::TvOS:
    case llvm::Triple::WatchOS:
    case llvm::Triple::XROS:
      if (triple_ref.getVendor() == llvm::Triple::Apple) {
        ObjectFile *exe_objfile = exe_module->GetObjectFile();
        if (exe_objfile->GetType() == ObjectFile::eTypeExecutable &&
            exe_objfile->GetStrata() == ObjectFile::eStrataKernel)
          return true;
      }
      break;

    default:
      break;
    }
  }
  return false;
}

// ProcessKDP constructor
ProcessKDP::ProcessKDP(TargetSP target_sp, ListenerSP listener_sp)
    : Process(target_sp, listener_sp),
      m_comm("lldb.process.kdp-remote.communication"),
      m_async_broadcaster(NULL, "lldb.process.kdp-remote.async-broadcaster"),
      m_kernel_load_addr(LLDB_INVALID_ADDRESS), m_command_sp(),
      m_kernel_thread_wp() {
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncThreadShouldExit,
                                   "async thread should exit");
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncContinue,
                                   "async thread continue");
  const uint64_t timeout_seconds =
      GetGlobalPluginProperties().GetPacketTimeout();
  if (timeout_seconds > 0)
    m_comm.SetPacketTimeout(std::chrono::seconds(timeout_seconds));
}

// Destructor
ProcessKDP::~ProcessKDP() {
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize(true /* destructing */);
}

Status ProcessKDP::DoWillLaunch(Module *module) {
  Status error;
  error.SetErrorString("launching not supported in kdp-remote plug-in");
  return error;
}

Status ProcessKDP::DoWillAttachToProcessWithID(lldb::pid_t pid) {
  Status error;
  error.SetErrorString(
      "attaching to a by process ID not supported in kdp-remote plug-in");
  return error;
}

Status ProcessKDP::DoWillAttachToProcessWithName(const char *process_name,
                                                 bool wait_for_launch) {
  Status error;
  error.SetErrorString(
      "attaching to a by process name not supported in kdp-remote plug-in");
  return error;
}

bool ProcessKDP::GetHostArchitecture(ArchSpec &arch) {
  uint32_t cpu = m_comm.GetCPUType();
  if (cpu) {
    uint32_t sub = m_comm.GetCPUSubtype();
    arch.SetArchitecture(eArchTypeMachO, cpu, sub);
    // Leave architecture vendor as unspecified unknown
    arch.GetTriple().setVendor(llvm::Triple::UnknownVendor);
    arch.GetTriple().setVendorName(llvm::StringRef());
    return true;
  }
  arch.Clear();
  return false;
}

Status ProcessKDP::DoConnectRemote(llvm::StringRef remote_url) {
  Status error;

  // Don't let any JIT happen when doing KDP as we can't allocate memory and we
  // don't want to be mucking with threads that might already be handling
  // exceptions
  SetCanJIT(false);

  if (remote_url.empty()) {
    error.SetErrorStringWithFormat("empty connection URL");
    return error;
  }

  std::unique_ptr<ConnectionFileDescriptor> conn_up(
      new ConnectionFileDescriptor());
  if (conn_up) {
    // Only try once for now.
    // TODO: check if we should be retrying?
    const uint32_t max_retry_count = 1;
    for (uint32_t retry_count = 0; retry_count < max_retry_count;
         ++retry_count) {
      if (conn_up->Connect(remote_url, &error) == eConnectionStatusSuccess)
        break;
      usleep(100000);
    }
  }

  if (conn_up->IsConnected()) {
    const TCPSocket &socket =
        static_cast<const TCPSocket &>(*conn_up->GetReadObject());
    const uint16_t reply_port = socket.GetLocalPortNumber();

    if (reply_port != 0) {
      m_comm.SetConnection(std::move(conn_up));

      if (m_comm.SendRequestReattach(reply_port)) {
        if (m_comm.SendRequestConnect(reply_port, reply_port,
                                      "Greetings from LLDB...")) {
          m_comm.GetVersion();

          Target &target = GetTarget();
          ArchSpec kernel_arch;
          // The host architecture
          GetHostArchitecture(kernel_arch);
          ArchSpec target_arch = target.GetArchitecture();
          // Merge in any unspecified stuff into the target architecture in
          // case the target arch isn't set at all or incompletely.
          target_arch.MergeFrom(kernel_arch);
          target.SetArchitecture(target_arch);

          /* Get the kernel's UUID and load address via KDP_KERNELVERSION
           * packet.  */
          /* An EFI kdp session has neither UUID nor load address. */

          UUID kernel_uuid = m_comm.GetUUID();
          addr_t kernel_load_addr = m_comm.GetLoadAddress();

          if (m_comm.RemoteIsEFI()) {
            // Select an invalid plugin name for the dynamic loader so one
            // doesn't get used since EFI does its own manual loading via
            // python scripting
            m_dyld_plugin_name = "none";

            if (kernel_uuid.IsValid()) {
              // If EFI passed in a UUID= try to lookup UUID The slide will not
              // be provided. But the UUID lookup will be used to launch EFI
              // debug scripts from the dSYM, that can load all of the symbols.
              ModuleSpec module_spec;
              module_spec.GetUUID() = kernel_uuid;
              module_spec.GetArchitecture() = target.GetArchitecture();

              // Lookup UUID locally, before attempting dsymForUUID like action
              FileSpecList search_paths =
                  Target::GetDefaultDebugFileSearchPaths();
              module_spec.GetSymbolFileSpec() =
                  PluginManager::LocateExecutableSymbolFile(module_spec,
                                                            search_paths);
              if (module_spec.GetSymbolFileSpec()) {
                ModuleSpec executable_module_spec =
                    PluginManager::LocateExecutableObjectFile(module_spec);
                if (FileSystem::Instance().Exists(
                        executable_module_spec.GetFileSpec())) {
                  module_spec.GetFileSpec() =
                      executable_module_spec.GetFileSpec();
                }
              }
              if (!module_spec.GetSymbolFileSpec() ||
                  !module_spec.GetSymbolFileSpec()) {
                Status symbl_error;
                PluginManager::DownloadObjectAndSymbolFile(module_spec,
                                                           symbl_error, true);
              }

              if (FileSystem::Instance().Exists(module_spec.GetFileSpec())) {
                ModuleSP module_sp(new Module(module_spec));
                if (module_sp.get() && module_sp->GetObjectFile()) {
                  // Get the current target executable
                  ModuleSP exe_module_sp(target.GetExecutableModule());

                  // Make sure you don't already have the right module loaded
                  // and they will be uniqued
                  if (exe_module_sp.get() != module_sp.get())
                    target.SetExecutableModule(module_sp, eLoadDependentsNo);
                }
              }
            }
          } else if (m_comm.RemoteIsDarwinKernel()) {
            m_dyld_plugin_name =
                DynamicLoaderDarwinKernel::GetPluginNameStatic();
            if (kernel_load_addr != LLDB_INVALID_ADDRESS) {
              m_kernel_load_addr = kernel_load_addr;
            }
          }

          // Set the thread ID
          UpdateThreadListIfNeeded();
          SetID(1);
          GetThreadList();
          SetPrivateState(eStateStopped);
          StreamSP async_strm_sp(target.GetDebugger().GetAsyncOutputStream());
          if (async_strm_sp) {
            const char *cstr;
            if ((cstr = m_comm.GetKernelVersion()) != NULL) {
              async_strm_sp->Printf("Version: %s\n", cstr);
              async_strm_sp->Flush();
            }
            //                      if ((cstr = m_comm.GetImagePath ()) != NULL)
            //                      {
            //                          async_strm_sp->Printf ("Image Path:
            //                          %s\n", cstr);
            //                          async_strm_sp->Flush();
            //                      }
          }
        } else {
          error.SetErrorString("KDP_REATTACH failed");
        }
      } else {
        error.SetErrorString("KDP_REATTACH failed");
      }
    } else {
      error.SetErrorString("invalid reply port from UDP connection");
    }
  } else {
    if (error.Success())
      error.SetErrorStringWithFormat("failed to connect to '%s'",
                                     remote_url.str().c_str());
  }
  if (error.Fail())
    m_comm.Disconnect();

  return error;
}

// Process Control
Status ProcessKDP::DoLaunch(Module *exe_module,
                            ProcessLaunchInfo &launch_info) {
  Status error;
  error.SetErrorString("launching not supported in kdp-remote plug-in");
  return error;
}

Status
ProcessKDP::DoAttachToProcessWithID(lldb::pid_t attach_pid,
                                    const ProcessAttachInfo &attach_info) {
  Status error;
  error.SetErrorString(
      "attach to process by ID is not supported in kdp remote debugging");
  return error;
}

Status
ProcessKDP::DoAttachToProcessWithName(const char *process_name,
                                      const ProcessAttachInfo &attach_info) {
  Status error;
  error.SetErrorString(
      "attach to process by name is not supported in kdp remote debugging");
  return error;
}

void ProcessKDP::DidAttach(ArchSpec &process_arch) {
  Process::DidAttach(process_arch);

  Log *log = GetLog(KDPLog::Process);
  LLDB_LOGF(log, "ProcessKDP::DidAttach()");
  if (GetID() != LLDB_INVALID_PROCESS_ID) {
    GetHostArchitecture(process_arch);
  }
}

addr_t ProcessKDP::GetImageInfoAddress() { return m_kernel_load_addr; }

lldb_private::DynamicLoader *ProcessKDP::GetDynamicLoader() {
  if (m_dyld_up.get() == NULL)
    m_dyld_up.reset(DynamicLoader::FindPlugin(this, m_dyld_plugin_name));
  return m_dyld_up.get();
}

Status ProcessKDP::WillResume() { return Status(); }

Status ProcessKDP::DoResume() {
  Status error;
  Log *log = GetLog(KDPLog::Process);
  // Only start the async thread if we try to do any process control
  if (!m_async_thread.IsJoinable())
    StartAsyncThread();

  bool resume = false;

  // With KDP there is only one thread we can tell what to do
  ThreadSP kernel_thread_sp(m_thread_list.FindThreadByProtocolID(g_kernel_tid));

  if (kernel_thread_sp) {
    const StateType thread_resume_state =
        kernel_thread_sp->GetTemporaryResumeState();

    LLDB_LOGF(log, "ProcessKDP::DoResume() thread_resume_state = %s",
              StateAsCString(thread_resume_state));
    switch (thread_resume_state) {
    case eStateSuspended:
      // Nothing to do here when a thread will stay suspended we just leave the
      // CPU mask bit set to zero for the thread
      LLDB_LOGF(log, "ProcessKDP::DoResume() = suspended???");
      break;

    case eStateStepping: {
      lldb::RegisterContextSP reg_ctx_sp(
          kernel_thread_sp->GetRegisterContext());

      if (reg_ctx_sp) {
        LLDB_LOGF(
            log,
            "ProcessKDP::DoResume () reg_ctx_sp->HardwareSingleStep (true);");
        reg_ctx_sp->HardwareSingleStep(true);
        resume = true;
      } else {
        error.SetErrorStringWithFormat(
            "KDP thread 0x%llx has no register context",
            kernel_thread_sp->GetID());
      }
    } break;

    case eStateRunning: {
      lldb::RegisterContextSP reg_ctx_sp(
          kernel_thread_sp->GetRegisterContext());

      if (reg_ctx_sp) {
        LLDB_LOGF(log, "ProcessKDP::DoResume () reg_ctx_sp->HardwareSingleStep "
                       "(false);");
        reg_ctx_sp->HardwareSingleStep(false);
        resume = true;
      } else {
        error.SetErrorStringWithFormat(
            "KDP thread 0x%llx has no register context",
            kernel_thread_sp->GetID());
      }
    } break;

    default:
      // The only valid thread resume states are listed above
      llvm_unreachable("invalid thread resume state");
    }
  }

  if (resume) {
    LLDB_LOGF(log, "ProcessKDP::DoResume () sending resume");

    if (m_comm.SendRequestResume()) {
      m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue);
      SetPrivateState(eStateRunning);
    } else
      error.SetErrorString("KDP resume failed");
  } else {
    error.SetErrorString("kernel thread is suspended");
  }

  return error;
}

lldb::ThreadSP ProcessKDP::GetKernelThread() {
  // KDP only tells us about one thread/core. Any other threads will usually
  // be the ones that are read from memory by the OS plug-ins.

  ThreadSP thread_sp(m_kernel_thread_wp.lock());
  if (!thread_sp) {
    thread_sp = std::make_shared<ThreadKDP>(*this, g_kernel_tid);
    m_kernel_thread_wp = thread_sp;
  }
  return thread_sp;
}

bool ProcessKDP::DoUpdateThreadList(ThreadList &old_thread_list,
                                    ThreadList &new_thread_list) {
  // locker will keep a mutex locked until it goes out of scope
  Log *log = GetLog(KDPLog::Thread);
  LLDB_LOGV(log, "pid = {0}", GetID());

  // Even though there is a CPU mask, it doesn't mean we can see each CPU
  // individually, there is really only one. Lets call this thread 1.
  ThreadSP thread_sp(
      old_thread_list.FindThreadByProtocolID(g_kernel_tid, false));
  if (!thread_sp)
    thread_sp = GetKernelThread();
  new_thread_list.AddThread(thread_sp);

  return new_thread_list.GetSize(false) > 0;
}

void ProcessKDP::RefreshStateAfterStop() {
  // Let all threads recover from stopping and do any clean up based on the
  // previous thread state (if any).
  m_thread_list.RefreshStateAfterStop();
}

Status ProcessKDP::DoHalt(bool &caused_stop) {
  Status error;

  if (m_comm.IsRunning()) {
    if (m_destroy_in_process) {
      // If we are attempting to destroy, we need to not return an error to Halt
      // or DoDestroy won't get called. We are also currently running, so send
      // a process stopped event
      SetPrivateState(eStateStopped);
    } else {
      error.SetErrorString("KDP cannot interrupt a running kernel");
    }
  }
  return error;
}

Status ProcessKDP::DoDetach(bool keep_stopped) {
  Status error;
  Log *log = GetLog(KDPLog::Process);
  LLDB_LOGF(log, "ProcessKDP::DoDetach(keep_stopped = %i)", keep_stopped);

  if (m_comm.IsRunning()) {
    // We are running and we can't interrupt a running kernel, so we need to
    // just close the connection to the kernel and hope for the best
  } else {
    // If we are going to keep the target stopped, then don't send the
    // disconnect message.
    if (!keep_stopped && m_comm.IsConnected()) {
      const bool success = m_comm.SendRequestDisconnect();
      if (log) {
        if (success)
          log->PutCString(
              "ProcessKDP::DoDetach() detach packet sent successfully");
        else
          log->PutCString(
              "ProcessKDP::DoDetach() connection channel shutdown failed");
      }
      m_comm.Disconnect();
    }
  }
  StopAsyncThread();
  m_comm.Clear();

  SetPrivateState(eStateDetached);
  ResumePrivateStateThread();

  // KillDebugserverProcess ();
  return error;
}

Status ProcessKDP::DoDestroy() {
  // For KDP there really is no difference between destroy and detach
  bool keep_stopped = false;
  return DoDetach(keep_stopped);
}

// Process Queries

bool ProcessKDP::IsAlive() {
  return m_comm.IsConnected() && Process::IsAlive();
}

// Process Memory
size_t ProcessKDP::DoReadMemory(addr_t addr, void *buf, size_t size,
                                Status &error) {
  uint8_t *data_buffer = (uint8_t *)buf;
  if (m_comm.IsConnected()) {
    const size_t max_read_size = 512;
    size_t total_bytes_read = 0;

    // Read the requested amount of memory in 512 byte chunks
    while (total_bytes_read < size) {
      size_t bytes_to_read_this_request = size - total_bytes_read;
      if (bytes_to_read_this_request > max_read_size) {
        bytes_to_read_this_request = max_read_size;
      }
      size_t bytes_read = m_comm.SendRequestReadMemory(
          addr + total_bytes_read, data_buffer + total_bytes_read,
          bytes_to_read_this_request, error);
      total_bytes_read += bytes_read;
      if (error.Fail() || bytes_read == 0) {
        return total_bytes_read;
      }
    }

    return total_bytes_read;
  }
  error.SetErrorString("not connected");
  return 0;
}

size_t ProcessKDP::DoWriteMemory(addr_t addr, const void *buf, size_t size,
                                 Status &error) {
  if (m_comm.IsConnected())
    return m_comm.SendRequestWriteMemory(addr, buf, size, error);
  error.SetErrorString("not connected");
  return 0;
}

lldb::addr_t ProcessKDP::DoAllocateMemory(size_t size, uint32_t permissions,
                                          Status &error) {
  error.SetErrorString(
      "memory allocation not supported in kdp remote debugging");
  return LLDB_INVALID_ADDRESS;
}

Status ProcessKDP::DoDeallocateMemory(lldb::addr_t addr) {
  Status error;
  error.SetErrorString(
      "memory deallocation not supported in kdp remote debugging");
  return error;
}

Status ProcessKDP::EnableBreakpointSite(BreakpointSite *bp_site) {
  if (bp_site->HardwareRequired())
    return Status("Hardware breakpoints are not supported.");

  if (m_comm.LocalBreakpointsAreSupported()) {
    Status error;
    if (!bp_site->IsEnabled()) {
      if (m_comm.SendRequestBreakpoint(true, bp_site->GetLoadAddress())) {
        bp_site->SetEnabled(true);
        bp_site->SetType(BreakpointSite::eExternal);
      } else {
        error.SetErrorString("KDP set breakpoint failed");
      }
    }
    return error;
  }
  return EnableSoftwareBreakpoint(bp_site);
}

Status ProcessKDP::DisableBreakpointSite(BreakpointSite *bp_site) {
  if (m_comm.LocalBreakpointsAreSupported()) {
    Status error;
    if (bp_site->IsEnabled()) {
      BreakpointSite::Type bp_type = bp_site->GetType();
      if (bp_type == BreakpointSite::eExternal) {
        if (m_destroy_in_process && m_comm.IsRunning()) {
          // We are trying to destroy our connection and we are running
          bp_site->SetEnabled(false);
        } else {
          if (m_comm.SendRequestBreakpoint(false, bp_site->GetLoadAddress()))
            bp_site->SetEnabled(false);
          else
            error.SetErrorString("KDP remove breakpoint failed");
        }
      } else {
        error = DisableSoftwareBreakpoint(bp_site);
      }
    }
    return error;
  }
  return DisableSoftwareBreakpoint(bp_site);
}

void ProcessKDP::Clear() { m_thread_list.Clear(); }

Status ProcessKDP::DoSignal(int signo) {
  Status error;
  error.SetErrorString(
      "sending signals is not supported in kdp remote debugging");
  return error;
}

void ProcessKDP::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance,
                                  DebuggerInitialize);

    ProcessKDPLog::Initialize();
  });
}

void ProcessKDP::DebuggerInitialize(lldb_private::Debugger &debugger) {
  if (!PluginManager::GetSettingForProcessPlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForProcessPlugin(
        debugger, GetGlobalPluginProperties().GetValueProperties(),
        "Properties for the kdp-remote process plug-in.", is_global_setting);
  }
}

bool ProcessKDP::StartAsyncThread() {
  Log *log = GetLog(KDPLog::Process);

  LLDB_LOGF(log, "ProcessKDP::StartAsyncThread ()");

  if (m_async_thread.IsJoinable())
    return true;

  llvm::Expected<HostThread> async_thread = ThreadLauncher::LaunchThread(
      "<lldb.process.kdp-remote.async>", [this] { return AsyncThread(); });
  if (!async_thread) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Host), async_thread.takeError(),
                   "failed to launch host thread: {0}");
    return false;
  }
  m_async_thread = *async_thread;
  return m_async_thread.IsJoinable();
}

void ProcessKDP::StopAsyncThread() {
  Log *log = GetLog(KDPLog::Process);

  LLDB_LOGF(log, "ProcessKDP::StopAsyncThread ()");

  m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncThreadShouldExit);

  // Stop the stdio thread
  if (m_async_thread.IsJoinable())
    m_async_thread.Join(nullptr);
}

void *ProcessKDP::AsyncThread() {
  const lldb::pid_t pid = GetID();

  Log *log = GetLog(KDPLog::Process);
  LLDB_LOGF(log,
            "ProcessKDP::AsyncThread(pid = %" PRIu64 ") thread starting...",
            pid);

  ListenerSP listener_sp(Listener::MakeListener("ProcessKDP::AsyncThread"));
  EventSP event_sp;
  const uint32_t desired_event_mask =
      eBroadcastBitAsyncContinue | eBroadcastBitAsyncThreadShouldExit;

  if (listener_sp->StartListeningForEvents(
          &m_async_broadcaster, desired_event_mask) == desired_event_mask) {
    bool done = false;
    while (!done) {
      LLDB_LOGF(log,
                "ProcessKDP::AsyncThread (pid = %" PRIu64
                ") listener.WaitForEvent (NULL, event_sp)...",
                pid);
      if (listener_sp->GetEvent(event_sp, std::nullopt)) {
        uint32_t event_type = event_sp->GetType();
        LLDB_LOGF(log,
                  "ProcessKDP::AsyncThread (pid = %" PRIu64
                  ") Got an event of type: %d...",
                  pid, event_type);

        // When we are running, poll for 1 second to try and get an exception
        // to indicate the process has stopped. If we don't get one, check to
        // make sure no one asked us to exit
        bool is_running = false;
        DataExtractor exc_reply_packet;
        do {
          switch (event_type) {
          case eBroadcastBitAsyncContinue: {
            is_running = true;
            if (m_comm.WaitForPacketWithTimeoutMicroSeconds(
                    exc_reply_packet, 1 * USEC_PER_SEC)) {
              ThreadSP thread_sp(GetKernelThread());
              if (thread_sp) {
                lldb::RegisterContextSP reg_ctx_sp(
                    thread_sp->GetRegisterContext());
                if (reg_ctx_sp)
                  reg_ctx_sp->InvalidateAllRegisters();
                static_cast<ThreadKDP *>(thread_sp.get())
                    ->SetStopInfoFrom_KDP_EXCEPTION(exc_reply_packet);
              }

              // TODO: parse the stop reply packet
              is_running = false;
              SetPrivateState(eStateStopped);
            } else {
              // Check to see if we are supposed to exit. There is no way to
              // interrupt a running kernel, so all we can do is wait for an
              // exception or detach...
              if (listener_sp->GetEvent(event_sp,
                                        std::chrono::microseconds(0))) {
                // We got an event, go through the loop again
                event_type = event_sp->GetType();
              }
            }
          } break;

          case eBroadcastBitAsyncThreadShouldExit:
            LLDB_LOGF(log,
                      "ProcessKDP::AsyncThread (pid = %" PRIu64
                      ") got eBroadcastBitAsyncThreadShouldExit...",
                      pid);
            done = true;
            is_running = false;
            break;

          default:
            LLDB_LOGF(log,
                      "ProcessKDP::AsyncThread (pid = %" PRIu64
                      ") got unknown event 0x%8.8x",
                      pid, event_type);
            done = true;
            is_running = false;
            break;
          }
        } while (is_running);
      } else {
        LLDB_LOGF(log,
                  "ProcessKDP::AsyncThread (pid = %" PRIu64
                  ") listener.WaitForEvent (NULL, event_sp) => false",
                  pid);
        done = true;
      }
    }
  }

  LLDB_LOGF(log, "ProcessKDP::AsyncThread(pid = %" PRIu64 ") thread exiting...",
            pid);

  m_async_thread.Reset();
  return NULL;
}

class CommandObjectProcessKDPPacketSend : public CommandObjectParsed {
private:
  OptionGroupOptions m_option_group;
  OptionGroupUInt64 m_command_byte;
  OptionGroupString m_packet_data;

  Options *GetOptions() override { return &m_option_group; }

public:
  CommandObjectProcessKDPPacketSend(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet send",
                            "Send a custom packet through the KDP protocol by "
                            "specifying the command byte and the packet "
                            "payload data. A packet will be sent with a "
                            "correct header and payload, and the raw result "
                            "bytes will be displayed as a string value. ",
                            NULL),
        m_option_group(),
        m_command_byte(LLDB_OPT_SET_1, true, "command", 'c', 0, eArgTypeNone,
                       "Specify the command byte to use when sending the KDP "
                       "request packet.",
                       0),
        m_packet_data(LLDB_OPT_SET_1, false, "payload", 'p', 0, eArgTypeNone,
                      "Specify packet payload bytes as a hex ASCII string with "
                      "no spaces or hex prefixes.",
                      NULL) {
    m_option_group.Append(&m_command_byte, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_packet_data, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectProcessKDPPacketSend() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (!m_command_byte.GetOptionValue().OptionWasSet()) {
      result.AppendError(
          "the --command option must be set to a valid command byte");
    } else {
      const uint64_t command_byte =
          m_command_byte.GetOptionValue().GetValueAs<uint64_t>().value_or(0);
      if (command_byte > 0 && command_byte <= UINT8_MAX) {
        ProcessKDP *process =
            (ProcessKDP *)m_interpreter.GetExecutionContext().GetProcessPtr();
        if (process) {
          const StateType state = process->GetState();

          if (StateIsStoppedState(state, true)) {
            std::vector<uint8_t> payload_bytes;
            const char *ascii_hex_bytes_cstr =
                m_packet_data.GetOptionValue().GetCurrentValue();
            if (ascii_hex_bytes_cstr && ascii_hex_bytes_cstr[0]) {
              StringExtractor extractor(ascii_hex_bytes_cstr);
              const size_t ascii_hex_bytes_cstr_len =
                  extractor.GetStringRef().size();
              if (ascii_hex_bytes_cstr_len & 1) {
                result.AppendErrorWithFormat("payload data must contain an "
                                             "even number of ASCII hex "
                                             "characters: '%s'",
                                             ascii_hex_bytes_cstr);
                return;
              }
              payload_bytes.resize(ascii_hex_bytes_cstr_len / 2);
              if (extractor.GetHexBytes(payload_bytes, '\xdd') !=
                  payload_bytes.size()) {
                result.AppendErrorWithFormat("payload data must only contain "
                                             "ASCII hex characters (no "
                                             "spaces or hex prefixes): '%s'",
                                             ascii_hex_bytes_cstr);
                return;
              }
            }
            Status error;
            DataExtractor reply;
            process->GetCommunication().SendRawRequest(
                command_byte,
                payload_bytes.empty() ? NULL : payload_bytes.data(),
                payload_bytes.size(), reply, error);

            if (error.Success()) {
              // Copy the binary bytes into a hex ASCII string for the result
              StreamString packet;
              packet.PutBytesAsRawHex8(
                  reply.GetDataStart(), reply.GetByteSize(),
                  endian::InlHostByteOrder(), endian::InlHostByteOrder());
              result.AppendMessage(packet.GetString());
              result.SetStatus(eReturnStatusSuccessFinishResult);
              return;
            } else {
              const char *error_cstr = error.AsCString();
              if (error_cstr && error_cstr[0])
                result.AppendError(error_cstr);
              else
                result.AppendErrorWithFormat("unknown error 0x%8.8x",
                                             error.GetError());
              return;
            }
          } else {
            result.AppendErrorWithFormat("process must be stopped in order "
                                         "to send KDP packets, state is %s",
                                         StateAsCString(state));
          }
        } else {
          result.AppendError("invalid process");
        }
      } else {
        result.AppendErrorWithFormat("invalid command byte 0x%" PRIx64
                                     ", valid values are 1 - 255",
                                     command_byte);
      }
    }
  }
};

class CommandObjectProcessKDPPacket : public CommandObjectMultiword {
private:
public:
  CommandObjectProcessKDPPacket(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "process plugin packet",
                               "Commands that deal with KDP remote packets.",
                               NULL) {
    LoadSubCommand(
        "send",
        CommandObjectSP(new CommandObjectProcessKDPPacketSend(interpreter)));
  }

  ~CommandObjectProcessKDPPacket() override = default;
};

class CommandObjectMultiwordProcessKDP : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcessKDP(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "process plugin",
            "Commands for operating on a ProcessKDP process.",
            "process plugin <subcommand> [<subcommand-options>]") {
    LoadSubCommand("packet", CommandObjectSP(new CommandObjectProcessKDPPacket(
                                 interpreter)));
  }

  ~CommandObjectMultiwordProcessKDP() override = default;
};

CommandObject *ProcessKDP::GetPluginCommandObject() {
  if (!m_command_sp)
    m_command_sp = std::make_shared<CommandObjectMultiwordProcessKDP>(
        GetTarget().GetDebugger().GetCommandInterpreter());
  return m_command_sp.get();
}
