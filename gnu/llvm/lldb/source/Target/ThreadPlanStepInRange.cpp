//===-- ThreadPlanStepInRange.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Core/Architecture.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

uint32_t ThreadPlanStepInRange::s_default_flag_values =
    ThreadPlanShouldStopHere::eStepInAvoidNoDebug;

// ThreadPlanStepInRange: Step through a stack range, either stepping over or
// into based on the value of \a type.

ThreadPlanStepInRange::ThreadPlanStepInRange(
    Thread &thread, const AddressRange &range,
    const SymbolContext &addr_context, const char *step_into_target,
    lldb::RunMode stop_others, LazyBool step_in_avoids_code_without_debug_info,
    LazyBool step_out_avoids_code_without_debug_info)
    : ThreadPlanStepRange(ThreadPlan::eKindStepInRange,
                          "Step Range stepping in", thread, range, addr_context,
                          stop_others),
      ThreadPlanShouldStopHere(this), m_step_past_prologue(true),
      m_virtual_step(false), m_step_into_target(step_into_target) {
  SetCallbacks();
  SetFlagsToDefault();
  SetupAvoidNoDebug(step_in_avoids_code_without_debug_info,
                    step_out_avoids_code_without_debug_info);
}

ThreadPlanStepInRange::~ThreadPlanStepInRange() = default;

void ThreadPlanStepInRange::SetupAvoidNoDebug(
    LazyBool step_in_avoids_code_without_debug_info,
    LazyBool step_out_avoids_code_without_debug_info) {
  bool avoid_nodebug = true;
  Thread &thread = GetThread();
  switch (step_in_avoids_code_without_debug_info) {
  case eLazyBoolYes:
    avoid_nodebug = true;
    break;
  case eLazyBoolNo:
    avoid_nodebug = false;
    break;
  case eLazyBoolCalculate:
    avoid_nodebug = thread.GetStepInAvoidsNoDebug();
    break;
  }
  if (avoid_nodebug)
    GetFlags().Set(ThreadPlanShouldStopHere::eStepInAvoidNoDebug);
  else
    GetFlags().Clear(ThreadPlanShouldStopHere::eStepInAvoidNoDebug);

  switch (step_out_avoids_code_without_debug_info) {
  case eLazyBoolYes:
    avoid_nodebug = true;
    break;
  case eLazyBoolNo:
    avoid_nodebug = false;
    break;
  case eLazyBoolCalculate:
    avoid_nodebug = thread.GetStepOutAvoidsNoDebug();
    break;
  }
  if (avoid_nodebug)
    GetFlags().Set(ThreadPlanShouldStopHere::eStepOutAvoidNoDebug);
  else
    GetFlags().Clear(ThreadPlanShouldStopHere::eStepOutAvoidNoDebug);
}

void ThreadPlanStepInRange::GetDescription(Stream *s,
                                           lldb::DescriptionLevel level) {

  auto PrintFailureIfAny = [&]() {
    if (m_status.Success())
      return;
    s->Printf(" failed (%s)", m_status.AsCString());
  };

  if (level == lldb::eDescriptionLevelBrief) {
    s->Printf("step in");
    PrintFailureIfAny();
    return;
  }

  s->Printf("Stepping in");
  bool printed_line_info = false;
  if (m_addr_context.line_entry.IsValid()) {
    s->Printf(" through line ");
    m_addr_context.line_entry.DumpStopContext(s, false);
    printed_line_info = true;
  }

  const char *step_into_target = m_step_into_target.AsCString();
  if (step_into_target && step_into_target[0] != '\0')
    s->Printf(" targeting %s", m_step_into_target.AsCString());

  if (!printed_line_info || level == eDescriptionLevelVerbose) {
    s->Printf(" using ranges:");
    DumpRanges(s);
  }

  PrintFailureIfAny();

  s->PutChar('.');
}

