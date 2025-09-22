//===-- NativeThreadLinux.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeThreadLinux.h"

#include <csignal>
#include <sstream>

#include "NativeProcessLinux.h"
#include "NativeRegisterContextLinux.h"
#include "SingleStepCheck.h"

#include "lldb/Host/HostNativeThread.h"
#include "lldb/Host/linux/Ptrace.h"
#include "lldb/Host/linux/Support.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/SmallString.h"

#include "Plugins/Process/POSIX/CrashReason.h"
#include "Plugins/Process/Utility/MemoryTagManagerAArch64MTE.h"

#include <sys/syscall.h>
// Try to define a macro to encapsulate the tgkill syscall
#define tgkill(pid, tid, sig)                                                  \
  syscall(__NR_tgkill, static_cast<::pid_t>(pid), static_cast<::pid_t>(tid),   \
          sig)

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_linux;

namespace {
void LogThreadStopInfo(Log &log, const ThreadStopInfo &stop_info,
                       const char *const header) {
  switch (stop_info.reason) {
  case eStopReasonNone:
    log.Printf("%s: %s no stop reason", __FUNCTION__, header);
    return;
  case eStopReasonTrace:
    log.Printf("%s: %s trace, stopping signal 0x%" PRIx32, __FUNCTION__, header,
               stop_info.signo);
    return;
  case eStopReasonBreakpoint:
    log.Printf("%s: %s breakpoint, stopping signal 0x%" PRIx32, __FUNCTION__,
               header, stop_info.signo);
    return;
  case eStopReasonWatchpoint:
    log.Printf("%s: %s watchpoint, stopping signal 0x%" PRIx32, __FUNCTION__,
               header, stop_info.signo);
    return;
  case eStopReasonSignal:
    log.Printf("%s: %s signal 0x%02" PRIx32, __FUNCTION__, header,
               stop_info.signo);
    return;
  case eStopReasonException:
    log.Printf("%s: %s exception type 0x%02" PRIx64, __FUNCTION__, header,
               stop_info.details.exception.type);
    return;
  case eStopReasonExec:
    log.Printf("%s: %s exec, stopping signal 0x%" PRIx32, __FUNCTION__, header,
               stop_info.signo);
    return;
  case eStopReasonPlanComplete:
    log.Printf("%s: %s plan complete", __FUNCTION__, header);
    return;
  case eStopReasonThreadExiting:
    log.Printf("%s: %s thread exiting", __FUNCTION__, header);
    return;
  case eStopReasonInstrumentation:
    log.Printf("%s: %s instrumentation", __FUNCTION__, header);
    return;
  case eStopReasonProcessorTrace:
    log.Printf("%s: %s processor trace", __FUNCTION__, header);
    return;
  default:
    log.Printf("%s: %s invalid stop reason %" PRIu32, __FUNCTION__, header,
               static_cast<uint32_t>(stop_info.reason));
  }
}
}

NativeThreadLinux::NativeThreadLinux(NativeProcessLinux &process,
                                     lldb::tid_t tid)
    : NativeThreadProtocol(process, tid), m_state(StateType::eStateInvalid),
      m_stop_info(),
      m_reg_context_up(
          NativeRegisterContextLinux::CreateHostNativeRegisterContextLinux(
              process.GetArchitecture(), *this)),
      m_stop_description() {}

std::string NativeThreadLinux::GetName() {
  NativeProcessLinux &process = GetProcess();

  auto BufferOrError = getProcFile(process.GetID(), GetID(), "comm");
  if (!BufferOrError)
    return "";
  return std::string(BufferOrError.get()->getBuffer().rtrim('\n'));
}

lldb::StateType NativeThreadLinux::GetState() { return m_state; }

bool NativeThreadLinux::GetStopReason(ThreadStopInfo &stop_info,
                                      std::string &description) {
  Log *log = GetLog(LLDBLog::Thread);

  description.clear();

  switch (m_state) {
  case eStateStopped:
  case eStateCrashed:
  case eStateExited:
  case eStateSuspended:
  case eStateUnloaded:
    if (log)
      LogThreadStopInfo(*log, m_stop_info, "m_stop_info in thread:");
    stop_info = m_stop_info;
    description = m_stop_description;
    if (log)
      LogThreadStopInfo(*log, stop_info, "returned stop_info:");

    return true;

  case eStateInvalid:
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
  case eStateDetached:
    if (log) {
      LLDB_LOGF(log,
                "NativeThreadLinux::%s tid %" PRIu64
                " in state %s cannot answer stop reason",
                __FUNCTION__, GetID(), StateAsCString(m_state));
    }
    return false;
  }
  llvm_unreachable("unhandled StateType!");
}

