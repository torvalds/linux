//===-- NativeProcessLinux.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeProcessLinux.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include "NativeThreadLinux.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Plugins/Process/Utility/LinuxProcMaps.h"
#include "Procfs.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/common/NativeRegisterContext.h"
#include "lldb/Host/linux/Host.h"
#include "lldb/Host/linux/Ptrace.h"
#include "lldb/Host/linux/Uio.h"
#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StringExtractor.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"

#include <linux/unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#ifdef __aarch64__
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

// Support hardware breakpoints in case it has not been defined
#ifndef TRAP_HWBKPT
#define TRAP_HWBKPT 4
#endif

#ifndef HWCAP2_MTE
#define HWCAP2_MTE (1 << 18)
#endif

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_linux;
using namespace llvm;

// Private bits we only need internally.

static bool ProcessVmReadvSupported() {
  static bool is_supported;
  static llvm::once_flag flag;

  llvm::call_once(flag, [] {
    Log *log = GetLog(POSIXLog::Process);

    uint32_t source = 0x47424742;
    uint32_t dest = 0;

    struct iovec local, remote;
    remote.iov_base = &source;
    local.iov_base = &dest;
    remote.iov_len = local.iov_len = sizeof source;

    // We shall try if cross-process-memory reads work by attempting to read a
    // value from our own process.
    ssize_t res = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    is_supported = (res == sizeof(source) && source == dest);
    if (is_supported)
      LLDB_LOG(log,
               "Detected kernel support for process_vm_readv syscall. "
               "Fast memory reads enabled.");
    else
      LLDB_LOG(log,
               "syscall process_vm_readv failed (error: {0}). Fast memory "
               "reads disabled.",
               llvm::sys::StrError());
  });

  return is_supported;
}

static void MaybeLogLaunchInfo(const ProcessLaunchInfo &info) {
  Log *log = GetLog(POSIXLog::Process);
  if (!log)
    return;

  if (const FileAction *action = info.GetFileActionForFD(STDIN_FILENO))
    LLDB_LOG(log, "setting STDIN to '{0}'", action->GetFileSpec());
  else
    LLDB_LOG(log, "leaving STDIN as is");

  if (const FileAction *action = info.GetFileActionForFD(STDOUT_FILENO))
    LLDB_LOG(log, "setting STDOUT to '{0}'", action->GetFileSpec());
  else
    LLDB_LOG(log, "leaving STDOUT as is");

  if (const FileAction *action = info.GetFileActionForFD(STDERR_FILENO))
    LLDB_LOG(log, "setting STDERR to '{0}'", action->GetFileSpec());
  else
    LLDB_LOG(log, "leaving STDERR as is");

  int i = 0;
  for (const char **args = info.GetArguments().GetConstArgumentVector(); *args;
       ++args, ++i)
    LLDB_LOG(log, "arg {0}: '{1}'", i, *args);
}

static void DisplayBytes(StreamString &s, void *bytes, uint32_t count) {
  uint8_t *ptr = (uint8_t *)bytes;
  const uint32_t loop_count = std::min<uint32_t>(DEBUG_PTRACE_MAXBYTES, count);
  for (uint32_t i = 0; i < loop_count; i++) {
    s.Printf("[%x]", *ptr);
    ptr++;
  }
}

static void PtraceDisplayBytes(int &req, void *data, size_t data_size) {
  Log *log = GetLog(POSIXLog::Ptrace);
  if (!log)
    return;
  StreamString buf;

  switch (req) {
  case PTRACE_POKETEXT: {
    DisplayBytes(buf, &data, 8);
    LLDB_LOGV(log, "PTRACE_POKETEXT {0}", buf.GetData());
    break;
  }
  case PTRACE_POKEDATA: {
    DisplayBytes(buf, &data, 8);
    LLDB_LOGV(log, "PTRACE_POKEDATA {0}", buf.GetData());
    break;
  }
  case PTRACE_POKEUSER: {
    DisplayBytes(buf, &data, 8);
    LLDB_LOGV(log, "PTRACE_POKEUSER {0}", buf.GetData());
    break;
  }
  case PTRACE_SETREGS: {
    DisplayBytes(buf, data, data_size);
    LLDB_LOGV(log, "PTRACE_SETREGS {0}", buf.GetData());
    break;
  }
  case PTRACE_SETFPREGS: {
    DisplayBytes(buf, data, data_size);
    LLDB_LOGV(log, "PTRACE_SETFPREGS {0}", buf.GetData());
    break;
  }
  case PTRACE_SETSIGINFO: {
    DisplayBytes(buf, data, sizeof(siginfo_t));
    LLDB_LOGV(log, "PTRACE_SETSIGINFO {0}", buf.GetData());
    break;
  }
  case PTRACE_SETREGSET: {
    // Extract iov_base from data, which is a pointer to the struct iovec
    DisplayBytes(buf, *(void **)data, data_size);
    LLDB_LOGV(log, "PTRACE_SETREGSET {0}", buf.GetData());
    break;
  }
  default: {}
  }
}

static constexpr unsigned k_ptrace_word_size = sizeof(void *);
static_assert(sizeof(long) >= k_ptrace_word_size,
              "Size of long must be larger than ptrace word size");

// Simple helper function to ensure flags are enabled on the given file
// descriptor.
static Status EnsureFDFlags(int fd, int flags) {
  Status error;

  int status = fcntl(fd, F_GETFL);
  if (status == -1) {
    error.SetErrorToErrno();
    return error;
  }

  if (fcntl(fd, F_SETFL, status | flags) == -1) {
    error.SetErrorToErrno();
    return error;
  }

  return error;
}

static llvm::Error AddPtraceScopeNote(llvm::Error original_error) {
  Expected<int> ptrace_scope = GetPtraceScope();
  if (auto E = ptrace_scope.takeError()) {
    Log *log = GetLog(POSIXLog::Process);
    LLDB_LOG(log, "error reading value of ptrace_scope: {0}", E);

    // The original error is probably more interesting than not being able to
    // read or interpret ptrace_scope.
    return original_error;
  }

  // We only have suggestions to provide for 1-3.
  switch (*ptrace_scope) {
  case 1:
  case 2:
    return llvm::createStringError(
        std::error_code(errno, std::generic_category()),
        "The current value of ptrace_scope is %d, which can cause ptrace to "
        "fail to attach to a running process. To fix this, run:\n"
        "\tsudo sysctl -w kernel.yama.ptrace_scope=0\n"
        "For more information, see: "
        "https://www.kernel.org/doc/Documentation/security/Yama.txt.",
        *ptrace_scope);
  case 3:
    return llvm::createStringError(
        std::error_code(errno, std::generic_category()),
        "The current value of ptrace_scope is 3, which will cause ptrace to "
        "fail to attach to a running process. This value cannot be changed "
        "without rebooting.\n"
        "For more information, see: "
        "https://www.kernel.org/doc/Documentation/security/Yama.txt.");
  case 0:
  default:
    return original_error;
  }
}

NativeProcessLinux::Manager::Manager(MainLoop &mainloop)
    : NativeProcessProtocol::Manager(mainloop) {
  Status status;
  m_sigchld_handle = mainloop.RegisterSignal(
      SIGCHLD, [this](MainLoopBase &) { SigchldHandler(); }, status);
  assert(m_sigchld_handle && status.Success());
}

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessLinux::Manager::Launch(ProcessLaunchInfo &launch_info,
                                    NativeDelegate &native_delegate) {
  Log *log = GetLog(POSIXLog::Process);

  MaybeLogLaunchInfo(launch_info);

  Status status;
  ::pid_t pid = ProcessLauncherPosixFork()
                    .LaunchProcess(launch_info, status)
                    .GetProcessId();
  LLDB_LOG(log, "pid = {0:x}", pid);
  if (status.Fail()) {
    LLDB_LOG(log, "failed to launch process: {0}", status);
    return status.ToError();
  }

  // Wait for the child process to trap on its call to execve.
  int wstatus = 0;
  ::pid_t wpid = llvm::sys::RetryAfterSignal(-1, ::waitpid, pid, &wstatus, 0);
  assert(wpid == pid);
  UNUSED_IF_ASSERT_DISABLED(wpid);
  if (!WIFSTOPPED(wstatus)) {
    LLDB_LOG(log, "Could not sync with inferior process: wstatus={1}",
             WaitStatus::Decode(wstatus));
    return llvm::make_error<StringError>("Could not sync with inferior process",
                                         llvm::inconvertibleErrorCode());
  }
  LLDB_LOG(log, "inferior started, now in stopped state");

  status = SetDefaultPtraceOpts(pid);
  if (status.Fail()) {
    LLDB_LOG(log, "failed to set default ptrace options: {0}", status);
    return status.ToError();
  }

  llvm::Expected<ArchSpec> arch_or =
      NativeRegisterContextLinux::DetermineArchitecture(pid);
  if (!arch_or)
    return arch_or.takeError();

  return std::unique_ptr<NativeProcessLinux>(new NativeProcessLinux(
      pid, launch_info.GetPTY().ReleasePrimaryFileDescriptor(), native_delegate,
      *arch_or, *this, {pid}));
}

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessLinux::Manager::Attach(
    lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &native_delegate) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid = {0:x}", pid);

  auto tids_or = NativeProcessLinux::Attach(pid);
  if (!tids_or)
    return tids_or.takeError();
  ArrayRef<::pid_t> tids = *tids_or;
  llvm::Expected<ArchSpec> arch_or =
      NativeRegisterContextLinux::DetermineArchitecture(tids[0]);
  if (!arch_or)
    return arch_or.takeError();

  return std::unique_ptr<NativeProcessLinux>(
      new NativeProcessLinux(pid, -1, native_delegate, *arch_or, *this, tids));
}

