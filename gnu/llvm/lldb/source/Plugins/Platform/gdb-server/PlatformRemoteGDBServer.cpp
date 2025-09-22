//===-- PlatformRemoteGDBServer.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformRemoteGDBServer.h"
#include "lldb/Host/Config.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/UriParser.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatAdapters.h"

#include "Plugins/Process/Utility/GDBRemoteSignals.h"
#include "Plugins/Process/gdb-remote/ProcessGDBRemote.h"
#include <mutex>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_gdb_server;

LLDB_PLUGIN_DEFINE_ADV(PlatformRemoteGDBServer, PlatformGDB)

static bool g_initialized = false;
// UnixSignals does not store the signal names or descriptions itself.
// It holds onto StringRefs. Becaue we may get signal information dynamically
// from the remote, these strings need persistent storage client-side.
static std::mutex g_signal_string_mutex;
static llvm::StringSet<> g_signal_string_storage;

void PlatformRemoteGDBServer::Initialize() {
  Platform::Initialize();

  if (!g_initialized) {
    g_initialized = true;
    PluginManager::RegisterPlugin(
        PlatformRemoteGDBServer::GetPluginNameStatic(),
        PlatformRemoteGDBServer::GetDescriptionStatic(),
        PlatformRemoteGDBServer::CreateInstance);
  }
}

void PlatformRemoteGDBServer::Terminate() {
  if (g_initialized) {
    g_initialized = false;
    PluginManager::UnregisterPlugin(PlatformRemoteGDBServer::CreateInstance);
  }

  Platform::Terminate();
}

PlatformSP PlatformRemoteGDBServer::CreateInstance(bool force,
                                                   const ArchSpec *arch) {
  bool create = force;
  if (!create) {
    create = !arch->TripleVendorWasSpecified() && !arch->TripleOSWasSpecified();
  }
  if (create)
    return PlatformSP(new PlatformRemoteGDBServer());
  return PlatformSP();
}

llvm::StringRef PlatformRemoteGDBServer::GetDescriptionStatic() {
  return "A platform that uses the GDB remote protocol as the communication "
         "transport.";
}

llvm::StringRef PlatformRemoteGDBServer::GetDescription() {
  if (m_platform_description.empty()) {
    if (IsConnected()) {
      // Send the get description packet
    }
  }

  if (!m_platform_description.empty())
    return m_platform_description.c_str();
  return GetDescriptionStatic();
}

bool PlatformRemoteGDBServer::GetModuleSpec(const FileSpec &module_file_spec,
                                            const ArchSpec &arch,
                                            ModuleSpec &module_spec) {
  Log *log = GetLog(LLDBLog::Platform);

  const auto module_path = module_file_spec.GetPath(false);

  if (!m_gdb_client_up ||
      !m_gdb_client_up->GetModuleInfo(module_file_spec, arch, module_spec)) {
    LLDB_LOGF(
        log,
        "PlatformRemoteGDBServer::%s - failed to get module info for %s:%s",
        __FUNCTION__, module_path.c_str(),
        arch.GetTriple().getTriple().c_str());
    return false;
  }

  if (log) {
    StreamString stream;
    module_spec.Dump(stream);
    LLDB_LOGF(log,
              "PlatformRemoteGDBServer::%s - got module info for (%s:%s) : %s",
              __FUNCTION__, module_path.c_str(),
              arch.GetTriple().getTriple().c_str(), stream.GetData());
  }

  return true;
}

Status PlatformRemoteGDBServer::GetFileWithUUID(const FileSpec &platform_file,
                                                const UUID *uuid_ptr,
                                                FileSpec &local_file) {
  // Default to the local case
  local_file = platform_file;
  return Status();
}

/// Default Constructor
PlatformRemoteGDBServer::PlatformRemoteGDBServer()
    : Platform(/*is_host=*/false) {}

/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
PlatformRemoteGDBServer::~PlatformRemoteGDBServer() = default;

size_t PlatformRemoteGDBServer::GetSoftwareBreakpointTrapOpcode(
    Target &target, BreakpointSite *bp_site) {
  // This isn't needed if the z/Z packets are supported in the GDB remote
  // server. But we might need a packet to detect this.
  return 0;
}

