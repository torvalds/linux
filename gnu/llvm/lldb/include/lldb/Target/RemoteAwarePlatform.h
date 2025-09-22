//===-- RemoteAwarePlatform.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_REMOTEAWAREPLATFORM_H
#define LLDB_TARGET_REMOTEAWAREPLATFORM_H

#include "lldb/Target/Platform.h"
#include <optional>

namespace lldb_private {

/// A base class for platforms which automatically want to be able to forward
/// operations to a remote platform instance (such as PlatformRemoteGDBServer).
class RemoteAwarePlatform : public Platform {
public:
  using Platform::Platform;

  virtual Status
  ResolveExecutable(const ModuleSpec &module_spec,
                    lldb::ModuleSP &exe_module_sp,
                    const FileSpecList *module_search_paths_ptr) override;

  bool GetModuleSpec(const FileSpec &module_file_spec, const ArchSpec &arch,
                     ModuleSpec &module_spec) override;

  lldb::user_id_t OpenFile(const FileSpec &file_spec, File::OpenOptions flags,
                           uint32_t mode, Status &error) override;

  bool CloseFile(lldb::user_id_t fd, Status &error) override;

  uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                    uint64_t dst_len, Status &error) override;

  uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset, const void *src,
                     uint64_t src_len, Status &error) override;

  lldb::user_id_t GetFileSize(const FileSpec &file_spec) override;

  Status CreateSymlink(const FileSpec &src, const FileSpec &dst) override;

  bool GetFileExists(const FileSpec &file_spec) override;

  Status Unlink(const FileSpec &file_spec) override;

  FileSpec GetRemoteWorkingDirectory() override;

  bool SetRemoteWorkingDirectory(const FileSpec &working_dir) override;

  Status MakeDirectory(const FileSpec &file_spec, uint32_t mode) override;

  Status GetFilePermissions(const FileSpec &file_spec,
                            uint32_t &file_permissions) override;

  Status SetFilePermissions(const FileSpec &file_spec,
                            uint32_t file_permissions) override;

  llvm::ErrorOr<llvm::MD5::MD5Result>
  CalculateMD5(const FileSpec &file_spec) override;

  Status GetFileWithUUID(const FileSpec &platform_file, const UUID *uuid,
                         FileSpec &local_file) override;

  bool GetRemoteOSVersion() override;
  std::optional<std::string> GetRemoteOSBuildString() override;
  std::optional<std::string> GetRemoteOSKernelDescription() override;
  ArchSpec GetRemoteSystemArchitecture() override;

  Status RunShellCommand(llvm::StringRef command, const FileSpec &working_dir,
                         int *status_ptr, int *signo_ptr,
                         std::string *command_output,
                         const Timeout<std::micro> &timeout) override;

  Status RunShellCommand(llvm::StringRef interpreter, llvm::StringRef command,
                         const FileSpec &working_dir, int *status_ptr,
                         int *signo_ptr, std::string *command_output,
                         const Timeout<std::micro> &timeout) override;

  const char *GetHostname() override;
  UserIDResolver &GetUserIDResolver() override;
  lldb_private::Environment GetEnvironment() override;

  bool IsConnected() const override;

  bool GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &proc_info) override;
  uint32_t FindProcesses(const ProcessInstanceInfoMatch &match_info,
                         ProcessInstanceInfoList &process_infos) override;

  lldb::ProcessSP ConnectProcess(llvm::StringRef connect_url,
                                 llvm::StringRef plugin_name,
                                 Debugger &debugger, Target *target,
                                 Status &error) override;

  Status LaunchProcess(ProcessLaunchInfo &launch_info) override;

  Status KillProcess(const lldb::pid_t pid) override;

  size_t ConnectToWaitingProcesses(Debugger &debugger,
                                   Status &error) override;

protected:
  lldb::PlatformSP m_remote_platform_sp;
};

} // namespace lldb_private

#endif // LLDB_TARGET_REMOTEAWAREPLATFORM_H
