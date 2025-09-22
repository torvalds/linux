//===-- Host.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOST_H
#define LLDB_HOST_HOST_H

#include "lldb/Host/File.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Environment.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-private.h"
#include <cerrno>
#include <cstdarg>
#include <map>
#include <string>
#include <type_traits>

namespace lldb_private {

class FileAction;
class ProcessLaunchInfo;
class ProcessInstanceInfo;
class ProcessInstanceInfoMatch;
typedef std::vector<ProcessInstanceInfo> ProcessInstanceInfoList;

// Exit Type for inferior processes
struct WaitStatus {
  enum Type : uint8_t {
    Exit,   // The status represents the return code from normal
            // program exit (i.e. WIFEXITED() was true)
    Signal, // The status represents the signal number that caused
            // the program to exit (i.e. WIFSIGNALED() was true)
    Stop,   // The status represents the signal number that caused the
            // program to stop (i.e. WIFSTOPPED() was true)
  };

  Type type;
  uint8_t status;

  WaitStatus(Type type, uint8_t status) : type(type), status(status) {}

  static WaitStatus Decode(int wstatus);
};

inline bool operator==(WaitStatus a, WaitStatus b) {
  return a.type == b.type && a.status == b.status;
}

inline bool operator!=(WaitStatus a, WaitStatus b) { return !(a == b); }

/// \class Host Host.h "lldb/Host/Host.h"
/// A class that provides host computer information.
///
/// Host is a class that answers information about the host operating system.
class Host {
public:
  typedef std::function<void(lldb::pid_t pid,
                             int signal,  // Zero for no signal
                             int status)> // Exit value of process if signal is
                                          // zero
      MonitorChildProcessCallback;

  /// Start monitoring a child process.
  ///
  /// Allows easy monitoring of child processes. \a callback will be called
  /// when the child process exits or if it dies from a signal.
  ///
  /// \param[in] callback
  ///     A function callback to call when a child receives a signal
  ///     or exits.
  ///
  /// \param[in] pid
  ///     The process ID of a child process to monitor.
  ///
  /// \return
  ///     A thread handle that can be used to cancel the thread that
  ///     was spawned to monitor \a pid.
  static llvm::Expected<HostThread>
  StartMonitoringChildProcess(const MonitorChildProcessCallback &callback,
                              lldb::pid_t pid);

  /// Emit the given message to the operating system log.
  static void SystemLog(lldb::Severity severity, llvm::StringRef message);

  /// Get the process ID for the calling process.
  ///
  /// \return
  ///     The process ID for the current process.
  static lldb::pid_t GetCurrentProcessID();

  static void Kill(lldb::pid_t pid, int signo);

  /// Get the thread token (the one returned by ThreadCreate when the thread
  /// was created) for the calling thread in the current process.
  ///
  /// \return
  ///     The thread token for the calling thread in the current process.
  static lldb::thread_t GetCurrentThread();

  static const char *GetSignalAsCString(int signo);

  /// Given an address in the current process (the process that is running the
  /// LLDB code), return the name of the module that it comes from. This can
  /// be useful when you need to know the path to the shared library that your
  /// code is running in for loading resources that are relative to your
  /// binary.
  ///
  /// \param[in] host_addr
  ///     The pointer to some code in the current process.
  ///
  /// \return
  ///     \b A file spec with the module that contains \a host_addr,
  ///     which may be invalid if \a host_addr doesn't fall into
  ///     any valid module address range.
  static FileSpec GetModuleFileSpecForHostAddress(const void *host_addr);

  /// If you have an executable that is in a bundle and want to get back to
  /// the bundle directory from the path itself, this function will change a
  /// path to a file within a bundle to the bundle directory itself.
  ///
  /// \param[in] file
  ///     A file spec that might point to a file in a bundle.
  ///
  /// \param[out] bundle_directory
  ///     An object will be filled in with the bundle directory for
  ///     the bundle when \b true is returned. Otherwise \a file is
  ///     left untouched and \b false is returned.
  ///
  /// \return
  ///     \b true if \a file was resolved in \a bundle_directory,
  ///     \b false otherwise.
  static bool GetBundleDirectory(const FileSpec &file,
                                 FileSpec &bundle_directory);

  /// When executable files may live within a directory, where the directory
  /// represents an executable bundle (like the MacOSX app bundles), then
  /// locate the executable within the containing bundle.
  ///
  /// \param[in,out] file
  ///     A file spec that currently points to the bundle that will
  ///     be filled in with the executable path within the bundle
  ///     if \b true is returned. Otherwise \a file is left untouched.
  ///
  /// \return
  ///     \b true if \a file was resolved, \b false if this function
  ///     was not able to resolve the path.
  static bool ResolveExecutableInBundle(FileSpec &file);

  static uint32_t FindProcesses(const ProcessInstanceInfoMatch &match_info,
                                ProcessInstanceInfoList &proc_infos);

