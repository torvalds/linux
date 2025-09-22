//===-- RemoteAwarePlatform.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/RemoteAwarePlatform.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/StreamString.h"
#include <optional>

using namespace lldb_private;
using namespace lldb;

bool RemoteAwarePlatform::GetModuleSpec(const FileSpec &module_file_spec,
                                        const ArchSpec &arch,
                                        ModuleSpec &module_spec) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetModuleSpec(module_file_spec, arch,
                                               module_spec);

  return false;
}

Status RemoteAwarePlatform::ResolveExecutable(
    const ModuleSpec &module_spec, lldb::ModuleSP &exe_module_sp,
    const FileSpecList *module_search_paths_ptr) {
  ModuleSpec resolved_module_spec(module_spec);

  // The host platform can resolve the path more aggressively.
  if (IsHost()) {
    FileSpec &resolved_file_spec = resolved_module_spec.GetFileSpec();

    if (!FileSystem::Instance().Exists(resolved_file_spec)) {
      resolved_module_spec.GetFileSpec().SetFile(resolved_file_spec.GetPath(),
                                                 FileSpec::Style::native);
      FileSystem::Instance().Resolve(resolved_file_spec);
    }

    if (!FileSystem::Instance().Exists(resolved_file_spec))
      FileSystem::Instance().ResolveExecutableLocation(resolved_file_spec);
  } else if (m_remote_platform_sp) {
    return GetCachedExecutable(resolved_module_spec, exe_module_sp,
                               module_search_paths_ptr);
  }

  return Platform::ResolveExecutable(resolved_module_spec, exe_module_sp,
                                     module_search_paths_ptr);
}

Status RemoteAwarePlatform::RunShellCommand(
    llvm::StringRef command, const FileSpec &working_dir, int *status_ptr,
    int *signo_ptr, std::string *command_output,
    const Timeout<std::micro> &timeout) {
  return RunShellCommand(llvm::StringRef(), command, working_dir, status_ptr,
                         signo_ptr, command_output, timeout);
}

Status RemoteAwarePlatform::RunShellCommand(
    llvm::StringRef shell, llvm::StringRef command, const FileSpec &working_dir,
    int *status_ptr, int *signo_ptr, std::string *command_output,
    const Timeout<std::micro> &timeout) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->RunShellCommand(shell, command, working_dir,
                                                 status_ptr, signo_ptr,
                                                 command_output, timeout);
  return Platform::RunShellCommand(shell, command, working_dir, status_ptr,
                                   signo_ptr, command_output, timeout);
}

Status RemoteAwarePlatform::MakeDirectory(const FileSpec &file_spec,
                                          uint32_t file_permissions) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->MakeDirectory(file_spec, file_permissions);
  return Platform::MakeDirectory(file_spec, file_permissions);
}

Status RemoteAwarePlatform::GetFilePermissions(const FileSpec &file_spec,
                                               uint32_t &file_permissions) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetFilePermissions(file_spec,
                                                    file_permissions);
  return Platform::GetFilePermissions(file_spec, file_permissions);
}

Status RemoteAwarePlatform::SetFilePermissions(const FileSpec &file_spec,
                                               uint32_t file_permissions) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->SetFilePermissions(file_spec,
                                                    file_permissions);
  return Platform::SetFilePermissions(file_spec, file_permissions);
}

lldb::user_id_t RemoteAwarePlatform::OpenFile(const FileSpec &file_spec,
                                              File::OpenOptions flags,
                                              uint32_t mode, Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->OpenFile(file_spec, flags, mode, error);
  return Platform::OpenFile(file_spec, flags, mode, error);
}

bool RemoteAwarePlatform::CloseFile(lldb::user_id_t fd, Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->CloseFile(fd, error);
  return Platform::CloseFile(fd, error);
}

uint64_t RemoteAwarePlatform::ReadFile(lldb::user_id_t fd, uint64_t offset,
                                       void *dst, uint64_t dst_len,
                                       Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->ReadFile(fd, offset, dst, dst_len, error);
  return Platform::ReadFile(fd, offset, dst, dst_len, error);
}

uint64_t RemoteAwarePlatform::WriteFile(lldb::user_id_t fd, uint64_t offset,
                                        const void *src, uint64_t src_len,
                                        Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->WriteFile(fd, offset, src, src_len, error);
  return Platform::WriteFile(fd, offset, src, src_len, error);
}

