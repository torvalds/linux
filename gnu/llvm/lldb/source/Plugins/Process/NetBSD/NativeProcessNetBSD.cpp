//===-- NativeProcessNetBSD.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeProcessNetBSD.h"

#include "Plugins/Process/NetBSD/NativeRegisterContextNetBSD.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/common/NativeRegisterContext.h"
#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/State.h"
#include "llvm/Support/Errno.h"

// System includes - They have to be included after framework includes because
// they define some macros which collide with variable names in other modules
// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <uvm/uvm_prot.h>
#include <elf.h>
#include <util.h>
// clang-format on

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_netbsd;
using namespace llvm;

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

// Public Static Methods

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessNetBSD::Manager::Launch(ProcessLaunchInfo &launch_info,
                                     NativeDelegate &native_delegate) {
  Log *log = GetLog(POSIXLog::Process);

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
  int wstatus;
  ::pid_t wpid = llvm::sys::RetryAfterSignal(-1, ::waitpid, pid, &wstatus, 0);
  assert(wpid == pid);
  (void)wpid;
  if (!WIFSTOPPED(wstatus)) {
    LLDB_LOG(log, "Could not sync with inferior process: wstatus={1}",
             WaitStatus::Decode(wstatus));
    return llvm::make_error<StringError>("Could not sync with inferior process",
                                         llvm::inconvertibleErrorCode());
  }
  LLDB_LOG(log, "inferior started, now in stopped state");

  ProcessInstanceInfo Info;
  if (!Host::GetProcessInfo(pid, Info)) {
    return llvm::make_error<StringError>("Cannot get process architecture",
                                         llvm::inconvertibleErrorCode());
  }

  // Set the architecture to the exe architecture.
  LLDB_LOG(log, "pid = {0:x}, detected architecture {1}", pid,
           Info.GetArchitecture().GetArchitectureName());

  std::unique_ptr<NativeProcessNetBSD> process_up(new NativeProcessNetBSD(
      pid, launch_info.GetPTY().ReleasePrimaryFileDescriptor(), native_delegate,
      Info.GetArchitecture(), m_mainloop));

  status = process_up->SetupTrace();
  if (status.Fail())
    return status.ToError();

  for (const auto &thread : process_up->m_threads)
    static_cast<NativeThreadNetBSD &>(*thread).SetStoppedBySignal(SIGSTOP);
  process_up->SetState(StateType::eStateStopped, false);

  return std::move(process_up);
}

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessNetBSD::Manager::Attach(
    lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &native_delegate) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid = {0:x}", pid);

  // Retrieve the architecture for the running process.
  ProcessInstanceInfo Info;
  if (!Host::GetProcessInfo(pid, Info)) {
    return llvm::make_error<StringError>("Cannot get process architecture",
                                         llvm::inconvertibleErrorCode());
  }

  std::unique_ptr<NativeProcessNetBSD> process_up(new NativeProcessNetBSD(
      pid, -1, native_delegate, Info.GetArchitecture(), m_mainloop));

  Status status = process_up->Attach();
  if (!status.Success())
    return status.ToError();

  return std::move(process_up);
}

NativeProcessNetBSD::Extension
NativeProcessNetBSD::Manager::GetSupportedExtensions() const {
  return Extension::multiprocess | Extension::fork | Extension::vfork |
         Extension::pass_signals | Extension::auxv | Extension::libraries_svr4 |
         Extension::savecore;
}

// Public Instance Methods

NativeProcessNetBSD::NativeProcessNetBSD(::pid_t pid, int terminal_fd,
                                         NativeDelegate &delegate,
                                         const ArchSpec &arch,
                                         MainLoop &mainloop)
    : NativeProcessELF(pid, terminal_fd, delegate), m_arch(arch),
      m_main_loop(mainloop) {
  if (m_terminal_fd != -1) {
    Status status = EnsureFDFlags(m_terminal_fd, O_NONBLOCK);
    assert(status.Success());
  }

  Status status;
  m_sigchld_handle = mainloop.RegisterSignal(
      SIGCHLD, [this](MainLoopBase &) { SigchldHandler(); }, status);
  assert(m_sigchld_handle && status.Success());
}

