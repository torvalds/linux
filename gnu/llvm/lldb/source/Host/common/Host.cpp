//===-- Host.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// C includes
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sys/types.h>
#ifndef _WIN32
#include <dlfcn.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__) ||                              \
    defined(__FreeBSD_kernel__) || defined(__APPLE__) ||                       \
    defined(__NetBSD__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
#if !defined(__ANDROID__)
#include <spawn.h>
#endif
#include <sys/syscall.h>
#include <sys/wait.h>
#endif

#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif

#if defined(__NetBSD__)
#include <lwp.h>
#endif

#include <csignal>

#include "lldb/Host/FileAction.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/MonitoringProcessLauncher.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Host/ProcessLauncher.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/posix/ConnectionFileDescriptorPosix.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-forward.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"

#if defined(_WIN32)
#include "lldb/Host/windows/ConnectionGenericFileWindows.h"
#include "lldb/Host/windows/ProcessLauncherWindows.h"
#else
#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#endif

#if defined(__APPLE__)
#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

extern "C" {
int __pthread_chdir(const char *path);
int __pthread_fchdir(int fildes);
}

#endif

using namespace lldb;
using namespace lldb_private;

#if !defined(__APPLE__)
#if !defined(_WIN32)
#include <syslog.h>
void Host::SystemLog(Severity severity, llvm::StringRef message) {
  static llvm::once_flag g_openlog_once;
  llvm::call_once(g_openlog_once, [] {
    openlog("lldb", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);
  });
  int level = LOG_DEBUG;
  switch (severity) {
  case lldb::eSeverityInfo:
    level = LOG_INFO;
    break;
  case lldb::eSeverityWarning:
    level = LOG_WARNING;
    break;
  case lldb::eSeverityError:
    level = LOG_ERR;
    break;
  }
  syslog(level, "%s", message.data());
}
#else
void Host::SystemLog(Severity severity, llvm::StringRef message) {
  switch (severity) {
  case lldb::eSeverityInfo:
  case lldb::eSeverityWarning:
    llvm::outs() << message;
    break;
  case lldb::eSeverityError:
    llvm::errs() << message;
    break;
  }
}
#endif
#endif

#if !defined(__APPLE__) && !defined(_WIN32)
static thread_result_t
MonitorChildProcessThreadFunction(::pid_t pid,
                                  Host::MonitorChildProcessCallback callback);

llvm::Expected<HostThread> Host::StartMonitoringChildProcess(
    const Host::MonitorChildProcessCallback &callback, lldb::pid_t pid) {
  char thread_name[256];
  ::snprintf(thread_name, sizeof(thread_name),
             "<lldb.host.wait4(pid=%" PRIu64 ")>", pid);
  assert(pid <= UINT32_MAX);
  return ThreadLauncher::LaunchThread(thread_name, [pid, callback] {
    return MonitorChildProcessThreadFunction(pid, callback);
  });
}

#ifndef __linux__
// Scoped class that will disable thread canceling when it is constructed, and
// exception safely restore the previous value it when it goes out of scope.
class ScopedPThreadCancelDisabler {
public:
  ScopedPThreadCancelDisabler() {
    // Disable the ability for this thread to be cancelled
    int err = ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &m_old_state);
    if (err != 0)
      m_old_state = -1;
  }

  ~ScopedPThreadCancelDisabler() {
    // Restore the ability for this thread to be cancelled to what it
    // previously was.
    if (m_old_state != -1)
      ::pthread_setcancelstate(m_old_state, 0);
  }

private:
  int m_old_state; // Save the old cancelability state.
};
#endif // __linux__

#ifdef __linux__
static thread_local volatile sig_atomic_t g_usr1_called;

static void SigUsr1Handler(int) { g_usr1_called = 1; }
#endif // __linux__

static bool CheckForMonitorCancellation() {
#ifdef __linux__
  if (g_usr1_called) {
    g_usr1_called = 0;
    return true;
  }
#else
  ::pthread_testcancel();
#endif
  return false;
}

static thread_result_t
MonitorChildProcessThreadFunction(::pid_t pid,
                                  Host::MonitorChildProcessCallback callback) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "pid = {0}", pid);

  int status = -1;

