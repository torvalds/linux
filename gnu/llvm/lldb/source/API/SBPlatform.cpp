//===-- SBPlatform.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEnvironment.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBModuleSpec.h"
#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBProcessInfoList.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBUnixSignals.h"
#include "lldb/Host/File.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/FileSystem.h"

#include <functional>

using namespace lldb;
using namespace lldb_private;

// PlatformConnectOptions
struct PlatformConnectOptions {
  PlatformConnectOptions(const char *url = nullptr) {
    if (url && url[0])
      m_url = url;
  }

  ~PlatformConnectOptions() = default;

  std::string m_url;
  std::string m_rsync_options;
  std::string m_rsync_remote_path_prefix;
  bool m_rsync_enabled = false;
  bool m_rsync_omit_hostname_from_remote_path = false;
  ConstString m_local_cache_directory;
};

// PlatformShellCommand
struct PlatformShellCommand {
  PlatformShellCommand(llvm::StringRef shell_interpreter,
                       llvm::StringRef shell_command) {
    if (!shell_interpreter.empty())
      m_shell = shell_interpreter.str();

    if (!m_shell.empty() && !shell_command.empty())
      m_command = shell_command.str();
  }

  PlatformShellCommand(llvm::StringRef shell_command = llvm::StringRef()) {
    if (!shell_command.empty())
      m_command = shell_command.str();
  }

  ~PlatformShellCommand() = default;

  std::string m_shell;
  std::string m_command;
  std::string m_working_dir;
  std::string m_output;
  int m_status = 0;
  int m_signo = 0;
  Timeout<std::ratio<1>> m_timeout = std::nullopt;
};
// SBPlatformConnectOptions
SBPlatformConnectOptions::SBPlatformConnectOptions(const char *url)
    : m_opaque_ptr(new PlatformConnectOptions(url)) {
  LLDB_INSTRUMENT_VA(this, url);
}

SBPlatformConnectOptions::SBPlatformConnectOptions(
    const SBPlatformConnectOptions &rhs)
    : m_opaque_ptr(new PlatformConnectOptions()) {
  LLDB_INSTRUMENT_VA(this, rhs);

  *m_opaque_ptr = *rhs.m_opaque_ptr;
}

SBPlatformConnectOptions::~SBPlatformConnectOptions() { delete m_opaque_ptr; }

SBPlatformConnectOptions &
SBPlatformConnectOptions::operator=(const SBPlatformConnectOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  *m_opaque_ptr = *rhs.m_opaque_ptr;
  return *this;
}

const char *SBPlatformConnectOptions::GetURL() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr->m_url.empty())
    return nullptr;
  return ConstString(m_opaque_ptr->m_url.c_str()).GetCString();
}

void SBPlatformConnectOptions::SetURL(const char *url) {
  LLDB_INSTRUMENT_VA(this, url);

  if (url && url[0])
    m_opaque_ptr->m_url = url;
  else
    m_opaque_ptr->m_url.clear();
}

bool SBPlatformConnectOptions::GetRsyncEnabled() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr->m_rsync_enabled;
}

void SBPlatformConnectOptions::EnableRsync(
    const char *options, const char *remote_path_prefix,
    bool omit_hostname_from_remote_path) {
  LLDB_INSTRUMENT_VA(this, options, remote_path_prefix,
                     omit_hostname_from_remote_path);

  m_opaque_ptr->m_rsync_enabled = true;
  m_opaque_ptr->m_rsync_omit_hostname_from_remote_path =
      omit_hostname_from_remote_path;
  if (remote_path_prefix && remote_path_prefix[0])
    m_opaque_ptr->m_rsync_remote_path_prefix = remote_path_prefix;
  else
    m_opaque_ptr->m_rsync_remote_path_prefix.clear();

  if (options && options[0])
    m_opaque_ptr->m_rsync_options = options;
  else
    m_opaque_ptr->m_rsync_options.clear();
}

void SBPlatformConnectOptions::DisableRsync() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_ptr->m_rsync_enabled = false;
}