// Handles all waitpid events from the inferior process.
void NativeProcessNetBSD::MonitorCallback(lldb::pid_t pid, int signal) {
  switch (signal) {
  case SIGTRAP:
    return MonitorSIGTRAP(pid);
  case SIGSTOP:
    return MonitorSIGSTOP(pid);
  default:
    return MonitorSignal(pid, signal);
  }
}

void NativeProcessNetBSD::MonitorExited(lldb::pid_t pid, WaitStatus status) {
  Log *log = GetLog(POSIXLog::Process);

  LLDB_LOG(log, "got exit signal({0}) , pid = {1}", status, pid);

  /* Stop Tracking All Threads attached to Process */
  m_threads.clear();

  SetExitStatus(status, true);

  // Notify delegate that our process has exited.
  SetState(StateType::eStateExited, true);
}

void NativeProcessNetBSD::MonitorSIGSTOP(lldb::pid_t pid) {
  ptrace_siginfo_t info;

  const auto siginfo_err =
      PtraceWrapper(PT_GET_SIGINFO, pid, &info, sizeof(info));

  // Get details on the signal raised.
  if (siginfo_err.Success()) {
    // Handle SIGSTOP from LLGS (LLDB GDB Server)
    if (info.psi_siginfo.si_code == SI_USER &&
        info.psi_siginfo.si_pid == ::getpid()) {
      /* Stop Tracking all Threads attached to Process */
      for (const auto &thread : m_threads) {
        static_cast<NativeThreadNetBSD &>(*thread).SetStoppedBySignal(
            SIGSTOP, &info.psi_siginfo);
      }
    }
    SetState(StateType::eStateStopped, true);
  }
}

