//===-- ThreadPlanStepOverRange.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepOverRange.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;
using namespace lldb;

uint32_t ThreadPlanStepOverRange::s_default_flag_values = 0;

// ThreadPlanStepOverRange: Step through a stack range, either stepping over or
// into based on the value of \a type.

ThreadPlanStepOverRange::ThreadPlanStepOverRange(
    Thread &thread, const AddressRange &range,
    const SymbolContext &addr_context, lldb::RunMode stop_others,
    LazyBool step_out_avoids_code_without_debug_info)
    : ThreadPlanStepRange(ThreadPlan::eKindStepOverRange,
                          "Step range stepping over", thread, range,
                          addr_context, stop_others),
      ThreadPlanShouldStopHere(this), m_first_resume(true) {
  SetFlagsToDefault();
  SetupAvoidNoDebug(step_out_avoids_code_without_debug_info);
}

ThreadPlanStepOverRange::~ThreadPlanStepOverRange() = default;

void ThreadPlanStepOverRange::GetDescription(Stream *s,
                                             lldb::DescriptionLevel level) {
  auto PrintFailureIfAny = [&]() {
    if (m_status.Success())
      return;
    s->Printf(" failed (%s)", m_status.AsCString());
  };

  if (level == lldb::eDescriptionLevelBrief) {
    s->Printf("step over");
    PrintFailureIfAny();
    return;
  }

  s->Printf("Stepping over");
  bool printed_line_info = false;
  if (m_addr_context.line_entry.IsValid()) {
    s->Printf(" line ");
    m_addr_context.line_entry.DumpStopContext(s, false);
    printed_line_info = true;
  }

  if (!printed_line_info || level == eDescriptionLevelVerbose) {
    s->Printf(" using ranges: ");
    DumpRanges(s);
  }

  PrintFailureIfAny();

  s->PutChar('.');
}

void ThreadPlanStepOverRange::SetupAvoidNoDebug(
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
  // Step Over plans should always avoid no-debug on step in.  Seems like you
  // shouldn't have to say this, but a tail call looks more like a step in that
  // a step out, so we want to catch this case.
  GetFlags().Set(ThreadPlanShouldStopHere::eStepInAvoidNoDebug);
}

bool ThreadPlanStepOverRange::IsEquivalentContext(
    const SymbolContext &context) {
  // Match as much as is specified in the m_addr_context: This is a fairly
  // loose sanity check.  Note, sometimes the target doesn't get filled in so I
  // left out the target check.  And sometimes the module comes in as the .o
  // file from the inlined range, so I left that out too...
  if (m_addr_context.comp_unit) {
    if (m_addr_context.comp_unit != context.comp_unit)
      return false;
    if (m_addr_context.function) {
      if (m_addr_context.function != context.function)
        return false;
      // It is okay to return to a different block of a straight function, we
      // only have to be more careful if returning from one inlined block to
      // another.
      if (m_addr_context.block->GetInlinedFunctionInfo() == nullptr &&
          context.block->GetInlinedFunctionInfo() == nullptr)
        return true;
      return m_addr_context.block == context.block;
    }
  }
  // Fall back to symbol if we have no decision from comp_unit/function/block.
  return m_addr_context.symbol && m_addr_context.symbol == context.symbol;
}