const char *SBPlatformConnectOptions::GetLocalCacheDirectory() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr->m_local_cache_directory.GetCString();
}

void SBPlatformConnectOptions::SetLocalCacheDirectory(const char *path) {
  LLDB_INSTRUMENT_VA(this, path);

  if (path && path[0])
    m_opaque_ptr->m_local_cache_directory.SetCString(path);
  else
    m_opaque_ptr->m_local_cache_directory = ConstString();
}

// SBPlatformShellCommand
SBPlatformShellCommand::SBPlatformShellCommand(const char *shell_interpreter,
                                               const char *shell_command)
    : m_opaque_ptr(new PlatformShellCommand(shell_interpreter, shell_command)) {
  LLDB_INSTRUMENT_VA(this, shell_interpreter, shell_command);
}

SBPlatformShellCommand::SBPlatformShellCommand(const char *shell_command)
    : m_opaque_ptr(new PlatformShellCommand(shell_command)) {
  LLDB_INSTRUMENT_VA(this, shell_command);
}

SBPlatformShellCommand::SBPlatformShellCommand(
    const SBPlatformShellCommand &rhs)
    : m_opaque_ptr(new PlatformShellCommand()) {
  LLDB_INSTRUMENT_VA(this, rhs);

  *m_opaque_ptr = *rhs.m_opaque_ptr;
}

SBPlatformShellCommand &
SBPlatformShellCommand::operator=(const SBPlatformShellCommand &rhs) {

  LLDB_INSTRUMENT_VA(this, rhs);

  *m_opaque_ptr = *rhs.m_opaque_ptr;
  return *this;
}

SBPlatformShellCommand::~SBPlatformShellCommand() { delete m_opaque_ptr; }

void SBPlatformShellCommand::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_ptr->m_output = std::string();
  m_opaque_ptr->m_status = 0;
  m_opaque_ptr->m_signo = 0;
}

const char *SBPlatformShellCommand::GetShell() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr->m_shell.empty())
    return nullptr;
  return ConstString(m_opaque_ptr->m_shell.c_str()).GetCString();
}

void SBPlatformShellCommand::SetShell(const char *shell_interpreter) {
  LLDB_INSTRUMENT_VA(this, shell_interpreter);

  if (shell_interpreter && shell_interpreter[0])
    m_opaque_ptr->m_shell = shell_interpreter;
  else
    m_opaque_ptr->m_shell.clear();
}

const char *SBPlatformShellCommand::GetCommand() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr->m_command.empty())
    return nullptr;
  return ConstString(m_opaque_ptr->m_command.c_str()).GetCString();
}

void SBPlatformShellCommand::SetCommand(const char *shell_command) {
  LLDB_INSTRUMENT_VA(this, shell_command);

  if (shell_command && shell_command[0])
    m_opaque_ptr->m_command = shell_command;
  else
    m_opaque_ptr->m_command.clear();
}

const char *SBPlatformShellCommand::GetWorkingDirectory() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr->m_working_dir.empty())
    return nullptr;
  return ConstString(m_opaque_ptr->m_working_dir.c_str()).GetCString();
}

void SBPlatformShellCommand::SetWorkingDirectory(const char *path) {
  LLDB_INSTRUMENT_VA(this, path);

  if (path && path[0])
    m_opaque_ptr->m_working_dir = path;
  else
    m_opaque_ptr->m_working_dir.clear();
}

uint32_t SBPlatformShellCommand::GetTimeoutSeconds() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr->m_timeout)
    return m_opaque_ptr->m_timeout->count();
  return UINT32_MAX;
}

void SBPlatformShellCommand::SetTimeoutSeconds(uint32_t sec) {
  LLDB_INSTRUMENT_VA(this, sec);

  if (sec == UINT32_MAX)
    m_opaque_ptr->m_timeout = std::nullopt;
  else
    m_opaque_ptr->m_timeout = std::chrono::seconds(sec);
}

int SBPlatformShellCommand::GetSignal() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr->m_signo;
}

int SBPlatformShellCommand::GetStatus() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr->m_status;
}