void NativeProcessNetBSD::MonitorSIGTRAP(lldb::pid_t pid) {
  Log *log = GetLog(POSIXLog::Process);
  ptrace_siginfo_t info;

  const auto siginfo_err =
      PtraceWrapper(PT_GET_SIGINFO, pid, &info, sizeof(info));

  // Get details on the signal raised.
  if (siginfo_err.Fail()) {
    LLDB_LOG(log, "PT_GET_SIGINFO failed {0}", siginfo_err);
    return;
  }

  LLDB_LOG(log, "got SIGTRAP, pid = {0}, lwpid = {1}, si_code = {2}", pid,
           info.psi_lwpid, info.psi_siginfo.si_code);
  NativeThreadNetBSD *thread = nullptr;

  if (info.psi_lwpid > 0) {
    for (const auto &t : m_threads) {
      if (t->GetID() == static_cast<lldb::tid_t>(info.psi_lwpid)) {
        thread = static_cast<NativeThreadNetBSD *>(t.get());
        break;
      }
      static_cast<NativeThreadNetBSD *>(t.get())->SetStoppedWithNoReason();
    }
    if (!thread)
      LLDB_LOG(log, "thread not found in m_threads, pid = {0}, LWP = {1}", pid,
               info.psi_lwpid);
  }

  switch (info.psi_siginfo.si_code) {
  case TRAP_BRKPT:
    if (thread) {
      thread->SetStoppedByBreakpoint();
      FixupBreakpointPCAsNeeded(*thread);
    }
    SetState(StateType::eStateStopped, true);
    return;
  case TRAP_TRACE:
    if (thread)
      thread->SetStoppedByTrace();
    SetState(StateType::eStateStopped, true);
    return;
  case TRAP_EXEC: {
    Status error = ReinitializeThreads();
    if (error.Fail()) {
      SetState(StateType::eStateInvalid);
      return;
    }

    // Let our delegate know we have just exec'd.
    NotifyDidExec();

    for (const auto &thread : m_threads)
      static_cast<NativeThreadNetBSD &>(*thread).SetStoppedByExec();
    SetState(StateType::eStateStopped, true);
    return;
  }
  case TRAP_CHLD: {
    ptrace_state_t pst;
    Status error = PtraceWrapper(PT_GET_PROCESS_STATE, pid, &pst, sizeof(pst));
    if (error.Fail()) {
      SetState(StateType::eStateInvalid);
      return;
    }

    assert(thread);
    if (pst.pe_report_event == PTRACE_VFORK_DONE) {
      if ((m_enabled_extensions & Extension::vfork) == Extension::vfork) {
        thread->SetStoppedByVForkDone();
        SetState(StateType::eStateStopped, true);
      } else {
        Status error =
            PtraceWrapper(PT_CONTINUE, pid, reinterpret_cast<void *>(1), 0);
        if (error.Fail())
          SetState(StateType::eStateInvalid);
      }
    } else {
      assert(pst.pe_report_event == PTRACE_FORK ||
             pst.pe_report_event == PTRACE_VFORK);
      MonitorClone(pst.pe_other_pid, pst.pe_report_event == PTRACE_VFORK,
                   *thread);
    }
    return;
  }
  case TRAP_LWP: {
    ptrace_state_t pst;
    Status error = PtraceWrapper(PT_GET_PROCESS_STATE, pid, &pst, sizeof(pst));
    if (error.Fail()) {
      SetState(StateType::eStateInvalid);
      return;
    }

    switch (pst.pe_report_event) {
    case PTRACE_LWP_CREATE: {
      LLDB_LOG(log, "monitoring new thread, pid = {0}, LWP = {1}", pid,
               pst.pe_lwp);
      NativeThreadNetBSD &t = AddThread(pst.pe_lwp);
      error = t.CopyWatchpointsFrom(
          static_cast<NativeThreadNetBSD &>(*GetCurrentThread()));
      if (error.Fail()) {
        LLDB_LOG(log, "failed to copy watchpoints to new thread {0}: {1}",
                 pst.pe_lwp, error);
        SetState(StateType::eStateInvalid);
        return;
      }
    } break;
    case PTRACE_LWP_EXIT:
      LLDB_LOG(log, "removing exited thread, pid = {0}, LWP = {1}", pid,
               pst.pe_lwp);
      RemoveThread(pst.pe_lwp);
      break;
    }

    error = PtraceWrapper(PT_CONTINUE, pid, reinterpret_cast<void *>(1), 0);
    if (error.Fail())
      SetState(StateType::eStateInvalid);
    return;
  }
  case TRAP_DBREG: {
    if (!thread)
      break;

    auto &regctx = static_cast<NativeRegisterContextNetBSD &>(
        thread->GetRegisterContext());
    uint32_t wp_index = LLDB_INVALID_INDEX32;
    Status error = regctx.GetWatchpointHitIndex(
        wp_index, (uintptr_t)info.psi_siginfo.si_addr);
    if (error.Fail())
      LLDB_LOG(log,
               "received error while checking for watchpoint hits, pid = "
               "{0}, LWP = {1}, error = {2}",
               pid, info.psi_lwpid, error);
    if (wp_index != LLDB_INVALID_INDEX32) {
      thread->SetStoppedByWatchpoint(wp_index);
      regctx.ClearWatchpointHit(wp_index);
      SetState(StateType::eStateStopped, true);
      return;
    }

    thread->SetStoppedByTrace();
    SetState(StateType::eStateStopped, true);
    return;
  }
  }

  // Either user-generated SIGTRAP or an unknown event that would
  // otherwise leave the debugger hanging.
  LLDB_LOG(log, "unknown SIGTRAP, passing to generic handler");
  MonitorSignal(pid, SIGTRAP);
}

void NativeProcessNetBSD::MonitorSignal(lldb::pid_t pid, int signal) {
  Log *log = GetLog(POSIXLog::Process);
  ptrace_siginfo_t info;

  const auto siginfo_err =
      PtraceWrapper(PT_GET_SIGINFO, pid, &info, sizeof(info));
  if (siginfo_err.Fail()) {
    LLDB_LOG(log, "PT_LWPINFO failed {0}", siginfo_err);
    return;
  }

  for (const auto &abs_thread : m_threads) {
    NativeThreadNetBSD &thread = static_cast<NativeThreadNetBSD &>(*abs_thread);
    assert(info.psi_lwpid >= 0);
    if (info.psi_lwpid == 0 ||
        static_cast<lldb::tid_t>(info.psi_lwpid) == thread.GetID())
      thread.SetStoppedBySignal(info.psi_siginfo.si_signo, &info.psi_siginfo);
    else
      thread.SetStoppedWithNoReason();
  }
  SetState(StateType::eStateStopped, true);
}

Status NativeProcessNetBSD::StopProcess(lldb::pid_t pid) {
#ifdef PT_STOP
  return PtraceWrapper(PT_STOP, pid);
#else
  Log *log = GetLog(POSIXLog::Ptrace);
  Status error;
  int ret;

  errno = 0;
  ret = kill(pid, SIGSTOP);

  if (ret == -1)
    error.SetErrorToErrno();

  LLDB_LOG(log, "kill({0}, SIGSTOP)", pid);

  if (error.Fail())
    LLDB_LOG(log, "kill() failed: {0}", error);

  return error;
#endif
}

