//===-- AppleThreadPlanStepThroughObjCTrampoline.cpp-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AppleThreadPlanStepThroughObjCTrampoline.h"

#include "AppleObjCTrampolineHandler.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

// ThreadPlanStepThroughObjCTrampoline constructor
AppleThreadPlanStepThroughObjCTrampoline::
    AppleThreadPlanStepThroughObjCTrampoline(
        Thread &thread, AppleObjCTrampolineHandler &trampoline_handler,
        ValueList &input_values, lldb::addr_t isa_addr, lldb::addr_t sel_addr,
        lldb::addr_t sel_str_addr, llvm::StringRef sel_str)
    : ThreadPlan(ThreadPlan::eKindGeneric,
                 "MacOSX Step through ObjC Trampoline", thread, eVoteNoOpinion,
                 eVoteNoOpinion),
      m_trampoline_handler(trampoline_handler),
      m_args_addr(LLDB_INVALID_ADDRESS), m_input_values(input_values),
      m_isa_addr(isa_addr), m_sel_addr(sel_addr), m_impl_function(nullptr),
      m_sel_str_addr(sel_str_addr), m_sel_str(sel_str) {}

// Destructor
AppleThreadPlanStepThroughObjCTrampoline::
    ~AppleThreadPlanStepThroughObjCTrampoline() = default;

void AppleThreadPlanStepThroughObjCTrampoline::DidPush() {
  // Setting up the memory space for the called function text might require
  // allocations, i.e. a nested function call.  This needs to be done as a
  // PreResumeAction.
  m_process.AddPreResumeAction(PreResumeInitializeFunctionCaller, (void *)this);
}

bool AppleThreadPlanStepThroughObjCTrampoline::InitializeFunctionCaller() {
  if (!m_func_sp) {
    DiagnosticManager diagnostics;
    m_args_addr =
        m_trampoline_handler.SetupDispatchFunction(GetThread(), m_input_values);

    if (m_args_addr == LLDB_INVALID_ADDRESS) {
      return false;
    }
    m_impl_function =
        m_trampoline_handler.GetLookupImplementationFunctionCaller();
    ExecutionContext exc_ctx;
    EvaluateExpressionOptions options;
    options.SetUnwindOnError(true);
    options.SetIgnoreBreakpoints(true);
    options.SetStopOthers(false);
    GetThread().CalculateExecutionContext(exc_ctx);
    m_func_sp = m_impl_function->GetThreadPlanToCallFunction(
        exc_ctx, m_args_addr, options, diagnostics);
    m_func_sp->SetOkayToDiscard(true);
    PushPlan(m_func_sp);
  }
  return true;
}

bool AppleThreadPlanStepThroughObjCTrampoline::
    PreResumeInitializeFunctionCaller(void *void_myself) {
  AppleThreadPlanStepThroughObjCTrampoline *myself =
      static_cast<AppleThreadPlanStepThroughObjCTrampoline *>(void_myself);
  return myself->InitializeFunctionCaller();
}

void AppleThreadPlanStepThroughObjCTrampoline::GetDescription(
    Stream *s, lldb::DescriptionLevel level) {
  if (level == lldb::eDescriptionLevelBrief)
    s->Printf("Step through ObjC trampoline");
  else {
    s->Printf("Stepping to implementation of ObjC method - obj: 0x%llx, isa: "
              "0x%" PRIx64 ", sel: 0x%" PRIx64,
              m_input_values.GetValueAtIndex(0)->GetScalar().ULongLong(),
              m_isa_addr, m_sel_addr);
  }
}

bool AppleThreadPlanStepThroughObjCTrampoline::ValidatePlan(Stream *error) {
  return true;
}

bool AppleThreadPlanStepThroughObjCTrampoline::DoPlanExplainsStop(
    Event *event_ptr) {
  // If we get asked to explain the stop it will be because something went
  // wrong (like the implementation for selector function crashed...  We're
  // going to figure out what to do about that, so we do explain the stop.
  return true;
}

lldb::StateType AppleThreadPlanStepThroughObjCTrampoline::GetPlanRunState() {
  return eStateRunning;
}