bool ThreadPlanStepInRange::ShouldStop(Event *event_ptr) {
  Log *log = GetLog(LLDBLog::Step);

  if (log) {
    StreamString s;
    DumpAddress(s.AsRawOstream(), GetThread().GetRegisterContext()->GetPC(),
                GetTarget().GetArchitecture().GetAddressByteSize());
    LLDB_LOGF(log, "ThreadPlanStepInRange reached %s.", s.GetData());
  }

  if (IsPlanComplete())
    return true;

  m_no_more_plans = false;
  if (m_sub_plan_sp && m_sub_plan_sp->IsPlanComplete()) {
    if (!m_sub_plan_sp->PlanSucceeded()) {
      SetPlanComplete();
      m_no_more_plans = true;
      return true;
    } else
      m_sub_plan_sp.reset();
  }

  if (m_virtual_step) {
    // If we've just completed a virtual step, all we need to do is check for a
    // ShouldStopHere plan, and otherwise we're done.
    // FIXME - This can be both a step in and a step out.  Probably should
    // record which in the m_virtual_step.
    m_sub_plan_sp =
        CheckShouldStopHereAndQueueStepOut(eFrameCompareYounger, m_status);
  } else {
    // Stepping through should be done running other threads in general, since
    // we're setting a breakpoint and continuing.  So only stop others if we
    // are explicitly told to do so.

    bool stop_others = (m_stop_others == lldb::eOnlyThisThread);

    FrameComparison frame_order = CompareCurrentFrameToStartFrame();

    Thread &thread = GetThread();
    if (frame_order == eFrameCompareOlder ||
        frame_order == eFrameCompareSameParent) {
      // If we're in an older frame then we should stop.
      //
      // A caveat to this is if we think the frame is older but we're actually
      // in a trampoline.
      // I'm going to make the assumption that you wouldn't RETURN to a
      // trampoline.  So if we are in a trampoline we think the frame is older
      // because the trampoline confused the backtracer.
      m_sub_plan_sp = thread.QueueThreadPlanForStepThrough(
          m_stack_id, false, stop_others, m_status);
      if (!m_sub_plan_sp) {
        // Otherwise check the ShouldStopHere for step out:
        m_sub_plan_sp =
            CheckShouldStopHereAndQueueStepOut(frame_order, m_status);
        if (log) {
          if (m_sub_plan_sp)
            LLDB_LOGF(log,
                      "ShouldStopHere found plan to step out of this frame.");
          else
            LLDB_LOGF(log, "ShouldStopHere no plan to step out of this frame.");
        }
      } else if (log) {
        LLDB_LOGF(
            log, "Thought I stepped out, but in fact arrived at a trampoline.");
      }
    } else if (frame_order == eFrameCompareEqual && InSymbol()) {
      // If we are not in a place we should step through, we're done. One
      // tricky bit here is that some stubs don't push a frame, so we have to
      // check both the case of a frame that is younger, or the same as this
      // frame. However, if the frame is the same, and we are still in the
      // symbol we started in, the we don't need to do this.  This first check
      // isn't strictly necessary, but it is more efficient.

      // If we're still in the range, keep going, either by running to the next
      // branch breakpoint, or by stepping.
      if (InRange()) {
        SetNextBranchBreakpoint();
        return false;
      }

      SetPlanComplete();
      m_no_more_plans = true;
      return true;
    }

    // If we get to this point, we're not going to use a previously set "next
    // branch" breakpoint, so delete it:
    ClearNextBranchBreakpoint();

    // We may have set the plan up above in the FrameIsOlder section:

    if (!m_sub_plan_sp)
      m_sub_plan_sp = thread.QueueThreadPlanForStepThrough(
          m_stack_id, false, stop_others, m_status);

    if (log) {
      if (m_sub_plan_sp)
        LLDB_LOGF(log, "Found a step through plan: %s",
                  m_sub_plan_sp->GetName());
      else
        LLDB_LOGF(log, "No step through plan found.");
    }

    // If not, give the "should_stop" callback a chance to push a plan to get
    // us out of here. But only do that if we actually have stepped in.
    if (!m_sub_plan_sp && frame_order == eFrameCompareYounger)
      m_sub_plan_sp = CheckShouldStopHereAndQueueStepOut(frame_order, m_status);

    // If we've stepped in and we are going to stop here, check to see if we
    // were asked to run past the prologue, and if so do that.

    if (!m_sub_plan_sp && frame_order == eFrameCompareYounger &&
        m_step_past_prologue) {
      lldb::StackFrameSP curr_frame = thread.GetStackFrameAtIndex(0);
      if (curr_frame) {
        size_t bytes_to_skip = 0;
        lldb::addr_t curr_addr = thread.GetRegisterContext()->GetPC();
        Address func_start_address;

        SymbolContext sc = curr_frame->GetSymbolContext(eSymbolContextFunction |
                                                        eSymbolContextSymbol);

        if (sc.function) {
          func_start_address = sc.function->GetAddressRange().GetBaseAddress();
          if (curr_addr == func_start_address.GetLoadAddress(&GetTarget()))
            bytes_to_skip = sc.function->GetPrologueByteSize();
        } else if (sc.symbol) {
          func_start_address = sc.symbol->GetAddress();
          if (curr_addr == func_start_address.GetLoadAddress(&GetTarget()))
            bytes_to_skip = sc.symbol->GetPrologueByteSize();
        }

        if (bytes_to_skip == 0 && sc.symbol) {
          const Architecture *arch = GetTarget().GetArchitecturePlugin();
          if (arch) {
            Address curr_sec_addr;
            GetTarget().GetSectionLoadList().ResolveLoadAddress(curr_addr,
                                                                curr_sec_addr);
            bytes_to_skip = arch->GetBytesToSkip(*sc.symbol, curr_sec_addr);
          }
        }

        if (bytes_to_skip != 0) {
          func_start_address.Slide(bytes_to_skip);
          log = GetLog(LLDBLog::Step);
          LLDB_LOGF(log, "Pushing past prologue ");

          m_sub_plan_sp = thread.QueueThreadPlanForRunToAddress(
              false, func_start_address, true, m_status);
        }
      }
    }
  }

  if (!m_sub_plan_sp) {
    m_no_more_plans = true;
    SetPlanComplete();
    return true;
  } else {
    m_no_more_plans = false;
    m_sub_plan_sp->SetPrivate(true);
    return false;
  }
}

