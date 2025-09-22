//===-- SBThreadPlan.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBThread.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Queue.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanPython.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepRange.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StructuredData.h"

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBThreadPlan.h"
#include "lldb/API/SBValue.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

// Constructors
SBThreadPlan::SBThreadPlan() { LLDB_INSTRUMENT_VA(this); }

SBThreadPlan::SBThreadPlan(const ThreadPlanSP &lldb_object_sp)
    : m_opaque_wp(lldb_object_sp) {
  LLDB_INSTRUMENT_VA(this, lldb_object_sp);
}

SBThreadPlan::SBThreadPlan(const SBThreadPlan &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBThreadPlan::SBThreadPlan(lldb::SBThread &sb_thread, const char *class_name) {
  LLDB_INSTRUMENT_VA(this, sb_thread, class_name);

  Thread *thread = sb_thread.get();
  if (thread)
    m_opaque_wp = std::make_shared<ThreadPlanPython>(*thread, class_name,
                                                     StructuredDataImpl());
}

SBThreadPlan::SBThreadPlan(lldb::SBThread &sb_thread, const char *class_name,
                           lldb::SBStructuredData &args_data) {
  LLDB_INSTRUMENT_VA(this, sb_thread, class_name, args_data);

  Thread *thread = sb_thread.get();
  if (thread)
    m_opaque_wp = std::make_shared<ThreadPlanPython>(*thread, class_name,
                                                     *args_data.m_impl_up);
}

// Assignment operator

const lldb::SBThreadPlan &SBThreadPlan::operator=(const SBThreadPlan &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}
// Destructor
SBThreadPlan::~SBThreadPlan() = default;

bool SBThreadPlan::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBThreadPlan::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return static_cast<bool>(GetSP());
}

void SBThreadPlan::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_wp.reset();
}

lldb::StopReason SBThreadPlan::GetStopReason() {
  LLDB_INSTRUMENT_VA(this);

  return eStopReasonNone;
}

size_t SBThreadPlan::GetStopReasonDataCount() {
  LLDB_INSTRUMENT_VA(this);

  return 0;
}

uint64_t SBThreadPlan::GetStopReasonDataAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  return 0;
}

SBThread SBThreadPlan::GetThread() const {
  LLDB_INSTRUMENT_VA(this);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    return SBThread(thread_plan_sp->GetThread().shared_from_this());
  } else
    return SBThread();
}

bool SBThreadPlan::GetDescription(lldb::SBStream &description) const {
  LLDB_INSTRUMENT_VA(this, description);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    thread_plan_sp->GetDescription(description.get(), eDescriptionLevelFull);
  } else {
    description.Printf("Empty SBThreadPlan");
  }
  return true;
}

void SBThreadPlan::SetThreadPlan(const ThreadPlanSP &lldb_object_wp) {
  m_opaque_wp = lldb_object_wp;
}

void SBThreadPlan::SetPlanComplete(bool success) {
  LLDB_INSTRUMENT_VA(this, success);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp)
    thread_plan_sp->SetPlanComplete(success);
}

bool SBThreadPlan::IsPlanComplete() {
  LLDB_INSTRUMENT_VA(this);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp)
    return thread_plan_sp->IsPlanComplete();
  return true;
}

bool SBThreadPlan::IsPlanStale() {
  LLDB_INSTRUMENT_VA(this);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp)
    return thread_plan_sp->IsPlanStale();
  return true;
}

bool SBThreadPlan::IsValid() {
  LLDB_INSTRUMENT_VA(this);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp)
    return thread_plan_sp->ValidatePlan(nullptr);
  return false;
}

bool SBThreadPlan::GetStopOthers() {
  LLDB_INSTRUMENT_VA(this);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp)
    return thread_plan_sp->StopOthers();
  return false;
}

void SBThreadPlan::SetStopOthers(bool stop_others) {
  LLDB_INSTRUMENT_VA(this, stop_others);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp)
    thread_plan_sp->SetStopOthers(stop_others);
}

// This section allows an SBThreadPlan to push another of the common types of
// plans...
//
// FIXME, you should only be able to queue thread plans from inside the methods
// of a Scripted Thread Plan.  Need a way to enforce that.

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepOverRange(SBAddress &sb_start_address,
                                              lldb::addr_t size) {
  LLDB_INSTRUMENT_VA(this, sb_start_address, size);

  SBError error;
  return QueueThreadPlanForStepOverRange(sb_start_address, size, error);
}