bool PlatformRemoteGDBServer::GetRemoteOSVersion() {
  if (m_gdb_client_up)
    m_os_version = m_gdb_client_up->GetOSVersion();
  return !m_os_version.empty();
}

std::optional<std::string> PlatformRemoteGDBServer::GetRemoteOSBuildString() {
  if (!m_gdb_client_up)
    return std::nullopt;
  return m_gdb_client_up->GetOSBuildString();
}

std::optional<std::string>
PlatformRemoteGDBServer::GetRemoteOSKernelDescription() {
  if (!m_gdb_client_up)
    return std::nullopt;
  return m_gdb_client_up->GetOSKernelDescription();
}

// Remote Platform subclasses need to override this function
ArchSpec PlatformRemoteGDBServer::GetRemoteSystemArchitecture() {
  if (!m_gdb_client_up)
    return ArchSpec();
  return m_gdb_client_up->GetSystemArchitecture();
}

FileSpec PlatformRemoteGDBServer::GetRemoteWorkingDirectory() {
  if (IsConnected()) {
    Log *log = GetLog(LLDBLog::Platform);
    FileSpec working_dir;
    if (m_gdb_client_up->GetWorkingDir(working_dir) && log)
      LLDB_LOGF(log,
                "PlatformRemoteGDBServer::GetRemoteWorkingDirectory() -> '%s'",
                working_dir.GetPath().c_str());
    return working_dir;
  } else {
    return Platform::GetRemoteWorkingDirectory();
  }
}

bool PlatformRemoteGDBServer::SetRemoteWorkingDirectory(
    const FileSpec &working_dir) {
  if (IsConnected()) {
    // Clear the working directory it case it doesn't get set correctly. This
    // will for use to re-read it
    Log *log = GetLog(LLDBLog::Platform);
    LLDB_LOGF(log, "PlatformRemoteGDBServer::SetRemoteWorkingDirectory('%s')",
              working_dir.GetPath().c_str());
    return m_gdb_client_up->SetWorkingDir(working_dir) == 0;
  } else
    return Platform::SetRemoteWorkingDirectory(working_dir);
}

bool PlatformRemoteGDBServer::IsConnected() const {
  if (m_gdb_client_up) {
    assert(m_gdb_client_up->IsConnected());
    return true;
  }
  return false;
}

Status PlatformRemoteGDBServer::ConnectRemote(Args &args) {
  Status error;
  if (IsConnected()) {
    error.SetErrorStringWithFormat("the platform is already connected to '%s', "
                                   "execute 'platform disconnect' to close the "
                                   "current connection",
                                   GetHostname());
    return error;
  }

  if (args.GetArgumentCount() != 1) {
    error.SetErrorString(
        "\"platform connect\" takes a single argument: <connect-url>");
    return error;
  }

  const char *url = args.GetArgumentAtIndex(0);
  if (!url)
    return Status("URL is null.");

  std::optional<URI> parsed_url = URI::Parse(url);
  if (!parsed_url)
    return Status("Invalid URL: %s", url);

  // We're going to reuse the hostname when we connect to the debugserver.
  m_platform_scheme = parsed_url->scheme.str();
  m_platform_hostname = parsed_url->hostname.str();

  auto client_up =
      std::make_unique<process_gdb_remote::GDBRemoteCommunicationClient>();
  client_up->SetPacketTimeout(
      process_gdb_remote::ProcessGDBRemote::GetPacketTimeout());
  client_up->SetConnection(std::make_unique<ConnectionFileDescriptor>());
  client_up->Connect(url, &error);

  if (error.Fail())
    return error;

  if (client_up->HandshakeWithServer(&error)) {
    m_gdb_client_up = std::move(client_up);
    m_gdb_client_up->GetHostInfo();
    // If a working directory was set prior to connecting, send it down
    // now.
    if (m_working_dir)
      m_gdb_client_up->SetWorkingDir(m_working_dir);

    m_supported_architectures.clear();
    ArchSpec remote_arch = m_gdb_client_up->GetSystemArchitecture();
    if (remote_arch) {
      m_supported_architectures.push_back(remote_arch);
      if (remote_arch.GetTriple().isArch64Bit())
        m_supported_architectures.push_back(
            ArchSpec(remote_arch.GetTriple().get32BitArchVariant()));
    }
  } else {
    client_up->Disconnect();
    if (error.Success())
      error.SetErrorString("handshake failed");
  }
  return error;
}

