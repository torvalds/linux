//===-- ThreadPlanStepRange.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepRange.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

// ThreadPlanStepRange: Step through a stack range, either stepping over or
// into based on the value of \a type.

ThreadPlanStepRange::ThreadPlanStepRange(ThreadPlanKind kind, const char *name,
                                         Thread &thread,
                                         const AddressRange &range,
                                         const SymbolContext &addr_context,
                                         lldb::RunMode stop_others,
                                         bool given_ranges_only)
    : ThreadPlan(kind, name, thread, eVoteNoOpinion, eVoteNoOpinion),
      m_addr_context(addr_context), m_address_ranges(),
      m_stop_others(stop_others), m_stack_id(), m_parent_stack_id(),
      m_no_more_plans(false), m_first_run_event(true), m_use_fast_step(false),
      m_given_ranges_only(given_ranges_only) {
  m_use_fast_step = GetTarget().GetUseFastStepping();
  AddRange(range);
  m_stack_id = thread.GetStackFrameAtIndex(0)->GetStackID();
  StackFrameSP parent_stack = thread.GetStackFrameAtIndex(1);
  if (parent_stack)
    m_parent_stack_id = parent_stack->GetStackID();
}

ThreadPlanStepRange::~ThreadPlanStepRange() { ClearNextBranchBreakpoint(); }

void ThreadPlanStepRange::DidPush() {
  // See if we can find a "next range" breakpoint:
  SetNextBranchBreakpoint();
}

bool ThreadPlanStepRange::ValidatePlan(Stream *error) {
  if (m_could_not_resolve_hw_bp) {
    if (error)
      error->PutCString(
          "Could not create hardware breakpoint for thread plan.");
    return false;
  }
  return true;
}

Vote ThreadPlanStepRange::ShouldReportStop(Event *event_ptr) {
  Log *log = GetLog(LLDBLog::Step);

  const Vote vote = IsPlanComplete() ? eVoteYes : eVoteNo;
  LLDB_LOGF(log, "ThreadPlanStepRange::ShouldReportStop() returning vote %i\n",
            vote);
  return vote;
}

void ThreadPlanStepRange::AddRange(const AddressRange &new_range) {
  // For now I'm just adding the ranges.  At some point we may want to condense
  // the ranges if they overlap, though I don't think it is likely to be very
  // important.
  m_address_ranges.push_back(new_range);

  // Fill the slot for this address range with an empty DisassemblerSP in the
  // instruction ranges. I want the indices to match, but I don't want to do
  // the work to disassemble this range if I don't step into it.
  m_instruction_ranges.push_back(DisassemblerSP());
}

void ThreadPlanStepRange::DumpRanges(Stream *s) {
  size_t num_ranges = m_address_ranges.size();
  if (num_ranges == 1) {
    m_address_ranges[0].Dump(s, &GetTarget(), Address::DumpStyleLoadAddress);
  } else {
    for (size_t i = 0; i < num_ranges; i++) {
      s->Printf(" %" PRIu64 ": ", uint64_t(i));
      m_address_ranges[i].Dump(s, &GetTarget(), Address::DumpStyleLoadAddress);
    }
  }
}