Status NativeProcessNetBSD::PtraceWrapper(int req, lldb::pid_t pid, void *addr,
                                          int data, int *result) {
  Log *log = GetLog(POSIXLog::Ptrace);
  Status error;
  int ret;

  errno = 0;
  ret = ptrace(req, static_cast<::pid_t>(pid), addr, data);

  if (ret == -1)
    error.SetErrorToErrno();

  if (result)
    *result = ret;

  LLDB_LOG(log, "ptrace({0}, {1}, {2}, {3})={4:x}", req, pid, addr, data, ret);

  if (error.Fail())
    LLDB_LOG(log, "ptrace() failed: {0}", error);

  return error;
}

static llvm::Expected<ptrace_siginfo_t> ComputeSignalInfo(
    const std::vector<std::unique_ptr<NativeThreadProtocol>> &threads,
    const ResumeActionList &resume_actions) {
  // We need to account for three possible scenarios:
  // 1. no signal being sent.
  // 2. a signal being sent to one thread.
  // 3. a signal being sent to the whole process.

  // Count signaled threads.  While at it, determine which signal is being sent
  // and ensure there's only one.
  size_t signaled_threads = 0;
  int signal = LLDB_INVALID_SIGNAL_NUMBER;
  lldb::tid_t signaled_lwp;
  for (const auto &thread : threads) {
    assert(thread && "thread list should not contain NULL threads");
    const ResumeAction *action =
        resume_actions.GetActionForThread(thread->GetID(), true);
    if (action) {
      if (action->signal != LLDB_INVALID_SIGNAL_NUMBER) {
        signaled_threads++;
        if (action->signal != signal) {
          if (signal != LLDB_INVALID_SIGNAL_NUMBER)
            return Status("NetBSD does not support passing multiple signals "
                          "simultaneously")
                .ToError();
          signal = action->signal;
          signaled_lwp = thread->GetID();
        }
      }
    }
  }

  if (signaled_threads == 0) {
    ptrace_siginfo_t siginfo;
    siginfo.psi_siginfo.si_signo = LLDB_INVALID_SIGNAL_NUMBER;
    return siginfo;
  }

  if (signaled_threads > 1 && signaled_threads < threads.size())
    return Status("NetBSD does not support passing signal to 1<i<all threads")
        .ToError();

  ptrace_siginfo_t siginfo;
  siginfo.psi_siginfo.si_signo = signal;
  siginfo.psi_siginfo.si_code = SI_USER;
  siginfo.psi_siginfo.si_pid = getpid();
  siginfo.psi_siginfo.si_uid = getuid();
  if (signaled_threads == 1)
    siginfo.psi_lwpid = signaled_lwp;
  else // signal for the whole process
    siginfo.psi_lwpid = 0;
  return siginfo;
}

Status NativeProcessNetBSD::Resume(const ResumeActionList &resume_actions) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid {0}", GetID());

  Status ret;

  Expected<ptrace_siginfo_t> siginfo =
      ComputeSignalInfo(m_threads, resume_actions);
  if (!siginfo)
    return Status(siginfo.takeError());

  for (const auto &abs_thread : m_threads) {
    assert(abs_thread && "thread list should not contain NULL threads");
    NativeThreadNetBSD &thread = static_cast<NativeThreadNetBSD &>(*abs_thread);

    const ResumeAction *action =
        resume_actions.GetActionForThread(thread.GetID(), true);
    // we need to explicit issue suspend requests, so it is simpler to map it
    // into proper action
    ResumeAction suspend_action{thread.GetID(), eStateSuspended,
                                LLDB_INVALID_SIGNAL_NUMBER};

    if (action == nullptr) {
      LLDB_LOG(log, "no action specified for pid {0} tid {1}", GetID(),
               thread.GetID());
      action = &suspend_action;
    }

    LLDB_LOG(
        log,
        "processing resume action state {0} signal {1} for pid {2} tid {3}",
        action->state, action->signal, GetID(), thread.GetID());

    switch (action->state) {
    case eStateRunning:
      ret = thread.Resume();
      break;
    case eStateStepping:
      ret = thread.SingleStep();
      break;
    case eStateSuspended:
    case eStateStopped:
      if (action->signal != LLDB_INVALID_SIGNAL_NUMBER)
        return Status("Passing signal to suspended thread unsupported");

      ret = thread.Suspend();
      break;

    default:
      return Status("NativeProcessNetBSD::%s (): unexpected state %s specified "
                    "for pid %" PRIu64 ", tid %" PRIu64,
                    __FUNCTION__, StateAsCString(action->state), GetID(),
                    thread.GetID());
    }

    if (!ret.Success())
      return ret;
  }

  int signal = 0;
  if (siginfo->psi_siginfo.si_signo != LLDB_INVALID_SIGNAL_NUMBER) {
    ret = PtraceWrapper(PT_SET_SIGINFO, GetID(), &siginfo.get(),
                        sizeof(*siginfo));
    if (!ret.Success())
      return ret;
    signal = siginfo->psi_siginfo.si_signo;
  }

  ret =
      PtraceWrapper(PT_CONTINUE, GetID(), reinterpret_cast<void *>(1), signal);
  if (ret.Success())
    SetState(eStateRunning, true);
  return ret;
}

