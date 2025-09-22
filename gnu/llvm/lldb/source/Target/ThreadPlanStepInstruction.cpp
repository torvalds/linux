//===-- ThreadPlanStepInstruction.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

// ThreadPlanStepInstruction: Step over the current instruction

ThreadPlanStepInstruction::ThreadPlanStepInstruction(Thread &thread,
                                                     bool step_over,
                                                     bool stop_other_threads,
                                                     Vote report_stop_vote,
                                                     Vote report_run_vote)
    : ThreadPlan(ThreadPlan::eKindStepInstruction,
                 "Step over single instruction", thread, report_stop_vote,
                 report_run_vote),
      m_instruction_addr(0), m_stop_other_threads(stop_other_threads),
      m_step_over(step_over) {
  m_takes_iteration_count = true;
  SetUpState();
}

ThreadPlanStepInstruction::~ThreadPlanStepInstruction() = default;

void ThreadPlanStepInstruction::SetUpState() {
  Thread &thread = GetThread();
  m_instruction_addr = thread.GetRegisterContext()->GetPC(0);
  StackFrameSP start_frame_sp(thread.GetStackFrameAtIndex(0));
  m_stack_id = start_frame_sp->GetStackID();

  m_start_has_symbol =
      start_frame_sp->GetSymbolContext(eSymbolContextSymbol).symbol != nullptr;

  StackFrameSP parent_frame_sp = thread.GetStackFrameAtIndex(1);
  if (parent_frame_sp)
    m_parent_frame_id = parent_frame_sp->GetStackID();
}

void ThreadPlanStepInstruction::GetDescription(Stream *s,
                                               lldb::DescriptionLevel level) {
  auto PrintFailureIfAny = [&]() {
    if (m_status.Success())
      return;
    s->Printf(" failed (%s)", m_status.AsCString());
  };

  if (level == lldb::eDescriptionLevelBrief) {
    if (m_step_over)
      s->Printf("instruction step over");
    else
      s->Printf("instruction step into");

    PrintFailureIfAny();
  } else {
    s->Printf("Stepping one instruction past ");
    DumpAddress(s->AsRawOstream(), m_instruction_addr, sizeof(addr_t));
    if (!m_start_has_symbol)
      s->Printf(" which has no symbol");

    if (m_step_over)
      s->Printf(" stepping over calls");
    else
      s->Printf(" stepping into calls");

    PrintFailureIfAny();
  }
}

bool ThreadPlanStepInstruction::ValidatePlan(Stream *error) {
  // Since we read the instruction we're stepping over from the thread, this
  // plan will always work.
  return true;
}

bool ThreadPlanStepInstruction::DoPlanExplainsStop(Event *event_ptr) {
  StopInfoSP stop_info_sp = GetPrivateStopInfo();
  if (stop_info_sp) {
    StopReason reason = stop_info_sp->GetStopReason();
    return (reason == eStopReasonTrace || reason == eStopReasonNone);
  }
  return false;
}

bool ThreadPlanStepInstruction::IsPlanStale() {
  Log *log = GetLog(LLDBLog::Step);
  Thread &thread = GetThread();
  StackID cur_frame_id = thread.GetStackFrameAtIndex(0)->GetStackID();
  if (cur_frame_id == m_stack_id) {
    // Set plan Complete when we reach next instruction
    uint64_t pc = thread.GetRegisterContext()->GetPC(0);
    uint32_t max_opcode_size =
        GetTarget().GetArchitecture().GetMaximumOpcodeByteSize();
    bool next_instruction_reached = (pc > m_instruction_addr) &&
        (pc <= m_instruction_addr + max_opcode_size);
    if (next_instruction_reached) {
      SetPlanComplete();
    }
    return (thread.GetRegisterContext()->GetPC(0) != m_instruction_addr);
  } else if (cur_frame_id < m_stack_id) {
    // If the current frame is younger than the start frame and we are stepping
    // over, then we need to continue, but if we are doing just one step, we're
    // done.
    return !m_step_over;
  } else {
    if (log) {
      LLDB_LOGF(log,
                "ThreadPlanStepInstruction::IsPlanStale - Current frame is "
                "older than start frame, plan is stale.");
    }
    return true;
  }
}