const char *SBPlatformShellCommand::GetOutput() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr->m_output.empty())
    return nullptr;
  return ConstString(m_opaque_ptr->m_output.c_str()).GetCString();
}

// SBPlatform
SBPlatform::SBPlatform() { LLDB_INSTRUMENT_VA(this); }

SBPlatform::SBPlatform(const char *platform_name) {
  LLDB_INSTRUMENT_VA(this, platform_name);

  m_opaque_sp = Platform::Create(platform_name);
}

SBPlatform::SBPlatform(const SBPlatform &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_sp = rhs.m_opaque_sp;
}

SBPlatform &SBPlatform::operator=(const SBPlatform &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBPlatform::~SBPlatform() = default;

SBPlatform SBPlatform::GetHostPlatform() {
  LLDB_INSTRUMENT();

  SBPlatform host_platform;
  host_platform.m_opaque_sp = Platform::GetHostPlatform();
  return host_platform;
}

bool SBPlatform::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBPlatform::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

void SBPlatform::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_sp.reset();
}

const char *SBPlatform::GetName() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return ConstString(platform_sp->GetName()).AsCString();
  return nullptr;
}

lldb::PlatformSP SBPlatform::GetSP() const { return m_opaque_sp; }

void SBPlatform::SetSP(const lldb::PlatformSP &platform_sp) {
  m_opaque_sp = platform_sp;
}

const char *SBPlatform::GetWorkingDirectory() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return platform_sp->GetWorkingDirectory().GetPathAsConstString().AsCString();
  return nullptr;
}

bool SBPlatform::SetWorkingDirectory(const char *path) {
  LLDB_INSTRUMENT_VA(this, path);

  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    if (path)
      platform_sp->SetWorkingDirectory(FileSpec(path));
    else
      platform_sp->SetWorkingDirectory(FileSpec());
    return true;
  }
  return false;
}

SBError SBPlatform::ConnectRemote(SBPlatformConnectOptions &connect_options) {
  LLDB_INSTRUMENT_VA(this, connect_options);

  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp && connect_options.GetURL()) {
    Args args;
    args.AppendArgument(connect_options.GetURL());
    sb_error.ref() = platform_sp->ConnectRemote(args);
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

void SBPlatform::DisconnectRemote() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    platform_sp->DisconnectRemote();
}

bool SBPlatform::IsConnected() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return platform_sp->IsConnected();
  return false;
}

const char *SBPlatform::GetTriple() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    ArchSpec arch(platform_sp->GetSystemArchitecture());
    if (arch.IsValid()) {
      // Const-ify the string so we don't need to worry about the lifetime of
      // the string
      return ConstString(arch.GetTriple().getTriple().c_str()).GetCString();
    }
  }
  return nullptr;
}

const char *SBPlatform::GetOSBuild() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    std::string s = platform_sp->GetOSBuildString().value_or("");
    if (!s.empty()) {
      // Const-ify the string so we don't need to worry about the lifetime of
      // the string
      return ConstString(s).GetCString();
    }
  }
  return nullptr;
}

const char *SBPlatform::GetOSDescription() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    std::string s = platform_sp->GetOSKernelDescription().value_or("");
    if (!s.empty()) {
      // Const-ify the string so we don't need to worry about the lifetime of
      // the string
      return ConstString(s.c_str()).GetCString();
    }
  }
  return nullptr;
}

const char *SBPlatform::GetHostname() {
  LLDB_INSTRUMENT_VA(this);

  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return ConstString(platform_sp->GetHostname()).GetCString();
  return nullptr;
}

uint32_t SBPlatform::GetOSMajorVersion() {
  LLDB_INSTRUMENT_VA(this);

  llvm::VersionTuple version;
  if (PlatformSP platform_sp = GetSP())
    version = platform_sp->GetOSVersion();
  return version.empty() ? UINT32_MAX : version.getMajor();
}

uint32_t SBPlatform::GetOSMinorVersion() {
  LLDB_INSTRUMENT_VA(this);

  llvm::VersionTuple version;
  if (PlatformSP platform_sp = GetSP())
    version = platform_sp->GetOSVersion();
  return version.getMinor().value_or(UINT32_MAX);
}