Status NativeProcessNetBSD::Halt() { return StopProcess(GetID()); }

Status NativeProcessNetBSD::Detach() {
  Status error;

  // Stop monitoring the inferior.
  m_sigchld_handle.reset();

  // Tell ptrace to detach from the process.
  if (GetID() == LLDB_INVALID_PROCESS_ID)
    return error;

  return PtraceWrapper(PT_DETACH, GetID(), reinterpret_cast<void *>(1));
}

Status NativeProcessNetBSD::Signal(int signo) {
  Status error;

  if (kill(GetID(), signo))
    error.SetErrorToErrno();

  return error;
}

Status NativeProcessNetBSD::Interrupt() { return StopProcess(GetID()); }

Status NativeProcessNetBSD::Kill() {
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
             StateAsCString(m_state));
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

Status NativeProcessNetBSD::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                                MemoryRegionInfo &range_info) {

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
    // Sanity check assumption that memory map entries are ascending.
    assert((proc_entry_info.GetRange().GetRangeBase() >= prev_base_address) &&
           "descending memory map entries detected, unexpected");
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

Status NativeProcessNetBSD::PopulateMemoryRegionCache() {
  Log *log = GetLog(POSIXLog::Process);
  // If our cache is empty, pull the latest.  There should always be at least
  // one memory region if memory region handling is supported.
  if (!m_mem_region_cache.empty()) {
    LLDB_LOG(log, "reusing {0} cached memory region entries",
             m_mem_region_cache.size());
    return Status();
  }

  struct kinfo_vmentry *vm;
  size_t count, i;
  vm = kinfo_getvmmap(GetID(), &count);
  if (vm == NULL) {
    m_supports_mem_region = LazyBool::eLazyBoolNo;
    Status error;
    error.SetErrorString("not supported");
    return error;
  }
  for (i = 0; i < count; i++) {
    MemoryRegionInfo info;
    info.Clear();
    info.GetRange().SetRangeBase(vm[i].kve_start);
    info.GetRange().SetRangeEnd(vm[i].kve_end);
    info.SetMapped(MemoryRegionInfo::OptionalBool::eYes);

    if (vm[i].kve_protection & VM_PROT_READ)
      info.SetReadable(MemoryRegionInfo::OptionalBool::eYes);
    else
      info.SetReadable(MemoryRegionInfo::OptionalBool::eNo);

    if (vm[i].kve_protection & VM_PROT_WRITE)
      info.SetWritable(MemoryRegionInfo::OptionalBool::eYes);
    else
      info.SetWritable(MemoryRegionInfo::OptionalBool::eNo);

    if (vm[i].kve_protection & VM_PROT_EXECUTE)
      info.SetExecutable(MemoryRegionInfo::OptionalBool::eYes);
    else
      info.SetExecutable(MemoryRegionInfo::OptionalBool::eNo);

    if (vm[i].kve_path[0])
      info.SetName(vm[i].kve_path);

    m_mem_region_cache.emplace_back(info,
                                    FileSpec(info.GetName().GetCString()));
  }
  free(vm);

  if (m_mem_region_cache.empty()) {
    // No entries after attempting to read them.  This shouldn't happen. Assume
    // we don't support map entries.
    LLDB_LOG(log, "failed to find any vmmap entries, assuming no support "
                  "for memory region metadata retrieval");
    m_supports_mem_region = LazyBool::eLazyBoolNo;
    Status error;
    error.SetErrorString("not supported");
    return error;
  }
  LLDB_LOG(log, "read {0} memory region entries from process {1}",
           m_mem_region_cache.size(), GetID());
  // We support memory retrieval, remember that.
  m_supports_mem_region = LazyBool::eLazyBoolYes;
  return Status();
}

lldb::addr_t NativeProcessNetBSD::GetSharedLibraryInfoAddress() {
  // punt on this for now
  return LLDB_INVALID_ADDRESS;
}

size_t NativeProcessNetBSD::UpdateThreads() { return m_threads.size(); }

Status NativeProcessNetBSD::SetBreakpoint(lldb::addr_t addr, uint32_t size,
                                          bool hardware) {
  if (hardware)
    return Status("NativeProcessNetBSD does not support hardware breakpoints");
  else
    return SetSoftwareBreakpoint(addr, size);
}

Status NativeProcessNetBSD::GetLoadedModuleFileSpec(const char *module_path,
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
  return Status("Module file (%s) not found in process' memory map!",
                module_file_spec.GetFilename().AsCString());
}

Status NativeProcessNetBSD::GetFileLoadAddress(const llvm::StringRef &file_name,
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
  return Status("No load address found for file %s.", file_name.str().c_str());
}

void NativeProcessNetBSD::SigchldHandler() {
  Log *log = GetLog(POSIXLog::Process);
  int status;
  ::pid_t wait_pid = llvm::sys::RetryAfterSignal(-1, waitpid, GetID(), &status,
                                                 WALLSIG | WNOHANG);

  if (wait_pid == 0)
    return;

  if (wait_pid == -1) {
    Status error(errno, eErrorTypePOSIX);
    LLDB_LOG(log, "waitpid ({0}, &status, _) failed: {1}", GetID(), error);
    return;
  }

  WaitStatus wait_status = WaitStatus::Decode(status);
  bool exited = wait_status.type == WaitStatus::Exit ||
                (wait_status.type == WaitStatus::Signal &&
                 wait_pid == static_cast<::pid_t>(GetID()));

  LLDB_LOG(log,
           "waitpid ({0}, &status, _) => pid = {1}, status = {2}, exited = {3}",
           GetID(), wait_pid, status, exited);

  if (exited)
    MonitorExited(wait_pid, wait_status);
  else {
    assert(wait_status.type == WaitStatus::Stop);
    MonitorCallback(wait_pid, wait_status.status);
  }
}

bool NativeProcessNetBSD::HasThreadNoLock(lldb::tid_t thread_id) {
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

NativeThreadNetBSD &NativeProcessNetBSD::AddThread(lldb::tid_t thread_id) {
  Log *log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "pid {0} adding thread with tid {1}", GetID(), thread_id);

  assert(thread_id > 0);
  assert(!HasThreadNoLock(thread_id) &&
         "attempted to add a thread by id that already exists");

  // If this is the first thread, save it as the current thread
  if (m_threads.empty())
    SetCurrentThreadID(thread_id);

  m_threads.push_back(std::make_unique<NativeThreadNetBSD>(*this, thread_id));
  return static_cast<NativeThreadNetBSD &>(*m_threads.back());
}

void NativeProcessNetBSD::RemoveThread(lldb::tid_t thread_id) {
  Log *log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "pid {0} removing thread with tid {1}", GetID(), thread_id);

  assert(thread_id > 0);
  assert(HasThreadNoLock(thread_id) &&
         "attempted to remove a thread that does not exist");

  for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
    if ((*it)->GetID() == thread_id) {
      m_threads.erase(it);
      break;
    }
  }
}