NativeProcessLinux::Extension
NativeProcessLinux::Manager::GetSupportedExtensions() const {
  NativeProcessLinux::Extension supported =
      Extension::multiprocess | Extension::fork | Extension::vfork |
      Extension::pass_signals | Extension::auxv | Extension::libraries_svr4 |
      Extension::siginfo_read;

#ifdef __aarch64__
  // At this point we do not have a process so read auxv directly.
  if ((getauxval(AT_HWCAP2) & HWCAP2_MTE))
    supported |= Extension::memory_tagging;
#endif

  return supported;
}

static std::optional<std::pair<lldb::pid_t, WaitStatus>> WaitPid() {
  Log *log = GetLog(POSIXLog::Process);

  int status;
  ::pid_t wait_pid = llvm::sys::RetryAfterSignal(
      -1, ::waitpid, -1, &status, __WALL | __WNOTHREAD | WNOHANG);

  if (wait_pid == 0)
    return std::nullopt;

  if (wait_pid == -1) {
    Status error(errno, eErrorTypePOSIX);
    LLDB_LOG(log, "waitpid(-1, &status, _) failed: {1}", error);
    return std::nullopt;
  }

  WaitStatus wait_status = WaitStatus::Decode(status);

  LLDB_LOG(log, "waitpid(-1, &status, _) = {0}, status = {1}", wait_pid,
           wait_status);
  return std::make_pair(wait_pid, wait_status);
}

void NativeProcessLinux::Manager::SigchldHandler() {
  Log *log = GetLog(POSIXLog::Process);
  while (true) {
    auto wait_result = WaitPid();
    if (!wait_result)
      return;
    lldb::pid_t pid = wait_result->first;
    WaitStatus status = wait_result->second;

    // Ask each process whether it wants to handle the event. Each event should
    // be handled by exactly one process, but thread creation events require
    // special handling.
    // Thread creation consists of two events (one on the parent and one on the
    // child thread) and they can arrive in any order nondeterministically. The
    // parent event carries the information about the child thread, but not
    // vice-versa. This means that if the child event arrives first, it may not
    // be handled by any process (because it doesn't know the thread belongs to
    // it).
    bool handled = llvm::any_of(m_processes, [&](NativeProcessLinux *process) {
      return process->TryHandleWaitStatus(pid, status);
    });
    if (!handled) {
      if (status.type == WaitStatus::Stop && status.status == SIGSTOP) {
        // Store the thread creation event for later collection.
        m_unowned_threads.insert(pid);
      } else {
        LLDB_LOG(log, "Ignoring waitpid event {0} for pid {1}", status, pid);
      }
    }
  }
}

void NativeProcessLinux::Manager::CollectThread(::pid_t tid) {
  Log *log = GetLog(POSIXLog::Process);

  if (m_unowned_threads.erase(tid))
    return; // We've encountered this thread already.

  // The TID is not tracked yet, let's wait for it to appear.
  int status = -1;
  LLDB_LOG(log,
           "received clone event for tid {0}. tid not tracked yet, "
           "waiting for it to appear...",
           tid);
  ::pid_t wait_pid =
      llvm::sys::RetryAfterSignal(-1, ::waitpid, tid, &status, __WALL);

  // It's theoretically possible to get other events if the entire process was
  // SIGKILLed before we got a chance to check this. In that case, we'll just
  // clean everything up when we get the process exit event.

  LLDB_LOG(log,
           "waitpid({0}, &status, __WALL) => {1} (errno: {2}, status = {3})",
           tid, wait_pid, errno, WaitStatus::Decode(status));
}

// Public Instance Methods

NativeProcessLinux::NativeProcessLinux(::pid_t pid, int terminal_fd,
                                       NativeDelegate &delegate,
                                       const ArchSpec &arch, Manager &manager,
                                       llvm::ArrayRef<::pid_t> tids)
    : NativeProcessELF(pid, terminal_fd, delegate), m_manager(manager),
      m_arch(arch), m_intel_pt_collector(*this) {
  manager.AddProcess(*this);
  if (m_terminal_fd != -1) {
    Status status = EnsureFDFlags(m_terminal_fd, O_NONBLOCK);
    assert(status.Success());
  }

  for (const auto &tid : tids) {
    NativeThreadLinux &thread = AddThread(tid, /*resume*/ false);
    ThreadWasCreated(thread);
  }

  // Let our process instance know the thread has stopped.
  SetCurrentThreadID(tids[0]);
  SetState(StateType::eStateStopped, false);
}

llvm::Expected<std::vector<::pid_t>> NativeProcessLinux::Attach(::pid_t pid) {
  Log *log = GetLog(POSIXLog::Process);

  Status status;
  // Use a map to keep track of the threads which we have attached/need to
  // attach.
  Host::TidMap tids_to_attach;
  while (Host::FindProcessThreads(pid, tids_to_attach)) {
    for (Host::TidMap::iterator it = tids_to_attach.begin();
         it != tids_to_attach.end();) {
      if (it->second == false) {
        lldb::tid_t tid = it->first;

        // Attach to the requested process.
        // An attach will cause the thread to stop with a SIGSTOP.
        if ((status = PtraceWrapper(PTRACE_ATTACH, tid)).Fail()) {
          // No such thread. The thread may have exited. More error handling
          // may be needed.
          if (status.GetError() == ESRCH) {
            it = tids_to_attach.erase(it);
            continue;
          }
          if (status.GetError() == EPERM) {
            // Depending on the value of ptrace_scope, we can return a different
            // error that suggests how to fix it.
            return AddPtraceScopeNote(status.ToError());
          }
          return status.ToError();
        }

        int wpid =
            llvm::sys::RetryAfterSignal(-1, ::waitpid, tid, nullptr, __WALL);
        // Need to use __WALL otherwise we receive an error with errno=ECHLD At
        // this point we should have a thread stopped if waitpid succeeds.
        if (wpid < 0) {
          // No such thread. The thread may have exited. More error handling
          // may be needed.
          if (errno == ESRCH) {
            it = tids_to_attach.erase(it);
            continue;
          }
          return llvm::errorCodeToError(
              std::error_code(errno, std::generic_category()));
        }

        if ((status = SetDefaultPtraceOpts(tid)).Fail())
          return status.ToError();

        LLDB_LOG(log, "adding tid = {0}", tid);
        it->second = true;
      }

      // move the loop forward
      ++it;
    }
  }

  size_t tid_count = tids_to_attach.size();
  if (tid_count == 0)
    return llvm::make_error<StringError>("No such process",
                                         llvm::inconvertibleErrorCode());

  std::vector<::pid_t> tids;
  tids.reserve(tid_count);
  for (const auto &p : tids_to_attach)
    tids.push_back(p.first);
  return std::move(tids);
}

Status NativeProcessLinux::SetDefaultPtraceOpts(lldb::pid_t pid) {
  long ptrace_opts = 0;

  // Have the child raise an event on exit.  This is used to keep the child in
  // limbo until it is destroyed.
  ptrace_opts |= PTRACE_O_TRACEEXIT;

  // Have the tracer trace threads which spawn in the inferior process.
  ptrace_opts |= PTRACE_O_TRACECLONE;

  // Have the tracer notify us before execve returns (needed to disable legacy
  // SIGTRAP generation)
  ptrace_opts |= PTRACE_O_TRACEEXEC;

  // Have the tracer trace forked children.
  ptrace_opts |= PTRACE_O_TRACEFORK;

  // Have the tracer trace vforks.
  ptrace_opts |= PTRACE_O_TRACEVFORK;

  // Have the tracer trace vfork-done in order to restore breakpoints after
  // the child finishes sharing memory.
  ptrace_opts |= PTRACE_O_TRACEVFORKDONE;

  return PtraceWrapper(PTRACE_SETOPTIONS, pid, nullptr, (void *)ptrace_opts);
}

bool NativeProcessLinux::TryHandleWaitStatus(lldb::pid_t pid,
                                             WaitStatus status) {
  if (pid == GetID() &&
      (status.type == WaitStatus::Exit || status.type == WaitStatus::Signal)) {
    // The process exited.  We're done monitoring.  Report to delegate.
    SetExitStatus(status, true);
    return true;
  }
  if (NativeThreadLinux *thread = GetThreadByID(pid)) {
    MonitorCallback(*thread, status);
    return true;
  }
  return false;
}