uint32_t SBPlatform::GetOSUpdateVersion() {
  LLDB_INSTRUMENT_VA(this);

  llvm::VersionTuple version;
  if (PlatformSP platform_sp = GetSP())
    version = platform_sp->GetOSVersion();
  return version.getSubminor().value_or(UINT32_MAX);
}

void SBPlatform::SetSDKRoot(const char *sysroot) {
  LLDB_INSTRUMENT_VA(this, sysroot);
  if (PlatformSP platform_sp = GetSP())
    platform_sp->SetSDKRootDirectory(llvm::StringRef(sysroot).str());
}

SBError SBPlatform::Get(SBFileSpec &src, SBFileSpec &dst) {
  LLDB_INSTRUMENT_VA(this, src, dst);

  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    sb_error.ref() = platform_sp->GetFile(src.ref(), dst.ref());
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

SBError SBPlatform::Put(SBFileSpec &src, SBFileSpec &dst) {
  LLDB_INSTRUMENT_VA(this, src, dst);
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    if (src.Exists()) {
      uint32_t permissions = FileSystem::Instance().GetPermissions(src.ref());
      if (permissions == 0) {
        if (FileSystem::Instance().IsDirectory(src.ref()))
          permissions = eFilePermissionsDirectoryDefault;
        else
          permissions = eFilePermissionsFileDefault;
      }

      return platform_sp->PutFile(src.ref(), dst.ref(), permissions);
    }

    Status error;
    error.SetErrorStringWithFormat("'src' argument doesn't exist: '%s'",
                                   src.ref().GetPath().c_str());
    return error;
  });
}

SBError SBPlatform::Install(SBFileSpec &src, SBFileSpec &dst) {
  LLDB_INSTRUMENT_VA(this, src, dst);
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    if (src.Exists())
      return platform_sp->Install(src.ref(), dst.ref());

    Status error;
    error.SetErrorStringWithFormat("'src' argument doesn't exist: '%s'",
                                   src.ref().GetPath().c_str());
    return error;
  });
}

SBError SBPlatform::Run(SBPlatformShellCommand &shell_command) {
  LLDB_INSTRUMENT_VA(this, shell_command);
  return ExecuteConnected(
      [&](const lldb::PlatformSP &platform_sp) {
        const char *command = shell_command.GetCommand();
        if (!command)
          return Status("invalid shell command (empty)");

        if (shell_command.GetWorkingDirectory() == nullptr) {
          std::string platform_working_dir =
              platform_sp->GetWorkingDirectory().GetPath();
          if (!platform_working_dir.empty())
            shell_command.SetWorkingDirectory(platform_working_dir.c_str());
        }
        return platform_sp->RunShellCommand(
            shell_command.m_opaque_ptr->m_shell, command,
            FileSpec(shell_command.GetWorkingDirectory()),
            &shell_command.m_opaque_ptr->m_status,
            &shell_command.m_opaque_ptr->m_signo,
            &shell_command.m_opaque_ptr->m_output,
            shell_command.m_opaque_ptr->m_timeout);
      });
}

SBError SBPlatform::Launch(SBLaunchInfo &launch_info) {
  LLDB_INSTRUMENT_VA(this, launch_info);
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    ProcessLaunchInfo info = launch_info.ref();
    Status error = platform_sp->LaunchProcess(info);
    launch_info.set_ref(info);
    return error;
  });
}

SBProcess SBPlatform::Attach(SBAttachInfo &attach_info,
                             const SBDebugger &debugger, SBTarget &target,
                             SBError &error) {
  LLDB_INSTRUMENT_VA(this, attach_info, debugger, target, error);

  if (PlatformSP platform_sp = GetSP()) {
    if (platform_sp->IsConnected()) {
      ProcessAttachInfo &info = attach_info.ref();
      Status status;
      ProcessSP process_sp = platform_sp->Attach(info, debugger.ref(),
                                                 target.GetSP().get(), status);
      error.SetError(status);
      return SBProcess(process_sp);
    }

    error.SetErrorString("not connected");
    return {};
  }

  error.SetErrorString("invalid platform");
  return {};
}