Status NativeProcessNetBSD::Attach() {
  // Attach to the requested process.
  // An attach will cause the thread to stop with a SIGSTOP.
  Status status = PtraceWrapper(PT_ATTACH, m_pid);
  if (status.Fail())
    return status;

  int wstatus;
  // Need to use WALLSIG otherwise we receive an error with errno=ECHLD At this
  // point we should have a thread stopped if waitpid succeeds.
  if ((wstatus = llvm::sys::RetryAfterSignal(-1, waitpid, m_pid, nullptr,
                                             WALLSIG)) < 0)
    return Status(errno, eErrorTypePOSIX);

  // Initialize threads and tracing status
  // NB: this needs to be called before we set thread state
  status = SetupTrace();
  if (status.Fail())
    return status;

  for (const auto &thread : m_threads)
    static_cast<NativeThreadNetBSD &>(*thread).SetStoppedBySignal(SIGSTOP);

  // Let our process instance know the thread has stopped.
  SetCurrentThreadID(m_threads.front()->GetID());
  SetState(StateType::eStateStopped, false);
  return Status();
}

Status NativeProcessNetBSD::ReadMemory(lldb::addr_t addr, void *buf,
                                       size_t size, size_t &bytes_read) {
  unsigned char *dst = static_cast<unsigned char *>(buf);
  struct ptrace_io_desc io;

  Log *log = GetLog(POSIXLog::Memory);
  LLDB_LOG(log, "addr = {0}, buf = {1}, size = {2}", addr, buf, size);

  bytes_read = 0;
  io.piod_op = PIOD_READ_D;
  io.piod_len = size;

  do {
    io.piod_offs = (void *)(addr + bytes_read);
    io.piod_addr = dst + bytes_read;

    Status error = NativeProcessNetBSD::PtraceWrapper(PT_IO, GetID(), &io);
    if (error.Fail() || io.piod_len == 0)
      return error;

    bytes_read += io.piod_len;
    io.piod_len = size - bytes_read;
  } while (bytes_read < size);

  return Status();
}