SBThreadPlan SBThreadPlan::QueueThreadPlanForStepOverRange(
    SBAddress &sb_start_address, lldb::addr_t size, SBError &error) {
  LLDB_INSTRUMENT_VA(this, sb_start_address, size, error);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    Address *start_address = sb_start_address.get();
    if (!start_address) {
      return SBThreadPlan();
    }

    AddressRange range(*start_address, size);
    SymbolContext sc;
    start_address->CalculateSymbolContext(&sc);
    Status plan_status;

    SBThreadPlan plan = SBThreadPlan(
        thread_plan_sp->GetThread().QueueThreadPlanForStepOverRange(
            false, range, sc, eAllThreads, plan_status));

    if (plan_status.Fail())
      error.SetErrorString(plan_status.AsCString());
    else
      plan.GetSP()->SetPrivate(true);

    return plan;
  }
  return SBThreadPlan();
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepInRange(SBAddress &sb_start_address,
                                            lldb::addr_t size) {
  LLDB_INSTRUMENT_VA(this, sb_start_address, size);

  SBError error;
  return QueueThreadPlanForStepInRange(sb_start_address, size, error);
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepInRange(SBAddress &sb_start_address,
                                            lldb::addr_t size, SBError &error) {
  LLDB_INSTRUMENT_VA(this, sb_start_address, size, error);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    Address *start_address = sb_start_address.get();
    if (!start_address) {
      return SBThreadPlan();
    }

    AddressRange range(*start_address, size);
    SymbolContext sc;
    start_address->CalculateSymbolContext(&sc);

    Status plan_status;
    SBThreadPlan plan =
        SBThreadPlan(thread_plan_sp->GetThread().QueueThreadPlanForStepInRange(
            false, range, sc, nullptr, eAllThreads, plan_status));

    if (plan_status.Fail())
      error.SetErrorString(plan_status.AsCString());
    else
      plan.GetSP()->SetPrivate(true);

    return plan;
  }
  return SBThreadPlan();
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepOut(uint32_t frame_idx_to_step_to,
                                        bool first_insn) {
  LLDB_INSTRUMENT_VA(this, frame_idx_to_step_to, first_insn);

  SBError error;
  return QueueThreadPlanForStepOut(frame_idx_to_step_to, first_insn, error);
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepOut(uint32_t frame_idx_to_step_to,
                                        bool first_insn, SBError &error) {
  LLDB_INSTRUMENT_VA(this, frame_idx_to_step_to, first_insn, error);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    SymbolContext sc;
    sc = thread_plan_sp->GetThread().GetStackFrameAtIndex(0)->GetSymbolContext(
        lldb::eSymbolContextEverything);

    Status plan_status;
    SBThreadPlan plan =
        SBThreadPlan(thread_plan_sp->GetThread().QueueThreadPlanForStepOut(
            false, &sc, first_insn, false, eVoteYes, eVoteNoOpinion,
            frame_idx_to_step_to, plan_status));

    if (plan_status.Fail())
      error.SetErrorString(plan_status.AsCString());
    else
      plan.GetSP()->SetPrivate(true);

    return plan;
  }
  return SBThreadPlan();
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForRunToAddress(SBAddress sb_address) {
  LLDB_INSTRUMENT_VA(this, sb_address);

  SBError error;
  return QueueThreadPlanForRunToAddress(sb_address, error);
}

SBThreadPlan SBThreadPlan::QueueThreadPlanForRunToAddress(SBAddress sb_address,
                                                          SBError &error) {
  LLDB_INSTRUMENT_VA(this, sb_address, error);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    Address *address = sb_address.get();
    if (!address)
      return SBThreadPlan();

    Status plan_status;
    SBThreadPlan plan =
        SBThreadPlan(thread_plan_sp->GetThread().QueueThreadPlanForRunToAddress(
            false, *address, false, plan_status));

    if (plan_status.Fail())
      error.SetErrorString(plan_status.AsCString());
    else
      plan.GetSP()->SetPrivate(true);

    return plan;
  }
  return SBThreadPlan();
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepScripted(const char *script_class_name) {
  LLDB_INSTRUMENT_VA(this, script_class_name);

  SBError error;
  return QueueThreadPlanForStepScripted(script_class_name, error);
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepScripted(const char *script_class_name,
                                             SBError &error) {
  LLDB_INSTRUMENT_VA(this, script_class_name, error);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    Status plan_status;
    StructuredData::ObjectSP empty_args;
    SBThreadPlan plan =
        SBThreadPlan(thread_plan_sp->GetThread().QueueThreadPlanForStepScripted(
            false, script_class_name, empty_args, false, plan_status));

    if (plan_status.Fail())
      error.SetErrorString(plan_status.AsCString());
    else
      plan.GetSP()->SetPrivate(true);

    return plan;
  }
  return SBThreadPlan();
}

SBThreadPlan
SBThreadPlan::QueueThreadPlanForStepScripted(const char *script_class_name,
                                             lldb::SBStructuredData &args_data,
                                             SBError &error) {
  LLDB_INSTRUMENT_VA(this, script_class_name, args_data, error);

  ThreadPlanSP thread_plan_sp(GetSP());
  if (thread_plan_sp) {
    Status plan_status;
    StructuredData::ObjectSP args_obj = args_data.m_impl_up->GetObjectSP();
    SBThreadPlan plan =
        SBThreadPlan(thread_plan_sp->GetThread().QueueThreadPlanForStepScripted(
            false, script_class_name, args_obj, false, plan_status));

    if (plan_status.Fail())
      error.SetErrorString(plan_status.AsCString());
    else
      plan.GetSP()->SetPrivate(true);

    return plan;
  } else {
    return SBThreadPlan();
  }
}
