//===-- ThreadPlanStepOut.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlanStepOverRange.h"
#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

uint32_t ThreadPlanStepOut::s_default_flag_values = 0;

// ThreadPlanStepOut: Step out of the current frame
ThreadPlanStepOut::ThreadPlanStepOut(
    Thread &thread, SymbolContext *context, bool first_insn, bool stop_others,
    Vote report_stop_vote, Vote report_run_vote, uint32_t frame_idx,
    LazyBool step_out_avoids_code_without_debug_info,
    bool continue_to_next_branch, bool gather_return_value)
    : ThreadPlan(ThreadPlan::eKindStepOut, "Step out", thread, report_stop_vote,
                 report_run_vote),
      ThreadPlanShouldStopHere(this), m_step_from_insn(LLDB_INVALID_ADDRESS),
      m_return_bp_id(LLDB_INVALID_BREAK_ID),
      m_return_addr(LLDB_INVALID_ADDRESS), m_stop_others(stop_others),
      m_immediate_step_from_function(nullptr),
      m_calculate_return_value(gather_return_value) {
  Log *log = GetLog(LLDBLog::Step);
  SetFlagsToDefault();
  SetupAvoidNoDebug(step_out_avoids_code_without_debug_info);

  m_step_from_insn = thread.GetRegisterContext()->GetPC(0);

  uint32_t return_frame_index = frame_idx + 1;
  StackFrameSP return_frame_sp(thread.GetStackFrameAtIndex(return_frame_index));
  StackFrameSP immediate_return_from_sp(thread.GetStackFrameAtIndex(frame_idx));

  if (!return_frame_sp || !immediate_return_from_sp)
    return; // we can't do anything here.  ValidatePlan() will return false.

  // While stepping out, behave as-if artificial frames are not present.
  while (return_frame_sp->IsArtificial()) {
    m_stepped_past_frames.push_back(return_frame_sp);

    ++return_frame_index;
    return_frame_sp = thread.GetStackFrameAtIndex(return_frame_index);

    // We never expect to see an artificial frame without a regular ancestor.
    // If this happens, log the issue and defensively refuse to step out.
    if (!return_frame_sp) {
      LLDB_LOG(log, "Can't step out of frame with artificial ancestors");
      return;
    }
  }

  m_step_out_to_id = return_frame_sp->GetStackID();
  m_immediate_step_from_id = immediate_return_from_sp->GetStackID();

  // If the frame directly below the one we are returning to is inlined, we
  // have to be a little more careful.  It is non-trivial to determine the real
  // "return code address" for an inlined frame, so we have to work our way to
  // that frame and then step out.
  if (immediate_return_from_sp->IsInlined()) {
    if (frame_idx > 0) {
      // First queue a plan that gets us to this inlined frame, and when we get
      // there we'll queue a second plan that walks us out of this frame.
      m_step_out_to_inline_plan_sp = std::make_shared<ThreadPlanStepOut>(
          thread, nullptr, false, stop_others, eVoteNoOpinion, eVoteNoOpinion,
          frame_idx - 1, eLazyBoolNo, continue_to_next_branch);
      static_cast<ThreadPlanStepOut *>(m_step_out_to_inline_plan_sp.get())
          ->SetShouldStopHereCallbacks(nullptr, nullptr);
      m_step_out_to_inline_plan_sp->SetPrivate(true);
    } else {
      // If we're already at the inlined frame we're stepping through, then
      // just do that now.
      QueueInlinedStepPlan(false);
    }
  } else {
    // Find the return address and set a breakpoint there:
    // FIXME - can we do this more securely if we know first_insn?

    Address return_address(return_frame_sp->GetFrameCodeAddress());
    if (continue_to_next_branch) {
      SymbolContext return_address_sc;
      AddressRange range;
      Address return_address_decr_pc = return_address;
      if (return_address_decr_pc.GetOffset() > 0)
        return_address_decr_pc.Slide(-1);

      return_address_decr_pc.CalculateSymbolContext(
          &return_address_sc, lldb::eSymbolContextLineEntry);
      if (return_address_sc.line_entry.IsValid()) {
        const bool include_inlined_functions = false;
        range = return_address_sc.line_entry.GetSameLineContiguousAddressRange(
            include_inlined_functions);
        if (range.GetByteSize() > 0) {
          return_address = m_process.AdvanceAddressToNextBranchInstruction(
              return_address, range);
        }
      }
    }
    m_return_addr = return_address.GetLoadAddress(&m_process.GetTarget());

    if (m_return_addr == LLDB_INVALID_ADDRESS)
      return;

    // Perform some additional validation on the return address.
    uint32_t permissions = 0;
    if (!m_process.GetLoadAddressPermissions(m_return_addr, permissions)) {
      LLDB_LOGF(log, "ThreadPlanStepOut(%p): Return address (0x%" PRIx64
                ") permissions not found.", static_cast<void *>(this),
                m_return_addr);
    } else if (!(permissions & ePermissionsExecutable)) {
      m_constructor_errors.Printf("Return address (0x%" PRIx64
                                  ") did not point to executable memory.",
                                  m_return_addr);
      LLDB_LOGF(log, "ThreadPlanStepOut(%p): %s", static_cast<void *>(this),
                m_constructor_errors.GetData());
      return;
    }

    Breakpoint *return_bp = 
        GetTarget().CreateBreakpoint(m_return_addr, true, false).get();

    if (return_bp != nullptr) {
      if (return_bp->IsHardware() && !return_bp->HasResolvedLocations())
        m_could_not_resolve_hw_bp = true;
      return_bp->SetThreadID(m_tid);
      m_return_bp_id = return_bp->GetID();
      return_bp->SetBreakpointKind("step-out");
    }

    if (immediate_return_from_sp) {
      const SymbolContext &sc =
          immediate_return_from_sp->GetSymbolContext(eSymbolContextFunction);
      if (sc.function) {
        m_immediate_step_from_function = sc.function;
      }
    }
  }
}

