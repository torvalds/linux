//===-- ProcessLauncherPosixFork.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "llvm/Support/Errno.h"

#include <climits>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sstream>
#include <csignal>

#ifdef __ANDROID__
#include <android/api-level.h>
#define PT_TRACE_ME PTRACE_TRACEME
#endif

#if defined(__ANDROID_API__) && __ANDROID_API__ < 15
#include <linux/personality.h>
#elif defined(__linux__)
#include <sys/personality.h>
#endif

using namespace lldb;
using namespace lldb_private;

// Begin code running in the child process
// NB: This code needs to be async-signal safe, since we're invoking fork from
// multithreaded contexts.

static void write_string(int error_fd, const char *str) {
  int r = write(error_fd, str, strlen(str));
  (void)r;
}

[[noreturn]] static void ExitWithError(int error_fd,
                                       const char *operation) {
  int err = errno;
  write_string(error_fd, operation);
  write_string(error_fd, " failed: ");
  // strerror is not guaranteed to be async-signal safe, but it usually is.
  write_string(error_fd, strerror(err));
  _exit(1);
}

static void DisableASLR(int error_fd) {
#if defined(__linux__)
  const unsigned long personality_get_current = 0xffffffff;
  int value = personality(personality_get_current);
  if (value == -1)
    ExitWithError(error_fd, "personality get");

  value = personality(ADDR_NO_RANDOMIZE | value);
  if (value == -1)
    ExitWithError(error_fd, "personality set");
#endif
}

static void DupDescriptor(int error_fd, const char *file, int fd, int flags) {
  int target_fd = FileSystem::Instance().Open(file, flags, 0666);

  if (target_fd == -1)
    ExitWithError(error_fd, "DupDescriptor-open");

  if (target_fd == fd)
    return;

  if (::dup2(target_fd, fd) == -1)
    ExitWithError(error_fd, "DupDescriptor-dup2");

  ::close(target_fd);
}

namespace {
struct ForkFileAction {
  ForkFileAction(const FileAction &act);

  FileAction::Action action;
  int fd;
  std::string path;
  int arg;
};

struct ForkLaunchInfo {
  ForkLaunchInfo(const ProcessLaunchInfo &info);

  bool separate_process_group;
  bool debug;
  bool disable_aslr;
  std::string wd;
  const char **argv;
  Environment::Envp envp;
  std::vector<ForkFileAction> actions;

  bool has_action(int fd) const {
    for (const ForkFileAction &action : actions) {
      if (action.fd == fd)
        return true;
    }
    return false;
  }
};
} // namespace

[[noreturn]] static void ChildFunc(int error_fd, const ForkLaunchInfo &info) {
  if (info.separate_process_group) {
    if (setpgid(0, 0) != 0)
      ExitWithError(error_fd, "setpgid");
  }

  for (const ForkFileAction &action : info.actions) {
    switch (action.action) {
    case FileAction::eFileActionClose:
      if (close(action.fd) != 0)
        ExitWithError(error_fd, "close");
      break;
    case FileAction::eFileActionDuplicate:
      if (dup2(action.fd, action.arg) == -1)
        ExitWithError(error_fd, "dup2");
      break;
    case FileAction::eFileActionOpen:
      DupDescriptor(error_fd, action.path.c_str(), action.fd, action.arg);
      break;
    case FileAction::eFileActionNone:
      break;
    }
  }

  // Change working directory
  if (!info.wd.empty() && 0 != ::chdir(info.wd.c_str()))
    ExitWithError(error_fd, "chdir");

  if (info.disable_aslr)
    DisableASLR(error_fd);

  // Clear the signal mask to prevent the child from being affected by any
  // masking done by the parent.
  sigset_t set;
  if (sigemptyset(&set) != 0 ||
      pthread_sigmask(SIG_SETMASK, &set, nullptr) != 0)
    ExitWithError(error_fd, "pthread_sigmask");

  if (info.debug) {
    // Do not inherit setgid powers.
    if (setgid(getgid()) != 0)
      ExitWithError(error_fd, "setgid");

    // HACK:
    // Close everything besides stdin, stdout, and stderr that has no file
    // action to avoid leaking. Only do this when debugging, as elsewhere we
    // actually rely on passing open descriptors to child processes.
    // NB: This code is not async-signal safe, but we currently do not launch
    // processes for debugging from within multithreaded contexts.

    const llvm::StringRef proc_fd_path = "/proc/self/fd";
    std::error_code ec;
    bool result;
    ec = llvm::sys::fs::is_directory(proc_fd_path, result);
    if (result) {
      std::vector<int> files_to_close;
      // Directory iterator doesn't ensure any sequence.
      for (llvm::sys::fs::directory_iterator iter(proc_fd_path, ec), file_end;
           iter != file_end && !ec; iter.increment(ec)) {
        int fd = std::stoi(iter->path().substr(proc_fd_path.size() + 1));

        // Don't close first three entries since they are stdin, stdout and
        // stderr.
        if (fd > 2 && !info.has_action(fd) && fd != error_fd)
          files_to_close.push_back(fd);
      }
      for (int file_to_close : files_to_close)
        close(file_to_close);
    } else {
      // Since /proc/self/fd didn't work, trying the slow way instead.
      int max_fd = sysconf(_SC_OPEN_MAX);
      for (int fd = 3; fd < max_fd; ++fd)
        if (!info.has_action(fd) && fd != error_fd)
          close(fd);
    }

    // Start tracing this child that is about to exec.
    if (ptrace(PT_TRACE_ME, 0, nullptr, 0) == -1)
      ExitWithError(error_fd, "ptrace");
  }

  // Execute.  We should never return...
  execve(info.argv[0], const_cast<char *const *>(info.argv), info.envp);

#if defined(__linux__)
  if (errno == ETXTBSY) {
    // On android M and earlier we can get this error because the adb daemon
    // can hold a write handle on the executable even after it has finished
    // uploading it. This state lasts only a short time and happens only when
    // there are many concurrent adb commands being issued, such as when
    // running the test suite. (The file remains open when someone does an "adb
    // shell" command in the fork() child before it has had a chance to exec.)
    // Since this state should clear up quickly, wait a while and then give it
    // one more go.
    usleep(50000);
    execve(info.argv[0], const_cast<char *const *>(info.argv), info.envp);
  }
#endif

  // ...unless exec fails.  In which case we definitely need to end the child
  // here.
  ExitWithError(error_fd, "execve");
}

