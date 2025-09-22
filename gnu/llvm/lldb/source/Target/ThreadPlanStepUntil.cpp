//===-- ThreadPlanStepUntil.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepUntil.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Symbol/SymbolContextScope.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

// ThreadPlanStepUntil: Run until we reach a given line number or step out of
// the current frame

ThreadPlanStepUntil::ThreadPlanStepUntil(Thread &thread,
                                         lldb::addr_t *address_list,
                                         size_t num_addresses, bool stop_others,
                                         uint32_t frame_idx)
    : ThreadPlan(ThreadPlan::eKindStepUntil, "Step until", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_step_from_insn(LLDB_INVALID_ADDRESS),
      m_return_bp_id(LLDB_INVALID_BREAK_ID),
      m_return_addr(LLDB_INVALID_ADDRESS), m_stepped_out(false),
      m_should_stop(false), m_ran_analyze(false), m_explains_stop(false),
      m_until_points(), m_stop_others(stop_others) {
  // Stash away our "until" addresses:
  TargetSP target_sp(thread.CalculateTarget());

  StackFrameSP frame_sp(thread.GetStackFrameAtIndex(frame_idx));
  if (frame_sp) {
    m_step_from_insn = frame_sp->GetStackID().GetPC();

    // Find the return address and set a breakpoint there:
    // FIXME - can we do this more securely if we know first_insn?

    StackFrameSP return_frame_sp(thread.GetStackFrameAtIndex(frame_idx + 1));
    if (return_frame_sp) {
      // TODO: add inline functionality
      m_return_addr = return_frame_sp->GetStackID().GetPC();
      Breakpoint *return_bp =
          target_sp->CreateBreakpoint(m_return_addr, true, false).get();

      if (return_bp != nullptr) {
        if (return_bp->IsHardware() && !return_bp->HasResolvedLocations())
          m_could_not_resolve_hw_bp = true;
        return_bp->SetThreadID(m_tid);
        m_return_bp_id = return_bp->GetID();
        return_bp->SetBreakpointKind("until-return-backstop");
      }
    }

    m_stack_id = frame_sp->GetStackID();

    // Now set breakpoints on all our return addresses:
    for (size_t i = 0; i < num_addresses; i++) {
      Breakpoint *until_bp =
          target_sp->CreateBreakpoint(address_list[i], true, false).get();
      if (until_bp != nullptr) {
        until_bp->SetThreadID(m_tid);
        m_until_points[address_list[i]] = until_bp->GetID();
        until_bp->SetBreakpointKind("until-target");
      } else {
        m_until_points[address_list[i]] = LLDB_INVALID_BREAK_ID;
      }
    }
  }
}

ThreadPlanStepUntil::~ThreadPlanStepUntil() { Clear(); }

void ThreadPlanStepUntil::Clear() {
  Target &target = GetTarget();
  if (m_return_bp_id != LLDB_INVALID_BREAK_ID) {
    target.RemoveBreakpointByID(m_return_bp_id);
    m_return_bp_id = LLDB_INVALID_BREAK_ID;
  }

  until_collection::iterator pos, end = m_until_points.end();
  for (pos = m_until_points.begin(); pos != end; pos++) {
    target.RemoveBreakpointByID((*pos).second);
  }
  m_until_points.clear();
  m_could_not_resolve_hw_bp = false;
}

void ThreadPlanStepUntil::GetDescription(Stream *s,
                                         lldb::DescriptionLevel level) {
  if (level == lldb::eDescriptionLevelBrief) {
    s->Printf("step until");
    if (m_stepped_out)
      s->Printf(" - stepped out");
  } else {
    if (m_until_points.size() == 1)
      s->Printf("Stepping from address 0x%" PRIx64 " until we reach 0x%" PRIx64
                " using breakpoint %d",
                (uint64_t)m_step_from_insn,
                (uint64_t)(*m_until_points.begin()).first,
                (*m_until_points.begin()).second);
    else {
      until_collection::iterator pos, end = m_until_points.end();
      s->Printf("Stepping from address 0x%" PRIx64 " until we reach one of:",
                (uint64_t)m_step_from_insn);
      for (pos = m_until_points.begin(); pos != end; pos++) {
        s->Printf("\n\t0x%" PRIx64 " (bp: %d)", (uint64_t)(*pos).first,
                  (*pos).second);
      }
    }
    s->Printf(" stepped out address is 0x%" PRIx64 ".",
              (uint64_t)m_return_addr);
  }
}

bool ThreadPlanStepUntil::ValidatePlan(Stream *error) {
  if (m_could_not_resolve_hw_bp) {
    if (error)
      error->PutCString(
          "Could not create hardware breakpoint for thread plan.");
    return false;
  } else if (m_return_bp_id == LLDB_INVALID_BREAK_ID) {
    if (error)
      error->PutCString("Could not create return breakpoint.");
    return false;
  } else {
    until_collection::iterator pos, end = m_until_points.end();
    for (pos = m_until_points.begin(); pos != end; pos++) {
      if (!LLDB_BREAK_ID_IS_VALID((*pos).second))
        return false;
    }
    return true;
  }
}

void ThreadPlanStepUntil::AnalyzeStop() {
  if (m_ran_analyze)
    return;

  StopInfoSP stop_info_sp = GetPrivateStopInfo();
  m_should_stop = true;
  m_explains_stop = false;

  if (stop_info_sp) {
    StopReason reason = stop_info_sp->GetStopReason();

    if (reason == eStopReasonBreakpoint) {
      // If this is OUR breakpoint, we're fine, otherwise we don't know why
      // this happened...
      BreakpointSiteSP this_site =
          m_process.GetBreakpointSiteList().FindByID(stop_info_sp->GetValue());
      if (!this_site) {
        m_explains_stop = false;
        return;
      }

      if (this_site->IsBreakpointAtThisSite(m_return_bp_id)) {
        // If we are at our "step out" breakpoint, and the stack depth has
        // shrunk, then this is indeed our stop. If the stack depth has grown,
        // then we've hit our step out breakpoint recursively. If we are the
        // only breakpoint at that location, then we do explain the stop, and
        // we'll just continue. If there was another breakpoint here, then we
        // don't explain the stop, but we won't mark ourselves Completed,
        // because maybe that breakpoint will continue, and then we'll finish
        // the "until".
        bool done;
        StackID cur_frame_zero_id;

        done = (m_stack_id < cur_frame_zero_id);

        if (done) {
          m_stepped_out = true;
          SetPlanComplete();
        } else
          m_should_stop = false;

        if (this_site->GetNumberOfConstituents() == 1)
          m_explains_stop = true;
        else
          m_explains_stop = false;
        return;
      } else {
        // Check if we've hit one of our "until" breakpoints.
        until_collection::iterator pos, end = m_until_points.end();
        for (pos = m_until_points.begin(); pos != end; pos++) {
          if (this_site->IsBreakpointAtThisSite((*pos).second)) {
            // If we're at the right stack depth, then we're done.
            Thread &thread = GetThread();
            bool done;
            StackID frame_zero_id =
                thread.GetStackFrameAtIndex(0)->GetStackID();

            if (frame_zero_id == m_stack_id)
              done = true;
            else if (frame_zero_id < m_stack_id)
              done = false;
            else {
              StackFrameSP older_frame_sp = thread.GetStackFrameAtIndex(1);

              // But if we can't even unwind one frame we should just get out
              // of here & stop...
              if (older_frame_sp) {
                const SymbolContext &older_context =
                    older_frame_sp->GetSymbolContext(eSymbolContextEverything);
                SymbolContext stack_context;
                m_stack_id.GetSymbolContextScope()->CalculateSymbolContext(
                    &stack_context);

                done = (older_context == stack_context);
              } else
                done = false;
            }

            if (done)
              SetPlanComplete();
            else
              m_should_stop = false;

            // Otherwise we've hit this breakpoint recursively.  If we're the
            // only breakpoint here, then we do explain the stop, and we'll
            // continue. If not then we should let higher plans handle this
            // stop.
            if (this_site->GetNumberOfConstituents() == 1)
              m_explains_stop = true;
            else {
              m_should_stop = true;
              m_explains_stop = false;
            }
            return;
          }
        }
      }
      // If we get here we haven't hit any of our breakpoints, so let the
      // higher plans take care of the stop.
      m_explains_stop = false;
      return;
    } else if (IsUsuallyUnexplainedStopReason(reason)) {
      m_explains_stop = false;
    } else {
      m_explains_stop = true;
    }
  }
}

bool ThreadPlanStepUntil::DoPlanExplainsStop(Event *event_ptr) {
  // We don't explain signals or breakpoints (breakpoints that handle stepping
  // in or out will be handled by a child plan.
  AnalyzeStop();
  return m_explains_stop;
}

bool ThreadPlanStepUntil::ShouldStop(Event *event_ptr) {
  // If we've told our self in ExplainsStop that we plan to continue, then do
  // so here.  Otherwise, as long as this thread has stopped for a reason, we
  // will stop.

  StopInfoSP stop_info_sp = GetPrivateStopInfo();
  if (!stop_info_sp || stop_info_sp->GetStopReason() == eStopReasonNone)
    return false;

  AnalyzeStop();
  return m_should_stop;
}

bool ThreadPlanStepUntil::StopOthers() { return m_stop_others; }

StateType ThreadPlanStepUntil::GetPlanRunState() { return eStateRunning; }

bool ThreadPlanStepUntil::DoWillResume(StateType resume_state,
                                       bool current_plan) {
  if (current_plan) {
    Target &target = GetTarget();
    Breakpoint *return_bp = target.GetBreakpointByID(m_return_bp_id).get();
    if (return_bp != nullptr)
      return_bp->SetEnabled(true);

    until_collection::iterator pos, end = m_until_points.end();
    for (pos = m_until_points.begin(); pos != end; pos++) {
      Breakpoint *until_bp = target.GetBreakpointByID((*pos).second).get();
      if (until_bp != nullptr)
        until_bp->SetEnabled(true);
    }
  }

  m_should_stop = true;
  m_ran_analyze = false;
  m_explains_stop = false;
  return true;
}

bool ThreadPlanStepUntil::WillStop() {
  Target &target = GetTarget();
  Breakpoint *return_bp = target.GetBreakpointByID(m_return_bp_id).get();
  if (return_bp != nullptr)
    return_bp->SetEnabled(false);

  until_collection::iterator pos, end = m_until_points.end();
  for (pos = m_until_points.begin(); pos != end; pos++) {
    Breakpoint *until_bp = target.GetBreakpointByID((*pos).second).get();
    if (until_bp != nullptr)
      until_bp->SetEnabled(false);
  }
  return true;
}

bool ThreadPlanStepUntil::MischiefManaged() {
  // I'm letting "PlanExplainsStop" do all the work, and just reporting that
  // here.
  bool done = false;
  if (IsPlanComplete()) {
    Log *log = GetLog(LLDBLog::Step);
    LLDB_LOGF(log, "Completed step until plan.");

    Clear();
    done = true;
  }
  if (done)
    ThreadPlan::MischiefManaged();

  return done;
}