void ThreadPlanStepOut::SetupAvoidNoDebug(
    LazyBool step_out_avoids_code_without_debug_info) {
  bool avoid_nodebug = true;
  switch (step_out_avoids_code_without_debug_info) {
  case eLazyBoolYes:
    avoid_nodebug = true;
    break;
  case eLazyBoolNo:
    avoid_nodebug = false;
    break;
  case eLazyBoolCalculate:
    avoid_nodebug = GetThread().GetStepOutAvoidsNoDebug();
    break;
  }
  if (avoid_nodebug)
    GetFlags().Set(ThreadPlanShouldStopHere::eStepOutAvoidNoDebug);
  else
    GetFlags().Clear(ThreadPlanShouldStopHere::eStepOutAvoidNoDebug);
}

void ThreadPlanStepOut::DidPush() {
  Thread &thread = GetThread();
  if (m_step_out_to_inline_plan_sp)
    thread.QueueThreadPlan(m_step_out_to_inline_plan_sp, false);
  else if (m_step_through_inline_plan_sp)
    thread.QueueThreadPlan(m_step_through_inline_plan_sp, false);
}

ThreadPlanStepOut::~ThreadPlanStepOut() {
  if (m_return_bp_id != LLDB_INVALID_BREAK_ID)
    GetTarget().RemoveBreakpointByID(m_return_bp_id);
}