#ifdef __linux__
  // This signal is only used to interrupt the thread from waitpid
  struct sigaction sigUsr1Action;
  memset(&sigUsr1Action, 0, sizeof(sigUsr1Action));
  sigUsr1Action.sa_handler = SigUsr1Handler;
  ::sigaction(SIGUSR1, &sigUsr1Action, nullptr);
#endif // __linux__

  while (true) {
    log = GetLog(LLDBLog::Process);
    LLDB_LOG(log, "::waitpid({0}, &status, 0)...", pid);

    if (CheckForMonitorCancellation())
      return nullptr;

    const ::pid_t wait_pid = ::waitpid(pid, &status, 0);

    LLDB_LOG(log, "::waitpid({0}, &status, 0) => pid = {1}, status = {2:x}", pid,
             wait_pid, status);

    if (CheckForMonitorCancellation())
      return nullptr;

    if (wait_pid != -1)
      break;
    if (errno != EINTR) {
      LLDB_LOG(log, "pid = {0}, thread exiting because waitpid failed ({1})...",
               pid, llvm::sys::StrError());
      return nullptr;
    }
  }

  int signal = 0;
  int exit_status = 0;
  if (WIFEXITED(status)) {
    exit_status = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    signal = WTERMSIG(status);
    exit_status = -1;
  } else {
    llvm_unreachable("Unknown status");
  }

  // Scope for pthread_cancel_disabler
  {
#ifndef __linux__
    ScopedPThreadCancelDisabler pthread_cancel_disabler;
#endif

    if (callback)
      callback(pid, signal, exit_status);
  }

  LLDB_LOG(GetLog(LLDBLog::Process), "pid = {0} thread exiting...", pid);
  return nullptr;
}

#endif // #if !defined (__APPLE__) && !defined (_WIN32)

lldb::pid_t Host::GetCurrentProcessID() { return ::getpid(); }

#ifndef _WIN32

lldb::thread_t Host::GetCurrentThread() {
  return lldb::thread_t(pthread_self());
}

const char *Host::GetSignalAsCString(int signo) {
  switch (signo) {
  case SIGHUP:
    return "SIGHUP"; // 1    hangup
  case SIGINT:
    return "SIGINT"; // 2    interrupt
  case SIGQUIT:
    return "SIGQUIT"; // 3    quit
  case SIGILL:
    return "SIGILL"; // 4    illegal instruction (not reset when caught)
  case SIGTRAP:
    return "SIGTRAP"; // 5    trace trap (not reset when caught)
  case SIGABRT:
    return "SIGABRT"; // 6    abort()
#if defined(SIGPOLL)
#if !defined(SIGIO) || (SIGPOLL != SIGIO)
  // Under some GNU/Linux, SIGPOLL and SIGIO are the same. Causing the build to
  // fail with 'multiple define cases with same value'
  case SIGPOLL:
    return "SIGPOLL"; // 7    pollable event ([XSR] generated, not supported)
#endif
#endif
#if defined(SIGEMT)
  case SIGEMT:
    return "SIGEMT"; // 7    EMT instruction
#endif
  case SIGFPE:
    return "SIGFPE"; // 8    floating point exception
  case SIGKILL:
    return "SIGKILL"; // 9    kill (cannot be caught or ignored)
  case SIGBUS:
    return "SIGBUS"; // 10    bus error
  case SIGSEGV:
    return "SIGSEGV"; // 11    segmentation violation
  case SIGSYS:
    return "SIGSYS"; // 12    bad argument to system call
  case SIGPIPE:
    return "SIGPIPE"; // 13    write on a pipe with no one to read it
  case SIGALRM:
    return "SIGALRM"; // 14    alarm clock
  case SIGTERM:
    return "SIGTERM"; // 15    software termination signal from kill
  case SIGURG:
    return "SIGURG"; // 16    urgent condition on IO channel
  case SIGSTOP:
    return "SIGSTOP"; // 17    sendable stop signal not from tty
  case SIGTSTP:
    return "SIGTSTP"; // 18    stop signal from tty
  case SIGCONT:
    return "SIGCONT"; // 19    continue a stopped process
  case SIGCHLD:
    return "SIGCHLD"; // 20    to parent on child stop or exit
  case SIGTTIN:
    return "SIGTTIN"; // 21    to readers pgrp upon background tty read
  case SIGTTOU:
    return "SIGTTOU"; // 22    like TTIN for output if (tp->t_local&LTOSTOP)
#if defined(SIGIO)
  case SIGIO:
    return "SIGIO"; // 23    input/output possible signal
#endif
  case SIGXCPU:
    return "SIGXCPU"; // 24    exceeded CPU time limit
  case SIGXFSZ:
    return "SIGXFSZ"; // 25    exceeded file size limit
  case SIGVTALRM:
    return "SIGVTALRM"; // 26    virtual time alarm
  case SIGPROF:
    return "SIGPROF"; // 27    profiling time alarm
#if defined(SIGWINCH)
  case SIGWINCH:
    return "SIGWINCH"; // 28    window size changes
#endif
#if defined(SIGINFO)
  case SIGINFO:
    return "SIGINFO"; // 29    information request
#endif
  case SIGUSR1:
    return "SIGUSR1"; // 30    user defined signal 1
  case SIGUSR2:
    return "SIGUSR2"; // 31    user defined signal 2
  default:
    break;
  }
  return nullptr;
}