Status PlatformRemoteGDBServer::DisconnectRemote() {
  Status error;
  m_gdb_client_up.reset();
  m_remote_signals_sp.reset();
  return error;
}

const char *PlatformRemoteGDBServer::GetHostname() {
  if (m_gdb_client_up)
    m_gdb_client_up->GetHostname(m_hostname);
  if (m_hostname.empty())
    return nullptr;
  return m_hostname.c_str();
}

std::optional<std::string>
PlatformRemoteGDBServer::DoGetUserName(UserIDResolver::id_t uid) {
  std::string name;
  if (m_gdb_client_up && m_gdb_client_up->GetUserName(uid, name))
    return std::move(name);
  return std::nullopt;
}

std::optional<std::string>
PlatformRemoteGDBServer::DoGetGroupName(UserIDResolver::id_t gid) {
  std::string name;
  if (m_gdb_client_up && m_gdb_client_up->GetGroupName(gid, name))
    return std::move(name);
  return std::nullopt;
}

uint32_t PlatformRemoteGDBServer::FindProcesses(
    const ProcessInstanceInfoMatch &match_info,
    ProcessInstanceInfoList &process_infos) {
  if (m_gdb_client_up)
    return m_gdb_client_up->FindProcesses(match_info, process_infos);
  return 0;
}

bool PlatformRemoteGDBServer::GetProcessInfo(
    lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  if (m_gdb_client_up)
    return m_gdb_client_up->GetProcessInfo(pid, process_info);
  return false;
}

Status PlatformRemoteGDBServer::LaunchProcess(ProcessLaunchInfo &launch_info) {
  Log *log = GetLog(LLDBLog::Platform);
  Status error;

  LLDB_LOGF(log, "PlatformRemoteGDBServer::%s() called", __FUNCTION__);

  if (!IsConnected())
    return Status("Not connected.");
  auto num_file_actions = launch_info.GetNumFileActions();
  for (decltype(num_file_actions) i = 0; i < num_file_actions; ++i) {
    const auto file_action = launch_info.GetFileActionAtIndex(i);
    if (file_action->GetAction() != FileAction::eFileActionOpen)
      continue;
    switch (file_action->GetFD()) {
    case STDIN_FILENO:
      m_gdb_client_up->SetSTDIN(file_action->GetFileSpec());
      break;
    case STDOUT_FILENO:
      m_gdb_client_up->SetSTDOUT(file_action->GetFileSpec());
      break;
    case STDERR_FILENO:
      m_gdb_client_up->SetSTDERR(file_action->GetFileSpec());
      break;
    }
  }

  m_gdb_client_up->SetDisableASLR(
      launch_info.GetFlags().Test(eLaunchFlagDisableASLR));
  m_gdb_client_up->SetDetachOnError(
      launch_info.GetFlags().Test(eLaunchFlagDetachOnError));

  FileSpec working_dir = launch_info.GetWorkingDirectory();
  if (working_dir) {
    m_gdb_client_up->SetWorkingDir(working_dir);
  }

  // Send the environment and the program + arguments after we connect
  m_gdb_client_up->SendEnvironment(launch_info.GetEnvironment());

  ArchSpec arch_spec = launch_info.GetArchitecture();
  const char *arch_triple = arch_spec.GetTriple().str().c_str();

  m_gdb_client_up->SendLaunchArchPacket(arch_triple);
  LLDB_LOGF(
      log,
      "PlatformRemoteGDBServer::%s() set launch architecture triple to '%s'",
      __FUNCTION__, arch_triple ? arch_triple : "<NULL>");

  {
    // Scope for the scoped timeout object
    process_gdb_remote::GDBRemoteCommunication::ScopedTimeout timeout(
        *m_gdb_client_up, std::chrono::seconds(5));
    // Since we can't send argv0 separate from the executable path, we need to
    // make sure to use the actual executable path found in the launch_info...
    Args args = launch_info.GetArguments();
    if (FileSpec exe_file = launch_info.GetExecutableFile())
      args.ReplaceArgumentAtIndex(0, exe_file.GetPath(false));
    if (llvm::Error err = m_gdb_client_up->LaunchProcess(args)) {
      error.SetErrorStringWithFormatv("Cannot launch '{0}': {1}",
                                      args.GetArgumentAtIndex(0),
                                      llvm::fmt_consume(std::move(err)));
      return error;
    }
  }

  const auto pid = m_gdb_client_up->GetCurrentProcessID(false);
  if (pid != LLDB_INVALID_PROCESS_ID) {
    launch_info.SetProcessID(pid);
    LLDB_LOGF(log,
              "PlatformRemoteGDBServer::%s() pid %" PRIu64
              " launched successfully",
              __FUNCTION__, pid);
  } else {
    LLDB_LOGF(log,
              "PlatformRemoteGDBServer::%s() launch succeeded but we "
              "didn't get a valid process id back!",
              __FUNCTION__);
    error.SetErrorString("failed to get PID");
  }
  return error;
}

