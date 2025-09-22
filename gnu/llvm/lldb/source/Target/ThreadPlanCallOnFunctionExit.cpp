//===-- ThreadPlanCallOnFunctionExit.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanCallOnFunctionExit.h"

using namespace lldb;
using namespace lldb_private;

ThreadPlanCallOnFunctionExit::ThreadPlanCallOnFunctionExit(
    Thread &thread, const Callback &callback)
    : ThreadPlan(ThreadPlanKind::eKindGeneric, "CallOnFunctionExit", thread,
                 eVoteNoOpinion, eVoteNoOpinion // TODO check with Jim on these
                 ),
      m_callback(callback) {
  // We are not a user-generated plan.
  SetIsControllingPlan(false);
}

void ThreadPlanCallOnFunctionExit::DidPush() {
  // We now want to queue the "step out" thread plan so it executes and
  // completes.

  // Set stop vote to eVoteNo.
  Status status;
  m_step_out_threadplan_sp = GetThread().QueueThreadPlanForStepOut(
      false,             // abort other plans
      nullptr,           // addr_context
      true,              // first instruction
      true,              // stop other threads
      eVoteNo,           // do not say "we're stopping"
      eVoteNoOpinion,    // don't care about run state broadcasting
      0,                 // frame_idx
      status,            // status
      eLazyBoolCalculate // avoid code w/o debinfo
  );
}

// ThreadPlan API

void ThreadPlanCallOnFunctionExit::GetDescription(
    Stream *s, lldb::DescriptionLevel level) {
  if (!s)
    return;
  s->Printf("Running until completion of current function, then making "
            "callback.");
}

bool ThreadPlanCallOnFunctionExit::ValidatePlan(Stream *error) {
  // We'll say we're always good since I don't know what would make this
  // invalid.
  return true;
}

bool ThreadPlanCallOnFunctionExit::ShouldStop(Event *event_ptr) {
  // If this is where we find out that an internal stop came in, then: Check if
  // the step-out plan completed.  If it did, then we want to run the callback
  // here (our reason for living...)
  if (m_step_out_threadplan_sp && m_step_out_threadplan_sp->IsPlanComplete()) {
    m_callback();

    // We no longer need the pointer to the step-out thread plan.
    m_step_out_threadplan_sp.reset();

    // Indicate that this plan is done and can be discarded.
    SetPlanComplete();

    // We're done now, but we want to return false so that we don't cause the
    // thread to really stop.
  }

  return false;
}

bool ThreadPlanCallOnFunctionExit::WillStop() {
  // The code looks like the return value is ignored via ThreadList::
  // ShouldStop(). This is called when we really are going to stop.  We don't
  // care and don't need to do anything here.
  return false;
}

bool ThreadPlanCallOnFunctionExit::DoPlanExplainsStop(Event *event_ptr) {
  // We don't ever explain a stop.  The only stop that is relevant to us
  // directly is the step_out plan we added to do the heavy lifting of getting
  // us past the current method.
  return false;
}

lldb::StateType ThreadPlanCallOnFunctionExit::GetPlanRunState() {
  // This value doesn't matter - we'll never be the top thread plan, so nobody
  // will ask us this question.
  return eStateRunning;
}
