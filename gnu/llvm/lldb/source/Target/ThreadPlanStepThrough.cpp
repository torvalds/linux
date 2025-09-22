//===-- ThreadPlanStepThrough.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

// ThreadPlanStepThrough: If the current instruction is a trampoline, step
// through it If it is the beginning of the prologue of a function, step
// through that as well.

ThreadPlanStepThrough::ThreadPlanStepThrough(Thread &thread,
                                             StackID &m_stack_id,
                                             bool stop_others)
    : ThreadPlan(ThreadPlan::eKindStepThrough,
                 "Step through trampolines and prologues", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_start_address(0), m_backstop_bkpt_id(LLDB_INVALID_BREAK_ID),
      m_backstop_addr(LLDB_INVALID_ADDRESS), m_return_stack_id(m_stack_id),
      m_stop_others(stop_others) {
  LookForPlanToStepThroughFromCurrentPC();

  // If we don't get a valid step through plan, don't bother to set up a
  // backstop.
  if (m_sub_plan_sp) {
    m_start_address = GetThread().GetRegisterContext()->GetPC(0);

    // We are going to return back to the concrete frame 1, we might pass by
    // some inlined code that we're in the middle of by doing this, but it's
    // easier than trying to figure out where the inlined code might return to.

    StackFrameSP return_frame_sp = thread.GetFrameWithStackID(m_stack_id);

    if (return_frame_sp) {
      m_backstop_addr = return_frame_sp->GetFrameCodeAddress().GetLoadAddress(
          thread.CalculateTarget().get());
      Breakpoint *return_bp =
          m_process.GetTarget()
              .CreateBreakpoint(m_backstop_addr, true, false)
              .get();

      if (return_bp != nullptr) {
        if (return_bp->IsHardware() && !return_bp->HasResolvedLocations())
          m_could_not_resolve_hw_bp = true;
        return_bp->SetThreadID(m_tid);
        m_backstop_bkpt_id = return_bp->GetID();
        return_bp->SetBreakpointKind("step-through-backstop");
      }
      Log *log = GetLog(LLDBLog::Step);
      if (log) {
        LLDB_LOGF(log, "Setting backstop breakpoint %d at address: 0x%" PRIx64,
                  m_backstop_bkpt_id, m_backstop_addr);
      }
    }
  }
}

ThreadPlanStepThrough::~ThreadPlanStepThrough() { ClearBackstopBreakpoint(); }

void ThreadPlanStepThrough::DidPush() {
  if (m_sub_plan_sp)
    PushPlan(m_sub_plan_sp);
}

void ThreadPlanStepThrough::LookForPlanToStepThroughFromCurrentPC() {
  Thread &thread = GetThread();
  DynamicLoader *loader = thread.GetProcess()->GetDynamicLoader();
  if (loader)
    m_sub_plan_sp = loader->GetStepThroughTrampolinePlan(thread, m_stop_others);

  // If the DynamicLoader was unable to provide us with a ThreadPlan, then we
  // try the LanguageRuntimes.
  if (!m_sub_plan_sp) {
    for (LanguageRuntime *runtime : m_process.GetLanguageRuntimes()) {
      m_sub_plan_sp =
          runtime->GetStepThroughTrampolinePlan(thread, m_stop_others);

      if (m_sub_plan_sp)
        break;
    }
  }

  Log *log = GetLog(LLDBLog::Step);
  if (log) {
    lldb::addr_t current_address = GetThread().GetRegisterContext()->GetPC(0);
    if (m_sub_plan_sp) {
      StreamString s;
      m_sub_plan_sp->GetDescription(&s, lldb::eDescriptionLevelFull);
      LLDB_LOGF(log, "Found step through plan from 0x%" PRIx64 ": %s",
                current_address, s.GetData());
    } else {
      LLDB_LOGF(log,
                "Couldn't find step through plan from address 0x%" PRIx64 ".",
                current_address);
    }
  }
}

void ThreadPlanStepThrough::GetDescription(Stream *s,
                                           lldb::DescriptionLevel level) {
  if (level == lldb::eDescriptionLevelBrief)
    s->Printf("Step through");
  else {
    s->PutCString("Stepping through trampoline code from: ");
    DumpAddress(s->AsRawOstream(), m_start_address, sizeof(addr_t));
    if (m_backstop_bkpt_id != LLDB_INVALID_BREAK_ID) {
      s->Printf(" with backstop breakpoint ID: %d at address: ",
                m_backstop_bkpt_id);
      DumpAddress(s->AsRawOstream(), m_backstop_addr, sizeof(addr_t));
    } else
      s->PutCString(" unable to set a backstop breakpoint.");
  }
}

bool ThreadPlanStepThrough::ValidatePlan(Stream *error) {
  if (m_could_not_resolve_hw_bp) {
    if (error)
      error->PutCString(
          "Could not create hardware breakpoint for thread plan.");
    return false;
  }

  if (m_backstop_bkpt_id == LLDB_INVALID_BREAK_ID) {
    if (error)
      error->PutCString("Could not create backstop breakpoint.");
    return false;
  }

  if (!m_sub_plan_sp.get()) {
    if (error)
      error->PutCString("Does not have a subplan.");
    return false;
  }

  return true;
}

bool ThreadPlanStepThrough::DoPlanExplainsStop(Event *event_ptr) {
  // If we have a sub-plan, it will have been asked first if we explain the
  // stop, and we won't get asked.  The only time we would be the one directly
  // asked this question is if we hit our backstop breakpoint.

  return HitOurBackstopBreakpoint();
}

bool ThreadPlanStepThrough::ShouldStop(Event *event_ptr) {
  // If we've already marked ourselves done, then we're done...
  if (IsPlanComplete())
    return true;

  // First, did we hit the backstop breakpoint?
  if (HitOurBackstopBreakpoint()) {
    SetPlanComplete(true);
    return true;
  }

  // If we don't have a sub-plan, then we're also done (can't see how we would
  // ever get here without a plan, but just in case.

  if (!m_sub_plan_sp) {
    SetPlanComplete();
    return true;
  }

  // If the current sub plan is not done, we don't want to stop.  Actually, we
  // probably won't ever get here in this state, since we generally won't get
  // asked any questions if out current sub-plan is not done...
  if (!m_sub_plan_sp->IsPlanComplete())
    return false;

  // If our current sub plan failed, then let's just run to our backstop.  If
  // we can't do that then just stop.
  if (!m_sub_plan_sp->PlanSucceeded()) {
    if (m_backstop_bkpt_id != LLDB_INVALID_BREAK_ID) {
      m_sub_plan_sp.reset();
      return false;
    } else {
      SetPlanComplete(false);
      return true;
    }
  }

  // Next see if there is a specific step through plan at our current pc (these
  // might chain, for instance stepping through a dylib trampoline to the objc
  // dispatch function...)
  LookForPlanToStepThroughFromCurrentPC();
  if (m_sub_plan_sp) {
    PushPlan(m_sub_plan_sp);
    return false;
  } else {
    SetPlanComplete();
    return true;
  }
}

bool ThreadPlanStepThrough::StopOthers() { return m_stop_others; }

StateType ThreadPlanStepThrough::GetPlanRunState() { return eStateRunning; }

bool ThreadPlanStepThrough::DoWillResume(StateType resume_state,
                                         bool current_plan) {
  return true;
}

bool ThreadPlanStepThrough::WillStop() { return true; }

void ThreadPlanStepThrough::ClearBackstopBreakpoint() {
  if (m_backstop_bkpt_id != LLDB_INVALID_BREAK_ID) {
    m_process.GetTarget().RemoveBreakpointByID(m_backstop_bkpt_id);
    m_backstop_bkpt_id = LLDB_INVALID_BREAK_ID;
    m_could_not_resolve_hw_bp = false;
  }
}

bool ThreadPlanStepThrough::MischiefManaged() {
  Log *log = GetLog(LLDBLog::Step);

  if (!IsPlanComplete()) {
    return false;
  } else {
    LLDB_LOGF(log, "Completed step through step plan.");

    ClearBackstopBreakpoint();
    ThreadPlan::MischiefManaged();
    return true;
  }
}

bool ThreadPlanStepThrough::HitOurBackstopBreakpoint() {
  Thread &thread = GetThread();
  StopInfoSP stop_info_sp(thread.GetStopInfo());
  if (stop_info_sp && stop_info_sp->GetStopReason() == eStopReasonBreakpoint) {
    break_id_t stop_value = (break_id_t)stop_info_sp->GetValue();
    BreakpointSiteSP cur_site_sp =
        m_process.GetBreakpointSiteList().FindByID(stop_value);
    if (cur_site_sp &&
        cur_site_sp->IsBreakpointAtThisSite(m_backstop_bkpt_id)) {
      StackID cur_frame_zero_id = thread.GetStackFrameAtIndex(0)->GetStackID();

      if (cur_frame_zero_id == m_return_stack_id) {
        Log *log = GetLog(LLDBLog::Step);
        if (log)
          log->PutCString("ThreadPlanStepThrough hit backstop breakpoint.");
        return true;
      }
    }
  }
  return false;
}
