//===-- SBExecutionContext.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBExecutionContext.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBFrame.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"

#include "lldb/Target/ExecutionContext.h"

using namespace lldb;
using namespace lldb_private;

SBExecutionContext::SBExecutionContext() { LLDB_INSTRUMENT_VA(this); }

SBExecutionContext::SBExecutionContext(const lldb::SBExecutionContext &rhs)
    : m_exe_ctx_sp(rhs.m_exe_ctx_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBExecutionContext::SBExecutionContext(
    lldb::ExecutionContextRefSP exe_ctx_ref_sp)
    : m_exe_ctx_sp(exe_ctx_ref_sp) {
  LLDB_INSTRUMENT_VA(this, exe_ctx_ref_sp);
}

SBExecutionContext::SBExecutionContext(const lldb::SBTarget &target)
    : m_exe_ctx_sp(new ExecutionContextRef()) {
  LLDB_INSTRUMENT_VA(this, target);

  m_exe_ctx_sp->SetTargetSP(target.GetSP());
}

SBExecutionContext::SBExecutionContext(const lldb::SBProcess &process)
    : m_exe_ctx_sp(new ExecutionContextRef()) {
  LLDB_INSTRUMENT_VA(this, process);

  m_exe_ctx_sp->SetProcessSP(process.GetSP());
}

SBExecutionContext::SBExecutionContext(lldb::SBThread thread)
    : m_exe_ctx_sp(new ExecutionContextRef()) {
  LLDB_INSTRUMENT_VA(this, thread);

  m_exe_ctx_sp->SetThreadPtr(thread.get());
}

SBExecutionContext::SBExecutionContext(const lldb::SBFrame &frame)
    : m_exe_ctx_sp(new ExecutionContextRef()) {
  LLDB_INSTRUMENT_VA(this, frame);

  m_exe_ctx_sp->SetFrameSP(frame.GetFrameSP());
}

SBExecutionContext::~SBExecutionContext() = default;

const SBExecutionContext &SBExecutionContext::
operator=(const lldb::SBExecutionContext &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_exe_ctx_sp = rhs.m_exe_ctx_sp;
  return *this;
}

ExecutionContextRef *SBExecutionContext::get() const {
  return m_exe_ctx_sp.get();
}

SBTarget SBExecutionContext::GetTarget() const {
  LLDB_INSTRUMENT_VA(this);

  SBTarget sb_target;
  if (m_exe_ctx_sp) {
    TargetSP target_sp(m_exe_ctx_sp->GetTargetSP());
    if (target_sp)
      sb_target.SetSP(target_sp);
  }
  return sb_target;
}

SBProcess SBExecutionContext::GetProcess() const {
  LLDB_INSTRUMENT_VA(this);

  SBProcess sb_process;
  if (m_exe_ctx_sp) {
    ProcessSP process_sp(m_exe_ctx_sp->GetProcessSP());
    if (process_sp)
      sb_process.SetSP(process_sp);
  }
  return sb_process;
}

SBThread SBExecutionContext::GetThread() const {
  LLDB_INSTRUMENT_VA(this);

  SBThread sb_thread;
  if (m_exe_ctx_sp) {
    ThreadSP thread_sp(m_exe_ctx_sp->GetThreadSP());
    if (thread_sp)
      sb_thread.SetThread(thread_sp);
  }
  return sb_thread;
}

SBFrame SBExecutionContext::GetFrame() const {
  LLDB_INSTRUMENT_VA(this);

  SBFrame sb_frame;
  if (m_exe_ctx_sp) {
    StackFrameSP frame_sp(m_exe_ctx_sp->GetFrameSP());
    if (frame_sp)
      sb_frame.SetFrameSP(frame_sp);
  }
  return sb_frame;
}