void NativeProcessLinux::MonitorCallback(NativeThreadLinux &thread,
                                         WaitStatus status) {
  Log *log = GetLog(LLDBLog::Process);

  // Certain activities differ based on whether the pid is the tid of the main
  // thread.
  const bool is_main_thread = (thread.GetID() == GetID());

  // Handle when the thread exits.
  if (status.type == WaitStatus::Exit || status.type == WaitStatus::Signal) {
    LLDB_LOG(log,
             "got exit status({0}) , tid = {1} ({2} main thread), process "
             "state = {3}",
             status, thread.GetID(), is_main_thread ? "is" : "is not",
             GetState());

    // This is a thread that exited.  Ensure we're not tracking it anymore.
    StopTrackingThread(thread);

    assert(!is_main_thread && "Main thread exits handled elsewhere");
    return;
  }

  siginfo_t info;
  const auto info_err = GetSignalInfo(thread.GetID(), &info);

  // Get details on the signal raised.
  if (info_err.Success()) {
    // We have retrieved the signal info.  Dispatch appropriately.
    if (info.si_signo == SIGTRAP)
      MonitorSIGTRAP(info, thread);
    else
      MonitorSignal(info, thread);
  } else {
    if (info_err.GetError() == EINVAL) {
      // This is a group stop reception for this tid. We can reach here if we
      // reinject SIGSTOP, SIGSTP, SIGTTIN or SIGTTOU into the tracee,
      // triggering the group-stop mechanism. Normally receiving these would
      // stop the process, pending a SIGCONT. Simulating this state in a
      // debugger is hard and is generally not needed (one use case is
      // debugging background task being managed by a shell). For general use,
      // it is sufficient to stop the process in a signal-delivery stop which
      // happens before the group stop. This done by MonitorSignal and works
      // correctly for all signals.
      LLDB_LOG(log,
               "received a group stop for pid {0} tid {1}. Transparent "
               "handling of group stops not supported, resuming the "
               "thread.",
               GetID(), thread.GetID());
      ResumeThread(thread, thread.GetState(), LLDB_INVALID_SIGNAL_NUMBER);
    } else {
      // ptrace(GETSIGINFO) failed (but not due to group-stop).

      // A return value of ESRCH means the thread/process has died in the mean
      // time. This can (e.g.) happen when another thread does an exit_group(2)
      // or the entire process get SIGKILLed.
      // We can't do anything with this thread anymore, but we keep it around
      // until we get the WIFEXITED event.

      LLDB_LOG(log,
               "GetSignalInfo({0}) failed: {1}, status = {2}, main_thread = "
               "{3}. Expecting WIFEXITED soon.",
               thread.GetID(), info_err, status, is_main_thread);
    }
  }
}

void NativeProcessLinux::MonitorSIGTRAP(const siginfo_t &info,
                                        NativeThreadLinux &thread) {
  Log *log = GetLog(POSIXLog::Process);
  const bool is_main_thread = (thread.GetID() == GetID());

  assert(info.si_signo == SIGTRAP && "Unexpected child signal!");

  switch (info.si_code) {
  case (SIGTRAP | (PTRACE_EVENT_FORK << 8)):
  case (SIGTRAP | (PTRACE_EVENT_VFORK << 8)):
  case (SIGTRAP | (PTRACE_EVENT_CLONE << 8)): {
    // This can either mean a new thread or a new process spawned via
    // clone(2) without SIGCHLD or CLONE_VFORK flag.  Note that clone(2)
    // can also cause PTRACE_EVENT_FORK and PTRACE_EVENT_VFORK if one
    // of these flags are passed.

    unsigned long event_message = 0;
    if (GetEventMessage(thread.GetID(), &event_message).Fail()) {
      LLDB_LOG(log,
               "pid {0} received clone() event but GetEventMessage failed "
               "so we don't know the new pid/tid",
               thread.GetID());
      ResumeThread(thread, thread.GetState(), LLDB_INVALID_SIGNAL_NUMBER);
    } else {
      MonitorClone(thread, event_message, info.si_code >> 8);
    }

    break;
  }

  case (SIGTRAP | (PTRACE_EVENT_EXEC << 8)): {
    LLDB_LOG(log, "received exec event, code = {0}", info.si_code ^ SIGTRAP);

    // Exec clears any pending notifications.
    m_pending_notification_tid = LLDB_INVALID_THREAD_ID;

    // Remove all but the main thread here.  Linux fork creates a new process
    // which only copies the main thread.
    LLDB_LOG(log, "exec received, stop tracking all but main thread");

    llvm::erase_if(m_threads, [&](std::unique_ptr<NativeThreadProtocol> &t) {
      return t->GetID() != GetID();
    });
    assert(m_threads.size() == 1);
    auto *main_thread = static_cast<NativeThreadLinux *>(m_threads[0].get());

    SetCurrentThreadID(main_thread->GetID());
    main_thread->SetStoppedByExec();

    // Tell coordinator about the "new" (since exec) stopped main thread.
    ThreadWasCreated(*main_thread);

    // Let our delegate know we have just exec'd.
    NotifyDidExec();

    // Let the process know we're stopped.
    StopRunningThreads(main_thread->GetID());

    break;
  }

  case (SIGTRAP | (PTRACE_EVENT_EXIT << 8)): {
    // The inferior process or one of its threads is about to exit. We don't
    // want to do anything with the thread so we just resume it. In case we
    // want to implement "break on thread exit" functionality, we would need to
    // stop here.

    unsigned long data = 0;
    if (GetEventMessage(thread.GetID(), &data).Fail())
      data = -1;

    LLDB_LOG(log,
             "received PTRACE_EVENT_EXIT, data = {0:x}, WIFEXITED={1}, "
             "WIFSIGNALED={2}, pid = {3}, main_thread = {4}",
             data, WIFEXITED(data), WIFSIGNALED(data), thread.GetID(),
             is_main_thread);


    StateType state = thread.GetState();
    if (!StateIsRunningState(state)) {
      // Due to a kernel bug, we may sometimes get this stop after the inferior
      // gets a SIGKILL. This confuses our state tracking logic in
      // ResumeThread(), since normally, we should not be receiving any ptrace
      // events while the inferior is stopped. This makes sure that the
      // inferior is resumed and exits normally.
      state = eStateRunning;
    }
    ResumeThread(thread, state, LLDB_INVALID_SIGNAL_NUMBER);

    if (is_main_thread) {
      // Main thread report the read (WIFEXITED) event only after all threads in
      // the process exit, so we need to stop tracking it here instead of in
      // MonitorCallback
      StopTrackingThread(thread);
    }

    break;
  }

  case (SIGTRAP | (PTRACE_EVENT_VFORK_DONE << 8)): {
    if (bool(m_enabled_extensions & Extension::vfork)) {
      thread.SetStoppedByVForkDone();
      StopRunningThreads(thread.GetID());
    }
    else
      ResumeThread(thread, thread.GetState(), LLDB_INVALID_SIGNAL_NUMBER);
    break;
  }

  case 0:
  case TRAP_TRACE:  // We receive this on single stepping.
  case TRAP_HWBKPT: // We receive this on watchpoint hit
  {
    // If a watchpoint was hit, report it
    uint32_t wp_index;
    Status error = thread.GetRegisterContext().GetWatchpointHitIndex(
        wp_index, (uintptr_t)info.si_addr);
    if (error.Fail())
      LLDB_LOG(log,
               "received error while checking for watchpoint hits, pid = "
               "{0}, error = {1}",
               thread.GetID(), error);
    if (wp_index != LLDB_INVALID_INDEX32) {
      MonitorWatchpoint(thread, wp_index);
      break;
    }

    // If a breakpoint was hit, report it
    uint32_t bp_index;
    error = thread.GetRegisterContext().GetHardwareBreakHitIndex(
        bp_index, (uintptr_t)info.si_addr);
    if (error.Fail())
      LLDB_LOG(log, "received error while checking for hardware "
                    "breakpoint hits, pid = {0}, error = {1}",
               thread.GetID(), error);
    if (bp_index != LLDB_INVALID_INDEX32) {
      MonitorBreakpoint(thread);
      break;
    }

    // Otherwise, report step over
    MonitorTrace(thread);
    break;
  }

  case SI_KERNEL:
#if defined __mips__
    // For mips there is no special signal for watchpoint So we check for
    // watchpoint in kernel trap
    {
      // If a watchpoint was hit, report it
      uint32_t wp_index;
      Status error = thread.GetRegisterContext().GetWatchpointHitIndex(
          wp_index, LLDB_INVALID_ADDRESS);
      if (error.Fail())
        LLDB_LOG(log,
                 "received error while checking for watchpoint hits, pid = "
                 "{0}, error = {1}",
                 thread.GetID(), error);
      if (wp_index != LLDB_INVALID_INDEX32) {
        MonitorWatchpoint(thread, wp_index);
        break;
      }
    }
// NO BREAK
#endif
  case TRAP_BRKPT:
    MonitorBreakpoint(thread);
    break;

  case SIGTRAP:
  case (SIGTRAP | 0x80):
    LLDB_LOG(
        log,
        "received unknown SIGTRAP stop event ({0}, pid {1} tid {2}, resuming",
        info.si_code, GetID(), thread.GetID());

    // Ignore these signals until we know more about them.
    ResumeThread(thread, thread.GetState(), LLDB_INVALID_SIGNAL_NUMBER);
    break;

  default:
    LLDB_LOG(log, "received unknown SIGTRAP stop event ({0}, pid {1} tid {2}",
             info.si_code, GetID(), thread.GetID());
    MonitorSignal(info, thread);
    break;
  }
}