Status NativeThreadLinux::SetWatchpoint(lldb::addr_t addr, size_t size,
                                        uint32_t watch_flags, bool hardware) {
  if (!hardware)
    return Status("not implemented");
  if (m_state == eStateLaunching)
    return Status();
  Status error = RemoveWatchpoint(addr);
  if (error.Fail())
    return error;
  uint32_t wp_index =
      m_reg_context_up->SetHardwareWatchpoint(addr, size, watch_flags);
  if (wp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware watchpoint failed.");
  m_watchpoint_index_map.insert({addr, wp_index});
  return Status();
}

Status NativeThreadLinux::RemoveWatchpoint(lldb::addr_t addr) {
  auto wp = m_watchpoint_index_map.find(addr);
  if (wp == m_watchpoint_index_map.end())
    return Status();
  uint32_t wp_index = wp->second;
  m_watchpoint_index_map.erase(wp);
  if (m_reg_context_up->ClearHardwareWatchpoint(wp_index))
    return Status();
  return Status("Clearing hardware watchpoint failed.");
}

Status NativeThreadLinux::SetHardwareBreakpoint(lldb::addr_t addr,
                                                size_t size) {
  if (m_state == eStateLaunching)
    return Status();

  Status error = RemoveHardwareBreakpoint(addr);
  if (error.Fail())
    return error;

  uint32_t bp_index = m_reg_context_up->SetHardwareBreakpoint(addr, size);

  if (bp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware breakpoint failed.");

  m_hw_break_index_map.insert({addr, bp_index});
  return Status();
}

Status NativeThreadLinux::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  auto bp = m_hw_break_index_map.find(addr);
  if (bp == m_hw_break_index_map.end())
    return Status();

  uint32_t bp_index = bp->second;
  if (m_reg_context_up->ClearHardwareBreakpoint(bp_index)) {
    m_hw_break_index_map.erase(bp);
    return Status();
  }

  return Status("Clearing hardware breakpoint failed.");
}

Status NativeThreadLinux::Resume(uint32_t signo) {
  const StateType new_state = StateType::eStateRunning;
  MaybeLogStateChange(new_state);
  m_state = new_state;

  m_stop_info.reason = StopReason::eStopReasonNone;
  m_stop_description.clear();

  // If watchpoints have been set, but none on this thread, then this is a new
  // thread. So set all existing watchpoints.
  if (m_watchpoint_index_map.empty()) {
    NativeProcessLinux &process = GetProcess();

    const auto &watchpoint_map = process.GetWatchpointMap();
    m_reg_context_up->ClearAllHardwareWatchpoints();
    for (const auto &pair : watchpoint_map) {
      const auto &wp = pair.second;
      SetWatchpoint(wp.m_addr, wp.m_size, wp.m_watch_flags, wp.m_hardware);
    }
  }

  // Set all active hardware breakpoint on all threads.
  if (m_hw_break_index_map.empty()) {
    NativeProcessLinux &process = GetProcess();

    const auto &hw_breakpoint_map = process.GetHardwareBreakpointMap();
    m_reg_context_up->ClearAllHardwareBreakpoints();
    for (const auto &pair : hw_breakpoint_map) {
      const auto &bp = pair.second;
      SetHardwareBreakpoint(bp.m_addr, bp.m_size);
    }
  }

  intptr_t data = 0;

  if (signo != LLDB_INVALID_SIGNAL_NUMBER)
    data = signo;

  return NativeProcessLinux::PtraceWrapper(PTRACE_CONT, GetID(), nullptr,
                                           reinterpret_cast<void *>(data));
}

Status NativeThreadLinux::SingleStep(uint32_t signo) {
  const StateType new_state = StateType::eStateStepping;
  MaybeLogStateChange(new_state);
  m_state = new_state;
  m_stop_info.reason = StopReason::eStopReasonNone;

  if(!m_step_workaround) {
    // If we already hava a workaround inplace, don't reset it. Otherwise, the
    // destructor of the existing instance will run after the new instance has
    // fetched the cpu mask, and the thread will end up with the wrong mask.
    m_step_workaround = SingleStepWorkaround::Get(m_tid);
  }

  intptr_t data = 0;
  if (signo != LLDB_INVALID_SIGNAL_NUMBER)
    data = signo;

  // If hardware single-stepping is not supported, we just do a continue. The
  // breakpoint on the next instruction has been setup in
  // NativeProcessLinux::Resume.
  return NativeProcessLinux::PtraceWrapper(
      GetProcess().SupportHardwareSingleStepping() ? PTRACE_SINGLESTEP
                                                   : PTRACE_CONT,
      m_tid, nullptr, reinterpret_cast<void *>(data));
}

void NativeThreadLinux::SetStoppedBySignal(uint32_t signo,
                                           const siginfo_t *info) {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "NativeThreadLinux::%s called with signal 0x%02" PRIx32,
            __FUNCTION__, signo);

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
#ifndef SEGV_MTESERR
#define SEGV_MTESERR 9
#endif
      if (info->si_signo == SIGSEGV && info->si_code == SEGV_MTESERR)
        AnnotateSyncTagCheckFault(
            reinterpret_cast<lldb::addr_t>(info->si_addr));
      break;
    }
  }
}