#endif

#if !defined(__APPLE__) // see Host.mm

bool Host::GetBundleDirectory(const FileSpec &file, FileSpec &bundle) {
  bundle.Clear();
  return false;
}

bool Host::ResolveExecutableInBundle(FileSpec &file) { return false; }
#endif

#ifndef _WIN32

FileSpec Host::GetModuleFileSpecForHostAddress(const void *host_addr) {
  FileSpec module_filespec;
#if !defined(__ANDROID__)
  Dl_info info;
  if (::dladdr(host_addr, &info)) {
    if (info.dli_fname) {
      module_filespec.SetFile(info.dli_fname, FileSpec::Style::native);
      FileSystem::Instance().Resolve(module_filespec);
    }
  }
#endif
  return module_filespec;
}

#endif

#if !defined(__linux__)
bool Host::FindProcessThreads(const lldb::pid_t pid, TidMap &tids_to_attach) {
  return false;
}
#endif

struct ShellInfo {
  ShellInfo() : process_reaped(false) {}

  lldb_private::Predicate<bool> process_reaped;
  lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
  int signo = -1;
  int status = -1;
};

static void
MonitorShellCommand(std::shared_ptr<ShellInfo> shell_info, lldb::pid_t pid,
                    int signo,  // Zero for no signal
                    int status) // Exit value of process if signal is zero
{
  shell_info->pid = pid;
  shell_info->signo = signo;
  shell_info->status = status;
  // Let the thread running Host::RunShellCommand() know that the process
  // exited and that ShellInfo has been filled in by broadcasting to it
  shell_info->process_reaped.SetValue(true, eBroadcastAlways);
}

Status Host::RunShellCommand(llvm::StringRef command,
                             const FileSpec &working_dir, int *status_ptr,
                             int *signo_ptr, std::string *command_output_ptr,
                             const Timeout<std::micro> &timeout,
                             bool run_in_shell, bool hide_stderr) {
  return RunShellCommand(llvm::StringRef(), Args(command), working_dir,
                         status_ptr, signo_ptr, command_output_ptr, timeout,
                         run_in_shell, hide_stderr);
}

Status Host::RunShellCommand(llvm::StringRef shell_path,
                             llvm::StringRef command,
                             const FileSpec &working_dir, int *status_ptr,
                             int *signo_ptr, std::string *command_output_ptr,
                             const Timeout<std::micro> &timeout,
                             bool run_in_shell, bool hide_stderr) {
  return RunShellCommand(shell_path, Args(command), working_dir, status_ptr,
                         signo_ptr, command_output_ptr, timeout, run_in_shell,
                         hide_stderr);
}