Status PlatformRemoteGDBServer::KillProcess(const lldb::pid_t pid) {
  if (!KillSpawnedProcess(pid))
    return Status("failed to kill remote spawned process");
  return Status();
}

lldb::ProcessSP
PlatformRemoteGDBServer::DebugProcess(ProcessLaunchInfo &launch_info,
                                      Debugger &debugger, Target &target,
                                      Status &error) {
  lldb::ProcessSP process_sp;
  if (IsRemote()) {
    if (IsConnected()) {
      lldb::pid_t debugserver_pid = LLDB_INVALID_PROCESS_ID;
      std::string connect_url;
      if (!LaunchGDBServer(debugserver_pid, connect_url)) {
        error.SetErrorStringWithFormat("unable to launch a GDB server on '%s'",
                                       GetHostname());
      } else {
        // The darwin always currently uses the GDB remote debugger plug-in
        // so even when debugging locally we are debugging remotely!
        process_sp = target.CreateProcess(launch_info.GetListener(),
                                          "gdb-remote", nullptr, true);

        if (process_sp) {
          process_sp->HijackProcessEvents(launch_info.GetHijackListener());
          process_sp->SetShadowListener(launch_info.GetShadowListener());

          error = process_sp->ConnectRemote(connect_url.c_str());
          // Retry the connect remote one time...
          if (error.Fail())
            error = process_sp->ConnectRemote(connect_url.c_str());
          if (error.Success())
            error = process_sp->Launch(launch_info);
          else if (debugserver_pid != LLDB_INVALID_PROCESS_ID) {
            printf("error: connect remote failed (%s)\n", error.AsCString());
            KillSpawnedProcess(debugserver_pid);
          }
        }
      }
    } else {
      error.SetErrorString("not connected to remote gdb server");
    }
  }
  return process_sp;
}

bool PlatformRemoteGDBServer::LaunchGDBServer(lldb::pid_t &pid,
                                              std::string &connect_url) {
  assert(IsConnected());

  ArchSpec remote_arch = GetRemoteSystemArchitecture();
  llvm::Triple &remote_triple = remote_arch.GetTriple();

  uint16_t port = 0;
  std::string socket_name;
  bool launch_result = false;
  if (remote_triple.getVendor() == llvm::Triple::Apple &&
      remote_triple.getOS() == llvm::Triple::IOS) {
    // When remote debugging to iOS, we use a USB mux that always talks to
    // localhost, so we will need the remote debugserver to accept connections
    // only from localhost, no matter what our current hostname is
    launch_result =
        m_gdb_client_up->LaunchGDBServer("127.0.0.1", pid, port, socket_name);
  } else {
    // All other hosts should use their actual hostname
    launch_result =
        m_gdb_client_up->LaunchGDBServer(nullptr, pid, port, socket_name);
  }

  if (!launch_result)
    return false;

  connect_url =
      MakeGdbServerUrl(m_platform_scheme, m_platform_hostname, port,
                       (socket_name.empty()) ? nullptr : socket_name.c_str());
  return true;
}

bool PlatformRemoteGDBServer::KillSpawnedProcess(lldb::pid_t pid) {
  assert(IsConnected());
  return m_gdb_client_up->KillSpawnedProcess(pid);
}