void ThreadPlanStepOut::GetDescription(Stream *s,
                                       lldb::DescriptionLevel level) {
  if (level == lldb::eDescriptionLevelBrief)
    s->Printf("step out");
  else {
    if (m_step_out_to_inline_plan_sp)
      s->Printf("Stepping out to inlined frame so we can walk through it.");
    else if (m_step_through_inline_plan_sp)
      s->Printf("Stepping out by stepping through inlined function.");
    else {
      s->Printf("Stepping out from ");
      Address tmp_address;
      if (tmp_address.SetLoadAddress(m_step_from_insn, &GetTarget())) {
        tmp_address.Dump(s, &m_process, Address::DumpStyleResolvedDescription,
                         Address::DumpStyleLoadAddress);
      } else {
        s->Printf("address 0x%" PRIx64 "", (uint64_t)m_step_from_insn);
      }

      // FIXME: find some useful way to present the m_return_id, since there may
      // be multiple copies of the
      // same function on the stack.

      s->Printf(" returning to frame at ");
      if (tmp_address.SetLoadAddress(m_return_addr, &GetTarget())) {
        tmp_address.Dump(s, &m_process, Address::DumpStyleResolvedDescription,
                         Address::DumpStyleLoadAddress);
      } else {
        s->Printf("address 0x%" PRIx64 "", (uint64_t)m_return_addr);
      }

      if (level == eDescriptionLevelVerbose)
        s->Printf(" using breakpoint site %d", m_return_bp_id);
    }
  }

  if (m_stepped_past_frames.empty())
    return;

  s->Printf("\n");
  for (StackFrameSP frame_sp : m_stepped_past_frames) {
    s->Printf("Stepped out past: ");
    frame_sp->DumpUsingSettingsFormat(s);
  }
}

bool ThreadPlanStepOut::ValidatePlan(Stream *error) {
  if (m_step_out_to_inline_plan_sp)
    return m_step_out_to_inline_plan_sp->ValidatePlan(error);

  if (m_step_through_inline_plan_sp)
    return m_step_through_inline_plan_sp->ValidatePlan(error);

  if (m_could_not_resolve_hw_bp) {
    if (error)
      error->PutCString(
          "Could not create hardware breakpoint for thread plan.");
    return false;
  }

  if (m_return_bp_id == LLDB_INVALID_BREAK_ID) {
    if (error) {
      error->PutCString("Could not create return address breakpoint.");
      if (m_constructor_errors.GetSize() > 0) {
        error->PutCString(" ");
        error->PutCString(m_constructor_errors.GetString());
      }
    }
    return false;
  }

  return true;
}

bool ThreadPlanStepOut::DoPlanExplainsStop(Event *event_ptr) {
  // If the step out plan is done, then we just need to step through the
  // inlined frame.
  if (m_step_out_to_inline_plan_sp) {
    return m_step_out_to_inline_plan_sp->MischiefManaged();
  } else if (m_step_through_inline_plan_sp) {
    if (m_step_through_inline_plan_sp->MischiefManaged()) {
      CalculateReturnValue();
      SetPlanComplete();
      return true;
    } else
      return false;
  } else if (m_step_out_further_plan_sp) {
    return m_step_out_further_plan_sp->MischiefManaged();
  }

  // We don't explain signals or breakpoints (breakpoints that handle stepping
  // in or out will be handled by a child plan.

  StopInfoSP stop_info_sp = GetPrivateStopInfo();
  if (stop_info_sp) {
    StopReason reason = stop_info_sp->GetStopReason();
    if (reason == eStopReasonBreakpoint) {
      // If this is OUR breakpoint, we're fine, otherwise we don't know why
      // this happened...
      BreakpointSiteSP site_sp(
          m_process.GetBreakpointSiteList().FindByID(stop_info_sp->GetValue()));
      if (site_sp && site_sp->IsBreakpointAtThisSite(m_return_bp_id)) {
        bool done;

        StackID frame_zero_id =
            GetThread().GetStackFrameAtIndex(0)->GetStackID();

        if (m_step_out_to_id == frame_zero_id)
          done = true;
        else if (m_step_out_to_id < frame_zero_id) {
          // Either we stepped past the breakpoint, or the stack ID calculation
          // was incorrect and we should probably stop.
          done = true;
        } else {
          done = (m_immediate_step_from_id < frame_zero_id);
        }

        if (done) {
          if (InvokeShouldStopHereCallback(eFrameCompareOlder, m_status)) {
            CalculateReturnValue();
            SetPlanComplete();
          }
        }

        // If there was only one owner, then we're done.  But if we also hit
        // some user breakpoint on our way out, we should mark ourselves as
        // done, but also not claim to explain the stop, since it is more
        // important to report the user breakpoint than the step out
        // completion.

        if (site_sp->GetNumberOfConstituents() == 1)
          return true;
      }
      return false;
    } else if (IsUsuallyUnexplainedStopReason(reason))
      return false;
    else
      return true;
  }
  return true;
}

