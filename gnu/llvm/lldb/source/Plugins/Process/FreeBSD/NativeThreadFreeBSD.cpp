//===-- NativeThreadFreeBSD.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeThreadFreeBSD.h"
#include "NativeRegisterContextFreeBSD.h"

#include "NativeProcessFreeBSD.h"

#include "Plugins/Process/POSIX/CrashReason.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/State.h"
#include "llvm/Support/Errno.h"

// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/user.h>
// clang-format on

#include <sstream>
#include <vector>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_freebsd;

NativeThreadFreeBSD::NativeThreadFreeBSD(NativeProcessFreeBSD &process,
                                         lldb::tid_t tid)
    : NativeThreadProtocol(process, tid), m_state(StateType::eStateInvalid),
      m_stop_info(),
      m_reg_context_up(
          NativeRegisterContextFreeBSD::CreateHostNativeRegisterContextFreeBSD(
              process.GetArchitecture(), *this)),
      m_stop_description() {}

Status NativeThreadFreeBSD::Resume() {
  Status ret = NativeProcessFreeBSD::PtraceWrapper(PT_RESUME, GetID());
  if (!ret.Success())
    return ret;
  ret = NativeProcessFreeBSD::PtraceWrapper(PT_CLEARSTEP, GetID());
  // we can get EINVAL if the architecture in question does not support
  // hardware single-stepping -- that's fine, we have nothing to clear
  // then
  if (ret.GetError() == EINVAL)
    ret.Clear();
  if (ret.Success())
    SetRunning();
  return ret;
}

Status NativeThreadFreeBSD::SingleStep() {
  Status ret = NativeProcessFreeBSD::PtraceWrapper(PT_RESUME, GetID());
  if (!ret.Success())
    return ret;
  ret = NativeProcessFreeBSD::PtraceWrapper(PT_SETSTEP, GetID());
  if (ret.Success())
    SetStepping();
  return ret;
}

Status NativeThreadFreeBSD::Suspend() {
  Status ret = NativeProcessFreeBSD::PtraceWrapper(PT_SUSPEND, GetID());
  if (ret.Success())
    SetStopped();
  return ret;
}

void NativeThreadFreeBSD::SetStoppedBySignal(uint32_t signo,
                                             const siginfo_t *info) {
  Log *log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "tid = {0} in called with signal {1}", GetID(), signo);

  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonSignal;
  m_stop_info.signo = signo;

  m_stop_description.clear();
  if (info) {
    switch (signo) {
    case SIGSEGV:
    case SIGBUS:
    case SIGFPE:
    case SIGILL:
      m_stop_description = GetCrashReasonString(*info);
      break;
    }
  }
}

void NativeThreadFreeBSD::SetStoppedByBreakpoint() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonBreakpoint;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadFreeBSD::SetStoppedByTrace() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonTrace;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadFreeBSD::SetStoppedByExec() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonExec;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadFreeBSD::SetStoppedByWatchpoint(uint32_t wp_index) {
  lldbassert(wp_index != LLDB_INVALID_INDEX32 && "wp_index cannot be invalid");

  std::ostringstream ostr;
  ostr << GetRegisterContext().GetWatchpointAddress(wp_index) << " ";
  ostr << wp_index;

  ostr << " " << GetRegisterContext().GetWatchpointHitAddress(wp_index);

  SetStopped();
  m_stop_description = ostr.str();
  m_stop_info.reason = StopReason::eStopReasonWatchpoint;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadFreeBSD::SetStoppedByFork(lldb::pid_t child_pid,
                                           lldb::tid_t child_tid) {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonFork;
  m_stop_info.signo = SIGTRAP;
  m_stop_info.details.fork.child_pid = child_pid;
  m_stop_info.details.fork.child_tid = child_tid;
}

void NativeThreadFreeBSD::SetStoppedByVFork(lldb::pid_t child_pid,
                                            lldb::tid_t child_tid) {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonVFork;
  m_stop_info.signo = SIGTRAP;
  m_stop_info.details.fork.child_pid = child_pid;
  m_stop_info.details.fork.child_tid = child_tid;
}

void NativeThreadFreeBSD::SetStoppedByVForkDone() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonVForkDone;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadFreeBSD::SetStoppedWithNoReason() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonNone;
  m_stop_info.signo = 0;
}

void NativeThreadFreeBSD::SetStopped() {
  const StateType new_state = StateType::eStateStopped;
  m_state = new_state;
  m_stop_description.clear();
}