bool ThreadPlanStepRange::InRange() {
  Log *log = GetLog(LLDBLog::Step);
  bool ret_value = false;
  Thread &thread = GetThread();
  lldb::addr_t pc_load_addr = thread.GetRegisterContext()->GetPC();

  size_t num_ranges = m_address_ranges.size();
  for (size_t i = 0; i < num_ranges; i++) {
    ret_value = 
        m_address_ranges[i].ContainsLoadAddress(pc_load_addr, &GetTarget());
    if (ret_value)
      break;
  }

  if (!ret_value && !m_given_ranges_only) {
    // See if we've just stepped to another part of the same line number...
    StackFrame *frame = thread.GetStackFrameAtIndex(0).get();

    SymbolContext new_context(
        frame->GetSymbolContext(eSymbolContextEverything));
    if (m_addr_context.line_entry.IsValid() &&
        new_context.line_entry.IsValid()) {
      if (m_addr_context.line_entry.original_file_sp->Equal(
              *new_context.line_entry.original_file_sp,
              SupportFile::eEqualFileSpecAndChecksumIfSet)) {
        if (m_addr_context.line_entry.line == new_context.line_entry.line) {
          m_addr_context = new_context;
          const bool include_inlined_functions =
              GetKind() == eKindStepOverRange;
          AddRange(m_addr_context.line_entry.GetSameLineContiguousAddressRange(
              include_inlined_functions));
          ret_value = true;
          if (log) {
            StreamString s;
            m_addr_context.line_entry.Dump(&s, &GetTarget(), true,
                                           Address::DumpStyleLoadAddress,
                                           Address::DumpStyleLoadAddress, true);

            LLDB_LOGF(
                log,
                "Step range plan stepped to another range of same line: %s",
                s.GetData());
          }
        } else if (new_context.line_entry.line == 0) {
          new_context.line_entry.line = m_addr_context.line_entry.line;
          m_addr_context = new_context;
          const bool include_inlined_functions =
              GetKind() == eKindStepOverRange;
          AddRange(m_addr_context.line_entry.GetSameLineContiguousAddressRange(
              include_inlined_functions));
          ret_value = true;
          if (log) {
            StreamString s;
            m_addr_context.line_entry.Dump(&s, &GetTarget(), true,
                                           Address::DumpStyleLoadAddress,
                                           Address::DumpStyleLoadAddress, true);

            LLDB_LOGF(log,
                      "Step range plan stepped to a range at linenumber 0 "
                      "stepping through that range: %s",
                      s.GetData());
          }
        } else if (new_context.line_entry.range.GetBaseAddress().GetLoadAddress(
                       &GetTarget()) != pc_load_addr) {
          // Another thing that sometimes happens here is that we step out of
          // one line into the MIDDLE of another line.  So far I mostly see
          // this due to bugs in the debug information. But we probably don't
          // want to be in the middle of a line range, so in that case reset
          // the stepping range to the line we've stepped into the middle of
          // and continue.
          m_addr_context = new_context;
          m_address_ranges.clear();
          AddRange(m_addr_context.line_entry.range);
          ret_value = true;
          if (log) {
            StreamString s;
            m_addr_context.line_entry.Dump(&s, &GetTarget(), true,
                                           Address::DumpStyleLoadAddress,
                                           Address::DumpStyleLoadAddress, true);

            LLDB_LOGF(log,
                      "Step range plan stepped to the middle of new "
                      "line(%d): %s, continuing to clear this line.",
                      new_context.line_entry.line, s.GetData());
          }
        }
      }
    }
  }

  if (!ret_value && log)
    LLDB_LOGF(log, "Step range plan out of range to 0x%" PRIx64, pc_load_addr);

  return ret_value;
}

bool ThreadPlanStepRange::InSymbol() {
  lldb::addr_t cur_pc = GetThread().GetRegisterContext()->GetPC();
  if (m_addr_context.function != nullptr) {
    return m_addr_context.function->GetAddressRange().ContainsLoadAddress(
        cur_pc, &GetTarget());
  } else if (m_addr_context.symbol && m_addr_context.symbol->ValueIsAddress()) {
    AddressRange range(m_addr_context.symbol->GetAddressRef(),
                       m_addr_context.symbol->GetByteSize());
    return range.ContainsLoadAddress(cur_pc, &GetTarget());
  }
  return false;
}

// FIXME: This should also handle inlining if we aren't going to do inlining in
// the
// main stack.
//
// Ideally we should remember the whole stack frame list, and then compare that
// to the current list.

lldb::FrameComparison ThreadPlanStepRange::CompareCurrentFrameToStartFrame() {
  FrameComparison frame_order;
  Thread &thread = GetThread();
  StackID cur_frame_id = thread.GetStackFrameAtIndex(0)->GetStackID();

  if (cur_frame_id == m_stack_id) {
    frame_order = eFrameCompareEqual;
  } else if (cur_frame_id < m_stack_id) {
    frame_order = eFrameCompareYounger;
  } else {
    StackFrameSP cur_parent_frame = thread.GetStackFrameAtIndex(1);
    StackID cur_parent_id;
    if (cur_parent_frame)
      cur_parent_id = cur_parent_frame->GetStackID();
    if (m_parent_stack_id.IsValid() && cur_parent_id.IsValid() &&
        m_parent_stack_id == cur_parent_id)
      frame_order = eFrameCompareSameParent;
    else
      frame_order = eFrameCompareOlder;
  }
  return frame_order;
}

bool ThreadPlanStepRange::StopOthers() {
  switch (m_stop_others) {
  case lldb::eOnlyThisThread:
    return true;
  case lldb::eOnlyDuringStepping:
    // If there is a call in the range of the next branch breakpoint,
    // then we should always run all threads, since a call can execute
    // arbitrary code which might for instance take a lock that's held
    // by another thread.
    return !m_found_calls;
  case lldb::eAllThreads:
    return false;
  }
  llvm_unreachable("Unhandled run mode!");
}