bool ThreadPlanStepOut::ShouldStop(Event *event_ptr) {
  if (IsPlanComplete())
    return true;

  bool done = false;
  if (m_step_out_to_inline_plan_sp) {
    if (m_step_out_to_inline_plan_sp->MischiefManaged()) {
      // Now step through the inlined stack we are in:
      if (QueueInlinedStepPlan(true)) {
        // If we can't queue a plan to do this, then just call ourselves done.
        m_step_out_to_inline_plan_sp.reset();
        SetPlanComplete(false);
        return true;
      } else
        done = true;
    } else
      return m_step_out_to_inline_plan_sp->ShouldStop(event_ptr);
  } else if (m_step_through_inline_plan_sp) {
    if (m_step_through_inline_plan_sp->MischiefManaged())
      done = true;
    else
      return m_step_through_inline_plan_sp->ShouldStop(event_ptr);
  } else if (m_step_out_further_plan_sp) {
    if (m_step_out_further_plan_sp->MischiefManaged())
      m_step_out_further_plan_sp.reset();
    else
      return m_step_out_further_plan_sp->ShouldStop(event_ptr);
  }

  if (!done) {
    StackID frame_zero_id = GetThread().GetStackFrameAtIndex(0)->GetStackID();
    done = !(frame_zero_id < m_step_out_to_id);
  }

  // The normal step out computations think we are done, so all we need to do
  // is consult the ShouldStopHere, and we are done.

  if (done) {
    if (InvokeShouldStopHereCallback(eFrameCompareOlder, m_status)) {
      CalculateReturnValue();
      SetPlanComplete();
    } else {
      m_step_out_further_plan_sp =
          QueueStepOutFromHerePlan(m_flags, eFrameCompareOlder, m_status);
      done = false;
    }
  }

  return done;
}

bool ThreadPlanStepOut::StopOthers() { return m_stop_others; }

StateType ThreadPlanStepOut::GetPlanRunState() { return eStateRunning; }

bool ThreadPlanStepOut::DoWillResume(StateType resume_state,
                                     bool current_plan) {
  if (m_step_out_to_inline_plan_sp || m_step_through_inline_plan_sp)
    return true;

  if (m_return_bp_id == LLDB_INVALID_BREAK_ID)
    return false;

  if (current_plan) {
    Breakpoint *return_bp = GetTarget().GetBreakpointByID(m_return_bp_id).get();
    if (return_bp != nullptr)
      return_bp->SetEnabled(true);
  }
  return true;
}

bool ThreadPlanStepOut::WillStop() {
  if (m_return_bp_id != LLDB_INVALID_BREAK_ID) {
    Breakpoint *return_bp = GetTarget().GetBreakpointByID(m_return_bp_id).get();
    if (return_bp != nullptr)
      return_bp->SetEnabled(false);
  }

  return true;
}