void NativeThreadFreeBSD::SetRunning() {
  m_state = StateType::eStateRunning;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

void NativeThreadFreeBSD::SetStepping() {
  m_state = StateType::eStateStepping;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

std::string NativeThreadFreeBSD::GetName() {
  Log *log = GetLog(POSIXLog::Thread);

  std::vector<struct kinfo_proc> kp;
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD,
                static_cast<int>(GetProcess().GetID())};

  while (1) {
    size_t len = kp.size() * sizeof(struct kinfo_proc);
    void *ptr = len == 0 ? nullptr : kp.data();
    int error = ::sysctl(mib, 4, ptr, &len, nullptr, 0);
    if (ptr == nullptr || (error != 0 && errno == ENOMEM)) {
      kp.resize(len / sizeof(struct kinfo_proc));
      continue;
    }
    if (error != 0) {
      len = 0;
      LLDB_LOG(log, "tid = {0} in state {1} failed to get thread name: {2}",
               GetID(), m_state, strerror(errno));
    }
    kp.resize(len / sizeof(struct kinfo_proc));
    break;
  }

  for (auto &procinfo : kp) {
    if (procinfo.ki_tid == static_cast<lwpid_t>(GetID()))
      return procinfo.ki_tdname;
  }

  return "";
}

lldb::StateType NativeThreadFreeBSD::GetState() { return m_state; }

bool NativeThreadFreeBSD::GetStopReason(ThreadStopInfo &stop_info,
                                        std::string &description) {
  Log *log = GetLog(POSIXLog::Thread);
  description.clear();

  switch (m_state) {
  case eStateStopped:
  case eStateCrashed:
  case eStateExited:
  case eStateSuspended:
  case eStateUnloaded:
    stop_info = m_stop_info;
    description = m_stop_description;

    return true;

  case eStateInvalid:
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
  case eStateDetached:
    LLDB_LOG(log, "tid = {0} in state {1} cannot answer stop reason", GetID(),
             StateAsCString(m_state));
    return false;
  }
  llvm_unreachable("unhandled StateType!");
}

NativeRegisterContextFreeBSD &NativeThreadFreeBSD::GetRegisterContext() {
  assert(m_reg_context_up);
  return *m_reg_context_up;
}

Status NativeThreadFreeBSD::SetWatchpoint(lldb::addr_t addr, size_t size,
                                          uint32_t watch_flags, bool hardware) {
  assert(m_state == eStateStopped);
  if (!hardware)
    return Status("not implemented");
  Status error = RemoveWatchpoint(addr);
  if (error.Fail())
    return error;
  uint32_t wp_index =
      GetRegisterContext().SetHardwareWatchpoint(addr, size, watch_flags);
  if (wp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware watchpoint failed.");
  m_watchpoint_index_map.insert({addr, wp_index});
  return Status();
}

Status NativeThreadFreeBSD::RemoveWatchpoint(lldb::addr_t addr) {
  auto wp = m_watchpoint_index_map.find(addr);
  if (wp == m_watchpoint_index_map.end())
    return Status();
  uint32_t wp_index = wp->second;
  m_watchpoint_index_map.erase(wp);
  if (GetRegisterContext().ClearHardwareWatchpoint(wp_index))
    return Status();
  return Status("Clearing hardware watchpoint failed.");
}

Status NativeThreadFreeBSD::SetHardwareBreakpoint(lldb::addr_t addr,
                                                  size_t size) {
  assert(m_state == eStateStopped);
  Status error = RemoveHardwareBreakpoint(addr);
  if (error.Fail())
    return error;

  uint32_t bp_index = GetRegisterContext().SetHardwareBreakpoint(addr, size);

  if (bp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware breakpoint failed.");

  m_hw_break_index_map.insert({addr, bp_index});
  return Status();
}

Status NativeThreadFreeBSD::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  auto bp = m_hw_break_index_map.find(addr);
  if (bp == m_hw_break_index_map.end())
    return Status();

  uint32_t bp_index = bp->second;
  if (GetRegisterContext().ClearHardwareBreakpoint(bp_index)) {
    m_hw_break_index_map.erase(bp);
    return Status();
  }

  return Status("Clearing hardware breakpoint failed.");
}

llvm::Error
NativeThreadFreeBSD::CopyWatchpointsFrom(NativeThreadFreeBSD &source) {
  llvm::Error s = GetRegisterContext().CopyHardwareWatchpointsFrom(
      source.GetRegisterContext());
  if (!s) {
    m_watchpoint_index_map = source.m_watchpoint_index_map;
    m_hw_break_index_map = source.m_hw_break_index_map;
  }
  return s;
}

NativeProcessFreeBSD &NativeThreadFreeBSD::GetProcess() {
  return static_cast<NativeProcessFreeBSD &>(m_process);
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
NativeThreadFreeBSD::GetSiginfo() const {
  Log *log = GetLog(POSIXLog::Process);

  struct ptrace_lwpinfo info;
  const auto siginfo_err = NativeProcessFreeBSD::PtraceWrapper(
      PT_LWPINFO, GetID(), &info, sizeof(info));
  if (siginfo_err.Fail()) {
    LLDB_LOG(log, "PT_LWPINFO failed {0}", siginfo_err);
    return siginfo_err.ToError();
  }

  if (info.pl_event != PL_EVENT_SIGNAL)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Thread not signaled");
  if (!(info.pl_flags & PL_FLAG_SI))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "No siginfo for thread");

  return llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(&info.pl_siginfo),
                      sizeof(info.pl_siginfo)));
}