Status NativeProcessNetBSD::WriteMemory(lldb::addr_t addr, const void *buf,
                                        size_t size, size_t &bytes_written) {
  const unsigned char *src = static_cast<const unsigned char *>(buf);
  Status error;
  struct ptrace_io_desc io;

  Log *log = GetLog(POSIXLog::Memory);
  LLDB_LOG(log, "addr = {0}, buf = {1}, size = {2}", addr, buf, size);

  bytes_written = 0;
  io.piod_op = PIOD_WRITE_D;
  io.piod_len = size;

  do {
    io.piod_addr =
        const_cast<void *>(static_cast<const void *>(src + bytes_written));
    io.piod_offs = (void *)(addr + bytes_written);

    Status error = NativeProcessNetBSD::PtraceWrapper(PT_IO, GetID(), &io);
    if (error.Fail() || io.piod_len == 0)
      return error;

    bytes_written += io.piod_len;
    io.piod_len = size - bytes_written;
  } while (bytes_written < size);

  return error;
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
NativeProcessNetBSD::GetAuxvData() const {
  /*
   * ELF_AUX_ENTRIES is currently restricted to kernel
   * (<sys/exec_elf.h> r. 1.155 specifies 15)
   *
   * ptrace(2) returns the whole AUXV including extra fiels after AT_NULL this
   * information isn't needed.
   */
  size_t auxv_size = 100 * sizeof(AuxInfo);

  ErrorOr<std::unique_ptr<WritableMemoryBuffer>> buf =
      llvm::WritableMemoryBuffer::getNewMemBuffer(auxv_size);

  struct ptrace_io_desc io;
  io.piod_op = PIOD_READ_AUXV;
  io.piod_offs = 0;
  io.piod_addr = static_cast<void *>(buf.get()->getBufferStart());
  io.piod_len = auxv_size;

  Status error = NativeProcessNetBSD::PtraceWrapper(PT_IO, GetID(), &io);

  if (error.Fail())
    return std::error_code(error.GetError(), std::generic_category());

  if (io.piod_len < 1)
    return std::error_code(ECANCELED, std::generic_category());

  return std::move(buf);
}

Status NativeProcessNetBSD::SetupTrace() {
  // Enable event reporting
  ptrace_event_t events;
  Status status =
      PtraceWrapper(PT_GET_EVENT_MASK, GetID(), &events, sizeof(events));
  if (status.Fail())
    return status;
  // TODO: PTRACE_POSIX_SPAWN?
  events.pe_set_event |= PTRACE_LWP_CREATE | PTRACE_LWP_EXIT | PTRACE_FORK |
                         PTRACE_VFORK | PTRACE_VFORK_DONE;
  status = PtraceWrapper(PT_SET_EVENT_MASK, GetID(), &events, sizeof(events));
  if (status.Fail())
    return status;

  return ReinitializeThreads();
}

Status NativeProcessNetBSD::ReinitializeThreads() {
  // Clear old threads
  m_threads.clear();

  // Initialize new thread
#ifdef PT_LWPSTATUS
  struct ptrace_lwpstatus info = {};
  int op = PT_LWPNEXT;
#else
  struct ptrace_lwpinfo info = {};
  int op = PT_LWPINFO;
#endif

  Status error = PtraceWrapper(op, GetID(), &info, sizeof(info));

  if (error.Fail()) {
    return error;
  }
  // Reinitialize from scratch threads and register them in process
  while (info.pl_lwpid != 0) {
    AddThread(info.pl_lwpid);
    error = PtraceWrapper(op, GetID(), &info, sizeof(info));
    if (error.Fail()) {
      return error;
    }
  }

  return error;
}

void NativeProcessNetBSD::MonitorClone(::pid_t child_pid, bool is_vfork,
                                       NativeThreadNetBSD &parent_thread) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "clone, child_pid={0}", child_pid);

  int status;
  ::pid_t wait_pid =
      llvm::sys::RetryAfterSignal(-1, ::waitpid, child_pid, &status, 0);
  if (wait_pid != child_pid) {
    LLDB_LOG(log,
             "waiting for pid {0} failed. Assuming the pid has "
             "disappeared in the meantime",
             child_pid);
    return;
  }
  if (WIFEXITED(status)) {
    LLDB_LOG(log,
             "waiting for pid {0} returned an 'exited' event. Not "
             "tracking it.",
             child_pid);
    return;
  }

  ptrace_siginfo_t info;
  const auto siginfo_err =
      PtraceWrapper(PT_GET_SIGINFO, child_pid, &info, sizeof(info));
  if (siginfo_err.Fail()) {
    LLDB_LOG(log, "PT_GET_SIGINFO failed {0}", siginfo_err);
    return;
  }
  assert(info.psi_lwpid >= 0);
  lldb::tid_t child_tid = info.psi_lwpid;

  std::unique_ptr<NativeProcessNetBSD> child_process{
      new NativeProcessNetBSD(static_cast<::pid_t>(child_pid), m_terminal_fd,
                              m_delegate, m_arch, m_main_loop)};
  if (!is_vfork)
    child_process->m_software_breakpoints = m_software_breakpoints;

  Extension expected_ext = is_vfork ? Extension::vfork : Extension::fork;
  if ((m_enabled_extensions & expected_ext) == expected_ext) {
    child_process->SetupTrace();
    for (const auto &thread : child_process->m_threads)
      static_cast<NativeThreadNetBSD &>(*thread).SetStoppedBySignal(SIGSTOP);
    child_process->SetState(StateType::eStateStopped, false);

    m_delegate.NewSubprocess(this, std::move(child_process));
    if (is_vfork)
      parent_thread.SetStoppedByVFork(child_pid, child_tid);
    else
      parent_thread.SetStoppedByFork(child_pid, child_tid);
    SetState(StateType::eStateStopped, true);
  } else {
    child_process->Detach();
    Status pt_error =
        PtraceWrapper(PT_CONTINUE, GetID(), reinterpret_cast<void *>(1), 0);
    if (pt_error.Fail()) {
      LLDB_LOG_ERROR(log, std::move(pt_error.ToError()),
                     "unable to resume parent process {1}: {0}", GetID());
      SetState(StateType::eStateInvalid);
    }
  }
}

llvm::Expected<std::string>
NativeProcessNetBSD::SaveCore(llvm::StringRef path_hint) {
  llvm::SmallString<128> path{path_hint};
  Status error;

  // Try with the suggested path first.
  if (!path.empty()) {
    error = PtraceWrapper(PT_DUMPCORE, GetID(), path.data(), path.size());
    if (!error.Fail())
      return path.str().str();

    // If the request errored, fall back to a generic temporary file.
  }

  if (std::error_code errc =
          llvm::sys::fs::createTemporaryFile("lldb", "core", path))
    return llvm::createStringError(errc, "Unable to create a temporary file");

  error = PtraceWrapper(PT_DUMPCORE, GetID(), path.data(), path.size());
  if (error.Fail())
    return error.ToError();
  return path.str().str();
}