bool AppleThreadPlanStepThroughObjCTrampoline::ShouldStop(Event *event_ptr) {
  // First stage: we are still handling the "call a function to get the target
  // of the dispatch"
  if (m_func_sp) {
    if (!m_func_sp->IsPlanComplete()) {
      return false;
    } else {
      if (!m_func_sp->PlanSucceeded()) {
        SetPlanComplete(false);
        return true;
      }
      m_func_sp.reset();
    }
  }

  // Second stage, if all went well with the function calling,  get the
  // implementation function address, and queue up a "run to that address" plan.
  Log *log = GetLog(LLDBLog::Step);

  if (!m_run_to_sp) {
    Value target_addr_value;
    ExecutionContext exc_ctx;
    GetThread().CalculateExecutionContext(exc_ctx);
    m_impl_function->FetchFunctionResults(exc_ctx, m_args_addr,
                                          target_addr_value);
    m_impl_function->DeallocateFunctionResults(exc_ctx, m_args_addr);
    lldb::addr_t target_addr = target_addr_value.GetScalar().ULongLong();

    if (ABISP abi_sp = GetThread().GetProcess()->GetABI()) {
      target_addr = abi_sp->FixCodeAddress(target_addr);
    }
    Address target_so_addr;
    target_so_addr.SetOpcodeLoadAddress(target_addr, exc_ctx.GetTargetPtr());
    if (target_addr == 0) {
      LLDB_LOGF(log, "Got target implementation of 0x0, stopping.");
      SetPlanComplete();
      return true;
    }
    if (m_trampoline_handler.AddrIsMsgForward(target_addr)) {
      LLDB_LOGF(log,
                "Implementation lookup returned msgForward function: 0x%" PRIx64
                ", stopping.",
                target_addr);

      SymbolContext sc = GetThread().GetStackFrameAtIndex(0)->GetSymbolContext(
          eSymbolContextEverything);
      Status status;
      const bool abort_other_plans = false;
      const bool first_insn = true;
      const uint32_t frame_idx = 0;
      m_run_to_sp = GetThread().QueueThreadPlanForStepOutNoShouldStop(
          abort_other_plans, &sc, first_insn, false, eVoteNoOpinion,
          eVoteNoOpinion, frame_idx, status);
      if (m_run_to_sp && status.Success())
        m_run_to_sp->SetPrivate(true);
      return false;
    }

    LLDB_LOGF(log, "Running to ObjC method implementation: 0x%" PRIx64,
              target_addr);

    ObjCLanguageRuntime *objc_runtime =
        ObjCLanguageRuntime::Get(*GetThread().GetProcess());
    assert(objc_runtime != nullptr);
    if (m_sel_str_addr != LLDB_INVALID_ADDRESS) {
      // Cache the string -> implementation and free the string in the target.
      Status dealloc_error =
          GetThread().GetProcess()->DeallocateMemory(m_sel_str_addr);
      // For now just log this:
      if (dealloc_error.Fail())
        LLDB_LOG(log, "Failed to deallocate the sel str at {0} - error: {1}",
                 m_sel_str_addr, dealloc_error);
      objc_runtime->AddToMethodCache(m_isa_addr, m_sel_str, target_addr);
      LLDB_LOG(log,
               "Adding \\{isa-addr={0}, sel-addr={1}\\} = addr={2} to cache.",
               m_isa_addr, m_sel_str, target_addr);
    } else {
      objc_runtime->AddToMethodCache(m_isa_addr, m_sel_addr, target_addr);
      LLDB_LOGF(log,
                "Adding {isa-addr=0x%" PRIx64 ", sel-addr=0x%" PRIx64
                "} = addr=0x%" PRIx64 " to cache.",
                m_isa_addr, m_sel_addr, target_addr);
    }

    m_run_to_sp = std::make_shared<ThreadPlanRunToAddress>(
        GetThread(), target_so_addr, false);
    PushPlan(m_run_to_sp);
    return false;
  } else if (GetThread().IsThreadPlanDone(m_run_to_sp.get())) {
    // Third stage, work the run to target plan.
    SetPlanComplete();
    return true;
  }
  return false;
}

// The base class MischiefManaged does some cleanup - so you have to call it in
// your MischiefManaged derived class.
bool AppleThreadPlanStepThroughObjCTrampoline::MischiefManaged() {
  return IsPlanComplete();
}

bool AppleThreadPlanStepThroughObjCTrampoline::WillStop() { return true; }