void NativeThreadLinux::AnnotateSyncTagCheckFault(lldb::addr_t fault_addr) {
  int32_t allocation_tag_type = 0;
  switch (GetProcess().GetArchitecture().GetMachine()) {
  // aarch64_32 deliberately not here because there's no 32 bit MTE
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    allocation_tag_type = MemoryTagManagerAArch64MTE::eMTE_allocation;
    break;
  default:
    return;
  }

  auto details =
      GetRegisterContext().GetMemoryTaggingDetails(allocation_tag_type);
  if (!details) {
    llvm::consumeError(details.takeError());
    return;
  }

  // We assume that the stop description is currently:
  // signal SIGSEGV: sync tag check fault (fault address: <addr>)
  // Remove the closing )
  m_stop_description.pop_back();

  std::stringstream ss;
  std::unique_ptr<MemoryTagManager> manager(std::move(details->manager));

  ss << " logical tag: 0x" << std::hex << manager->GetLogicalTag(fault_addr);

  std::vector<uint8_t> allocation_tag_data;
  // The fault address may not be granule aligned. ReadMemoryTags will granule
  // align any range you give it, potentially making it larger.
  // To prevent this set len to 1. This always results in a range that is at
  // most 1 granule in size and includes fault_addr.
  Status status = GetProcess().ReadMemoryTags(allocation_tag_type, fault_addr,
                                              1, allocation_tag_data);

  if (status.Success()) {
    llvm::Expected<std::vector<lldb::addr_t>> allocation_tag =
        manager->UnpackTagsData(allocation_tag_data, 1);
    if (allocation_tag) {
      ss << " allocation tag: 0x" << std::hex << allocation_tag->front() << ")";
    } else {
      llvm::consumeError(allocation_tag.takeError());
      ss << ")";
    }
  } else
    ss << ")";

  m_stop_description += ss.str();
}

bool NativeThreadLinux::IsStopped(int *signo) {
  if (!StateIsStoppedState(m_state, false))
    return false;

  // If we are stopped by a signal, return the signo.
  if (signo && m_state == StateType::eStateStopped &&
      m_stop_info.reason == StopReason::eStopReasonSignal) {
    *signo = m_stop_info.signo;
  }

  // Regardless, we are stopped.
  return true;
}

void NativeThreadLinux::SetStopped() {
  if (m_state == StateType::eStateStepping)
    m_step_workaround.reset();

  // On every stop, clear any cached register data structures
  GetRegisterContext().InvalidateAllRegisters();

  const StateType new_state = StateType::eStateStopped;
  MaybeLogStateChange(new_state);
  m_state = new_state;
  m_stop_description.clear();
}

void NativeThreadLinux::SetStoppedByExec() {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "NativeThreadLinux::%s()", __FUNCTION__);

  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonExec;
  m_stop_info.signo = SIGSTOP;
}

void NativeThreadLinux::SetStoppedByBreakpoint() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonBreakpoint;
  m_stop_info.signo = SIGTRAP;
  m_stop_description.clear();
}