bool ThreadPlanStepInstruction::ShouldStop(Event *event_ptr) {
  Thread &thread = GetThread();
  if (m_step_over) {
    Log *log = GetLog(LLDBLog::Step);
    StackFrameSP cur_frame_sp = thread.GetStackFrameAtIndex(0);
    if (!cur_frame_sp) {
      LLDB_LOGF(
          log,
          "ThreadPlanStepInstruction couldn't get the 0th frame, stopping.");
      SetPlanComplete();
      return true;
    }

    StackID cur_frame_zero_id = cur_frame_sp->GetStackID();

    if (cur_frame_zero_id == m_stack_id || m_stack_id < cur_frame_zero_id) {
      if (thread.GetRegisterContext()->GetPC(0) != m_instruction_addr) {
        if (--m_iteration_count <= 0) {
          SetPlanComplete();
          return true;
        } else {
          // We are still stepping, reset the start pc, and in case we've
          // stepped out, reset the current stack id.
          SetUpState();
          return false;
        }
      } else
        return false;
    } else {
      // We've stepped in, step back out again:
      StackFrame *return_frame = thread.GetStackFrameAtIndex(1).get();
      if (return_frame) {
        if (return_frame->GetStackID() != m_parent_frame_id ||
            m_start_has_symbol) {
          // next-instruction shouldn't step out of inlined functions.  But we
          // may have stepped into a real function that starts with an inlined
          // function, and we do want to step out of that...

          if (cur_frame_sp->IsInlined()) {
            StackFrameSP parent_frame_sp =
                thread.GetFrameWithStackID(m_stack_id);

            if (parent_frame_sp &&
                parent_frame_sp->GetConcreteFrameIndex() ==
                    cur_frame_sp->GetConcreteFrameIndex()) {
              SetPlanComplete();
              if (log) {
                LLDB_LOGF(log,
                          "Frame we stepped into is inlined into the frame "
                          "we were stepping from, stopping.");
              }
              return true;
            }
          }

          if (log) {
            StreamString s;
            s.PutCString("Stepped in to: ");
            addr_t stop_addr =
                thread.GetStackFrameAtIndex(0)->GetRegisterContext()->GetPC();
            DumpAddress(s.AsRawOstream(), stop_addr,
                        GetTarget().GetArchitecture().GetAddressByteSize());
            s.PutCString(" stepping out to: ");
            addr_t return_addr = return_frame->GetRegisterContext()->GetPC();
            DumpAddress(s.AsRawOstream(), return_addr,
                        GetTarget().GetArchitecture().GetAddressByteSize());
            LLDB_LOGF(log, "%s.", s.GetData());
          }

          // StepInstruction should probably have the tri-state RunMode, but
          // for now it is safer to run others.
          const bool stop_others = false;
          thread.QueueThreadPlanForStepOutNoShouldStop(
              false, nullptr, true, stop_others, eVoteNo, eVoteNoOpinion, 0,
              m_status);
          return false;
        } else {
          if (log) {
            log->PutCString(
                "The stack id we are stepping in changed, but our parent frame "
                "did not when stepping from code with no symbols.  "
                "We are probably just confused about where we are, stopping.");
          }
          SetPlanComplete();
          return true;
        }
      } else {
        LLDB_LOGF(log, "Could not find previous frame, stopping.");
        SetPlanComplete();
        return true;
      }
    }
  } else {
    lldb::addr_t pc_addr = thread.GetRegisterContext()->GetPC(0);
    if (pc_addr != m_instruction_addr) {
      if (--m_iteration_count <= 0) {
        SetPlanComplete();
        return true;
      } else {
        // We are still stepping, reset the start pc, and in case we've stepped
        // in or out, reset the current stack id.
        SetUpState();
        return false;
      }
    } else
      return false;
  }
}

bool ThreadPlanStepInstruction::StopOthers() { return m_stop_other_threads; }

StateType ThreadPlanStepInstruction::GetPlanRunState() {
  return eStateStepping;
}

bool ThreadPlanStepInstruction::WillStop() { return true; }

bool ThreadPlanStepInstruction::MischiefManaged() {
  if (IsPlanComplete()) {
    Log *log = GetLog(LLDBLog::Step);
    LLDB_LOGF(log, "Completed single instruction step plan.");
    ThreadPlan::MischiefManaged();
    return true;
  } else {
    return false;
  }
}
