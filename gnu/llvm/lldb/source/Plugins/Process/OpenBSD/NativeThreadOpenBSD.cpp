//===-- NativeThreadOpenBSD.cpp -------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeThreadOpenBSD.h"
#include "NativeRegisterContextOpenBSD.h"

#include "NativeProcessOpenBSD.h"

#include "Plugins/Process/POSIX/CrashReason.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/State.h"

#include <sstream>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_openbsd;

NativeThreadOpenBSD::NativeThreadOpenBSD(NativeProcessOpenBSD &process,
                                       lldb::tid_t tid)
    : NativeThreadProtocol(process, tid), m_state(StateType::eStateInvalid),
      m_stop_info(), m_stop_description() {
  m_reg_context_up = NativeRegisterContextOpenBSD::CreateHostNativeRegisterContextOpenBSD(process.GetArchitecture(), *this);
  if (!m_reg_context_up)
    llvm_unreachable("This architecture does not support debugging running processes.");
}

void NativeThreadOpenBSD::SetStoppedBySignal(uint32_t signo,
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
//XXX      const auto reason = GetCrashReason(*info);
//      m_stop_description = GetCrashReasonString(reason, *info);
      break;
    }
  }
}

void NativeThreadOpenBSD::SetStoppedByBreakpoint() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonBreakpoint;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadOpenBSD::SetStoppedByTrace() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonTrace;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadOpenBSD::SetStoppedByExec() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonExec;
  m_stop_info.signo = SIGTRAP;
}

void NativeThreadOpenBSD::SetStopped() {
  const StateType new_state = StateType::eStateStopped;
  m_state = new_state;
  m_stop_description.clear();
}

void NativeThreadOpenBSD::SetRunning() {
  m_state = StateType::eStateRunning;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

void NativeThreadOpenBSD::SetStepping() {
  m_state = StateType::eStateStepping;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

std::string NativeThreadOpenBSD::GetName() { return std::string(""); }

lldb::StateType NativeThreadOpenBSD::GetState() { return m_state; }

bool NativeThreadOpenBSD::GetStopReason(ThreadStopInfo &stop_info,
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

NativeRegisterContext& NativeThreadOpenBSD::GetRegisterContext() {
  assert(m_reg_context_up);
return  *m_reg_context_up;
}

Status NativeThreadOpenBSD::SetWatchpoint(lldb::addr_t addr, size_t size,
                                         uint32_t watch_flags, bool hardware) {
  if (hardware)
    return Status("Not Aailable");
  return Status("Software watchpoints not implemented");
}

Status NativeThreadOpenBSD::RemoveWatchpoint(lldb::addr_t addr) {
  return Status("Software watchpoints not implemented");
}

Status NativeThreadOpenBSD::SetHardwareBreakpoint(lldb::addr_t addr,
                                                 size_t size) {
  return Status("Not Available");
}

Status NativeThreadOpenBSD::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  return Status("Not Available");
}