Status Host::RunShellCommand(const Args &args, const FileSpec &working_dir,
                             int *status_ptr, int *signo_ptr,
                             std::string *command_output_ptr,
                             const Timeout<std::micro> &timeout,
                             bool run_in_shell, bool hide_stderr) {
  return RunShellCommand(llvm::StringRef(), args, working_dir, status_ptr,
                         signo_ptr, command_output_ptr, timeout, run_in_shell,
                         hide_stderr);
}

Status Host::RunShellCommand(llvm::StringRef shell_path, const Args &args,
                             const FileSpec &working_dir, int *status_ptr,
                             int *signo_ptr, std::string *command_output_ptr,
                             const Timeout<std::micro> &timeout,
                             bool run_in_shell, bool hide_stderr) {
  Status error;
  ProcessLaunchInfo launch_info;
  launch_info.SetArchitecture(HostInfo::GetArchitecture());
  if (run_in_shell) {
    // Run the command in a shell
    FileSpec shell = HostInfo::GetDefaultShell();
    if (!shell_path.empty())
      shell.SetPath(shell_path);

    launch_info.SetShell(shell);
    launch_info.GetArguments().AppendArguments(args);
    const bool will_debug = false;
    const bool first_arg_is_full_shell_command = false;
    launch_info.ConvertArgumentsForLaunchingInShell(
        error, will_debug, first_arg_is_full_shell_command, 0);
  } else {
    // No shell, just run it
    const bool first_arg_is_executable = true;
    launch_info.SetArguments(args, first_arg_is_executable);
  }

  launch_info.GetEnvironment() = Host::GetEnvironment();

  if (working_dir)
    launch_info.SetWorkingDirectory(working_dir);
  llvm::SmallString<64> output_file_path;

  if (command_output_ptr) {
    // Create a temporary file to get the stdout/stderr and redirect the output
    // of the command into this file. We will later read this file if all goes
    // well and fill the data into "command_output_ptr"
    if (FileSpec tmpdir_file_spec = HostInfo::GetProcessTempDir()) {
      tmpdir_file_spec.AppendPathComponent("lldb-shell-output.%%%%%%");
      llvm::sys::fs::createUniqueFile(tmpdir_file_spec.GetPath(),
                                      output_file_path);
    } else {
      llvm::sys::fs::createTemporaryFile("lldb-shell-output.%%%%%%", "",
                                         output_file_path);
    }
  }

  FileSpec output_file_spec(output_file_path.str());
  // Set up file descriptors.
  launch_info.AppendSuppressFileAction(STDIN_FILENO, true, false);
  if (output_file_spec)
    launch_info.AppendOpenFileAction(STDOUT_FILENO, output_file_spec, false,
                                     true);
  else
    launch_info.AppendSuppressFileAction(STDOUT_FILENO, false, true);

  if (output_file_spec && !hide_stderr)
    launch_info.AppendDuplicateFileAction(STDOUT_FILENO, STDERR_FILENO);
  else
    launch_info.AppendSuppressFileAction(STDERR_FILENO, false, true);

  std::shared_ptr<ShellInfo> shell_info_sp(new ShellInfo());
  launch_info.SetMonitorProcessCallback(
      std::bind(MonitorShellCommand, shell_info_sp, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));

  error = LaunchProcess(launch_info);
  const lldb::pid_t pid = launch_info.GetProcessID();

  if (error.Success() && pid == LLDB_INVALID_PROCESS_ID)
    error.SetErrorString("failed to get process ID");

  if (error.Success()) {
    if (!shell_info_sp->process_reaped.WaitForValueEqualTo(true, timeout)) {
      error.SetErrorString("timed out waiting for shell command to complete");

      // Kill the process since it didn't complete within the timeout specified
      Kill(pid, SIGKILL);
      // Wait for the monitor callback to get the message
      shell_info_sp->process_reaped.WaitForValueEqualTo(
          true, std::chrono::seconds(1));
    } else {
      if (status_ptr)
        *status_ptr = shell_info_sp->status;

      if (signo_ptr)
        *signo_ptr = shell_info_sp->signo;

      if (command_output_ptr) {
        command_output_ptr->clear();
        uint64_t file_size =
            FileSystem::Instance().GetByteSize(output_file_spec);
        if (file_size > 0) {
          if (file_size > command_output_ptr->max_size()) {
            error.SetErrorStringWithFormat(
                "shell command output is too large to fit into a std::string");
          } else {
            WritableDataBufferSP Buffer =
                FileSystem::Instance().CreateWritableDataBuffer(
                    output_file_spec);
            if (error.Success())
              command_output_ptr->assign(
                  reinterpret_cast<char *>(Buffer->GetBytes()),
                  Buffer->GetByteSize());
          }
        }
      }
    }
  }

  llvm::sys::fs::remove(output_file_spec.GetPath());
  return error;
}