lldb::ProcessSP PlatformRemoteGDBServer::Attach(
    ProcessAttachInfo &attach_info, Debugger &debugger,
    Target *target, // Can be NULL, if NULL create a new target, else use
                    // existing one
    Status &error) {
  lldb::ProcessSP process_sp;
  if (IsRemote()) {
    if (IsConnected()) {
      lldb::pid_t debugserver_pid = LLDB_INVALID_PROCESS_ID;
      std::string connect_url;
      if (!LaunchGDBServer(debugserver_pid, connect_url)) {
        error.SetErrorStringWithFormat("unable to launch a GDB server on '%s'",
                                       GetHostname());
      } else {
        if (target == nullptr) {
          TargetSP new_target_sp;

          error = debugger.GetTargetList().CreateTarget(
              debugger, "", "", eLoadDependentsNo, nullptr, new_target_sp);
          target = new_target_sp.get();
        } else
          error.Clear();

        if (target && error.Success()) {
          // The darwin always currently uses the GDB remote debugger plug-in
          // so even when debugging locally we are debugging remotely!
          process_sp =
              target->CreateProcess(attach_info.GetListenerForProcess(debugger),
                                    "gdb-remote", nullptr, true);
          if (process_sp) {
            error = process_sp->ConnectRemote(connect_url.c_str());
            if (error.Success()) {
              ListenerSP listener_sp = attach_info.GetHijackListener();
              if (listener_sp)
                process_sp->HijackProcessEvents(listener_sp);
              process_sp->SetShadowListener(attach_info.GetShadowListener());
              error = process_sp->Attach(attach_info);
            }

            if (error.Fail() && debugserver_pid != LLDB_INVALID_PROCESS_ID) {
              KillSpawnedProcess(debugserver_pid);
            }
          }
        }
      }
    } else {
      error.SetErrorString("not connected to remote gdb server");
    }
  }
  return process_sp;
}

Status PlatformRemoteGDBServer::MakeDirectory(const FileSpec &file_spec,
                                              uint32_t mode) {
  if (!IsConnected())
    return Status("Not connected.");
  Status error = m_gdb_client_up->MakeDirectory(file_spec, mode);
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOGF(log,
            "PlatformRemoteGDBServer::MakeDirectory(path='%s', mode=%o) "
            "error = %u (%s)",
            file_spec.GetPath().c_str(), mode, error.GetError(),
            error.AsCString());
  return error;
}

Status PlatformRemoteGDBServer::GetFilePermissions(const FileSpec &file_spec,
                                                   uint32_t &file_permissions) {
  if (!IsConnected())
    return Status("Not connected.");
  Status error =
      m_gdb_client_up->GetFilePermissions(file_spec, file_permissions);
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOGF(log,
            "PlatformRemoteGDBServer::GetFilePermissions(path='%s', "
            "file_permissions=%o) error = %u (%s)",
            file_spec.GetPath().c_str(), file_permissions, error.GetError(),
            error.AsCString());
  return error;
}

Status PlatformRemoteGDBServer::SetFilePermissions(const FileSpec &file_spec,
                                                   uint32_t file_permissions) {
  if (!IsConnected())
    return Status("Not connected.");
  Status error =
      m_gdb_client_up->SetFilePermissions(file_spec, file_permissions);
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOGF(log,
            "PlatformRemoteGDBServer::SetFilePermissions(path='%s', "
            "file_permissions=%o) error = %u (%s)",
            file_spec.GetPath().c_str(), file_permissions, error.GetError(),
            error.AsCString());
  return error;
}

lldb::user_id_t PlatformRemoteGDBServer::OpenFile(const FileSpec &file_spec,
                                                  File::OpenOptions flags,
                                                  uint32_t mode,
                                                  Status &error) {
  if (IsConnected())
    return m_gdb_client_up->OpenFile(file_spec, flags, mode, error);
  return LLDB_INVALID_UID;
}

bool PlatformRemoteGDBServer::CloseFile(lldb::user_id_t fd, Status &error) {
  if (IsConnected())
    return m_gdb_client_up->CloseFile(fd, error);
  error = Status("Not connected.");
  return false;
}

lldb::user_id_t
PlatformRemoteGDBServer::GetFileSize(const FileSpec &file_spec) {
  if (IsConnected())
    return m_gdb_client_up->GetFileSize(file_spec);
  return LLDB_INVALID_UID;
}

void PlatformRemoteGDBServer::AutoCompleteDiskFileOrDirectory(
    CompletionRequest &request, bool only_dir) {
  if (IsConnected())
    m_gdb_client_up->AutoCompleteDiskFileOrDirectory(request, only_dir);
}

