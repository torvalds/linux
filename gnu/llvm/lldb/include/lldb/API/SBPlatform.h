//===-- SBPlatform.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBPLATFORM_H
#define LLDB_API_SBPLATFORM_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBProcessInfoList.h"

#include <functional>

struct PlatformConnectOptions;
struct PlatformShellCommand;
class ProcessInstanceInfoMatch;

namespace lldb {

class SBAttachInfo;
class SBLaunchInfo;

class LLDB_API SBPlatformConnectOptions {
public:
  SBPlatformConnectOptions(const char *url);

  SBPlatformConnectOptions(const SBPlatformConnectOptions &rhs);

  ~SBPlatformConnectOptions();

  SBPlatformConnectOptions &operator=(const SBPlatformConnectOptions &rhs);

  const char *GetURL();

  void SetURL(const char *url);

  bool GetRsyncEnabled();

  void EnableRsync(const char *options, const char *remote_path_prefix,
                   bool omit_remote_hostname);

  void DisableRsync();

  const char *GetLocalCacheDirectory();

  void SetLocalCacheDirectory(const char *path);

protected:
  PlatformConnectOptions *m_opaque_ptr;
};

class LLDB_API SBPlatformShellCommand {
public:
  SBPlatformShellCommand(const char *shell, const char *shell_command);
  SBPlatformShellCommand(const char *shell_command);

  SBPlatformShellCommand(const SBPlatformShellCommand &rhs);

  SBPlatformShellCommand &operator=(const SBPlatformShellCommand &rhs);

  ~SBPlatformShellCommand();

  void Clear();

  const char *GetShell();

  void SetShell(const char *shell);

  const char *GetCommand();

  void SetCommand(const char *shell_command);

  const char *GetWorkingDirectory();

  void SetWorkingDirectory(const char *path);

  uint32_t GetTimeoutSeconds();

  void SetTimeoutSeconds(uint32_t sec);

  int GetSignal();

  int GetStatus();

  const char *GetOutput();

protected:
  friend class SBPlatform;

  PlatformShellCommand *m_opaque_ptr;
};

class LLDB_API SBPlatform {
public:
  SBPlatform();

  SBPlatform(const char *platform_name);

  SBPlatform(const SBPlatform &rhs);

  SBPlatform &operator=(const SBPlatform &rhs);

  ~SBPlatform();

  static SBPlatform GetHostPlatform();

  explicit operator bool() const;

  bool IsValid() const;

  void Clear();

  const char *GetWorkingDirectory();

  bool SetWorkingDirectory(const char *path);

  const char *GetName();

  SBError ConnectRemote(SBPlatformConnectOptions &connect_options);

  void DisconnectRemote();

  bool IsConnected();

  // The following functions will work if the platform is connected
  const char *GetTriple();

  const char *GetHostname();

  const char *GetOSBuild();

  const char *GetOSDescription();

  uint32_t GetOSMajorVersion();

  uint32_t GetOSMinorVersion();

  uint32_t GetOSUpdateVersion();

  void SetSDKRoot(const char *sysroot);

  SBError Put(SBFileSpec &src, SBFileSpec &dst);

  SBError Get(SBFileSpec &src, SBFileSpec &dst);

  SBError Install(SBFileSpec &src, SBFileSpec &dst);

  SBError Run(SBPlatformShellCommand &shell_command);

  SBError Launch(SBLaunchInfo &launch_info);

  SBProcess Attach(SBAttachInfo &attach_info, const SBDebugger &debugger,
                   SBTarget &target, SBError &error);

  SBProcessInfoList GetAllProcesses(SBError &error);

  SBError Kill(const lldb::pid_t pid);

  SBError
  MakeDirectory(const char *path,
                uint32_t file_permissions = eFilePermissionsDirectoryDefault);

  uint32_t GetFilePermissions(const char *path);

  SBError SetFilePermissions(const char *path, uint32_t file_permissions);

  SBUnixSignals GetUnixSignals() const;

  /// Return the environment variables of the remote platform connection
  /// process.
  ///
  /// \return
  ///     An lldb::SBEnvironment object which is a copy of the platform's
  ///     environment.
  SBEnvironment GetEnvironment();

  /// Set a callback as an implementation for locating module in order to
  /// implement own module cache system. For example, to leverage distributed
  /// build system, to bypass pulling files from remote platform, or to search
  /// symbol files from symbol servers. The target will call this callback to
  /// get a module file and a symbol file, and it will fallback to the LLDB
  /// implementation when this callback failed or returned non-existent file.
  /// This callback can set either module_file_spec or symbol_file_spec, or both
  /// module_file_spec and symbol_file_spec. The callback will be cleared if
  /// nullptr or None is set.
  SBError SetLocateModuleCallback(lldb::SBPlatformLocateModuleCallback callback,
                                  void *callback_baton);

protected:
  friend class SBDebugger;
  friend class SBTarget;

  lldb::PlatformSP GetSP() const;

  void SetSP(const lldb::PlatformSP &platform_sp);

  SBError ExecuteConnected(
      const std::function<lldb_private::Status(const lldb::PlatformSP &)>
          &func);

  lldb::PlatformSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBPLATFORM_H