// End of code running in the child process.

ForkFileAction::ForkFileAction(const FileAction &act)
    : action(act.GetAction()), fd(act.GetFD()), path(act.GetPath().str()),
      arg(act.GetActionArgument()) {}

static std::vector<ForkFileAction>
MakeForkActions(const ProcessLaunchInfo &info) {
  std::vector<ForkFileAction> result;
  for (size_t i = 0; i < info.GetNumFileActions(); ++i)
    result.emplace_back(*info.GetFileActionAtIndex(i));
  return result;
}

static Environment::Envp FixupEnvironment(Environment env) {
#ifdef __ANDROID__
  // If there is no PATH variable specified inside the environment then set the
  // path to /system/bin. It is required because the default path used by
  // execve() is wrong on android.
  env.try_emplace("PATH", "/system/bin");
#endif
  return env.getEnvp();
}

ForkLaunchInfo::ForkLaunchInfo(const ProcessLaunchInfo &info)
    : separate_process_group(
          info.GetFlags().Test(eLaunchFlagLaunchInSeparateProcessGroup)),
      debug(info.GetFlags().Test(eLaunchFlagDebug)),
      disable_aslr(info.GetFlags().Test(eLaunchFlagDisableASLR)),
      wd(info.GetWorkingDirectory().GetPath()),
      argv(info.GetArguments().GetConstArgumentVector()),
      envp(FixupEnvironment(info.GetEnvironment())),
      actions(MakeForkActions(info)) {}

HostProcess
ProcessLauncherPosixFork::LaunchProcess(const ProcessLaunchInfo &launch_info,
                                        Status &error) {
  // A pipe used by the child process to report errors.
  PipePosix pipe;
  const bool child_processes_inherit = false;
  error = pipe.CreateNew(child_processes_inherit);
  if (error.Fail())
    return HostProcess();

  const ForkLaunchInfo fork_launch_info(launch_info);

  ::pid_t pid = ::fork();
  if (pid == -1) {
    // Fork failed
    error.SetErrorStringWithFormatv("Fork failed with error message: {0}",
                                    llvm::sys::StrError());
    return HostProcess(LLDB_INVALID_PROCESS_ID);
  }
  if (pid == 0) {
    // child process
    pipe.CloseReadFileDescriptor();
    ChildFunc(pipe.ReleaseWriteFileDescriptor(), fork_launch_info);
  }

  // parent process

  pipe.CloseWriteFileDescriptor();
  llvm::SmallString<0> buf;
  size_t pos = 0;
  ssize_t r = 0;
  do {
    pos += r;
    buf.resize_for_overwrite(pos + 100);
    r = llvm::sys::RetryAfterSignal(-1, read, pipe.GetReadFileDescriptor(),
                                    buf.begin() + pos, buf.size() - pos);
  } while (r > 0);
  assert(r != -1);

  buf.resize(pos);
  if (buf.empty())
    return HostProcess(pid); // No error. We're done.

  error.SetErrorString(buf);

  llvm::sys::RetryAfterSignal(-1, waitpid, pid, nullptr, 0);

  return HostProcess();
}