// The functions below implement process launching for non-Apple-based
// platforms
#if !defined(__APPLE__)
Status Host::LaunchProcess(ProcessLaunchInfo &launch_info) {
  std::unique_ptr<ProcessLauncher> delegate_launcher;
#if defined(_WIN32)
  delegate_launcher.reset(new ProcessLauncherWindows());
#else
  delegate_launcher.reset(new ProcessLauncherPosixFork());
#endif
  MonitoringProcessLauncher launcher(std::move(delegate_launcher));

  Status error;
  HostProcess process = launcher.LaunchProcess(launch_info, error);

  // TODO(zturner): It would be better if the entire HostProcess were returned
  // instead of writing it into this structure.
  launch_info.SetProcessID(process.GetProcessId());

  return error;
}
#endif // !defined(__APPLE__)

#ifndef _WIN32
void Host::Kill(lldb::pid_t pid, int signo) { ::kill(pid, signo); }

#endif

#if !defined(__APPLE__)
llvm::Error Host::OpenFileInExternalEditor(llvm::StringRef editor,
                                           const FileSpec &file_spec,
                                           uint32_t line_no) {
  return llvm::errorCodeToError(
      std::error_code(ENOTSUP, std::system_category()));
}

bool Host::IsInteractiveGraphicSession() { return false; }
#endif

std::unique_ptr<Connection> Host::CreateDefaultConnection(llvm::StringRef url) {
#if defined(_WIN32)
  if (url.starts_with("file://"))
    return std::unique_ptr<Connection>(new ConnectionGenericFile());
#endif
  return std::unique_ptr<Connection>(new ConnectionFileDescriptor());
}

#if defined(LLVM_ON_UNIX)
WaitStatus WaitStatus::Decode(int wstatus) {
  if (WIFEXITED(wstatus))
    return {Exit, uint8_t(WEXITSTATUS(wstatus))};
  else if (WIFSIGNALED(wstatus))
    return {Signal, uint8_t(WTERMSIG(wstatus))};
  else if (WIFSTOPPED(wstatus))
    return {Stop, uint8_t(WSTOPSIG(wstatus))};
  llvm_unreachable("Unknown wait status");
}
#endif

void llvm::format_provider<WaitStatus>::format(const WaitStatus &WS,
                                               raw_ostream &OS,
                                               StringRef Options) {
  if (Options == "g") {
    char type;
    switch (WS.type) {
    case WaitStatus::Exit:
      type = 'W';
      break;
    case WaitStatus::Signal:
      type = 'X';
      break;
    case WaitStatus::Stop:
      type = 'S';
      break;
    }
    OS << formatv("{0}{1:x-2}", type, WS.status);
    return;
  }

  assert(Options.empty());
  const char *desc;
  switch(WS.type) {
  case WaitStatus::Exit:
    desc = "Exited with status";
    break;
  case WaitStatus::Signal:
    desc = "Killed by signal";
    break;
  case WaitStatus::Stop:
    desc = "Stopped by signal";
    break;
  }
  OS << desc << " " << int(WS.status);
}

uint32_t Host::FindProcesses(const ProcessInstanceInfoMatch &match_info,
                             ProcessInstanceInfoList &process_infos) {
  return FindProcessesImpl(match_info, process_infos);
}

char SystemLogHandler::ID;

SystemLogHandler::SystemLogHandler() {}

void SystemLogHandler::Emit(llvm::StringRef message) {
  Host::SystemLog(lldb::eSeverityInfo, message);
}