SBProcessInfoList SBPlatform::GetAllProcesses(SBError &error) {
  if (PlatformSP platform_sp = GetSP()) {
    if (platform_sp->IsConnected()) {
      ProcessInstanceInfoList list = platform_sp->GetAllProcesses();
      return SBProcessInfoList(list);
    }
    error.SetErrorString("not connected");
    return {};
  }

  error.SetErrorString("invalid platform");
  return {};
}

SBError SBPlatform::Kill(const lldb::pid_t pid) {
  LLDB_INSTRUMENT_VA(this, pid);
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    return platform_sp->KillProcess(pid);
  });
}

SBError SBPlatform::ExecuteConnected(
    const std::function<Status(const lldb::PlatformSP &)> &func) {
  SBError sb_error;
  const auto platform_sp(GetSP());
  if (platform_sp) {
    if (platform_sp->IsConnected())
      sb_error.ref() = func(platform_sp);
    else
      sb_error.SetErrorString("not connected");
  } else
    sb_error.SetErrorString("invalid platform");

  return sb_error;
}

SBError SBPlatform::MakeDirectory(const char *path, uint32_t file_permissions) {
  LLDB_INSTRUMENT_VA(this, path, file_permissions);

  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    sb_error.ref() =
        platform_sp->MakeDirectory(FileSpec(path), file_permissions);
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

uint32_t SBPlatform::GetFilePermissions(const char *path) {
  LLDB_INSTRUMENT_VA(this, path);

  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    uint32_t file_permissions = 0;
    platform_sp->GetFilePermissions(FileSpec(path), file_permissions);
    return file_permissions;
  }
  return 0;
}

SBError SBPlatform::SetFilePermissions(const char *path,
                                       uint32_t file_permissions) {
  LLDB_INSTRUMENT_VA(this, path, file_permissions);

  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    sb_error.ref() =
        platform_sp->SetFilePermissions(FileSpec(path), file_permissions);
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

SBUnixSignals SBPlatform::GetUnixSignals() const {
  LLDB_INSTRUMENT_VA(this);

  if (auto platform_sp = GetSP())
    return SBUnixSignals{platform_sp};

  return SBUnixSignals();
}

SBEnvironment SBPlatform::GetEnvironment() {
  LLDB_INSTRUMENT_VA(this);
  PlatformSP platform_sp(GetSP());

  if (platform_sp) {
    return SBEnvironment(platform_sp->GetEnvironment());
  }

  return SBEnvironment();
}

SBError SBPlatform::SetLocateModuleCallback(
    lldb::SBPlatformLocateModuleCallback callback, void *callback_baton) {
  LLDB_INSTRUMENT_VA(this, callback, callback_baton);
  PlatformSP platform_sp(GetSP());
  if (!platform_sp)
    return SBError("invalid platform");

  if (!callback) {
    // Clear the callback.
    platform_sp->SetLocateModuleCallback(nullptr);
    return SBError();
  }

  // Platform.h does not accept lldb::SBPlatformLocateModuleCallback directly
  // because of the SBModuleSpec and SBFileSpec dependencies. Use a lambda to
  // convert ModuleSpec/FileSpec <--> SBModuleSpec/SBFileSpec for the callback
  // arguments.
  platform_sp->SetLocateModuleCallback(
      [callback, callback_baton](const ModuleSpec &module_spec,
                                 FileSpec &module_file_spec,
                                 FileSpec &symbol_file_spec) {
        SBModuleSpec module_spec_sb(module_spec);
        SBFileSpec module_file_spec_sb;
        SBFileSpec symbol_file_spec_sb;

        SBError error = callback(callback_baton, module_spec_sb,
                                 module_file_spec_sb, symbol_file_spec_sb);

        if (error.Success()) {
          module_file_spec = module_file_spec_sb.ref();
          symbol_file_spec = symbol_file_spec_sb.ref();
        }

        return error.ref();
      });
  return SBError();
}