void NativeThreadLinux::SetStoppedByWatchpoint(uint32_t wp_index) {
  SetStopped();

  lldbassert(wp_index != LLDB_INVALID_INDEX32 && "wp_index cannot be invalid");

  std::ostringstream ostr;
  ostr << m_reg_context_up->GetWatchpointAddress(wp_index) << " ";
  ostr << wp_index;

  /*
   * MIPS: Last 3bits of the watchpoint address are masked by the kernel. For
   * example:
   * 'n' is at 0x120010d00 and 'm' is 0x120010d04. When a watchpoint is set at
   * 'm', then
   * watch exception is generated even when 'n' is read/written. To handle this
   * case,
   * find the base address of the load/store instruction and append it in the
   * stop-info
   * packet.
  */
  ostr << " " << m_reg_context_up->GetWatchpointHitAddress(wp_index);

  m_stop_description = ostr.str();

  m_stop_info.reason = StopReason::eStopReasonWatchpoint;
  m_stop_info.signo = SIGTRAP;
}

bool NativeThreadLinux::IsStoppedAtBreakpoint() {
  return GetState() == StateType::eStateStopped &&
         m_stop_info.reason == StopReason::eStopReasonBreakpoint;
}

bool NativeThreadLinux::IsStoppedAtWatchpoint() {
  return GetState() == StateType::eStateStopped &&
         m_stop_info.reason == StopReason::eStopReasonWatchpoint;
}

void NativeThreadLinux::SetStoppedByTrace() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonTrace;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadLinux::SetStoppedByFork(bool is_vfork, lldb::pid_t child_pid) {
  SetStopped();

  m_stop_info.reason =
      is_vfork ? StopReason::eStopReasonVFork : StopReason::eStopReasonFork;
  m_stop_info.signo = SIGTRAP;
  m_stop_info.details.fork.child_pid = child_pid;
  m_stop_info.details.fork.child_tid = child_pid;
  m_stop_description = std::to_string(child_pid);
  m_stop_description += " ";
  m_stop_description += std::to_string(child_pid);
}

void NativeThreadLinux::SetStoppedByVForkDone() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonVForkDone;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadLinux::SetStoppedWithNoReason() {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonNone;
  m_stop_info.signo = 0;
}

void NativeThreadLinux::SetStoppedByProcessorTrace(
    llvm::StringRef description) {
  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonProcessorTrace;
  m_stop_info.signo = 0;
  m_stop_description = description.str();
}

void NativeThreadLinux::SetExited() {
  const StateType new_state = StateType::eStateExited;
  MaybeLogStateChange(new_state);
  m_state = new_state;

  m_stop_info.reason = StopReason::eStopReasonThreadExiting;
}

Status NativeThreadLinux::RequestStop() {
  Log *log = GetLog(LLDBLog::Thread);

  NativeProcessLinux &process = GetProcess();

  lldb::pid_t pid = process.GetID();
  lldb::tid_t tid = GetID();

  LLDB_LOGF(log,
            "NativeThreadLinux::%s requesting thread stop(pid: %" PRIu64
            ", tid: %" PRIu64 ")",
            __FUNCTION__, pid, tid);

  Status err;
  errno = 0;
  if (::tgkill(pid, tid, SIGSTOP) != 0) {
    err.SetErrorToErrno();
    LLDB_LOGF(log,
              "NativeThreadLinux::%s tgkill(%" PRIu64 ", %" PRIu64
              ", SIGSTOP) failed: %s",
              __FUNCTION__, pid, tid, err.AsCString());
  }

  return err;
}

void NativeThreadLinux::MaybeLogStateChange(lldb::StateType new_state) {
  Log *log = GetLog(LLDBLog::Thread);
  // If we're not logging, we're done.
  if (!log)
    return;

  // If this is a state change to the same state, we're done.
  lldb::StateType old_state = m_state;
  if (new_state == old_state)
    return;

  LLDB_LOG(log, "pid={0}, tid={1}: changing from state {2} to {3}",
           m_process.GetID(), GetID(), old_state, new_state);
}

NativeProcessLinux &NativeThreadLinux::GetProcess() {
  return static_cast<NativeProcessLinux &>(m_process);
}

const NativeProcessLinux &NativeThreadLinux::GetProcess() const {
  return static_cast<const NativeProcessLinux &>(m_process);
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
NativeThreadLinux::GetSiginfo() const {
  auto siginfo_buf =
      llvm::WritableMemoryBuffer::getNewUninitMemBuffer(sizeof(siginfo_t));
  Status error =
      GetProcess().GetSignalInfo(GetID(), siginfo_buf->getBufferStart());
  if (!error.Success())
    return error.ToError();
  return std::move(siginfo_buf);
}