uint64_t PlatformRemoteGDBServer::ReadFile(lldb::user_id_t fd, uint64_t offset,
                                           void *dst, uint64_t dst_len,
                                           Status &error) {
  if (IsConnected())
    return m_gdb_client_up->ReadFile(fd, offset, dst, dst_len, error);
  error = Status("Not connected.");
  return 0;
}

uint64_t PlatformRemoteGDBServer::WriteFile(lldb::user_id_t fd, uint64_t offset,
                                            const void *src, uint64_t src_len,
                                            Status &error) {
  if (IsConnected())
    return m_gdb_client_up->WriteFile(fd, offset, src, src_len, error);
  error = Status("Not connected.");
  return 0;
}

Status PlatformRemoteGDBServer::PutFile(const FileSpec &source,
                                        const FileSpec &destination,
                                        uint32_t uid, uint32_t gid) {
  return Platform::PutFile(source, destination, uid, gid);
}

Status PlatformRemoteGDBServer::CreateSymlink(
    const FileSpec &src, // The name of the link is in src
    const FileSpec &dst) // The symlink points to dst
{
  if (!IsConnected())
    return Status("Not connected.");
  Status error = m_gdb_client_up->CreateSymlink(src, dst);
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOGF(log,
            "PlatformRemoteGDBServer::CreateSymlink(src='%s', dst='%s') "
            "error = %u (%s)",
            src.GetPath().c_str(), dst.GetPath().c_str(), error.GetError(),
            error.AsCString());
  return error;
}

Status PlatformRemoteGDBServer::Unlink(const FileSpec &file_spec) {
  if (!IsConnected())
    return Status("Not connected.");
  Status error = m_gdb_client_up->Unlink(file_spec);
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOGF(log, "PlatformRemoteGDBServer::Unlink(path='%s') error = %u (%s)",
            file_spec.GetPath().c_str(), error.GetError(), error.AsCString());
  return error;
}

bool PlatformRemoteGDBServer::GetFileExists(const FileSpec &file_spec) {
  if (IsConnected())
    return m_gdb_client_up->GetFileExists(file_spec);
  return false;
}

Status PlatformRemoteGDBServer::RunShellCommand(
    llvm::StringRef shell, llvm::StringRef command,
    const FileSpec &
        working_dir, // Pass empty FileSpec to use the current working directory
    int *status_ptr, // Pass NULL if you don't want the process exit status
    int *signo_ptr,  // Pass NULL if you don't want the signal that caused the
                     // process to exit
    std::string
        *command_output, // Pass NULL if you don't want the command output
    const Timeout<std::micro> &timeout) {
  if (!IsConnected())
    return Status("Not connected.");
  return m_gdb_client_up->RunShellCommand(command, working_dir, status_ptr,
                                          signo_ptr, command_output, timeout);
}

llvm::ErrorOr<llvm::MD5::MD5Result>
PlatformRemoteGDBServer::CalculateMD5(const FileSpec &file_spec) {
  if (!IsConnected())
    return std::make_error_code(std::errc::not_connected);

  return m_gdb_client_up->CalculateMD5(file_spec);
}

void PlatformRemoteGDBServer::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