void NativeProcessLinux::MonitorTrace(NativeThreadLinux &thread) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "received trace event, pid = {0}", thread.GetID());

  // This thread is currently stopped.
  thread.SetStoppedByTrace();

  StopRunningThreads(thread.GetID());
}

void NativeProcessLinux::MonitorBreakpoint(NativeThreadLinux &thread) {
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Breakpoints);
  LLDB_LOG(log, "received breakpoint event, pid = {0}", thread.GetID());

  // Mark the thread as stopped at breakpoint.
  thread.SetStoppedByBreakpoint();
  FixupBreakpointPCAsNeeded(thread);

  if (m_threads_stepping_with_breakpoint.find(thread.GetID()) !=
      m_threads_stepping_with_breakpoint.end())
    thread.SetStoppedByTrace();

  StopRunningThreads(thread.GetID());
}

void NativeProcessLinux::MonitorWatchpoint(NativeThreadLinux &thread,
                                           uint32_t wp_index) {
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Watchpoints);
  LLDB_LOG(log, "received watchpoint event, pid = {0}, wp_index = {1}",
           thread.GetID(), wp_index);

  // Mark the thread as stopped at watchpoint. The address is at
  // (lldb::addr_t)info->si_addr if we need it.
  thread.SetStoppedByWatchpoint(wp_index);

  // We need to tell all other running threads before we notify the delegate
  // about this stop.
  StopRunningThreads(thread.GetID());
}

void NativeProcessLinux::MonitorSignal(const siginfo_t &info,
                                       NativeThreadLinux &thread) {
  const int signo = info.si_signo;
  const bool is_from_llgs = info.si_pid == getpid();

  Log *log = GetLog(POSIXLog::Process);

  // POSIX says that process behaviour is undefined after it ignores a SIGFPE,
  // SIGILL, SIGSEGV, or SIGBUS *unless* that signal was generated by a kill(2)
  // or raise(3).  Similarly for tgkill(2) on Linux.
  //
  // IOW, user generated signals never generate what we consider to be a
  // "crash".
  //
  // Similarly, ACK signals generated by this monitor.

  // Handle the signal.
  LLDB_LOG(log,
           "received signal {0} ({1}) with code {2}, (siginfo pid = {3}, "
           "waitpid pid = {4})",
           Host::GetSignalAsCString(signo), signo, info.si_code,
           thread.GetID());

  // Check for thread stop notification.
  if (is_from_llgs && (info.si_code == SI_TKILL) && (signo == SIGSTOP)) {
    // This is a tgkill()-based stop.
    LLDB_LOG(log, "pid {0} tid {1}, thread stopped", GetID(), thread.GetID());

    // Check that we're not already marked with a stop reason. Note this thread
    // really shouldn't already be marked as stopped - if we were, that would
    // imply that the kernel signaled us with the thread stopping which we
    // handled and marked as stopped, and that, without an intervening resume,
    // we received another stop.  It is more likely that we are missing the
    // marking of a run state somewhere if we find that the thread was marked
    // as stopped.
    const StateType thread_state = thread.GetState();
    if (!StateIsStoppedState(thread_state, false)) {
      // An inferior thread has stopped because of a SIGSTOP we have sent it.
      // Generally, these are not important stops and we don't want to report
      // them as they are just used to stop other threads when one thread (the
      // one with the *real* stop reason) hits a breakpoint (watchpoint,
      // etc...). However, in the case of an asynchronous Interrupt(), this
      // *is* the real stop reason, so we leave the signal intact if this is
      // the thread that was chosen as the triggering thread.
      if (m_pending_notification_tid != LLDB_INVALID_THREAD_ID) {
        if (m_pending_notification_tid == thread.GetID())
          thread.SetStoppedBySignal(SIGSTOP, &info);
        else
          thread.SetStoppedWithNoReason();

        SetCurrentThreadID(thread.GetID());
        SignalIfAllThreadsStopped();
      } else {
        // We can end up here if stop was initiated by LLGS but by this time a
        // thread stop has occurred - maybe initiated by another event.
        Status error = ResumeThread(thread, thread.GetState(), 0);
        if (error.Fail())
          LLDB_LOG(log, "failed to resume thread {0}: {1}", thread.GetID(),
                   error);
      }
    } else {
      LLDB_LOG(log,
               "pid {0} tid {1}, thread was already marked as a stopped "
               "state (state={2}), leaving stop signal as is",
               GetID(), thread.GetID(), thread_state);
      SignalIfAllThreadsStopped();
    }

    // Done handling.
    return;
  }

  // Check if debugger should stop at this signal or just ignore it and resume
  // the inferior.
  if (m_signals_to_ignore.contains(signo)) {
     ResumeThread(thread, thread.GetState(), signo);
     return;
  }

  // This thread is stopped.
  LLDB_LOG(log, "received signal {0}", Host::GetSignalAsCString(signo));
  thread.SetStoppedBySignal(signo, &info);

  // Send a stop to the debugger after we get all other threads to stop.
  StopRunningThreads(thread.GetID());
}

bool NativeProcessLinux::MonitorClone(NativeThreadLinux &parent,
                                      lldb::pid_t child_pid, int event) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "parent_tid={0}, child_pid={1}, event={2}", parent.GetID(),
           child_pid, event);

  m_manager.CollectThread(child_pid);

  switch (event) {
  case PTRACE_EVENT_CLONE: {
    // PTRACE_EVENT_CLONE can either mean a new thread or a new process.
    // Try to grab the new process' PGID to figure out which one it is.
    // If PGID is the same as the PID, then it's a new process.  Otherwise,
    // it's a thread.
    auto tgid_ret = getPIDForTID(child_pid);
    if (tgid_ret != child_pid) {
      // A new thread should have PGID matching our process' PID.
      assert(!tgid_ret || *tgid_ret == GetID());

      NativeThreadLinux &child_thread = AddThread(child_pid, /*resume*/ true);
      ThreadWasCreated(child_thread);

      // Resume the parent.
      ResumeThread(parent, parent.GetState(), LLDB_INVALID_SIGNAL_NUMBER);
      break;
    }
  }
    [[fallthrough]];
  case PTRACE_EVENT_FORK:
  case PTRACE_EVENT_VFORK: {
    bool is_vfork = event == PTRACE_EVENT_VFORK;
    std::unique_ptr<NativeProcessLinux> child_process{new NativeProcessLinux(
        static_cast<::pid_t>(child_pid), m_terminal_fd, m_delegate, m_arch,
        m_manager, {static_cast<::pid_t>(child_pid)})};
    if (!is_vfork)
      child_process->m_software_breakpoints = m_software_breakpoints;

    Extension expected_ext = is_vfork ? Extension::vfork : Extension::fork;
    if (bool(m_enabled_extensions & expected_ext)) {
      m_delegate.NewSubprocess(this, std::move(child_process));
      // NB: non-vfork clone() is reported as fork
      parent.SetStoppedByFork(is_vfork, child_pid);
      StopRunningThreads(parent.GetID());
    } else {
      child_process->Detach();
      ResumeThread(parent, parent.GetState(), LLDB_INVALID_SIGNAL_NUMBER);
    }
    break;
  }
  default:
    llvm_unreachable("unknown clone_info.event");
  }

  return true;
}

bool NativeProcessLinux::SupportHardwareSingleStepping() const {
  if (m_arch.IsMIPS() || m_arch.GetMachine() == llvm::Triple::arm ||
      m_arch.GetTriple().isRISCV() || m_arch.GetTriple().isLoongArch())
    return false;
  return true;
}

Status NativeProcessLinux::Resume(const ResumeActionList &resume_actions) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid {0}", GetID());

  NotifyTracersProcessWillResume();

  bool software_single_step = !SupportHardwareSingleStepping();

  if (software_single_step) {
    for (const auto &thread : m_threads) {
      assert(thread && "thread list should not contain NULL threads");

      const ResumeAction *const action =
          resume_actions.GetActionForThread(thread->GetID(), true);
      if (action == nullptr)
        continue;

      if (action->state == eStateStepping) {
        Status error = SetupSoftwareSingleStepping(
            static_cast<NativeThreadLinux &>(*thread));
        if (error.Fail())
          return error;
      }
    }
  }

  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not contain NULL threads");

    const ResumeAction *const action =
        resume_actions.GetActionForThread(thread->GetID(), true);

    if (action == nullptr) {
      LLDB_LOG(log, "no action specified for pid {0} tid {1}", GetID(),
               thread->GetID());
      continue;
    }

    LLDB_LOG(log, "processing resume action state {0} for pid {1} tid {2}",
             action->state, GetID(), thread->GetID());

    switch (action->state) {
    case eStateRunning:
    case eStateStepping: {
      // Run the thread, possibly feeding it the signal.
      const int signo = action->signal;
      Status error = ResumeThread(static_cast<NativeThreadLinux &>(*thread),
                                  action->state, signo);
      if (error.Fail())
        return Status("NativeProcessLinux::%s: failed to resume thread "
                      "for pid %" PRIu64 ", tid %" PRIu64 ", error = %s",
                      __FUNCTION__, GetID(), thread->GetID(),
                      error.AsCString());

      break;
    }

    case eStateSuspended:
    case eStateStopped:
      break;

    default:
      return Status("NativeProcessLinux::%s (): unexpected state %s specified "
                    "for pid %" PRIu64 ", tid %" PRIu64,
                    __FUNCTION__, StateAsCString(action->state), GetID(),
                    thread->GetID());
    }
  }

  return Status();
}