lldb::user_id_t RemoteAwarePlatform::GetFileSize(const FileSpec &file_spec) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetFileSize(file_spec);
  return Platform::GetFileSize(file_spec);
}

Status RemoteAwarePlatform::CreateSymlink(const FileSpec &src,
                                          const FileSpec &dst) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->CreateSymlink(src, dst);
  return Platform::CreateSymlink(src, dst);
}

bool RemoteAwarePlatform::GetFileExists(const FileSpec &file_spec) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetFileExists(file_spec);
  return Platform::GetFileExists(file_spec);
}

Status RemoteAwarePlatform::Unlink(const FileSpec &file_spec) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->Unlink(file_spec);
  return Platform::Unlink(file_spec);
}

llvm::ErrorOr<llvm::MD5::MD5Result>
RemoteAwarePlatform::CalculateMD5(const FileSpec &file_spec) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->CalculateMD5(file_spec);
  return Platform::CalculateMD5(file_spec);
}

FileSpec RemoteAwarePlatform::GetRemoteWorkingDirectory() {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteWorkingDirectory();
  return Platform::GetRemoteWorkingDirectory();
}

bool RemoteAwarePlatform::SetRemoteWorkingDirectory(
    const FileSpec &working_dir) {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->SetRemoteWorkingDirectory(working_dir);
  return Platform::SetRemoteWorkingDirectory(working_dir);
}

Status RemoteAwarePlatform::GetFileWithUUID(const FileSpec &platform_file,
                                            const UUID *uuid_ptr,
                                            FileSpec &local_file) {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetFileWithUUID(platform_file, uuid_ptr,
                                                 local_file);

  // Default to the local case
  local_file = platform_file;
  return Status();
}

bool RemoteAwarePlatform::GetRemoteOSVersion() {
  if (m_remote_platform_sp) {
    m_os_version = m_remote_platform_sp->GetOSVersion();
    return !m_os_version.empty();
  }
  return false;
}

std::optional<std::string> RemoteAwarePlatform::GetRemoteOSBuildString() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteOSBuildString();
  return std::nullopt;
}

std::optional<std::string> RemoteAwarePlatform::GetRemoteOSKernelDescription() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteOSKernelDescription();
  return std::nullopt;
}

ArchSpec RemoteAwarePlatform::GetRemoteSystemArchitecture() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteSystemArchitecture();
  return ArchSpec();
}

const char *RemoteAwarePlatform::GetHostname() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetHostname();
  return Platform::GetHostname();
}

UserIDResolver &RemoteAwarePlatform::GetUserIDResolver() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetUserIDResolver();
  return Platform::GetUserIDResolver();
}

Environment RemoteAwarePlatform::GetEnvironment() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetEnvironment();
  return Platform::GetEnvironment();
}

bool RemoteAwarePlatform::IsConnected() const {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->IsConnected();
  return Platform::IsConnected();
}

bool RemoteAwarePlatform::GetProcessInfo(lldb::pid_t pid,
                                         ProcessInstanceInfo &process_info) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetProcessInfo(pid, process_info);
  return Platform::GetProcessInfo(pid, process_info);
}

uint32_t
RemoteAwarePlatform::FindProcesses(const ProcessInstanceInfoMatch &match_info,
                                   ProcessInstanceInfoList &process_infos) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->FindProcesses(match_info, process_infos);
  return Platform::FindProcesses(match_info, process_infos);
}

lldb::ProcessSP RemoteAwarePlatform::ConnectProcess(llvm::StringRef connect_url,
                                                    llvm::StringRef plugin_name,
                                                    Debugger &debugger,
                                                    Target *target,
                                                    Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->ConnectProcess(connect_url, plugin_name,
                                                debugger, target, error);
  return Platform::ConnectProcess(connect_url, plugin_name, debugger, target,
                                  error);
}

Status RemoteAwarePlatform::LaunchProcess(ProcessLaunchInfo &launch_info) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->LaunchProcess(launch_info);
  return Platform::LaunchProcess(launch_info);
}

Status RemoteAwarePlatform::KillProcess(const lldb::pid_t pid) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->KillProcess(pid);
  return Platform::KillProcess(pid);
}

size_t RemoteAwarePlatform::ConnectToWaitingProcesses(Debugger &debugger,
                                                Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->ConnectToWaitingProcesses(debugger, error);
  return Platform::ConnectToWaitingProcesses(debugger, error);
}