// Objective-C uses optimized dispatch functions for some common and seldom
// overridden methods.  For instance
//      [object respondsToSelector:];
// will get compiled to:
//      objc_opt_respondsToSelector(object);
// This checks whether the selector has been overridden, directly calling the
// implementation if it hasn't and calling objc_msgSend if it has.
//
// We need to get into the overridden implementation.  We'll do that by 
// setting a breakpoint on objc_msgSend, and doing a "step out".  If we stop
// at objc_msgSend, we can step through to the target of the send, and see if
// that's a place we want to stop.
//
// A couple of complexities.  The checking code might call some other method,
// so we might see objc_msgSend more than once.  Also, these optimized dispatch
// functions might dispatch more than one message at a time (e.g. alloc followed
// by init.)  So we can't give up at the first objc_msgSend.
// That means among other things that we have to handle the "ShouldStopHere" - 
// since we can't just return control to the plan that's controlling us on the
// first step.

AppleThreadPlanStepThroughDirectDispatch ::
    AppleThreadPlanStepThroughDirectDispatch(
        Thread &thread, AppleObjCTrampolineHandler &handler,
        llvm::StringRef dispatch_func_name)
    : ThreadPlanStepOut(thread, nullptr, true /* first instruction */, false,
                        eVoteNoOpinion, eVoteNoOpinion,
                        0 /* Step out of zeroth frame */,
                        eLazyBoolNo /* Our parent plan will decide this
                               when we are done */
                        ,
                        true /* Run to branch for inline step out */,
                        false /* Don't gather the return value */),
      m_trampoline_handler(handler),
      m_dispatch_func_name(std::string(dispatch_func_name)),
      m_at_msg_send(false) {
  // Set breakpoints on the dispatch functions:
  auto bkpt_callback = [&] (lldb::addr_t addr, 
                            const AppleObjCTrampolineHandler
                                ::DispatchFunction &dispatch) {
    m_msgSend_bkpts.push_back(GetTarget().CreateBreakpoint(addr,
                                                           true /* internal */,
                                                           false /* hard */));
    m_msgSend_bkpts.back()->SetThreadID(GetThread().GetID());
  };
  handler.ForEachDispatchFunction(bkpt_callback);

  // We'll set the step-out plan in the DidPush so it gets queued in the right
  // order.

  if (GetThread().GetStepInAvoidsNoDebug())
    GetFlags().Set(ThreadPlanShouldStopHere::eStepInAvoidNoDebug);
  else
    GetFlags().Clear(ThreadPlanShouldStopHere::eStepInAvoidNoDebug);
  // We only care about step in.  Our parent plan will figure out what to
  // do when we've stepped out again.
  GetFlags().Clear(ThreadPlanShouldStopHere::eStepOutAvoidNoDebug);
}

AppleThreadPlanStepThroughDirectDispatch::
    ~AppleThreadPlanStepThroughDirectDispatch() {
    for (BreakpointSP bkpt_sp : m_msgSend_bkpts) {
      GetTarget().RemoveBreakpointByID(bkpt_sp->GetID());
    }
}

void AppleThreadPlanStepThroughDirectDispatch::GetDescription(
    Stream *s, lldb::DescriptionLevel level) {
  switch (level) {
  case lldb::eDescriptionLevelBrief:
    s->PutCString("Step through ObjC direct dispatch function.");
    break;
  default:
    s->Printf("Step through ObjC direct dispatch '%s'  using breakpoints: ",
              m_dispatch_func_name.c_str());
    bool first = true;
    for (auto bkpt_sp : m_msgSend_bkpts) {
        if (!first) {
          s->PutCString(", ");
        }
        first = false;
        s->Printf("%d", bkpt_sp->GetID());
    }
    (*s) << ".";  
    break;
  }
}