Status NativeProcessLinux::Halt() {
  Status error;

  if (kill(GetID(), SIGSTOP) != 0)
    error.SetErrorToErrno();

  return error;
}

Status NativeProcessLinux::Detach() {
  Status error;

  // Tell ptrace to detach from the process.
  if (GetID() == LLDB_INVALID_PROCESS_ID)
    return error;

  // Cancel out any SIGSTOPs we may have sent while stopping the process.
  // Otherwise, the process may stop as soon as we detach from it.
  kill(GetID(), SIGCONT);

  for (const auto &thread : m_threads) {
    Status e = Detach(thread->GetID());
    if (e.Fail())
      error =
          e; // Save the error, but still attempt to detach from other threads.
  }

  m_intel_pt_collector.Clear();

  return error;
}

Status NativeProcessLinux::Signal(int signo) {
  Status error;

  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "sending signal {0} ({1}) to pid {1}", signo,
           Host::GetSignalAsCString(signo), GetID());

  if (kill(GetID(), signo))
    error.SetErrorToErrno();

  return error;
}

Status NativeProcessLinux::Interrupt() {
  // Pick a running thread (or if none, a not-dead stopped thread) as the
  // chosen thread that will be the stop-reason thread.
  Log *log = GetLog(POSIXLog::Process);

  NativeThreadProtocol *running_thread = nullptr;
  NativeThreadProtocol *stopped_thread = nullptr;

  LLDB_LOG(log, "selecting running thread for interrupt target");
  for (const auto &thread : m_threads) {
    // If we have a running or stepping thread, we'll call that the target of
    // the interrupt.
    const auto thread_state = thread->GetState();
    if (thread_state == eStateRunning || thread_state == eStateStepping) {
      running_thread = thread.get();
      break;
    } else if (!stopped_thread && StateIsStoppedState(thread_state, true)) {
      // Remember the first non-dead stopped thread.  We'll use that as a
      // backup if there are no running threads.
      stopped_thread = thread.get();
    }
  }

  if (!running_thread && !stopped_thread) {
    Status error("found no running/stepping or live stopped threads as target "
                 "for interrupt");
    LLDB_LOG(log, "skipping due to error: {0}", error);

    return error;
  }

  NativeThreadProtocol *deferred_signal_thread =
      running_thread ? running_thread : stopped_thread;

  LLDB_LOG(log, "pid {0} {1} tid {2} chosen for interrupt target", GetID(),
           running_thread ? "running" : "stopped",
           deferred_signal_thread->GetID());

  StopRunningThreads(deferred_signal_thread->GetID());

  return Status();
}

Status NativeProcessLinux::Kill() {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid {0}", GetID());

  Status error;

  switch (m_state) {
  case StateType::eStateInvalid:
  case StateType::eStateExited:
  case StateType::eStateCrashed:
  case StateType::eStateDetached:
  case StateType::eStateUnloaded:
    // Nothing to do - the process is already dead.
    LLDB_LOG(log, "ignored for PID {0} due to current state: {1}", GetID(),
             m_state);
    return error;

  case StateType::eStateConnected:
  case StateType::eStateAttaching:
  case StateType::eStateLaunching:
  case StateType::eStateStopped:
  case StateType::eStateRunning:
  case StateType::eStateStepping:
  case StateType::eStateSuspended:
    // We can try to kill a process in these states.
    break;
  }

  if (kill(GetID(), SIGKILL) != 0) {
    error.SetErrorToErrno();
    return error;
  }

  return error;
}

Status NativeProcessLinux::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                               MemoryRegionInfo &range_info) {
  // FIXME review that the final memory region returned extends to the end of
  // the virtual address space,
  // with no perms if it is not mapped.

  // Use an approach that reads memory regions from /proc/{pid}/maps. Assume
  // proc maps entries are in ascending order.
  // FIXME assert if we find differently.

  if (m_supports_mem_region == LazyBool::eLazyBoolNo) {
    // We're done.
    return Status("unsupported");
  }

  Status error = PopulateMemoryRegionCache();
  if (error.Fail()) {
    return error;
  }

  lldb::addr_t prev_base_address = 0;

  // FIXME start by finding the last region that is <= target address using
  // binary search.  Data is sorted.
  // There can be a ton of regions on pthreads apps with lots of threads.
  for (auto it = m_mem_region_cache.begin(); it != m_mem_region_cache.end();
       ++it) {
    MemoryRegionInfo &proc_entry_info = it->first;

    // Sanity check assumption that /proc/{pid}/maps entries are ascending.
    assert((proc_entry_info.GetRange().GetRangeBase() >= prev_base_address) &&
           "descending /proc/pid/maps entries detected, unexpected");
    prev_base_address = proc_entry_info.GetRange().GetRangeBase();
    UNUSED_IF_ASSERT_DISABLED(prev_base_address);

    // If the target address comes before this entry, indicate distance to next
    // region.
    if (load_addr < proc_entry_info.GetRange().GetRangeBase()) {
      range_info.GetRange().SetRangeBase(load_addr);
      range_info.GetRange().SetByteSize(
          proc_entry_info.GetRange().GetRangeBase() - load_addr);
      range_info.SetReadable(MemoryRegionInfo::OptionalBool::eNo);
      range_info.SetWritable(MemoryRegionInfo::OptionalBool::eNo);
      range_info.SetExecutable(MemoryRegionInfo::OptionalBool::eNo);
      range_info.SetMapped(MemoryRegionInfo::OptionalBool::eNo);

      return error;
    } else if (proc_entry_info.GetRange().Contains(load_addr)) {
      // The target address is within the memory region we're processing here.
      range_info = proc_entry_info;
      return error;
    }

    // The target memory address comes somewhere after the region we just
    // parsed.
  }

  // If we made it here, we didn't find an entry that contained the given
  // address. Return the load_addr as start and the amount of bytes betwwen
  // load address and the end of the memory as size.
  range_info.GetRange().SetRangeBase(load_addr);
  range_info.GetRange().SetRangeEnd(LLDB_INVALID_ADDRESS);
  range_info.SetReadable(MemoryRegionInfo::OptionalBool::eNo);
  range_info.SetWritable(MemoryRegionInfo::OptionalBool::eNo);
  range_info.SetExecutable(MemoryRegionInfo::OptionalBool::eNo);
  range_info.SetMapped(MemoryRegionInfo::OptionalBool::eNo);
  return error;
}

Status NativeProcessLinux::PopulateMemoryRegionCache() {
  Log *log = GetLog(POSIXLog::Process);

  // If our cache is empty, pull the latest.  There should always be at least
  // one memory region if memory region handling is supported.
  if (!m_mem_region_cache.empty()) {
    LLDB_LOG(log, "reusing {0} cached memory region entries",
             m_mem_region_cache.size());
    return Status();
  }

  Status Result;
  LinuxMapCallback callback = [&](llvm::Expected<MemoryRegionInfo> Info) {
    if (Info) {
      FileSpec file_spec(Info->GetName().GetCString());
      FileSystem::Instance().Resolve(file_spec);
      m_mem_region_cache.emplace_back(*Info, file_spec);
      return true;
    }

    Result = Info.takeError();
    m_supports_mem_region = LazyBool::eLazyBoolNo;
    LLDB_LOG(log, "failed to parse proc maps: {0}", Result);
    return false;
  };

  // Linux kernel since 2.6.14 has /proc/{pid}/smaps
  // if CONFIG_PROC_PAGE_MONITOR is enabled
  auto BufferOrError = getProcFile(GetID(), GetCurrentThreadID(), "smaps");
  if (BufferOrError)
    ParseLinuxSMapRegions(BufferOrError.get()->getBuffer(), callback);
  else {
    BufferOrError = getProcFile(GetID(), GetCurrentThreadID(), "maps");
    if (!BufferOrError) {
      m_supports_mem_region = LazyBool::eLazyBoolNo;
      return BufferOrError.getError();
    }

    ParseLinuxMapRegions(BufferOrError.get()->getBuffer(), callback);
  }

  if (Result.Fail())
    return Result;

  if (m_mem_region_cache.empty()) {
    // No entries after attempting to read them.  This shouldn't happen if
    // /proc/{pid}/maps is supported. Assume we don't support map entries via
    // procfs.
    m_supports_mem_region = LazyBool::eLazyBoolNo;
    LLDB_LOG(log,
             "failed to find any procfs maps entries, assuming no support "
             "for memory region metadata retrieval");
    return Status("not supported");
  }

  LLDB_LOG(log, "read {0} memory region entries from /proc/{1}/maps",
           m_mem_region_cache.size(), GetID());

  // We support memory retrieval, remember that.
  m_supports_mem_region = LazyBool::eLazyBoolYes;
  return Status();
}

