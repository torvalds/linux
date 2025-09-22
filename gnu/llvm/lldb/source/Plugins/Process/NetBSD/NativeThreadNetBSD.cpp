//===-- NativeThreadNetBSD.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeThreadNetBSD.h"
#include "NativeRegisterContextNetBSD.h"

#include "NativeProcessNetBSD.h"

#include "Plugins/Process/POSIX/CrashReason.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/State.h"
#include "llvm/Support/Errno.h"

// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
// clang-format on

#include <sstream>

// clang-format off
#include <sys/types.h>
#include <sys/sysctl.h>
// clang-format on

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_netbsd;

NativeThreadNetBSD::NativeThreadNetBSD(NativeProcessNetBSD &process,
                                       lldb::tid_t tid)
    : NativeThreadProtocol(process, tid), m_state(StateType::eStateInvalid),
      m_stop_info(), m_reg_context_up(
NativeRegisterContextNetBSD::CreateHostNativeRegisterContextNetBSD(process.GetArchitecture(), *this)
), m_stop_description() {}

Status NativeThreadNetBSD::Resume() {
  Status ret = NativeProcessNetBSD::PtraceWrapper(PT_RESUME, m_process.GetID(),
                                                  nullptr, GetID());
  if (!ret.Success())
    return ret;
  ret = NativeProcessNetBSD::PtraceWrapper(PT_CLEARSTEP, m_process.GetID(),
                                           nullptr, GetID());
  if (ret.Success())
    SetRunning();
  return ret;
}

Status NativeThreadNetBSD::SingleStep() {
  Status ret = NativeProcessNetBSD::PtraceWrapper(PT_RESUME, m_process.GetID(),
                                                  nullptr, GetID());
  if (!ret.Success())
    return ret;
  ret = NativeProcessNetBSD::PtraceWrapper(PT_SETSTEP, m_process.GetID(),
                                           nullptr, GetID());
  if (ret.Success())
    SetStepping();
  return ret;
}

Status NativeThreadNetBSD::Suspend() {
  Status ret = NativeProcessNetBSD::PtraceWrapper(PT_SUSPEND, m_process.GetID(),
                                                  nullptr, GetID());
  if (ret.Success())
    SetStopped();
  return ret;
}

void NativeThreadNetBSD::SetStoppedBySignal(uint32_t signo,
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

void NativeThreadNetBSD::SetStoppedByBreakpoint() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonBreakpoint;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedByTrace() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonTrace;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedByExec() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonExec;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedByWatchpoint(uint32_t wp_index) {
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

void NativeThreadNetBSD::SetStoppedByFork(lldb::pid_t child_pid,
                                           lldb::tid_t child_tid) {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonFork;
  m_stop_info.signo = SIGTRAP;
  m_stop_info.details.fork.child_pid = child_pid;
  m_stop_info.details.fork.child_tid = child_tid;
}

void NativeThreadNetBSD::SetStoppedByVFork(lldb::pid_t child_pid,
                                            lldb::tid_t child_tid) {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonVFork;
  m_stop_info.signo = SIGTRAP;
  m_stop_info.details.fork.child_pid = child_pid;
  m_stop_info.details.fork.child_tid = child_tid;
}

void NativeThreadNetBSD::SetStoppedByVForkDone() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonVForkDone;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedWithNoReason() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonNone;
  m_stop_info.signo = 0;
}

void NativeThreadNetBSD::SetStopped() {
  const StateType new_state = StateType::eStateStopped;
  m_state = new_state;
  m_stop_description.clear();
}

void NativeThreadNetBSD::SetRunning() {
  m_state = StateType::eStateRunning;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

void NativeThreadNetBSD::SetStepping() {
  m_state = StateType::eStateStepping;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

std::string NativeThreadNetBSD::GetName() {
#ifdef PT_LWPSTATUS
  struct ptrace_lwpstatus info = {};
  info.pl_lwpid = m_tid;
  Status error = NativeProcessNetBSD::PtraceWrapper(
      PT_LWPSTATUS, static_cast<int>(m_process.GetID()), &info, sizeof(info));
  if (error.Fail()) {
    return "";
  }
  return info.pl_name;
#else
  std::vector<struct kinfo_lwp> infos;
  Log *log = GetLog(POSIXLog::Thread);

  int mib[5] = {CTL_KERN, KERN_LWP, static_cast<int>(m_process.GetID()),
                sizeof(struct kinfo_lwp), 0};
  size_t size;

  if (::sysctl(mib, 5, nullptr, &size, nullptr, 0) == -1 || size == 0) {
    LLDB_LOG(log, "sysctl() for LWP info size failed: {0}",
             llvm::sys::StrError());
    return "";
  }

  mib[4] = size / sizeof(size_t);
  infos.resize(size / sizeof(struct kinfo_lwp));

  if (sysctl(mib, 5, infos.data(), &size, NULL, 0) == -1 || size == 0) {
    LLDB_LOG(log, "sysctl() for LWP info failed: {0}", llvm::sys::StrError());
    return "";
  }

  size_t nlwps = size / sizeof(struct kinfo_lwp);
  for (size_t i = 0; i < nlwps; i++) {
    if (static_cast<lldb::tid_t>(infos[i].l_lid) == m_tid) {
      return infos[i].l_name;
    }
  }

  LLDB_LOG(log, "unable to find lwp {0} in LWP infos", m_tid);
  return "";
#endif
}

lldb::StateType NativeThreadNetBSD::GetState() { return m_state; }

bool NativeThreadNetBSD::GetStopReason(ThreadStopInfo &stop_info,
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

NativeRegisterContextNetBSD &NativeThreadNetBSD::GetRegisterContext() {
  assert(m_reg_context_up);
  return *m_reg_context_up;
}

Status NativeThreadNetBSD::SetWatchpoint(lldb::addr_t addr, size_t size,
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

Status NativeThreadNetBSD::RemoveWatchpoint(lldb::addr_t addr) {
  auto wp = m_watchpoint_index_map.find(addr);
  if (wp == m_watchpoint_index_map.end())
    return Status();
  uint32_t wp_index = wp->second;
  m_watchpoint_index_map.erase(wp);
  if (GetRegisterContext().ClearHardwareWatchpoint(wp_index))
    return Status();
  return Status("Clearing hardware watchpoint failed.");
}

Status NativeThreadNetBSD::SetHardwareBreakpoint(lldb::addr_t addr,
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

Status NativeThreadNetBSD::RemoveHardwareBreakpoint(lldb::addr_t addr) {
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
NativeThreadNetBSD::CopyWatchpointsFrom(NativeThreadNetBSD &source) {
  llvm::Error s = GetRegisterContext().CopyHardwareWatchpointsFrom(
      source.GetRegisterContext());
  if (!s) {
    m_watchpoint_index_map = source.m_watchpoint_index_map;
    m_hw_break_index_map = source.m_hw_break_index_map;
  }
  return s;
}