bool ThreadPlanStepOut::MischiefManaged() {
  if (IsPlanComplete()) {
    // Did I reach my breakpoint?  If so I'm done.
    //
    // I also check the stack depth, since if we've blown past the breakpoint
    // for some
    // reason and we're now stopping for some other reason altogether, then
    // we're done with this step out operation.

    Log *log = GetLog(LLDBLog::Step);
    if (log)
      LLDB_LOGF(log, "Completed step out plan.");
    if (m_return_bp_id != LLDB_INVALID_BREAK_ID) {
      GetTarget().RemoveBreakpointByID(m_return_bp_id);
      m_return_bp_id = LLDB_INVALID_BREAK_ID;
    }

    ThreadPlan::MischiefManaged();
    return true;
  } else {
    return false;
  }
}

bool ThreadPlanStepOut::QueueInlinedStepPlan(bool queue_now) {
  // Now figure out the range of this inlined block, and set up a "step through
  // range" plan for that.  If we've been provided with a context, then use the
  // block in that context.
  Thread &thread = GetThread();
  StackFrameSP immediate_return_from_sp(thread.GetStackFrameAtIndex(0));
  if (!immediate_return_from_sp)
    return false;

  Log *log = GetLog(LLDBLog::Step);
  if (log) {
    StreamString s;
    immediate_return_from_sp->Dump(&s, true, false);
    LLDB_LOGF(log, "Queuing inlined frame to step past: %s.", s.GetData());
  }

  Block *from_block = immediate_return_from_sp->GetFrameBlock();
  if (from_block) {
    Block *inlined_block = from_block->GetContainingInlinedBlock();
    if (inlined_block) {
      size_t num_ranges = inlined_block->GetNumRanges();
      AddressRange inline_range;
      if (inlined_block->GetRangeAtIndex(0, inline_range)) {
        SymbolContext inlined_sc;
        inlined_block->CalculateSymbolContext(&inlined_sc);
        inlined_sc.target_sp = GetTarget().shared_from_this();
        RunMode run_mode =
            m_stop_others ? lldb::eOnlyThisThread : lldb::eAllThreads;
        const LazyBool avoid_no_debug = eLazyBoolNo;

        m_step_through_inline_plan_sp =
            std::make_shared<ThreadPlanStepOverRange>(
                thread, inline_range, inlined_sc, run_mode, avoid_no_debug);
        ThreadPlanStepOverRange *step_through_inline_plan_ptr =
            static_cast<ThreadPlanStepOverRange *>(
                m_step_through_inline_plan_sp.get());
        m_step_through_inline_plan_sp->SetPrivate(true);

        step_through_inline_plan_ptr->SetOkayToDiscard(true);
        StreamString errors;
        if (!step_through_inline_plan_ptr->ValidatePlan(&errors)) {
          // FIXME: Log this failure.
          delete step_through_inline_plan_ptr;
          return false;
        }

        for (size_t i = 1; i < num_ranges; i++) {
          if (inlined_block->GetRangeAtIndex(i, inline_range))
            step_through_inline_plan_ptr->AddRange(inline_range);
        }

        if (queue_now)
          thread.QueueThreadPlan(m_step_through_inline_plan_sp, false);
        return true;
      }
    }
  }

  return false;
}

void ThreadPlanStepOut::CalculateReturnValue() {
  if (m_return_valobj_sp)
    return;

  if (!m_calculate_return_value)
    return;

  if (m_immediate_step_from_function != nullptr) {
    CompilerType return_compiler_type =
        m_immediate_step_from_function->GetCompilerType()
            .GetFunctionReturnType();
    if (return_compiler_type) {
      lldb::ABISP abi_sp = m_process.GetABI();
      if (abi_sp)
        m_return_valobj_sp =
            abi_sp->GetReturnValueObject(GetThread(), return_compiler_type);
    }
  }
}

bool ThreadPlanStepOut::IsPlanStale() {
  // If we are still lower on the stack than the frame we are returning to,
  // then there's something for us to do.  Otherwise, we're stale.

  StackID frame_zero_id = GetThread().GetStackFrameAtIndex(0)->GetStackID();
  return !(frame_zero_id < m_step_out_to_id);
}