void NativeProcessLinux::DoStopIDBumped(uint32_t newBumpId) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "newBumpId={0}", newBumpId);
  LLDB_LOG(log, "clearing {0} entries from memory region cache",
           m_mem_region_cache.size());
  m_mem_region_cache.clear();
}

llvm::Expected<uint64_t>
NativeProcessLinux::Syscall(llvm::ArrayRef<uint64_t> args) {
  PopulateMemoryRegionCache();
  auto region_it = llvm::find_if(m_mem_region_cache, [](const auto &pair) {
    return pair.first.GetExecutable() == MemoryRegionInfo::eYes &&
        pair.first.GetShared() != MemoryRegionInfo::eYes;
  });
  if (region_it == m_mem_region_cache.end())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "No executable memory region found!");

  addr_t exe_addr = region_it->first.GetRange().GetRangeBase();

  NativeThreadLinux &thread = *GetCurrentThread();
  assert(thread.GetState() == eStateStopped);
  NativeRegisterContextLinux &reg_ctx = thread.GetRegisterContext();

  NativeRegisterContextLinux::SyscallData syscall_data =
      *reg_ctx.GetSyscallData();

  WritableDataBufferSP registers_sp;
  if (llvm::Error Err = reg_ctx.ReadAllRegisterValues(registers_sp).ToError())
    return std::move(Err);
  auto restore_regs = llvm::make_scope_exit(
      [&] { reg_ctx.WriteAllRegisterValues(registers_sp); });

  llvm::SmallVector<uint8_t, 8> memory(syscall_data.Insn.size());
  size_t bytes_read;
  if (llvm::Error Err =
          ReadMemory(exe_addr, memory.data(), memory.size(), bytes_read)
              .ToError()) {
    return std::move(Err);
  }

  auto restore_mem = llvm::make_scope_exit(
      [&] { WriteMemory(exe_addr, memory.data(), memory.size(), bytes_read); });

  if (llvm::Error Err = reg_ctx.SetPC(exe_addr).ToError())
    return std::move(Err);

  for (const auto &zip : llvm::zip_first(args, syscall_data.Args)) {
    if (llvm::Error Err =
            reg_ctx
                .WriteRegisterFromUnsigned(std::get<1>(zip), std::get<0>(zip))
                .ToError()) {
      return std::move(Err);
    }
  }
  if (llvm::Error Err = WriteMemory(exe_addr, syscall_data.Insn.data(),
                                    syscall_data.Insn.size(), bytes_read)
                            .ToError())
    return std::move(Err);

  m_mem_region_cache.clear();

  // With software single stepping the syscall insn buffer must also include a
  // trap instruction to stop the process.
  int req = SupportHardwareSingleStepping() ? PTRACE_SINGLESTEP : PTRACE_CONT;
  if (llvm::Error Err =
          PtraceWrapper(req, thread.GetID(), nullptr, nullptr).ToError())
    return std::move(Err);

  int status;
  ::pid_t wait_pid = llvm::sys::RetryAfterSignal(-1, ::waitpid, thread.GetID(),
                                                 &status, __WALL);
  if (wait_pid == -1) {
    return llvm::errorCodeToError(
        std::error_code(errno, std::generic_category()));
  }
  assert((unsigned)wait_pid == thread.GetID());

  uint64_t result = reg_ctx.ReadRegisterAsUnsigned(syscall_data.Result, -ESRCH);

  // Values larger than this are actually negative errno numbers.
  uint64_t errno_threshold =
      (uint64_t(-1) >> (64 - 8 * m_arch.GetAddressByteSize())) - 0x1000;
  if (result > errno_threshold) {
    return llvm::errorCodeToError(
        std::error_code(-result & 0xfff, std::generic_category()));
  }

  return result;
}

llvm::Expected<addr_t>
NativeProcessLinux::AllocateMemory(size_t size, uint32_t permissions) {

  std::optional<NativeRegisterContextLinux::MmapData> mmap_data =
      GetCurrentThread()->GetRegisterContext().GetMmapData();
  if (!mmap_data)
    return llvm::make_error<UnimplementedError>();

  unsigned prot = PROT_NONE;
  assert((permissions & (ePermissionsReadable | ePermissionsWritable |
                         ePermissionsExecutable)) == permissions &&
         "Unknown permission!");
  if (permissions & ePermissionsReadable)
    prot |= PROT_READ;
  if (permissions & ePermissionsWritable)
    prot |= PROT_WRITE;
  if (permissions & ePermissionsExecutable)
    prot |= PROT_EXEC;

  llvm::Expected<uint64_t> Result =
      Syscall({mmap_data->SysMmap, 0, size, prot, MAP_ANONYMOUS | MAP_PRIVATE,
               uint64_t(-1), 0});
  if (Result)
    m_allocated_memory.try_emplace(*Result, size);
  return Result;
}

llvm::Error NativeProcessLinux::DeallocateMemory(lldb::addr_t addr) {
  std::optional<NativeRegisterContextLinux::MmapData> mmap_data =
      GetCurrentThread()->GetRegisterContext().GetMmapData();
  if (!mmap_data)
    return llvm::make_error<UnimplementedError>();

  auto it = m_allocated_memory.find(addr);
  if (it == m_allocated_memory.end())
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "Memory not allocated by the debugger.");

  llvm::Expected<uint64_t> Result =
      Syscall({mmap_data->SysMunmap, addr, it->second});
  if (!Result)
    return Result.takeError();

  m_allocated_memory.erase(it);
  return llvm::Error::success();
}

Status NativeProcessLinux::ReadMemoryTags(int32_t type, lldb::addr_t addr,
                                          size_t len,
                                          std::vector<uint8_t> &tags) {
  llvm::Expected<NativeRegisterContextLinux::MemoryTaggingDetails> details =
      GetCurrentThread()->GetRegisterContext().GetMemoryTaggingDetails(type);
  if (!details)
    return Status(details.takeError());

  // Ignore 0 length read
  if (!len)
    return Status();

  // lldb will align the range it requests but it is not required to by
  // the protocol so we'll do it again just in case.
  // Remove tag bits too. Ptrace calls may work regardless but that
  // is not a guarantee.
  MemoryTagManager::TagRange range(details->manager->RemoveTagBits(addr), len);
  range = details->manager->ExpandToGranule(range);

  // Allocate enough space for all tags to be read
  size_t num_tags = range.GetByteSize() / details->manager->GetGranuleSize();
  tags.resize(num_tags * details->manager->GetTagSizeInBytes());

  struct iovec tags_iovec;
  uint8_t *dest = tags.data();
  lldb::addr_t read_addr = range.GetRangeBase();

  // This call can return partial data so loop until we error or
  // get all tags back.
  while (num_tags) {
    tags_iovec.iov_base = dest;
    tags_iovec.iov_len = num_tags;

    Status error = NativeProcessLinux::PtraceWrapper(
        details->ptrace_read_req, GetCurrentThreadID(),
        reinterpret_cast<void *>(read_addr), static_cast<void *>(&tags_iovec),
        0, nullptr);

    if (error.Fail()) {
      // Discard partial reads
      tags.resize(0);
      return error;
    }

    size_t tags_read = tags_iovec.iov_len;
    assert(tags_read && (tags_read <= num_tags));

    dest += tags_read * details->manager->GetTagSizeInBytes();
    read_addr += details->manager->GetGranuleSize() * tags_read;
    num_tags -= tags_read;
  }

  return Status();
}