InstructionList *ThreadPlanStepRange::GetInstructionsForAddress(
    lldb::addr_t addr, size_t &range_index, size_t &insn_offset) {
  size_t num_ranges = m_address_ranges.size();
  for (size_t i = 0; i < num_ranges; i++) {
    if (m_address_ranges[i].ContainsLoadAddress(addr, &GetTarget())) {
      // Some joker added a zero size range to the stepping range...
      if (m_address_ranges[i].GetByteSize() == 0)
        return nullptr;

      if (!m_instruction_ranges[i]) {
        // Disassemble the address range given:
        const char *plugin_name = nullptr;
        const char *flavor = nullptr;
        m_instruction_ranges[i] = Disassembler::DisassembleRange(
            GetTarget().GetArchitecture(), plugin_name, flavor, GetTarget(),
            m_address_ranges[i]);
      }
      if (!m_instruction_ranges[i])
        return nullptr;
      else {
        // Find where we are in the instruction list as well.  If we aren't at
        // an instruction, return nullptr. In this case, we're probably lost,
        // and shouldn't try to do anything fancy.

        insn_offset =
            m_instruction_ranges[i]
                ->GetInstructionList()
                .GetIndexOfInstructionAtLoadAddress(addr, GetTarget());
        if (insn_offset == UINT32_MAX)
          return nullptr;
        else {
          range_index = i;
          return &m_instruction_ranges[i]->GetInstructionList();
        }
      }
    }
  }
  return nullptr;
}

void ThreadPlanStepRange::ClearNextBranchBreakpoint() {
  if (m_next_branch_bp_sp) {
    Log *log = GetLog(LLDBLog::Step);
    LLDB_LOGF(log, "Removing next branch breakpoint: %d.",
              m_next_branch_bp_sp->GetID());
    GetTarget().RemoveBreakpointByID(m_next_branch_bp_sp->GetID());
    m_next_branch_bp_sp.reset();
    m_could_not_resolve_hw_bp = false;
    m_found_calls = false;
  }
}

bool ThreadPlanStepRange::SetNextBranchBreakpoint() {
  if (m_next_branch_bp_sp)
    return true;

  Log *log = GetLog(LLDBLog::Step);
  // Stepping through ranges using breakpoints doesn't work yet, but with this
  // off we fall back to instruction single stepping.
  if (!m_use_fast_step)
    return false;

  // clear the m_found_calls, we'll rediscover it for this range.
  m_found_calls = false;
  
  lldb::addr_t cur_addr = GetThread().GetRegisterContext()->GetPC();
  // Find the current address in our address ranges, and fetch the disassembly
  // if we haven't already:
  size_t pc_index;
  size_t range_index;
  InstructionList *instructions =
      GetInstructionsForAddress(cur_addr, range_index, pc_index);
  if (instructions == nullptr)
    return false;
  else {
    const bool ignore_calls = GetKind() == eKindStepOverRange;
    uint32_t branch_index = instructions->GetIndexOfNextBranchInstruction(
        pc_index, ignore_calls, &m_found_calls);
    Address run_to_address;

    // If we didn't find a branch, run to the end of the range.
    if (branch_index == UINT32_MAX) {
      uint32_t last_index = instructions->GetSize() - 1;
      if (last_index - pc_index > 1) {
        InstructionSP last_inst =
            instructions->GetInstructionAtIndex(last_index);
        size_t last_inst_size = last_inst->GetOpcode().GetByteSize();
        run_to_address = last_inst->GetAddress();
        run_to_address.Slide(last_inst_size);
      }
    } else if (branch_index - pc_index > 1) {
      run_to_address =
          instructions->GetInstructionAtIndex(branch_index)->GetAddress();
    }

    if (run_to_address.IsValid()) {
      const bool is_internal = true;
      m_next_branch_bp_sp =
          GetTarget().CreateBreakpoint(run_to_address, is_internal, false);
      if (m_next_branch_bp_sp) {

        if (m_next_branch_bp_sp->IsHardware() &&
            !m_next_branch_bp_sp->HasResolvedLocations())
          m_could_not_resolve_hw_bp = true;

        if (log) {
          lldb::break_id_t bp_site_id = LLDB_INVALID_BREAK_ID;
          BreakpointLocationSP bp_loc =
              m_next_branch_bp_sp->GetLocationAtIndex(0);
          if (bp_loc) {
            BreakpointSiteSP bp_site = bp_loc->GetBreakpointSite();
            if (bp_site) {
              bp_site_id = bp_site->GetID();
            }
          }
          LLDB_LOGF(log,
                    "ThreadPlanStepRange::SetNextBranchBreakpoint - Setting "
                    "breakpoint %d (site %d) to run to address 0x%" PRIx64,
                    m_next_branch_bp_sp->GetID(), bp_site_id,
                    run_to_address.GetLoadAddress(&m_process.GetTarget()));
        }

        m_next_branch_bp_sp->SetThreadID(m_tid);
        m_next_branch_bp_sp->SetBreakpointKind("next-branch-location");

        return true;
      } else
        return false;
    }
  }
  return false;
}