void ThreadPlanStepInRange::SetAvoidRegexp(const char *name) {
  if (m_avoid_regexp_up)
    *m_avoid_regexp_up = RegularExpression(name);
  else
    m_avoid_regexp_up = std::make_unique<RegularExpression>(name);
}

void ThreadPlanStepInRange::SetDefaultFlagValue(uint32_t new_value) {
  // TODO: Should we test this for sanity?
  ThreadPlanStepInRange::s_default_flag_values = new_value;
}

bool ThreadPlanStepInRange::FrameMatchesAvoidCriteria() {
  StackFrame *frame = GetThread().GetStackFrameAtIndex(0).get();

  // Check the library list first, as that's cheapest:
  bool libraries_say_avoid = false;

  FileSpecList libraries_to_avoid(GetThread().GetLibrariesToAvoid());
  size_t num_libraries = libraries_to_avoid.GetSize();
  if (num_libraries > 0) {
    SymbolContext sc(frame->GetSymbolContext(eSymbolContextModule));
    FileSpec frame_library(sc.module_sp->GetFileSpec());

    if (frame_library) {
      for (size_t i = 0; i < num_libraries; i++) {
        const FileSpec &file_spec(libraries_to_avoid.GetFileSpecAtIndex(i));
        if (FileSpec::Match(file_spec, frame_library)) {
          libraries_say_avoid = true;
          break;
        }
      }
    }
  }
  if (libraries_say_avoid)
    return true;

  const RegularExpression *avoid_regexp_to_use = m_avoid_regexp_up.get();
  if (avoid_regexp_to_use == nullptr)
    avoid_regexp_to_use = GetThread().GetSymbolsToAvoidRegexp();

  if (avoid_regexp_to_use != nullptr) {
    SymbolContext sc = frame->GetSymbolContext(
        eSymbolContextFunction | eSymbolContextBlock | eSymbolContextSymbol);
    if (sc.symbol != nullptr) {
      const char *frame_function_name =
          sc.GetFunctionName(Mangled::ePreferDemangledWithoutArguments)
              .GetCString();
      if (frame_function_name) {
        bool return_value = avoid_regexp_to_use->Execute(frame_function_name);
        if (return_value) {
          LLDB_LOGF(GetLog(LLDBLog::Step),
                    "Stepping out of function \"%s\" because it matches the "
                    "avoid regexp \"%s\".",
                    frame_function_name,
                    avoid_regexp_to_use->GetText().str().c_str());
        }
        return return_value;
      }
    }
  }
  return false;
}

