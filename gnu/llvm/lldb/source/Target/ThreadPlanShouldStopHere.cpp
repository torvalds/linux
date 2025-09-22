//===-- ThreadPlanShouldStopHere.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanShouldStopHere.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

// ThreadPlanShouldStopHere constructor
ThreadPlanShouldStopHere::ThreadPlanShouldStopHere(ThreadPlan *owner)
    : m_callbacks(), m_baton(nullptr), m_owner(owner),
      m_flags(ThreadPlanShouldStopHere::eNone) {
  m_callbacks.should_stop_here_callback =
      ThreadPlanShouldStopHere::DefaultShouldStopHereCallback;
  m_callbacks.step_from_here_callback =
      ThreadPlanShouldStopHere::DefaultStepFromHereCallback;
}

ThreadPlanShouldStopHere::ThreadPlanShouldStopHere(
    ThreadPlan *owner, const ThreadPlanShouldStopHereCallbacks *callbacks,
    void *baton)
    : m_callbacks(), m_baton(), m_owner(owner),
      m_flags(ThreadPlanShouldStopHere::eNone) {
  SetShouldStopHereCallbacks(callbacks, baton);
}

ThreadPlanShouldStopHere::~ThreadPlanShouldStopHere() = default;

bool ThreadPlanShouldStopHere::InvokeShouldStopHereCallback(
    FrameComparison operation, Status &status) {
  bool should_stop_here = true;
  if (m_callbacks.should_stop_here_callback) {
    should_stop_here = m_callbacks.should_stop_here_callback(
        m_owner, m_flags, operation, status, m_baton);
    Log *log = GetLog(LLDBLog::Step);
    if (log) {
      lldb::addr_t current_addr =
          m_owner->GetThread().GetRegisterContext()->GetPC(0);

      LLDB_LOGF(log, "ShouldStopHere callback returned %u from 0x%" PRIx64 ".",
                should_stop_here, current_addr);
    }
  }

  return should_stop_here;
}

bool ThreadPlanShouldStopHere::DefaultShouldStopHereCallback(
    ThreadPlan *current_plan, Flags &flags, FrameComparison operation,
    Status &status, void *baton) {
  bool should_stop_here = true;
  StackFrame *frame = current_plan->GetThread().GetStackFrameAtIndex(0).get();
  if (!frame)
    return true;

  Log *log = GetLog(LLDBLog::Step);

  if ((operation == eFrameCompareOlder && flags.Test(eStepOutAvoidNoDebug)) ||
      (operation == eFrameCompareYounger && flags.Test(eStepInAvoidNoDebug)) ||
      (operation == eFrameCompareSameParent &&
       flags.Test(eStepInAvoidNoDebug))) {
    if (!frame->HasDebugInformation()) {
      LLDB_LOGF(log, "Stepping out of frame with no debug info");

      should_stop_here = false;
    }
  }

  // Always avoid code with line number 0.
  // FIXME: At present the ShouldStop and the StepFromHere calculate this
  // independently.  If this ever
  // becomes expensive (this one isn't) we can try to have this set a state
  // that the StepFromHere can use.
  if (frame) {
    SymbolContext sc;
    sc = frame->GetSymbolContext(eSymbolContextLineEntry);
    if (sc.line_entry.line == 0)
      should_stop_here = false;
  }

  return should_stop_here;
}

ThreadPlanSP ThreadPlanShouldStopHere::DefaultStepFromHereCallback(
    ThreadPlan *current_plan, Flags &flags, FrameComparison operation,
    Status &status, void *baton) {
  const bool stop_others = false;
  const size_t frame_index = 0;
  ThreadPlanSP return_plan_sp;
  // If we are stepping through code at line number 0, then we need to step
  // over this range.  Otherwise we will step out.
  Log *log = GetLog(LLDBLog::Step);

  StackFrame *frame = current_plan->GetThread().GetStackFrameAtIndex(0).get();
  if (!frame)
    return return_plan_sp;
  SymbolContext sc;
  sc = frame->GetSymbolContext(eSymbolContextLineEntry | eSymbolContextSymbol);

  if (sc.line_entry.line == 0) {
    AddressRange range = sc.line_entry.range;

    // If the whole function is marked line 0 just step out, that's easier &
    // faster than continuing to step through it.
    bool just_step_out = false;
    if (sc.symbol && sc.symbol->ValueIsAddress()) {
      Address symbol_end = sc.symbol->GetAddress();
      symbol_end.Slide(sc.symbol->GetByteSize() - 1);
      if (range.ContainsFileAddress(sc.symbol->GetAddress()) &&
          range.ContainsFileAddress(symbol_end)) {
        LLDB_LOGF(log, "Stopped in a function with only line 0 lines, just "
                       "stepping out.");
        just_step_out = true;
      }
    }
    if (!just_step_out) {
      LLDB_LOGF(log, "ThreadPlanShouldStopHere::DefaultStepFromHereCallback "
                     "Queueing StepInRange plan to step through line 0 code.");

      return_plan_sp = current_plan->GetThread().QueueThreadPlanForStepInRange(
          false, range, sc, nullptr, eOnlyDuringStepping, status,
          eLazyBoolCalculate, eLazyBoolNo);
    }
  }

  if (!return_plan_sp)
    return_plan_sp =
        current_plan->GetThread().QueueThreadPlanForStepOutNoShouldStop(
            false, nullptr, true, stop_others, eVoteNo, eVoteNoOpinion,
            frame_index, status, true);
  return return_plan_sp;
}

ThreadPlanSP ThreadPlanShouldStopHere::QueueStepOutFromHerePlan(
    lldb_private::Flags &flags, lldb::FrameComparison operation,
    Status &status) {
  ThreadPlanSP return_plan_sp;
  if (m_callbacks.step_from_here_callback) {
    return_plan_sp = m_callbacks.step_from_here_callback(
        m_owner, flags, operation, status, m_baton);
  }
  return return_plan_sp;
}

lldb::ThreadPlanSP ThreadPlanShouldStopHere::CheckShouldStopHereAndQueueStepOut(
    lldb::FrameComparison operation, Status &status) {
  if (!InvokeShouldStopHereCallback(operation, status))
    return QueueStepOutFromHerePlan(m_flags, operation, status);
  else
    return ThreadPlanSP();
}
