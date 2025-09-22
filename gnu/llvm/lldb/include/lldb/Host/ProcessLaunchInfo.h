//===-- ProcessLaunchInfo.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_PROCESSLAUNCHINFO_H
#define LLDB_HOST_PROCESSLAUNCHINFO_H

// C++ Headers
#include <string>

// LLDB Headers
#include "lldb/Utility/Flags.h"

#include "lldb/Host/FileAction.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/ProcessInfo.h"

namespace lldb_private {

// ProcessLaunchInfo
//
// Describes any information that is required to launch a process.

class ProcessLaunchInfo : public ProcessInfo {
public:
  ProcessLaunchInfo();

  ProcessLaunchInfo(const FileSpec &stdin_file_spec,
                    const FileSpec &stdout_file_spec,
                    const FileSpec &stderr_file_spec,
                    const FileSpec &working_dir, uint32_t launch_flags);

  void AppendFileAction(const FileAction &info) {
    m_file_actions.push_back(info);
  }

  bool AppendCloseFileAction(int fd);

  bool AppendDuplicateFileAction(int fd, int dup_fd);

  bool AppendOpenFileAction(int fd, const FileSpec &file_spec, bool read,
                            bool write);

  bool AppendSuppressFileAction(int fd, bool read, bool write);

  // Redirect stdin/stdout/stderr to a pty, if no action for the respective file
  // descriptor is specified. (So if stdin and stdout already have file actions,
  // but stderr doesn't, then only stderr will be redirected to a pty.)
  llvm::Error SetUpPtyRedirection();

  size_t GetNumFileActions() const { return m_file_actions.size(); }

  const FileAction *GetFileActionAtIndex(size_t idx) const;

  const FileAction *GetFileActionForFD(int fd) const;

  Flags &GetFlags() { return m_flags; }

  const Flags &GetFlags() const { return m_flags; }

  const FileSpec &GetWorkingDirectory() const;

  void SetWorkingDirectory(const FileSpec &working_dir);

  llvm::StringRef GetProcessPluginName() const;

  void SetProcessPluginName(llvm::StringRef plugin);

  const FileSpec &GetShell() const;

  void SetShell(const FileSpec &shell);

  uint32_t GetResumeCount() const { return m_resume_count; }

  void SetResumeCount(uint32_t c) { m_resume_count = c; }

  bool GetLaunchInSeparateProcessGroup() const {
    return m_flags.Test(lldb::eLaunchFlagLaunchInSeparateProcessGroup);
  }

  void SetLaunchInSeparateProcessGroup(bool separate);

  bool GetShellExpandArguments() const {
    return m_flags.Test(lldb::eLaunchFlagShellExpandArguments);
  }

  void SetShellExpandArguments(bool expand);

  void Clear();

  bool ConvertArgumentsForLaunchingInShell(Status &error, bool will_debug,
                                           bool first_arg_is_full_shell_command,
                                           uint32_t num_resumes);

  void SetMonitorProcessCallback(Host::MonitorChildProcessCallback callback) {
    m_monitor_callback = std::move(callback);
  }

  const Host::MonitorChildProcessCallback &GetMonitorProcessCallback() const {
    return m_monitor_callback;
  }

  /// A Monitor callback which does not take any action on process events. Use
  /// this if you don't need to take any particular action when the process
  /// terminates, but you still need to reap it.
  static void NoOpMonitorCallback(lldb::pid_t pid, int signal, int status);

  // If the LaunchInfo has a monitor callback, then arrange to monitor the
  // process. Return true if the LaunchInfo has taken care of monitoring the
  // process, and false if the caller might want to monitor the process
  // themselves.

  bool MonitorProcess() const;

  PseudoTerminal &GetPTY() { return *m_pty; }

  void SetLaunchEventData(const char *data) { m_event_data.assign(data); }

  const char *GetLaunchEventData() const { return m_event_data.c_str(); }

  void SetDetachOnError(bool enable);

  bool GetDetachOnError() const {
    return m_flags.Test(lldb::eLaunchFlagDetachOnError);
  }

protected:
  FileSpec m_working_dir;
  std::string m_plugin_name;
  FileSpec m_shell;
  Flags m_flags; // Bitwise OR of bits from lldb::LaunchFlags
  std::vector<FileAction> m_file_actions; // File actions for any other files
  std::shared_ptr<PseudoTerminal> m_pty;
  uint32_t m_resume_count = 0; // How many times do we resume after launching
  Host::MonitorChildProcessCallback m_monitor_callback;
  std::string m_event_data; // A string passed to the plugin launch, having no
                            // meaning to the upper levels of lldb.
};
}

#endif // LLDB_HOST_PROCESSLAUNCHINFO_H