Status NativeProcessLinux::WriteMemoryTags(int32_t type, lldb::addr_t addr,
                                           size_t len,
                                           const std::vector<uint8_t> &tags) {
  llvm::Expected<NativeRegisterContextLinux::MemoryTaggingDetails> details =
      GetCurrentThread()->GetRegisterContext().GetMemoryTaggingDetails(type);
  if (!details)
    return Status(details.takeError());

  // Ignore 0 length write
  if (!len)
    return Status();

  // lldb will align the range it requests but it is not required to by
  // the protocol so we'll do it again just in case.
  // Remove tag bits too. Ptrace calls may work regardless but that
  // is not a guarantee.
  MemoryTagManager::TagRange range(details->manager->RemoveTagBits(addr), len);
  range = details->manager->ExpandToGranule(range);

  // Not checking number of tags here, we may repeat them below
  llvm::Expected<std::vector<lldb::addr_t>> unpacked_tags_or_err =
      details->manager->UnpackTagsData(tags);
  if (!unpacked_tags_or_err)
    return Status(unpacked_tags_or_err.takeError());

  llvm::Expected<std::vector<lldb::addr_t>> repeated_tags_or_err =
      details->manager->RepeatTagsForRange(*unpacked_tags_or_err, range);
  if (!repeated_tags_or_err)
    return Status(repeated_tags_or_err.takeError());

  // Repack them for ptrace to use
  llvm::Expected<std::vector<uint8_t>> final_tag_data =
      details->manager->PackTags(*repeated_tags_or_err);
  if (!final_tag_data)
    return Status(final_tag_data.takeError());

  struct iovec tags_vec;
  uint8_t *src = final_tag_data->data();
  lldb::addr_t write_addr = range.GetRangeBase();
  // unpacked tags size because the number of bytes per tag might not be 1
  size_t num_tags = repeated_tags_or_err->size();

  // This call can partially write tags, so we loop until we
  // error or all tags have been written.
  while (num_tags > 0) {
    tags_vec.iov_base = src;
    tags_vec.iov_len = num_tags;

    Status error = NativeProcessLinux::PtraceWrapper(
        details->ptrace_write_req, GetCurrentThreadID(),
        reinterpret_cast<void *>(write_addr), static_cast<void *>(&tags_vec), 0,
        nullptr);

    if (error.Fail()) {
      // Don't attempt to restore the original values in the case of a partial
      // write
      return error;
    }

    size_t tags_written = tags_vec.iov_len;
    assert(tags_written && (tags_written <= num_tags));

    src += tags_written * details->manager->GetTagSizeInBytes();
    write_addr += details->manager->GetGranuleSize() * tags_written;
    num_tags -= tags_written;
  }

  return Status();
}

size_t NativeProcessLinux::UpdateThreads() {
  // The NativeProcessLinux monitoring threads are always up to date with
  // respect to thread state and they keep the thread list populated properly.
  // All this method needs to do is return the thread count.
  return m_threads.size();
}

Status NativeProcessLinux::SetBreakpoint(lldb::addr_t addr, uint32_t size,
                                         bool hardware) {
  if (hardware)
    return SetHardwareBreakpoint(addr, size);
  else
    return SetSoftwareBreakpoint(addr, size);
}

Status NativeProcessLinux::RemoveBreakpoint(lldb::addr_t addr, bool hardware) {
  if (hardware)
    return RemoveHardwareBreakpoint(addr);
  else
    return NativeProcessProtocol::RemoveBreakpoint(addr);
}

llvm::Expected<llvm::ArrayRef<uint8_t>>
NativeProcessLinux::GetSoftwareBreakpointTrapOpcode(size_t size_hint) {
  // The ARM reference recommends the use of 0xe7fddefe and 0xdefe but the
  // linux kernel does otherwise.
  static const uint8_t g_arm_opcode[] = {0xf0, 0x01, 0xf0, 0xe7};
  static const uint8_t g_thumb_opcode[] = {0x01, 0xde};

  switch (GetArchitecture().GetMachine()) {
  case llvm::Triple::arm:
    switch (size_hint) {
    case 2:
      return llvm::ArrayRef(g_thumb_opcode);
    case 4:
      return llvm::ArrayRef(g_arm_opcode);
    default:
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Unrecognised trap opcode size hint!");
    }
  default:
    return NativeProcessProtocol::GetSoftwareBreakpointTrapOpcode(size_hint);
  }
}

Status NativeProcessLinux::ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                      size_t &bytes_read) {
  if (ProcessVmReadvSupported()) {
    // The process_vm_readv path is about 50 times faster than ptrace api. We
    // want to use this syscall if it is supported.

    struct iovec local_iov, remote_iov;
    local_iov.iov_base = buf;
    local_iov.iov_len = size;
    remote_iov.iov_base = reinterpret_cast<void *>(addr);
    remote_iov.iov_len = size;

    bytes_read = process_vm_readv(GetCurrentThreadID(), &local_iov, 1,
                                  &remote_iov, 1, 0);
    const bool success = bytes_read == size;

    Log *log = GetLog(POSIXLog::Process);
    LLDB_LOG(log,
             "using process_vm_readv to read {0} bytes from inferior "
             "address {1:x}: {2}",
             size, addr, success ? "Success" : llvm::sys::StrError(errno));

    if (success)
      return Status();
    // else the call failed for some reason, let's retry the read using ptrace
    // api.
  }

  unsigned char *dst = static_cast<unsigned char *>(buf);
  size_t remainder;
  long data;

  Log *log = GetLog(POSIXLog::Memory);
  LLDB_LOG(log, "addr = {0}, buf = {1}, size = {2}", addr, buf, size);

  for (bytes_read = 0; bytes_read < size; bytes_read += remainder) {
    Status error = NativeProcessLinux::PtraceWrapper(
        PTRACE_PEEKDATA, GetCurrentThreadID(), (void *)addr, nullptr, 0, &data);
    if (error.Fail())
      return error;

    remainder = size - bytes_read;
    remainder = remainder > k_ptrace_word_size ? k_ptrace_word_size : remainder;

    // Copy the data into our buffer
    memcpy(dst, &data, remainder);

    LLDB_LOG(log, "[{0:x}]:{1:x}", addr, data);
    addr += k_ptrace_word_size;
    dst += k_ptrace_word_size;
  }
  return Status();
}

Status NativeProcessLinux::WriteMemory(lldb::addr_t addr, const void *buf,
                                       size_t size, size_t &bytes_written) {
  const unsigned char *src = static_cast<const unsigned char *>(buf);
  size_t remainder;
  Status error;

  Log *log = GetLog(POSIXLog::Memory);
  LLDB_LOG(log, "addr = {0}, buf = {1}, size = {2}", addr, buf, size);

  for (bytes_written = 0; bytes_written < size; bytes_written += remainder) {
    remainder = size - bytes_written;
    remainder = remainder > k_ptrace_word_size ? k_ptrace_word_size : remainder;

    if (remainder == k_ptrace_word_size) {
      unsigned long data = 0;
      memcpy(&data, src, k_ptrace_word_size);

      LLDB_LOG(log, "[{0:x}]:{1:x}", addr, data);
      error = NativeProcessLinux::PtraceWrapper(
          PTRACE_POKEDATA, GetCurrentThreadID(), (void *)addr, (void *)data);
      if (error.Fail())
        return error;
    } else {
      unsigned char buff[8];
      size_t bytes_read;
      error = ReadMemory(addr, buff, k_ptrace_word_size, bytes_read);
      if (error.Fail())
        return error;

      memcpy(buff, src, remainder);

      size_t bytes_written_rec;
      error = WriteMemory(addr, buff, k_ptrace_word_size, bytes_written_rec);
      if (error.Fail())
        return error;

      LLDB_LOG(log, "[{0:x}]:{1:x} ({2:x})", addr, *(const unsigned long *)src,
               *(unsigned long *)buff);
    }

    addr += k_ptrace_word_size;
    src += k_ptrace_word_size;
  }
  return error;
}

Status NativeProcessLinux::GetSignalInfo(lldb::tid_t tid, void *siginfo) const {
  return PtraceWrapper(PTRACE_GETSIGINFO, tid, nullptr, siginfo);
}

Status NativeProcessLinux::GetEventMessage(lldb::tid_t tid,
                                           unsigned long *message) {
  return PtraceWrapper(PTRACE_GETEVENTMSG, tid, nullptr, message);
}

Status NativeProcessLinux::Detach(lldb::tid_t tid) {
  if (tid == LLDB_INVALID_THREAD_ID)
    return Status();

  return PtraceWrapper(PTRACE_DETACH, tid);
}

bool NativeProcessLinux::HasThreadNoLock(lldb::tid_t thread_id) {
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not contain NULL threads");
    if (thread->GetID() == thread_id) {
      // We have this thread.
      return true;
    }
  }

  // We don't have this thread.
  return false;
}

void NativeProcessLinux::StopTrackingThread(NativeThreadLinux &thread) {
  Log *const log = GetLog(POSIXLog::Thread);
  lldb::tid_t thread_id = thread.GetID();
  LLDB_LOG(log, "tid: {0}", thread_id);

  auto it = llvm::find_if(m_threads, [&](const auto &thread_up) {
    return thread_up.get() == &thread;
  });
  assert(it != m_threads.end());
  m_threads.erase(it);

  NotifyTracersOfThreadDestroyed(thread_id);
  SignalIfAllThreadsStopped();
}

void NativeProcessLinux::NotifyTracersProcessDidStop() {
  m_intel_pt_collector.ProcessDidStop();
}

void NativeProcessLinux::NotifyTracersProcessWillResume() {
  m_intel_pt_collector.ProcessWillResume();
}

Status NativeProcessLinux::NotifyTracersOfNewThread(lldb::tid_t tid) {
  Log *log = GetLog(POSIXLog::Thread);
  Status error(m_intel_pt_collector.OnThreadCreated(tid));
  if (error.Fail())
    LLDB_LOG(log, "Failed to trace a new thread with intel-pt, tid = {0}. {1}",
             tid, error.AsCString());
  return error;
}

Status NativeProcessLinux::NotifyTracersOfThreadDestroyed(lldb::tid_t tid) {
  Log *log = GetLog(POSIXLog::Thread);
  Status error(m_intel_pt_collector.OnThreadDestroyed(tid));
  if (error.Fail())
    LLDB_LOG(log,
             "Failed to stop a destroyed thread with intel-pt, tid = {0}. {1}",
             tid, error.AsCString());
  return error;
}