const UnixSignalsSP &PlatformRemoteGDBServer::GetRemoteUnixSignals() {
  if (!IsConnected())
    return Platform::GetRemoteUnixSignals();

  if (m_remote_signals_sp)
    return m_remote_signals_sp;

  // If packet not implemented or JSON failed to parse, we'll guess the signal
  // set based on the remote architecture.
  m_remote_signals_sp = UnixSignals::Create(GetRemoteSystemArchitecture());

  StringExtractorGDBRemote response;
  auto result =
      m_gdb_client_up->SendPacketAndWaitForResponse("jSignalsInfo", response);

  if (result != decltype(result)::Success ||
      response.GetResponseType() != response.eResponse)
    return m_remote_signals_sp;

  auto object_sp = StructuredData::ParseJSON(response.GetStringRef());
  if (!object_sp || !object_sp->IsValid())
    return m_remote_signals_sp;

  auto array_sp = object_sp->GetAsArray();
  if (!array_sp || !array_sp->IsValid())
    return m_remote_signals_sp;

  auto remote_signals_sp = std::make_shared<lldb_private::GDBRemoteSignals>();

  bool done = array_sp->ForEach(
      [&remote_signals_sp](StructuredData::Object *object) -> bool {
        if (!object || !object->IsValid())
          return false;

        auto dict = object->GetAsDictionary();
        if (!dict || !dict->IsValid())
          return false;

        // Signal number and signal name are required.
        uint64_t signo;
        if (!dict->GetValueForKeyAsInteger("signo", signo))
          return false;

        llvm::StringRef name;
        if (!dict->GetValueForKeyAsString("name", name))
          return false;

        // We can live without short_name, description, etc.
        bool suppress{false};
        auto object_sp = dict->GetValueForKey("suppress");
        if (object_sp && object_sp->IsValid())
          suppress = object_sp->GetBooleanValue();

        bool stop{false};
        object_sp = dict->GetValueForKey("stop");
        if (object_sp && object_sp->IsValid())
          stop = object_sp->GetBooleanValue();

        bool notify{false};
        object_sp = dict->GetValueForKey("notify");
        if (object_sp && object_sp->IsValid())
          notify = object_sp->GetBooleanValue();

        std::string description;
        object_sp = dict->GetValueForKey("description");
        if (object_sp && object_sp->IsValid())
          description = std::string(object_sp->GetStringValue());

        llvm::StringRef name_backed, description_backed;
        {
          std::lock_guard<std::mutex> guard(g_signal_string_mutex);
          name_backed =
              g_signal_string_storage.insert(name).first->getKeyData();
          if (!description.empty())
            description_backed =
                g_signal_string_storage.insert(description).first->getKeyData();
        }

        remote_signals_sp->AddSignal(signo, name_backed, suppress, stop, notify,
                                     description_backed);
        return true;
      });

  if (done)
    m_remote_signals_sp = std::move(remote_signals_sp);

  return m_remote_signals_sp;
}

std::string PlatformRemoteGDBServer::MakeGdbServerUrl(
    const std::string &platform_scheme, const std::string &platform_hostname,
    uint16_t port, const char *socket_name) {
  const char *override_scheme =
      getenv("LLDB_PLATFORM_REMOTE_GDB_SERVER_SCHEME");
  const char *override_hostname =
      getenv("LLDB_PLATFORM_REMOTE_GDB_SERVER_HOSTNAME");
  const char *port_offset_c_str =
      getenv("LLDB_PLATFORM_REMOTE_GDB_SERVER_PORT_OFFSET");
  int port_offset = port_offset_c_str ? ::atoi(port_offset_c_str) : 0;

  return MakeUrl(override_scheme ? override_scheme : platform_scheme.c_str(),
                 override_hostname ? override_hostname
                                   : platform_hostname.c_str(),
                 port + port_offset, socket_name);
}

std::string PlatformRemoteGDBServer::MakeUrl(const char *scheme,
                                             const char *hostname,
                                             uint16_t port, const char *path) {
  StreamString result;
  result.Printf("%s://[%s]", scheme, hostname);
  if (port != 0)
    result.Printf(":%u", port);
  if (path)
    result.Write(path, strlen(path));
  return std::string(result.GetString());
}

size_t PlatformRemoteGDBServer::ConnectToWaitingProcesses(Debugger &debugger,
                                                          Status &error) {
  std::vector<std::string> connection_urls;
  GetPendingGdbServerList(connection_urls);

  for (size_t i = 0; i < connection_urls.size(); ++i) {
    ConnectProcess(connection_urls[i].c_str(), "gdb-remote", debugger, nullptr, error);
    if (error.Fail())
      return i; // We already connected to i process successfully
  }
  return connection_urls.size();
}

size_t PlatformRemoteGDBServer::GetPendingGdbServerList(
    std::vector<std::string> &connection_urls) {
  std::vector<std::pair<uint16_t, std::string>> remote_servers;
  if (!IsConnected())
    return 0;
  m_gdb_client_up->QueryGDBServer(remote_servers);
  for (const auto &gdbserver : remote_servers) {
    const char *socket_name_cstr =
        gdbserver.second.empty() ? nullptr : gdbserver.second.c_str();
    connection_urls.emplace_back(
        MakeGdbServerUrl(m_platform_scheme, m_platform_hostname,
                         gdbserver.first, socket_name_cstr));
  }
  return connection_urls.size();
}