bool ThreadPlanStepInRange::DefaultShouldStopHereCallback(
    ThreadPlan *current_plan, Flags &flags, FrameComparison operation,
    Status &status, void *baton) {
  bool should_stop_here = true;
  StackFrame *frame = current_plan->GetThread().GetStackFrameAtIndex(0).get();
  Log *log = GetLog(LLDBLog::Step);

  // First see if the ThreadPlanShouldStopHere default implementation thinks we
  // should get out of here:
  should_stop_here = ThreadPlanShouldStopHere::DefaultShouldStopHereCallback(
      current_plan, flags, operation, status, baton);
  if (!should_stop_here)
    return false;

  if (should_stop_here && current_plan->GetKind() == eKindStepInRange &&
      operation == eFrameCompareYounger) {
    ThreadPlanStepInRange *step_in_range_plan =
        static_cast<ThreadPlanStepInRange *>(current_plan);
    if (step_in_range_plan->m_step_into_target) {
      SymbolContext sc = frame->GetSymbolContext(
          eSymbolContextFunction | eSymbolContextBlock | eSymbolContextSymbol);
      if (sc.symbol != nullptr) {
        // First try an exact match, since that's cheap with ConstStrings.
        // Then do a strstr compare.
        if (step_in_range_plan->m_step_into_target == sc.GetFunctionName()) {
          should_stop_here = true;
        } else {
          const char *target_name =
              step_in_range_plan->m_step_into_target.AsCString();
          const char *function_name = sc.GetFunctionName().AsCString();

          if (function_name == nullptr)
            should_stop_here = false;
          else if (strstr(function_name, target_name) == nullptr)
            should_stop_here = false;
        }
        if (log && !should_stop_here)
          LLDB_LOGF(log,
                    "Stepping out of frame %s which did not match step into "
                    "target %s.",
                    sc.GetFunctionName().AsCString(),
                    step_in_range_plan->m_step_into_target.AsCString());
      }
    }

    if (should_stop_here) {
      ThreadPlanStepInRange *step_in_range_plan =
          static_cast<ThreadPlanStepInRange *>(current_plan);
      // Don't log the should_step_out here, it's easier to do it in
      // FrameMatchesAvoidCriteria.
      should_stop_here = !step_in_range_plan->FrameMatchesAvoidCriteria();
    }
  }

  return should_stop_here;
}

bool ThreadPlanStepInRange::DoPlanExplainsStop(Event *event_ptr) {
  // We always explain a stop.  Either we've just done a single step, in which
  // case we'll do our ordinary processing, or we stopped for some reason that
  // isn't handled by our sub-plans, in which case we want to just stop right
  // away. In general, we don't want to mark the plan as complete for
  // unexplained stops. For instance, if you step in to some code with no debug
  // info, so you step out and in the course of that hit a breakpoint, then you
  // want to stop & show the user the breakpoint, but not unship the step in
  // plan, since you still may want to complete that plan when you continue.
  // This is particularly true when doing "step in to target function."
  // stepping.
  //
  // The only variation is that if we are doing "step by running to next
  // branch" in which case if we hit our branch breakpoint we don't set the
  // plan to complete.

  bool return_value = false;

  if (m_virtual_step) {
    return_value = true;
  } else {
    StopInfoSP stop_info_sp = GetPrivateStopInfo();
    if (stop_info_sp) {
      StopReason reason = stop_info_sp->GetStopReason();

      if (reason == eStopReasonBreakpoint) {
        if (NextRangeBreakpointExplainsStop(stop_info_sp)) {
          return_value = true;
        }
      } else if (IsUsuallyUnexplainedStopReason(reason)) {
        Log *log = GetLog(LLDBLog::Step);
        if (log)
          log->PutCString("ThreadPlanStepInRange got asked if it explains the "
                          "stop for some reason other than step.");
        return_value = false;
      } else {
        return_value = true;
      }
    } else
      return_value = true;
  }

  return return_value;
}

bool ThreadPlanStepInRange::DoWillResume(lldb::StateType resume_state,
                                         bool current_plan) {
  m_virtual_step = false;
  if (resume_state == eStateStepping && current_plan) {
    Thread &thread = GetThread();
    // See if we are about to step over a virtual inlined call.
    bool step_without_resume = thread.DecrementCurrentInlinedDepth();
    if (step_without_resume) {
      Log *log = GetLog(LLDBLog::Step);
      LLDB_LOGF(log,
                "ThreadPlanStepInRange::DoWillResume: returning false, "
                "inline_depth: %d",
                thread.GetCurrentInlinedDepth());
      SetStopInfo(StopInfo::CreateStopReasonToTrace(thread));

      // FIXME: Maybe it would be better to create a InlineStep stop reason, but
      // then
      // the whole rest of the world would have to handle that stop reason.
      m_virtual_step = true;
    }
    return !step_without_resume;
  }
  return true;
}

bool ThreadPlanStepInRange::IsVirtualStep() { return m_virtual_step; }