bool ThreadPlanStepRange::NextRangeBreakpointExplainsStop(
    lldb::StopInfoSP stop_info_sp) {
  Log *log = GetLog(LLDBLog::Step);
  if (!m_next_branch_bp_sp)
    return false;

  break_id_t bp_site_id = stop_info_sp->GetValue();
  BreakpointSiteSP bp_site_sp =
      m_process.GetBreakpointSiteList().FindByID(bp_site_id);
  if (!bp_site_sp)
    return false;
  else if (!bp_site_sp->IsBreakpointAtThisSite(m_next_branch_bp_sp->GetID()))
    return false;
  else {
    // If we've hit the next branch breakpoint, then clear it.
    size_t num_constituents = bp_site_sp->GetNumberOfConstituents();
    bool explains_stop = true;
    // If all the constituents are internal, then we are probably just stepping
    // over this range from multiple threads, or multiple frames, so we want to
    // continue.  If one is not internal, then we should not explain the stop,
    // and let the user breakpoint handle the stop.
    for (size_t i = 0; i < num_constituents; i++) {
      if (!bp_site_sp->GetConstituentAtIndex(i)->GetBreakpoint().IsInternal()) {
        explains_stop = false;
        break;
      }
    }
    LLDB_LOGF(log,
              "ThreadPlanStepRange::NextRangeBreakpointExplainsStop - Hit "
              "next range breakpoint which has %" PRIu64
              " constituents - explains stop: %u.",
              (uint64_t)num_constituents, explains_stop);
    ClearNextBranchBreakpoint();
    return explains_stop;
  }
}

bool ThreadPlanStepRange::WillStop() { return true; }

StateType ThreadPlanStepRange::GetPlanRunState() {
  if (m_next_branch_bp_sp)
    return eStateRunning;
  else
    return eStateStepping;
}

bool ThreadPlanStepRange::MischiefManaged() {
  // If we have pushed some plans between ShouldStop & MischiefManaged, then
  // we're not done...
  // I do this check first because we might have stepped somewhere that will
  // fool InRange into
  // thinking it needs to step past the end of that line.  This happens, for
  // instance, when stepping over inlined code that is in the middle of the
  // current line.

  if (!m_no_more_plans)
    return false;

  bool done = true;
  if (!IsPlanComplete()) {
    if (InRange()) {
      done = false;
    } else {
      FrameComparison frame_order = CompareCurrentFrameToStartFrame();
      done = (frame_order != eFrameCompareOlder) ? m_no_more_plans : true;
    }
  }

  if (done) {
    Log *log = GetLog(LLDBLog::Step);
    LLDB_LOGF(log, "Completed step through range plan.");
    ClearNextBranchBreakpoint();
    ThreadPlan::MischiefManaged();
    return true;
  } else {
    return false;
  }
}

bool ThreadPlanStepRange::IsPlanStale() {
  Log *log = GetLog(LLDBLog::Step);
  FrameComparison frame_order = CompareCurrentFrameToStartFrame();

  if (frame_order == eFrameCompareOlder) {
    if (log) {
      LLDB_LOGF(log, "ThreadPlanStepRange::IsPlanStale returning true, we've "
                     "stepped out.");
    }
    return true;
  } else if (frame_order == eFrameCompareEqual && InSymbol()) {
    // If we are not in a place we should step through, we've gotten stale. One
    // tricky bit here is that some stubs don't push a frame, so we should.
    // check that we are in the same symbol.
    if (!InRange()) {
      // Set plan Complete when we reach next instruction just after the range
      lldb::addr_t addr = GetThread().GetRegisterContext()->GetPC() - 1;
      size_t num_ranges = m_address_ranges.size();
      for (size_t i = 0; i < num_ranges; i++) {
        bool in_range = 
            m_address_ranges[i].ContainsLoadAddress(addr, &GetTarget());
        if (in_range) {
          SetPlanComplete();
        }
      }
      return true;
    }
  }
  return false;
}