  typedef std::map<lldb::pid_t, bool> TidMap;
  typedef std::pair<lldb::pid_t, bool> TidPair;
  static bool FindProcessThreads(const lldb::pid_t pid, TidMap &tids_to_attach);

  static bool GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &proc_info);

  /// Launch the process specified in launch_info. The monitoring callback in
  /// launch_info must be set, and it will be called when the process
  /// terminates.
  static Status LaunchProcess(ProcessLaunchInfo &launch_info);

  /// Perform expansion of the command-line for this launch info This can
  /// potentially involve wildcard expansion
  /// environment variable replacement, and whatever other
  /// argument magic the platform defines as part of its typical
  /// user experience
  static Status ShellExpandArguments(ProcessLaunchInfo &launch_info);

  /// Run a shell command.
  /// \arg command  shouldn't be empty
  /// \arg working_dir Pass empty FileSpec to use the current working directory
  /// \arg status_ptr  Pass NULL if you don't want the process exit status
  /// \arg signo_ptr   Pass NULL if you don't want the signal that caused the
  ///                  process to exit
  /// \arg command_output  Pass NULL if you don't want the command output
  /// \arg hide_stderr if this is false, redirect stderr to stdout
  static Status RunShellCommand(llvm::StringRef command,
                                const FileSpec &working_dir, int *status_ptr,
                                int *signo_ptr, std::string *command_output,
                                const Timeout<std::micro> &timeout,
                                bool run_in_shell = true,
                                bool hide_stderr = false);

  /// Run a shell command.
  /// \arg shell  Pass an empty string if you want to use the default shell
  /// interpreter \arg command \arg working_dir  Pass empty FileSpec to use the
  /// current working directory \arg status_ptr   Pass NULL if you don't want
  /// the process exit status \arg signo_ptr    Pass NULL if you don't want the
  /// signal that caused
  ///                   the process to exit
  /// \arg command_output  Pass NULL if you don't want the command output
  /// \arg hide_stderr  If this is \b false, redirect stderr to stdout
  static Status RunShellCommand(llvm::StringRef shell, llvm::StringRef command,
                                const FileSpec &working_dir, int *status_ptr,
                                int *signo_ptr, std::string *command_output,
                                const Timeout<std::micro> &timeout,
                                bool run_in_shell = true,
                                bool hide_stderr = false);

  /// Run a shell command.
  /// \arg working_dir Pass empty FileSpec to use the current working directory
  /// \arg status_ptr  Pass NULL if you don't want the process exit status
  /// \arg signo_ptr   Pass NULL if you don't want the signal that caused the
  ///                  process to exit
  /// \arg command_output  Pass NULL if you don't want the command output
  /// \arg hide_stderr if this is false, redirect stderr to stdout
  static Status RunShellCommand(const Args &args, const FileSpec &working_dir,
                                int *status_ptr, int *signo_ptr,
                                std::string *command_output,
                                const Timeout<std::micro> &timeout,
                                bool run_in_shell = true,
                                bool hide_stderr = false);

  /// Run a shell command.
  /// \arg shell            Pass an empty string if you want to use the default
  /// shell interpreter \arg command \arg working_dir Pass empty FileSpec to use
  /// the current working directory \arg status_ptr    Pass NULL if you don't
  /// want the process exit status \arg signo_ptr     Pass NULL if you don't
  /// want the signal that caused the
  ///               process to exit
  /// \arg command_output  Pass NULL if you don't want the command output
  /// \arg hide_stderr If this is \b false, redirect stderr to stdout
  static Status RunShellCommand(llvm::StringRef shell, const Args &args,
                                const FileSpec &working_dir, int *status_ptr,
                                int *signo_ptr, std::string *command_output,
                                const Timeout<std::micro> &timeout,
                                bool run_in_shell = true,
                                bool hide_stderr = false);

  static llvm::Error OpenFileInExternalEditor(llvm::StringRef editor,
                                              const FileSpec &file_spec,
                                              uint32_t line_no);

  /// Check if we're running in an interactive graphical session.
  ///
  /// \return
  ///     True if we're running in an interactive graphical session. False if
  ///     we're not or don't know.
  static bool IsInteractiveGraphicSession();

  static Environment GetEnvironment();

  static std::unique_ptr<Connection>
  CreateDefaultConnection(llvm::StringRef url);

protected:
  static uint32_t FindProcessesImpl(const ProcessInstanceInfoMatch &match_info,
                                    ProcessInstanceInfoList &proc_infos);
};

/// Log handler that emits log messages to the operating system log.
class SystemLogHandler : public LogHandler {
public:
  SystemLogHandler();
  void Emit(llvm::StringRef message) override;

  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const LogHandler *obj) { return obj->isA(&ID); }

private:
  static char ID;
};

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb_private::WaitStatus> {
  /// Options = "" gives a human readable description of the status Options =
  /// "g" gives a gdb-remote protocol status (e.g., X09)
  static void format(const lldb_private::WaitStatus &WS, raw_ostream &OS,
                     llvm::StringRef Options);
};
} // namespace llvm

#endif // LLDB_HOST_HOST_H