bool ThreadPlanStepOverRange::ShouldStop(Event *event_ptr) {
  Log *log = GetLog(LLDBLog::Step);
  Thread &thread = GetThread();

  if (log) {
    StreamString s;
    DumpAddress(s.AsRawOstream(), thread.GetRegisterContext()->GetPC(),
                GetTarget().GetArchitecture().GetAddressByteSize());
    LLDB_LOGF(log, "ThreadPlanStepOverRange reached %s.", s.GetData());
  }

  // If we're out of the range but in the same frame or in our caller's frame
  // then we should stop. When stepping out we only stop others if we are
  // forcing running one thread.
  bool stop_others = (m_stop_others == lldb::eOnlyThisThread);
  ThreadPlanSP new_plan_sp;
  FrameComparison frame_order = CompareCurrentFrameToStartFrame();

  if (frame_order == eFrameCompareOlder) {
    // If we're in an older frame then we should stop.
    //
    // A caveat to this is if we think the frame is older but we're actually in
    // a trampoline.
    // I'm going to make the assumption that you wouldn't RETURN to a
    // trampoline.  So if we are in a trampoline we think the frame is older
    // because the trampoline confused the backtracer. As below, we step
    // through first, and then try to figure out how to get back out again.

    new_plan_sp = thread.QueueThreadPlanForStepThrough(m_stack_id, false,
                                                       stop_others, m_status);

    if (new_plan_sp && log)
      LLDB_LOGF(log,
                "Thought I stepped out, but in fact arrived at a trampoline.");
  } else if (frame_order == eFrameCompareYounger) {
    // Make sure we really are in a new frame.  Do that by unwinding and seeing
    // if the start function really is our start function...
    for (uint32_t i = 1;; ++i) {
      StackFrameSP older_frame_sp = thread.GetStackFrameAtIndex(i);
      if (!older_frame_sp) {
        // We can't unwind the next frame we should just get out of here &
        // stop...
        break;
      }

      const SymbolContext &older_context =
          older_frame_sp->GetSymbolContext(eSymbolContextEverything);
      if (IsEquivalentContext(older_context)) {
        // If we have the  next-branch-breakpoint in the range, we can just
        // rely on that breakpoint to trigger once we return to the range.
        if (m_next_branch_bp_sp)
          return false;
        new_plan_sp = thread.QueueThreadPlanForStepOutNoShouldStop(
            false, nullptr, true, stop_others, eVoteNo, eVoteNoOpinion, 0,
            m_status, true);
        break;
      } else {
        new_plan_sp = thread.QueueThreadPlanForStepThrough(
            m_stack_id, false, stop_others, m_status);
        // If we found a way through, then we should stop recursing.
        if (new_plan_sp)
          break;
      }
    }
  } else {
    // If we're still in the range, keep going.
    if (InRange()) {
      SetNextBranchBreakpoint();
      return false;
    }

    if (!InSymbol()) {
      // This one is a little tricky.  Sometimes we may be in a stub or
      // something similar, in which case we need to get out of there.  But if
      // we are in a stub then it's likely going to be hard to get out from
      // here.  It is probably easiest to step into the stub, and then it will
      // be straight-forward to step out.
      new_plan_sp = thread.QueueThreadPlanForStepThrough(m_stack_id, false, 
                                                         stop_others, m_status);
    } else {
      // The current clang (at least through 424) doesn't always get the
      // address range for the DW_TAG_inlined_subroutines right, so that when
      // you leave the inlined range the line table says you are still in the
      // source file of the inlining function.  This is bad, because now you
      // are missing the stack frame for the function containing the inlining,
      // and if you sensibly do "finish" to get out of this function you will
      // instead exit the containing function. To work around this, we check
      // whether we are still in the source file we started in, and if not
      // assume it is an error, and push a plan to get us out of this line and
      // back to the containing file.

      if (m_addr_context.line_entry.IsValid()) {
        SymbolContext sc;
        StackFrameSP frame_sp = thread.GetStackFrameAtIndex(0);
        sc = frame_sp->GetSymbolContext(eSymbolContextEverything);
        if (sc.line_entry.IsValid()) {
          if (!sc.line_entry.original_file_sp->Equal(
                  *m_addr_context.line_entry.original_file_sp,
                  SupportFile::eEqualFileSpecAndChecksumIfSet) &&
              sc.comp_unit == m_addr_context.comp_unit &&
              sc.function == m_addr_context.function) {
            // Okay, find the next occurrence of this file in the line table:
            LineTable *line_table = m_addr_context.comp_unit->GetLineTable();
            if (line_table) {
              Address cur_address = frame_sp->GetFrameCodeAddress();
              uint32_t entry_idx;
              LineEntry line_entry;
              if (line_table->FindLineEntryByAddress(cur_address, line_entry,
                                                     &entry_idx)) {
                LineEntry next_line_entry;
                bool step_past_remaining_inline = false;
                if (entry_idx > 0) {
                  // We require the previous line entry and the current line
                  // entry come from the same file. The other requirement is
                  // that the previous line table entry be part of an inlined
                  // block, we don't want to step past cases where people have
                  // inlined some code fragment by using #include <source-
                  // fragment.c> directly.
                  LineEntry prev_line_entry;
                  if (line_table->GetLineEntryAtIndex(entry_idx - 1,
                                                      prev_line_entry) &&
                      prev_line_entry.original_file_sp->Equal(
                          *line_entry.original_file_sp,
                          SupportFile::eEqualFileSpecAndChecksumIfSet)) {
                    SymbolContext prev_sc;
                    Address prev_address =
                        prev_line_entry.range.GetBaseAddress();
                    prev_address.CalculateSymbolContext(&prev_sc);
                    if (prev_sc.block) {
                      Block *inlined_block =
                          prev_sc.block->GetContainingInlinedBlock();
                      if (inlined_block) {
                        AddressRange inline_range;
                        inlined_block->GetRangeContainingAddress(prev_address,
                                                                 inline_range);
                        if (!inline_range.ContainsFileAddress(cur_address)) {

                          step_past_remaining_inline = true;
                        }
                      }
                    }
                  }
                }

                if (step_past_remaining_inline) {
                  uint32_t look_ahead_step = 1;
                  while (line_table->GetLineEntryAtIndex(
                      entry_idx + look_ahead_step, next_line_entry)) {
                    // Make sure we haven't wandered out of the function we
                    // started from...
                    Address next_line_address =
                        next_line_entry.range.GetBaseAddress();
                    Function *next_line_function =
                        next_line_address.CalculateSymbolContextFunction();
                    if (next_line_function != m_addr_context.function)
                      break;

                    if (next_line_entry.original_file_sp->Equal(
                            *m_addr_context.line_entry.original_file_sp,
                            SupportFile::eEqualFileSpecAndChecksumIfSet)) {
                      const bool abort_other_plans = false;
                      const RunMode stop_other_threads = RunMode::eAllThreads;
                      lldb::addr_t cur_pc = thread.GetStackFrameAtIndex(0)
                                                ->GetRegisterContext()
                                                ->GetPC();
                      AddressRange step_range(
                          cur_pc,
                          next_line_address.GetLoadAddress(&GetTarget()) -
                              cur_pc);

                      new_plan_sp = thread.QueueThreadPlanForStepOverRange(
                          abort_other_plans, step_range, sc, stop_other_threads,
                          m_status);
                      break;
                    }
                    look_ahead_step++;
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  // If we get to this point, we're not going to use a previously set "next
  // branch" breakpoint, so delete it:
  ClearNextBranchBreakpoint();

  // If we haven't figured out something to do yet, then ask the ShouldStopHere
  // callback:
  if (!new_plan_sp) {
    new_plan_sp = CheckShouldStopHereAndQueueStepOut(frame_order, m_status);
  }

  if (!new_plan_sp)
    m_no_more_plans = true;
  else {
    // Any new plan will be an implementation plan, so mark it private:
    new_plan_sp->SetPrivate(true);
    m_no_more_plans = false;
  }

  if (!new_plan_sp) {
    // For efficiencies sake, we know we're done here so we don't have to do
    // this calculation again in MischiefManaged.
    SetPlanComplete(m_status.Success());
    return true;
  } else
    return false;
}

bool ThreadPlanStepOverRange::DoPlanExplainsStop(Event *event_ptr) {
  // For crashes, breakpoint hits, signals, etc, let the base plan (or some
  // plan above us) handle the stop.  That way the user can see the stop, step
  // around, and then when they are done, continue and have their step
  // complete.  The exception is if we've hit our "run to next branch"
  // breakpoint. Note, unlike the step in range plan, we don't mark ourselves
  // complete if we hit an unexplained breakpoint/crash.

  Log *log = GetLog(LLDBLog::Step);
  StopInfoSP stop_info_sp = GetPrivateStopInfo();
  bool return_value;

  if (stop_info_sp) {
    StopReason reason = stop_info_sp->GetStopReason();

    if (reason == eStopReasonTrace) {
      return_value = true;
    } else if (reason == eStopReasonBreakpoint) {
      return_value = NextRangeBreakpointExplainsStop(stop_info_sp);
    } else {
      if (log)
        log->PutCString("ThreadPlanStepOverRange got asked if it explains the "
                        "stop for some reason other than step.");
      return_value = false;
    }
  } else
    return_value = true;

  return return_value;
}

bool ThreadPlanStepOverRange::DoWillResume(lldb::StateType resume_state,
                                           bool current_plan) {
  if (resume_state != eStateSuspended && m_first_resume) {
    m_first_resume = false;
    if (resume_state == eStateStepping && current_plan) {
      Thread &thread = GetThread();
      // See if we are about to step over an inlined call in the middle of the
      // inlined stack, if so figure out its extents and reset our range to
      // step over that.
      bool in_inlined_stack = thread.DecrementCurrentInlinedDepth();
      if (in_inlined_stack) {
        Log *log = GetLog(LLDBLog::Step);
        LLDB_LOGF(log,
                  "ThreadPlanStepInRange::DoWillResume: adjusting range to "
                  "the frame at inlined depth %d.",
                  thread.GetCurrentInlinedDepth());
        StackFrameSP stack_sp = thread.GetStackFrameAtIndex(0);
        if (stack_sp) {
          Block *frame_block = stack_sp->GetFrameBlock();
          lldb::addr_t curr_pc = thread.GetRegisterContext()->GetPC();
          AddressRange my_range;
          if (frame_block->GetRangeContainingLoadAddress(
                  curr_pc, m_process.GetTarget(), my_range)) {
            m_address_ranges.clear();
            m_address_ranges.push_back(my_range);
            if (log) {
              StreamString s;
              const InlineFunctionInfo *inline_info =
                  frame_block->GetInlinedFunctionInfo();
              const char *name;
              if (inline_info)
                name = inline_info->GetName().AsCString();
              else
                name = "<unknown-notinlined>";

              s.Printf(
                  "Stepping over inlined function \"%s\" in inlined stack: ",
                  name);
              DumpRanges(&s);
              log->PutString(s.GetString());
            }
          }
        }
      }
    }
  }

  return true;
}