NativeThreadLinux &NativeProcessLinux::AddThread(lldb::tid_t thread_id,
                                                 bool resume) {
  Log *log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "pid {0} adding thread with tid {1}", GetID(), thread_id);

  assert(!HasThreadNoLock(thread_id) &&
         "attempted to add a thread by id that already exists");

  // If this is the first thread, save it as the current thread
  if (m_threads.empty())
    SetCurrentThreadID(thread_id);

  m_threads.push_back(std::make_unique<NativeThreadLinux>(*this, thread_id));
  NativeThreadLinux &thread =
      static_cast<NativeThreadLinux &>(*m_threads.back());

  Status tracing_error = NotifyTracersOfNewThread(thread.GetID());
  if (tracing_error.Fail()) {
    thread.SetStoppedByProcessorTrace(tracing_error.AsCString());
    StopRunningThreads(thread.GetID());
  } else if (resume)
    ResumeThread(thread, eStateRunning, LLDB_INVALID_SIGNAL_NUMBER);
  else
    thread.SetStoppedBySignal(SIGSTOP);

  return thread;
}

Status NativeProcessLinux::GetLoadedModuleFileSpec(const char *module_path,
                                                   FileSpec &file_spec) {
  Status error = PopulateMemoryRegionCache();
  if (error.Fail())
    return error;

  FileSpec module_file_spec(module_path);
  FileSystem::Instance().Resolve(module_file_spec);

  file_spec.Clear();
  for (const auto &it : m_mem_region_cache) {
    if (it.second.GetFilename() == module_file_spec.GetFilename()) {
      file_spec = it.second;
      return Status();
    }
  }
  return Status("Module file (%s) not found in /proc/%" PRIu64 "/maps file!",
                module_file_spec.GetFilename().AsCString(), GetID());
}

Status NativeProcessLinux::GetFileLoadAddress(const llvm::StringRef &file_name,
                                              lldb::addr_t &load_addr) {
  load_addr = LLDB_INVALID_ADDRESS;
  Status error = PopulateMemoryRegionCache();
  if (error.Fail())
    return error;

  FileSpec file(file_name);
  for (const auto &it : m_mem_region_cache) {
    if (it.second == file) {
      load_addr = it.first.GetRange().GetRangeBase();
      return Status();
    }
  }
  return Status("No load address found for specified file.");
}

NativeThreadLinux *NativeProcessLinux::GetThreadByID(lldb::tid_t tid) {
  return static_cast<NativeThreadLinux *>(
      NativeProcessProtocol::GetThreadByID(tid));
}

NativeThreadLinux *NativeProcessLinux::GetCurrentThread() {
  return static_cast<NativeThreadLinux *>(
      NativeProcessProtocol::GetCurrentThread());
}

Status NativeProcessLinux::ResumeThread(NativeThreadLinux &thread,
                                        lldb::StateType state, int signo) {
  Log *const log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "tid: {0}", thread.GetID());

  // Before we do the resume below, first check if we have a pending stop
  // notification that is currently waiting for all threads to stop.  This is
  // potentially a buggy situation since we're ostensibly waiting for threads
  // to stop before we send out the pending notification, and here we are
  // resuming one before we send out the pending stop notification.
  if (m_pending_notification_tid != LLDB_INVALID_THREAD_ID) {
    LLDB_LOG(log,
             "about to resume tid {0} per explicit request but we have a "
             "pending stop notification (tid {1}) that is actively "
             "waiting for this thread to stop. Valid sequence of events?",
             thread.GetID(), m_pending_notification_tid);
  }

  // Request a resume.  We expect this to be synchronous and the system to
  // reflect it is running after this completes.
  switch (state) {
  case eStateRunning: {
    const auto resume_result = thread.Resume(signo);
    if (resume_result.Success())
      SetState(eStateRunning, true);
    return resume_result;
  }
  case eStateStepping: {
    const auto step_result = thread.SingleStep(signo);
    if (step_result.Success())
      SetState(eStateRunning, true);
    return step_result;
  }
  default:
    LLDB_LOG(log, "Unhandled state {0}.", state);
    llvm_unreachable("Unhandled state for resume");
  }
}

//===----------------------------------------------------------------------===//

void NativeProcessLinux::StopRunningThreads(const lldb::tid_t triggering_tid) {
  Log *const log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "about to process event: (triggering_tid: {0})",
           triggering_tid);

  m_pending_notification_tid = triggering_tid;

  // Request a stop for all the thread stops that need to be stopped and are
  // not already known to be stopped.
  for (const auto &thread : m_threads) {
    if (StateIsRunningState(thread->GetState()))
      static_cast<NativeThreadLinux *>(thread.get())->RequestStop();
  }

  SignalIfAllThreadsStopped();
  LLDB_LOG(log, "event processing done");
}

void NativeProcessLinux::SignalIfAllThreadsStopped() {
  if (m_pending_notification_tid == LLDB_INVALID_THREAD_ID)
    return; // No pending notification. Nothing to do.

  for (const auto &thread_sp : m_threads) {
    if (StateIsRunningState(thread_sp->GetState()))
      return; // Some threads are still running. Don't signal yet.
  }

  // We have a pending notification and all threads have stopped.
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Breakpoints);

  // Clear any temporary breakpoints we used to implement software single
  // stepping.
  for (const auto &thread_info : m_threads_stepping_with_breakpoint) {
    Status error = RemoveBreakpoint(thread_info.second);
    if (error.Fail())
      LLDB_LOG(log, "pid = {0} remove stepping breakpoint: {1}",
               thread_info.first, error);
  }
  m_threads_stepping_with_breakpoint.clear();

  // Notify the delegate about the stop
  SetCurrentThreadID(m_pending_notification_tid);
  SetState(StateType::eStateStopped, true);
  m_pending_notification_tid = LLDB_INVALID_THREAD_ID;
}

void NativeProcessLinux::ThreadWasCreated(NativeThreadLinux &thread) {
  Log *const log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "tid: {0}", thread.GetID());

  if (m_pending_notification_tid != LLDB_INVALID_THREAD_ID &&
      StateIsRunningState(thread.GetState())) {
    // We will need to wait for this new thread to stop as well before firing
    // the notification.
    thread.RequestStop();
  }
}

// Wrapper for ptrace to catch errors and log calls. Note that ptrace sets
// errno on error because -1 can be a valid result (i.e. for PTRACE_PEEK*)
Status NativeProcessLinux::PtraceWrapper(int req, lldb::pid_t pid, void *addr,
                                         void *data, size_t data_size,
                                         long *result) {
  Status error;
  long int ret;

  Log *log = GetLog(POSIXLog::Ptrace);

  PtraceDisplayBytes(req, data, data_size);

  errno = 0;
  if (req == PTRACE_GETREGSET || req == PTRACE_SETREGSET)
    ret = ptrace(static_cast<__ptrace_request>(req), static_cast<::pid_t>(pid),
                 *(unsigned int *)addr, data);
  else
    ret = ptrace(static_cast<__ptrace_request>(req), static_cast<::pid_t>(pid),
                 addr, data);

  if (ret == -1)
    error.SetErrorToErrno();

  if (result)
    *result = ret;

  LLDB_LOG(log, "ptrace({0}, {1}, {2}, {3}, {4})={5:x}", req, pid, addr, data,
           data_size, ret);

  PtraceDisplayBytes(req, data, data_size);

  if (error.Fail())
    LLDB_LOG(log, "ptrace() failed: {0}", error);

  return error;
}

llvm::Expected<TraceSupportedResponse> NativeProcessLinux::TraceSupported() {
  if (IntelPTCollector::IsSupported())
    return TraceSupportedResponse{"intel-pt", "Intel Processor Trace"};
  return NativeProcessProtocol::TraceSupported();
}

Error NativeProcessLinux::TraceStart(StringRef json_request, StringRef type) {
  if (type == "intel-pt") {
    if (Expected<TraceIntelPTStartRequest> request =
            json::parse<TraceIntelPTStartRequest>(json_request,
                                                  "TraceIntelPTStartRequest")) {
      return m_intel_pt_collector.TraceStart(*request);
    } else
      return request.takeError();
  }

  return NativeProcessProtocol::TraceStart(json_request, type);
}

Error NativeProcessLinux::TraceStop(const TraceStopRequest &request) {
  if (request.type == "intel-pt")
    return m_intel_pt_collector.TraceStop(request);
  return NativeProcessProtocol::TraceStop(request);
}

Expected<json::Value> NativeProcessLinux::TraceGetState(StringRef type) {
  if (type == "intel-pt")
    return m_intel_pt_collector.GetState();
  return NativeProcessProtocol::TraceGetState(type);
}

Expected<std::vector<uint8_t>> NativeProcessLinux::TraceGetBinaryData(
    const TraceGetBinaryDataRequest &request) {
  if (request.type == "intel-pt")
    return m_intel_pt_collector.GetBinaryData(request);
  return NativeProcessProtocol::TraceGetBinaryData(request);
}