bool 
AppleThreadPlanStepThroughDirectDispatch::DoPlanExplainsStop(Event *event_ptr) {
  if (ThreadPlanStepOut::DoPlanExplainsStop(event_ptr))
    return true;

  StopInfoSP stop_info_sp = GetPrivateStopInfo();

  // Check if the breakpoint is one of ours msgSend dispatch breakpoints.

  StopReason stop_reason = eStopReasonNone;
  if (stop_info_sp)
    stop_reason = stop_info_sp->GetStopReason();

  // See if this is one of our msgSend breakpoints:
  if (stop_reason == eStopReasonBreakpoint) {
    ProcessSP process_sp = GetThread().GetProcess();
    uint64_t break_site_id = stop_info_sp->GetValue();
    BreakpointSiteSP site_sp 
        = process_sp->GetBreakpointSiteList().FindByID(break_site_id);
    // Some other plan might have deleted the site's last owner before this 
    // got to us.  In which case, it wasn't our breakpoint...    
    if (!site_sp)
      return false;
      
    for (BreakpointSP break_sp : m_msgSend_bkpts) {
      if (site_sp->IsBreakpointAtThisSite(break_sp->GetID())) {
        // If we aren't the only one with a breakpoint on this site, then we
        // should just stop and return control to the user.
        if (site_sp->GetNumberOfConstituents() > 1) {
          SetPlanComplete(true);
          return false;
        }
        m_at_msg_send = true;
        return true;
      }
    }
  }
  
  // We're done here.  If one of our sub-plans explained the stop, they 
  // would have already answered true to PlanExplainsStop, and if they were
  // done, we'll get called to figure out what to do in ShouldStop...
  return false;
}

bool AppleThreadPlanStepThroughDirectDispatch
         ::DoWillResume(lldb::StateType resume_state, bool current_plan) {
  ThreadPlanStepOut::DoWillResume(resume_state, current_plan);
  m_at_msg_send = false;
  return true;
}

bool AppleThreadPlanStepThroughDirectDispatch::ShouldStop(Event *event_ptr) {
  // If step out plan finished, that means we didn't find our way into a method
  // implementation.  Either we went directly to the default implementation, 
  // of the overridden implementation didn't have debug info.  
  // So we should mark ourselves as done.
  const bool step_out_should_stop = ThreadPlanStepOut::ShouldStop(event_ptr);
  if (step_out_should_stop) {
    SetPlanComplete(true);
    return true;
  }
  
  // If we have a step through plan, then w're in the process of getting 
  // through an ObjC msgSend.  If we arrived at the target function, then 
  // check whether we have debug info, and if we do, stop.
  Log *log = GetLog(LLDBLog::Step);

  if (m_objc_step_through_sp && m_objc_step_through_sp->IsPlanComplete()) {
    // If the plan failed for some reason, we should probably just let the
    // step over plan get us out of here...  We don't need to do anything about
    // the step through plan, it is done and will get popped when we continue.
    if (!m_objc_step_through_sp->PlanSucceeded()) {
      LLDB_LOGF(log, "ObjC Step through plan failed.  Stepping out.");
    }
    Status error;
    if (InvokeShouldStopHereCallback(eFrameCompareYounger, error)) {
      SetPlanComplete(true);
      return true;
    }
    // If we didn't want to stop at this msgSend, there might be another so
    // we should just continue on with the step out and see if our breakpoint
    // triggers again.
    m_objc_step_through_sp.reset();
    for (BreakpointSP bkpt_sp : m_msgSend_bkpts) {
      bkpt_sp->SetEnabled(true);
    }
    return false;
  }

  // If we hit an msgSend breakpoint, then we should queue the step through
  // plan:
  
  if (m_at_msg_send) {
    LanguageRuntime *objc_runtime 
      = GetThread().GetProcess()->GetLanguageRuntime(eLanguageTypeObjC);
    // There's no way we could have gotten here without an ObjC language 
    // runtime.
    assert(objc_runtime);
    m_objc_step_through_sp =
        objc_runtime->GetStepThroughTrampolinePlan(GetThread(), false);
    // If we failed to find the target for this dispatch, just keep going and
    // let the step out complete.
    if (!m_objc_step_through_sp) {
      LLDB_LOG(log, "Couldn't find target for message dispatch, continuing.");
      return false;
    }
    // Otherwise push the step through plan and continue.
    GetThread().QueueThreadPlan(m_objc_step_through_sp, false);
    for (BreakpointSP bkpt_sp : m_msgSend_bkpts) {
      bkpt_sp->SetEnabled(false);
    }
    return false;
  }
  return true;  
}

bool AppleThreadPlanStepThroughDirectDispatch::MischiefManaged() {
  if (IsPlanComplete())
    return true;
  return ThreadPlanStepOut::MischiefManaged();
}
