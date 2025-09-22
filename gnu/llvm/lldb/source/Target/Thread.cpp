//===-- Thread.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Thread.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Host/Host.h"
#include "lldb/Interpreter/OptionValueFileSpecList.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanBase.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Target/ThreadPlanPython.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Target/ThreadPlanStack.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepOverBreakpoint.h"
#include "lldb/Target/ThreadPlanStepOverRange.h"
#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Target/ThreadPlanStepUntil.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Target/UnwindLLDB.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;

ThreadProperties &Thread::GetGlobalProperties() {
  // NOTE: intentional leak so we don't crash if global destructor chain gets
  // called as other threads still use the result of this function
  static ThreadProperties *g_settings_ptr = new ThreadProperties(true);
  return *g_settings_ptr;
}

#define LLDB_PROPERTIES_thread
#include "TargetProperties.inc"

enum {
#define LLDB_PROPERTIES_thread
#include "TargetPropertiesEnum.inc"
};

class ThreadOptionValueProperties
    : public Cloneable<ThreadOptionValueProperties, OptionValueProperties> {
public:
  ThreadOptionValueProperties(llvm::StringRef name) : Cloneable(name) {}

  const Property *
  GetPropertyAtIndex(size_t idx,
                     const ExecutionContext *exe_ctx) const override {
    // When getting the value for a key from the thread options, we will always
    // try and grab the setting from the current thread if there is one. Else
    // we just use the one from this instance.
    if (exe_ctx) {
      Thread *thread = exe_ctx->GetThreadPtr();
      if (thread) {
        ThreadOptionValueProperties *instance_properties =
            static_cast<ThreadOptionValueProperties *>(
                thread->GetValueProperties().get());
        if (this != instance_properties)
          return instance_properties->ProtectedGetPropertyAtIndex(idx);
      }
    }
    return ProtectedGetPropertyAtIndex(idx);
  }
};

ThreadProperties::ThreadProperties(bool is_global) : Properties() {
  if (is_global) {
    m_collection_sp = std::make_shared<ThreadOptionValueProperties>("thread");
    m_collection_sp->Initialize(g_thread_properties);
  } else
    m_collection_sp =
        OptionValueProperties::CreateLocalCopy(Thread::GetGlobalProperties());
}

ThreadProperties::~ThreadProperties() = default;

const RegularExpression *ThreadProperties::GetSymbolsToAvoidRegexp() {
  const uint32_t idx = ePropertyStepAvoidRegex;
  return GetPropertyAtIndexAs<const RegularExpression *>(idx);
}

FileSpecList ThreadProperties::GetLibrariesToAvoid() const {
  const uint32_t idx = ePropertyStepAvoidLibraries;
  return GetPropertyAtIndexAs<FileSpecList>(idx, {});
}

bool ThreadProperties::GetTraceEnabledState() const {
  const uint32_t idx = ePropertyEnableThreadTrace;
  return GetPropertyAtIndexAs<bool>(
      idx, g_thread_properties[idx].default_uint_value != 0);
}

bool ThreadProperties::GetStepInAvoidsNoDebug() const {
  const uint32_t idx = ePropertyStepInAvoidsNoDebug;
  return GetPropertyAtIndexAs<bool>(
      idx, g_thread_properties[idx].default_uint_value != 0);
}

bool ThreadProperties::GetStepOutAvoidsNoDebug() const {
  const uint32_t idx = ePropertyStepOutAvoidsNoDebug;
  return GetPropertyAtIndexAs<bool>(
      idx, g_thread_properties[idx].default_uint_value != 0);
}

uint64_t ThreadProperties::GetMaxBacktraceDepth() const {
  const uint32_t idx = ePropertyMaxBacktraceDepth;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_thread_properties[idx].default_uint_value);
}

// Thread Event Data

llvm::StringRef Thread::ThreadEventData::GetFlavorString() {
  return "Thread::ThreadEventData";
}

Thread::ThreadEventData::ThreadEventData(const lldb::ThreadSP thread_sp)
    : m_thread_sp(thread_sp), m_stack_id() {}

Thread::ThreadEventData::ThreadEventData(const lldb::ThreadSP thread_sp,
                                         const StackID &stack_id)
    : m_thread_sp(thread_sp), m_stack_id(stack_id) {}

Thread::ThreadEventData::ThreadEventData() : m_thread_sp(), m_stack_id() {}

Thread::ThreadEventData::~ThreadEventData() = default;

void Thread::ThreadEventData::Dump(Stream *s) const {}

const Thread::ThreadEventData *
Thread::ThreadEventData::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr) {
    const EventData *event_data = event_ptr->GetData();
    if (event_data &&
        event_data->GetFlavor() == ThreadEventData::GetFlavorString())
      return static_cast<const ThreadEventData *>(event_ptr->GetData());
  }
  return nullptr;
}

ThreadSP Thread::ThreadEventData::GetThreadFromEvent(const Event *event_ptr) {
  ThreadSP thread_sp;
  const ThreadEventData *event_data = GetEventDataFromEvent(event_ptr);
  if (event_data)
    thread_sp = event_data->GetThread();
  return thread_sp;
}

StackID Thread::ThreadEventData::GetStackIDFromEvent(const Event *event_ptr) {
  StackID stack_id;
  const ThreadEventData *event_data = GetEventDataFromEvent(event_ptr);
  if (event_data)
    stack_id = event_data->GetStackID();
  return stack_id;
}

StackFrameSP
Thread::ThreadEventData::GetStackFrameFromEvent(const Event *event_ptr) {
  const ThreadEventData *event_data = GetEventDataFromEvent(event_ptr);
  StackFrameSP frame_sp;
  if (event_data) {
    ThreadSP thread_sp = event_data->GetThread();
    if (thread_sp) {
      frame_sp = thread_sp->GetStackFrameList()->GetFrameWithStackID(
          event_data->GetStackID());
    }
  }
  return frame_sp;
}

// Thread class

llvm::StringRef Thread::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.thread");
  return class_name;
}

Thread::Thread(Process &process, lldb::tid_t tid, bool use_invalid_index_id)
    : ThreadProperties(false), UserID(tid),
      Broadcaster(process.GetTarget().GetDebugger().GetBroadcasterManager(),
                  Thread::GetStaticBroadcasterClass().str()),
      m_process_wp(process.shared_from_this()), m_stop_info_sp(),
      m_stop_info_stop_id(0), m_stop_info_override_stop_id(0),
      m_should_run_before_public_stop(false),
      m_index_id(use_invalid_index_id ? LLDB_INVALID_INDEX32
                                      : process.GetNextThreadIndexID(tid)),
      m_reg_context_sp(), m_state(eStateUnloaded), m_state_mutex(),
      m_frame_mutex(), m_curr_frames_sp(), m_prev_frames_sp(),
      m_prev_framezero_pc(), m_resume_signal(LLDB_INVALID_SIGNAL_NUMBER),
      m_resume_state(eStateRunning), m_temporary_resume_state(eStateRunning),
      m_unwinder_up(), m_destroy_called(false),
      m_override_should_notify(eLazyBoolCalculate),
      m_extended_info_fetched(false), m_extended_info() {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOGF(log, "%p Thread::Thread(tid = 0x%4.4" PRIx64 ")",
            static_cast<void *>(this), GetID());

  CheckInWithManager();
}

Thread::~Thread() {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOGF(log, "%p Thread::~Thread(tid = 0x%4.4" PRIx64 ")",
            static_cast<void *>(this), GetID());
  /// If you hit this assert, it means your derived class forgot to call
  /// DoDestroy in its destructor.
  assert(m_destroy_called);
}

void Thread::DestroyThread() {
  m_destroy_called = true;
  m_stop_info_sp.reset();
  m_reg_context_sp.reset();
  m_unwinder_up.reset();
  std::lock_guard<std::recursive_mutex> guard(m_frame_mutex);
  m_curr_frames_sp.reset();
  m_prev_frames_sp.reset();
  m_prev_framezero_pc.reset();
}

void Thread::BroadcastSelectedFrameChange(StackID &new_frame_id) {
  if (EventTypeHasListeners(eBroadcastBitSelectedFrameChanged)) {
    auto data_sp =
        std::make_shared<ThreadEventData>(shared_from_this(), new_frame_id);
    BroadcastEvent(eBroadcastBitSelectedFrameChanged, data_sp);
  }
}

lldb::StackFrameSP
Thread::GetSelectedFrame(SelectMostRelevant select_most_relevant) {
  StackFrameListSP stack_frame_list_sp(GetStackFrameList());
  StackFrameSP frame_sp = stack_frame_list_sp->GetFrameAtIndex(
      stack_frame_list_sp->GetSelectedFrameIndex(select_most_relevant));
  FrameSelectedCallback(frame_sp.get());
  return frame_sp;
}

uint32_t Thread::SetSelectedFrame(lldb_private::StackFrame *frame,
                                  bool broadcast) {
  uint32_t ret_value = GetStackFrameList()->SetSelectedFrame(frame);
  if (broadcast)
    BroadcastSelectedFrameChange(frame->GetStackID());
  FrameSelectedCallback(frame);
  return ret_value;
}

bool Thread::SetSelectedFrameByIndex(uint32_t frame_idx, bool broadcast) {
  StackFrameSP frame_sp(GetStackFrameList()->GetFrameAtIndex(frame_idx));
  if (frame_sp) {
    GetStackFrameList()->SetSelectedFrame(frame_sp.get());
    if (broadcast)
      BroadcastSelectedFrameChange(frame_sp->GetStackID());
    FrameSelectedCallback(frame_sp.get());
    return true;
  } else
    return false;
}

bool Thread::SetSelectedFrameByIndexNoisily(uint32_t frame_idx,
                                            Stream &output_stream) {
  const bool broadcast = true;
  bool success = SetSelectedFrameByIndex(frame_idx, broadcast);
  if (success) {
    StackFrameSP frame_sp = GetSelectedFrame(DoNoSelectMostRelevantFrame);
    if (frame_sp) {
      bool already_shown = false;
      SymbolContext frame_sc(
          frame_sp->GetSymbolContext(eSymbolContextLineEntry));
      const Debugger &debugger = GetProcess()->GetTarget().GetDebugger();
      if (debugger.GetUseExternalEditor() && frame_sc.line_entry.GetFile() &&
          frame_sc.line_entry.line != 0) {
        if (llvm::Error e = Host::OpenFileInExternalEditor(
                debugger.GetExternalEditor(), frame_sc.line_entry.GetFile(),
                frame_sc.line_entry.line)) {
          LLDB_LOG_ERROR(GetLog(LLDBLog::Host), std::move(e),
                         "OpenFileInExternalEditor failed: {0}");
        } else {
          already_shown = true;
        }
      }

      bool show_frame_info = true;
      bool show_source = !already_shown;
      FrameSelectedCallback(frame_sp.get());
      return frame_sp->GetStatus(output_stream, show_frame_info, show_source);
    }
    return false;
  } else
    return false;
}

void Thread::FrameSelectedCallback(StackFrame *frame) {
  if (!frame)
    return;

  if (frame->HasDebugInformation() &&
      (GetProcess()->GetWarningsOptimization() ||
       GetProcess()->GetWarningsUnsupportedLanguage())) {
    SymbolContext sc =
        frame->GetSymbolContext(eSymbolContextFunction | eSymbolContextModule);
    GetProcess()->PrintWarningOptimization(sc);
    GetProcess()->PrintWarningUnsupportedLanguage(sc);
  }
}

lldb::StopInfoSP Thread::GetStopInfo() {
  if (m_destroy_called)
    return m_stop_info_sp;

  ThreadPlanSP completed_plan_sp(GetCompletedPlan());
  ProcessSP process_sp(GetProcess());
  const uint32_t stop_id = process_sp ? process_sp->GetStopID() : UINT32_MAX;

  // Here we select the stop info according to priorirty: - m_stop_info_sp (if
  // not trace) - preset value - completed plan stop info - new value with plan
  // from completed plan stack - m_stop_info_sp (trace stop reason is OK now) -
  // ask GetPrivateStopInfo to set stop info

  bool have_valid_stop_info = m_stop_info_sp &&
      m_stop_info_sp ->IsValid() &&
      m_stop_info_stop_id == stop_id;
  bool have_valid_completed_plan = completed_plan_sp && completed_plan_sp->PlanSucceeded();
  bool plan_failed = completed_plan_sp && !completed_plan_sp->PlanSucceeded();
  bool plan_overrides_trace =
    have_valid_stop_info && have_valid_completed_plan
    && (m_stop_info_sp->GetStopReason() == eStopReasonTrace);

  if (have_valid_stop_info && !plan_overrides_trace && !plan_failed) {
    return m_stop_info_sp;
  } else if (completed_plan_sp) {
    return StopInfo::CreateStopReasonWithPlan(
        completed_plan_sp, GetReturnValueObject(), GetExpressionVariable());
  } else {
    GetPrivateStopInfo();
    return m_stop_info_sp;
  }
}

void Thread::CalculatePublicStopInfo() {
  ResetStopInfo();
  SetStopInfo(GetStopInfo());
}

lldb::StopInfoSP Thread::GetPrivateStopInfo(bool calculate) {
  if (!calculate)
    return m_stop_info_sp;

  if (m_destroy_called)
    return m_stop_info_sp;

  ProcessSP process_sp(GetProcess());
  if (process_sp) {
    const uint32_t process_stop_id = process_sp->GetStopID();
    if (m_stop_info_stop_id != process_stop_id) {
      // We preserve the old stop info for a variety of reasons:
      // 1) Someone has already updated it by the time we get here
      // 2) We didn't get to execute the breakpoint instruction we stopped at
      // 3) This is a virtual step so we didn't actually run
      // 4) If this thread wasn't allowed to run the last time round.
      if (m_stop_info_sp) {
        if (m_stop_info_sp->IsValid() || IsStillAtLastBreakpointHit() ||
            GetCurrentPlan()->IsVirtualStep()
            || GetTemporaryResumeState() == eStateSuspended)
          SetStopInfo(m_stop_info_sp);
        else
          m_stop_info_sp.reset();
      }

      if (!m_stop_info_sp) {
        if (!CalculateStopInfo())
          SetStopInfo(StopInfoSP());
      }
    }

    // The stop info can be manually set by calling Thread::SetStopInfo() prior
    // to this function ever getting called, so we can't rely on
    // "m_stop_info_stop_id != process_stop_id" as the condition for the if
    // statement below, we must also check the stop info to see if we need to
    // override it. See the header documentation in
    // Architecture::OverrideStopInfo() for more information on the stop
    // info override callback.
    if (m_stop_info_override_stop_id != process_stop_id) {
      m_stop_info_override_stop_id = process_stop_id;
      if (m_stop_info_sp) {
        if (const Architecture *arch =
                process_sp->GetTarget().GetArchitecturePlugin())
          arch->OverrideStopInfo(*this);
      }
    }
  }

  // If we were resuming the process and it was interrupted,
  // return no stop reason.  This thread would like to resume.
  if (m_stop_info_sp && m_stop_info_sp->WasContinueInterrupted(*this))
    return {};

  return m_stop_info_sp;
}

lldb::StopReason Thread::GetStopReason() {
  lldb::StopInfoSP stop_info_sp(GetStopInfo());
  if (stop_info_sp)
    return stop_info_sp->GetStopReason();
  return eStopReasonNone;
}

bool Thread::StopInfoIsUpToDate() const {
  ProcessSP process_sp(GetProcess());
  if (process_sp)
    return m_stop_info_stop_id == process_sp->GetStopID();
  else
    return true; // Process is no longer around so stop info is always up to
                 // date...
}

void Thread::ResetStopInfo() {
  if (m_stop_info_sp) {
    m_stop_info_sp.reset();
  }
}

void Thread::SetStopInfo(const lldb::StopInfoSP &stop_info_sp) {
  m_stop_info_sp = stop_info_sp;
  if (m_stop_info_sp) {
    m_stop_info_sp->MakeStopInfoValid();
    // If we are overriding the ShouldReportStop, do that here:
    if (m_override_should_notify != eLazyBoolCalculate)
      m_stop_info_sp->OverrideShouldNotify(m_override_should_notify ==
                                           eLazyBoolYes);
  }

  ProcessSP process_sp(GetProcess());
  if (process_sp)
    m_stop_info_stop_id = process_sp->GetStopID();
  else
    m_stop_info_stop_id = UINT32_MAX;
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%p: tid = 0x%" PRIx64 ": stop info = %s (stop_id = %u)",
            static_cast<void *>(this), GetID(),
            stop_info_sp ? stop_info_sp->GetDescription() : "<NULL>",
            m_stop_info_stop_id);
}

void Thread::SetShouldReportStop(Vote vote) {
  if (vote == eVoteNoOpinion)
    return;
  else {
    m_override_should_notify = (vote == eVoteYes ? eLazyBoolYes : eLazyBoolNo);
    if (m_stop_info_sp)
      m_stop_info_sp->OverrideShouldNotify(m_override_should_notify ==
                                           eLazyBoolYes);
  }
}

void Thread::SetStopInfoToNothing() {
  // Note, we can't just NULL out the private reason, or the native thread
  // implementation will try to go calculate it again.  For now, just set it to
  // a Unix Signal with an invalid signal number.
  SetStopInfo(
      StopInfo::CreateStopReasonWithSignal(*this, LLDB_INVALID_SIGNAL_NUMBER));
}

bool Thread::ThreadStoppedForAReason() { return (bool)GetPrivateStopInfo(); }

bool Thread::CheckpointThreadState(ThreadStateCheckpoint &saved_state) {
  saved_state.register_backup_sp.reset();
  lldb::StackFrameSP frame_sp(GetStackFrameAtIndex(0));
  if (frame_sp) {
    lldb::RegisterCheckpointSP reg_checkpoint_sp(
        new RegisterCheckpoint(RegisterCheckpoint::Reason::eExpression));
    if (reg_checkpoint_sp) {
      lldb::RegisterContextSP reg_ctx_sp(frame_sp->GetRegisterContext());
      if (reg_ctx_sp && reg_ctx_sp->ReadAllRegisterValues(*reg_checkpoint_sp))
        saved_state.register_backup_sp = reg_checkpoint_sp;
    }
  }
  if (!saved_state.register_backup_sp)
    return false;

  saved_state.stop_info_sp = GetStopInfo();
  ProcessSP process_sp(GetProcess());
  if (process_sp)
    saved_state.orig_stop_id = process_sp->GetStopID();
  saved_state.current_inlined_depth = GetCurrentInlinedDepth();
  saved_state.m_completed_plan_checkpoint =
      GetPlans().CheckpointCompletedPlans();

  return true;
}

bool Thread::RestoreRegisterStateFromCheckpoint(
    ThreadStateCheckpoint &saved_state) {
  if (saved_state.register_backup_sp) {
    lldb::StackFrameSP frame_sp(GetStackFrameAtIndex(0));
    if (frame_sp) {
      lldb::RegisterContextSP reg_ctx_sp(frame_sp->GetRegisterContext());
      if (reg_ctx_sp) {
        bool ret =
            reg_ctx_sp->WriteAllRegisterValues(*saved_state.register_backup_sp);

        // Clear out all stack frames as our world just changed.
        ClearStackFrames();
        reg_ctx_sp->InvalidateIfNeeded(true);
        if (m_unwinder_up)
          m_unwinder_up->Clear();
        return ret;
      }
    }
  }
  return false;
}

void Thread::RestoreThreadStateFromCheckpoint(
    ThreadStateCheckpoint &saved_state) {
  if (saved_state.stop_info_sp)
    saved_state.stop_info_sp->MakeStopInfoValid();
  SetStopInfo(saved_state.stop_info_sp);
  GetStackFrameList()->SetCurrentInlinedDepth(
      saved_state.current_inlined_depth);
  GetPlans().RestoreCompletedPlanCheckpoint(
      saved_state.m_completed_plan_checkpoint);
}

StateType Thread::GetState() const {
  // If any other threads access this we will need a mutex for it
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);
  return m_state;
}

void Thread::SetState(StateType state) {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);
  m_state = state;
}

std::string Thread::GetStopDescription() {
  StackFrameSP frame_sp = GetStackFrameAtIndex(0);

  if (!frame_sp)
    return GetStopDescriptionRaw();

  auto recognized_frame_sp = frame_sp->GetRecognizedFrame();

  if (!recognized_frame_sp)
    return GetStopDescriptionRaw();

  std::string recognized_stop_description =
      recognized_frame_sp->GetStopDescription();

  if (!recognized_stop_description.empty())
    return recognized_stop_description;

  return GetStopDescriptionRaw();
}

std::string Thread::GetStopDescriptionRaw() {
  StopInfoSP stop_info_sp = GetStopInfo();
  std::string raw_stop_description;
  if (stop_info_sp && stop_info_sp->IsValid()) {
    raw_stop_description = stop_info_sp->GetDescription();
    assert((!raw_stop_description.empty() ||
            stop_info_sp->GetStopReason() == eStopReasonNone) &&
           "StopInfo returned an empty description.");
  }
  return raw_stop_description;
}

void Thread::WillStop() {
  ThreadPlan *current_plan = GetCurrentPlan();

  // FIXME: I may decide to disallow threads with no plans.  In which
  // case this should go to an assert.

  if (!current_plan)
    return;

  current_plan->WillStop();
}

void Thread::SetupForResume() {
  if (GetResumeState() != eStateSuspended) {
    // If we're at a breakpoint push the step-over breakpoint plan.  Do this
    // before telling the current plan it will resume, since we might change
    // what the current plan is.

    lldb::RegisterContextSP reg_ctx_sp(GetRegisterContext());
    if (reg_ctx_sp) {
      const addr_t thread_pc = reg_ctx_sp->GetPC();
      BreakpointSiteSP bp_site_sp =
          GetProcess()->GetBreakpointSiteList().FindByAddress(thread_pc);
      if (bp_site_sp) {
        // Note, don't assume there's a ThreadPlanStepOverBreakpoint, the
        // target may not require anything special to step over a breakpoint.

        ThreadPlan *cur_plan = GetCurrentPlan();

        bool push_step_over_bp_plan = false;
        if (cur_plan->GetKind() == ThreadPlan::eKindStepOverBreakpoint) {
          ThreadPlanStepOverBreakpoint *bp_plan =
              (ThreadPlanStepOverBreakpoint *)cur_plan;
          if (bp_plan->GetBreakpointLoadAddress() != thread_pc)
            push_step_over_bp_plan = true;
        } else
          push_step_over_bp_plan = true;

        if (push_step_over_bp_plan) {
          ThreadPlanSP step_bp_plan_sp(new ThreadPlanStepOverBreakpoint(*this));
          if (step_bp_plan_sp) {
            step_bp_plan_sp->SetPrivate(true);

            if (GetCurrentPlan()->RunState() != eStateStepping) {
              ThreadPlanStepOverBreakpoint *step_bp_plan =
                  static_cast<ThreadPlanStepOverBreakpoint *>(
                      step_bp_plan_sp.get());
              step_bp_plan->SetAutoContinue(true);
            }
            QueueThreadPlan(step_bp_plan_sp, false);
          }
        }
      }
    }
  }
}

bool Thread::ShouldResume(StateType resume_state) {
  // At this point clear the completed plan stack.
  GetPlans().WillResume();
  m_override_should_notify = eLazyBoolCalculate;

  StateType prev_resume_state = GetTemporaryResumeState();

  SetTemporaryResumeState(resume_state);

  lldb::ThreadSP backing_thread_sp(GetBackingThread());
  if (backing_thread_sp)
    backing_thread_sp->SetTemporaryResumeState(resume_state);

  // Make sure m_stop_info_sp is valid.  Don't do this for threads we suspended
  // in the previous run.
  if (prev_resume_state != eStateSuspended)
    GetPrivateStopInfo();

  // This is a little dubious, but we are trying to limit how often we actually
  // fetch stop info from the target, 'cause that slows down single stepping.
  // So assume that if we got to the point where we're about to resume, and we
  // haven't yet had to fetch the stop reason, then it doesn't need to know
  // about the fact that we are resuming...
  const uint32_t process_stop_id = GetProcess()->GetStopID();
  if (m_stop_info_stop_id == process_stop_id &&
      (m_stop_info_sp && m_stop_info_sp->IsValid())) {
    StopInfo *stop_info = GetPrivateStopInfo().get();
    if (stop_info)
      stop_info->WillResume(resume_state);
  }

  // Tell all the plans that we are about to resume in case they need to clear
  // any state. We distinguish between the plan on the top of the stack and the
  // lower plans in case a plan needs to do any special business before it
  // runs.

  bool need_to_resume = false;
  ThreadPlan *plan_ptr = GetCurrentPlan();
  if (plan_ptr) {
    need_to_resume = plan_ptr->WillResume(resume_state, true);

    while ((plan_ptr = GetPreviousPlan(plan_ptr)) != nullptr) {
      plan_ptr->WillResume(resume_state, false);
    }

    // If the WillResume for the plan says we are faking a resume, then it will
    // have set an appropriate stop info. In that case, don't reset it here.

    if (need_to_resume && resume_state != eStateSuspended) {
      m_stop_info_sp.reset();
    }
  }

  if (need_to_resume) {
    ClearStackFrames();
    // Let Thread subclasses do any special work they need to prior to resuming
    WillResume(resume_state);
  }

  return need_to_resume;
}

void Thread::DidResume() {
  SetResumeSignal(LLDB_INVALID_SIGNAL_NUMBER);
  // This will get recomputed each time when we stop.
  SetShouldRunBeforePublicStop(false);
}

void Thread::DidStop() { SetState(eStateStopped); }

bool Thread::ShouldStop(Event *event_ptr) {
  ThreadPlan *current_plan = GetCurrentPlan();

  bool should_stop = true;

  Log *log = GetLog(LLDBLog::Step);

  if (GetResumeState() == eStateSuspended) {
    LLDB_LOGF(log,
              "Thread::%s for tid = 0x%4.4" PRIx64 " 0x%4.4" PRIx64
              ", should_stop = 0 (ignore since thread was suspended)",
              __FUNCTION__, GetID(), GetProtocolID());
    return false;
  }

  if (GetTemporaryResumeState() == eStateSuspended) {
    LLDB_LOGF(log,
              "Thread::%s for tid = 0x%4.4" PRIx64 " 0x%4.4" PRIx64
              ", should_stop = 0 (ignore since thread was suspended)",
              __FUNCTION__, GetID(), GetProtocolID());
    return false;
  }

  // Based on the current thread plan and process stop info, check if this
  // thread caused the process to stop. NOTE: this must take place before the
  // plan is moved from the current plan stack to the completed plan stack.
  if (!ThreadStoppedForAReason()) {
    LLDB_LOGF(log,
              "Thread::%s for tid = 0x%4.4" PRIx64 " 0x%4.4" PRIx64
              ", pc = 0x%16.16" PRIx64
              ", should_stop = 0 (ignore since no stop reason)",
              __FUNCTION__, GetID(), GetProtocolID(),
              GetRegisterContext() ? GetRegisterContext()->GetPC()
                                   : LLDB_INVALID_ADDRESS);
    return false;
  }

  // Clear the "must run me before stop" if it was set:
  SetShouldRunBeforePublicStop(false);

  if (log) {
    LLDB_LOGF(log,
              "Thread::%s(%p) for tid = 0x%4.4" PRIx64 " 0x%4.4" PRIx64
              ", pc = 0x%16.16" PRIx64,
              __FUNCTION__, static_cast<void *>(this), GetID(), GetProtocolID(),
              GetRegisterContext() ? GetRegisterContext()->GetPC()
                                   : LLDB_INVALID_ADDRESS);
    LLDB_LOGF(log, "^^^^^^^^ Thread::ShouldStop Begin ^^^^^^^^");
    StreamString s;
    s.IndentMore();
    GetProcess()->DumpThreadPlansForTID(
        s, GetID(), eDescriptionLevelVerbose, true /* internal */,
        false /* condense_trivial */, true /* skip_unreported */);
    LLDB_LOGF(log, "Plan stack initial state:\n%s", s.GetData());
  }

  // The top most plan always gets to do the trace log...
  current_plan->DoTraceLog();

  // First query the stop info's ShouldStopSynchronous.  This handles
  // "synchronous" stop reasons, for example the breakpoint command on internal
  // breakpoints.  If a synchronous stop reason says we should not stop, then
  // we don't have to do any more work on this stop.
  StopInfoSP private_stop_info(GetPrivateStopInfo());
  if (private_stop_info &&
      !private_stop_info->ShouldStopSynchronous(event_ptr)) {
    LLDB_LOGF(log, "StopInfo::ShouldStop async callback says we should not "
                   "stop, returning ShouldStop of false.");
    return false;
  }

  // If we've already been restarted, don't query the plans since the state
  // they would examine is not current.
  if (Process::ProcessEventData::GetRestartedFromEvent(event_ptr))
    return false;

  // Before the plans see the state of the world, calculate the current inlined
  // depth.
  GetStackFrameList()->CalculateCurrentInlinedDepth();

  // If the base plan doesn't understand why we stopped, then we have to find a
  // plan that does. If that plan is still working, then we don't need to do
  // any more work.  If the plan that explains the stop is done, then we should
  // pop all the plans below it, and pop it, and then let the plans above it
  // decide whether they still need to do more work.

  bool done_processing_current_plan = false;

  if (!current_plan->PlanExplainsStop(event_ptr)) {
    if (current_plan->TracerExplainsStop()) {
      done_processing_current_plan = true;
      should_stop = false;
    } else {
      // If the current plan doesn't explain the stop, then find one that does
      // and let it handle the situation.
      ThreadPlan *plan_ptr = current_plan;
      while ((plan_ptr = GetPreviousPlan(plan_ptr)) != nullptr) {
        if (plan_ptr->PlanExplainsStop(event_ptr)) {
          LLDB_LOGF(log, "Plan %s explains stop.", plan_ptr->GetName());

          should_stop = plan_ptr->ShouldStop(event_ptr);

          // plan_ptr explains the stop, next check whether plan_ptr is done,
          // if so, then we should take it and all the plans below it off the
          // stack.

          if (plan_ptr->MischiefManaged()) {
            // We're going to pop the plans up to and including the plan that
            // explains the stop.
            ThreadPlan *prev_plan_ptr = GetPreviousPlan(plan_ptr);

            do {
              if (should_stop)
                current_plan->WillStop();
              PopPlan();
            } while ((current_plan = GetCurrentPlan()) != prev_plan_ptr);
            // Now, if the responsible plan was not "Okay to discard" then
            // we're done, otherwise we forward this to the next plan in the
            // stack below.
            done_processing_current_plan =
                (plan_ptr->IsControllingPlan() && !plan_ptr->OkayToDiscard());
          } else {
            bool should_force_run = plan_ptr->ShouldRunBeforePublicStop();
            if (should_force_run) {
              SetShouldRunBeforePublicStop(true);
              should_stop = false;
            }
            done_processing_current_plan = true;
          }
          break;
        }
      }
    }
  }

  if (!done_processing_current_plan) {
    bool override_stop = false;

    // We're starting from the base plan, so just let it decide;
    if (current_plan->IsBasePlan()) {
      should_stop = current_plan->ShouldStop(event_ptr);
      LLDB_LOGF(log, "Base plan says should stop: %i.", should_stop);
    } else {
      // Otherwise, don't let the base plan override what the other plans say
      // to do, since presumably if there were other plans they would know what
      // to do...
      while (true) {
        if (current_plan->IsBasePlan())
          break;

        should_stop = current_plan->ShouldStop(event_ptr);
        LLDB_LOGF(log, "Plan %s should stop: %d.", current_plan->GetName(),
                  should_stop);
        if (current_plan->MischiefManaged()) {
          if (should_stop)
            current_plan->WillStop();

          if (current_plan->ShouldAutoContinue(event_ptr)) {
            override_stop = true;
            LLDB_LOGF(log, "Plan %s auto-continue: true.",
                      current_plan->GetName());
          }

          // If a Controlling Plan wants to stop, we let it. Otherwise, see if
          // the plan's parent wants to stop.

          PopPlan();
          if (should_stop && current_plan->IsControllingPlan() &&
              !current_plan->OkayToDiscard()) {
            break;
          }

          current_plan = GetCurrentPlan();
          if (current_plan == nullptr) {
            break;
          }
        } else {
          break;
        }
      }
    }

    if (override_stop)
      should_stop = false;
  }

  // One other potential problem is that we set up a controlling plan, then stop
  // in before it is complete - for instance by hitting a breakpoint during a
  // step-over - then do some step/finish/etc operations that wind up past the
  // end point condition of the initial plan.  We don't want to strand the
  // original plan on the stack, This code clears stale plans off the stack.

  if (should_stop) {
    ThreadPlan *plan_ptr = GetCurrentPlan();

    // Discard the stale plans and all plans below them in the stack, plus move
    // the completed plans to the completed plan stack
    while (!plan_ptr->IsBasePlan()) {
      bool stale = plan_ptr->IsPlanStale();
      ThreadPlan *examined_plan = plan_ptr;
      plan_ptr = GetPreviousPlan(examined_plan);

      if (stale) {
        LLDB_LOGF(
            log,
            "Plan %s being discarded in cleanup, it says it is already done.",
            examined_plan->GetName());
        while (GetCurrentPlan() != examined_plan) {
          DiscardPlan();
        }
        if (examined_plan->IsPlanComplete()) {
          // plan is complete but does not explain the stop (example: step to a
          // line with breakpoint), let us move the plan to
          // completed_plan_stack anyway
          PopPlan();
        } else
          DiscardPlan();
      }
    }
  }

  if (log) {
    StreamString s;
    s.IndentMore();
    GetProcess()->DumpThreadPlansForTID(
        s, GetID(), eDescriptionLevelVerbose, true /* internal */,
        false /* condense_trivial */, true /* skip_unreported */);
    LLDB_LOGF(log, "Plan stack final state:\n%s", s.GetData());
    LLDB_LOGF(log, "vvvvvvvv Thread::ShouldStop End (returning %i) vvvvvvvv",
              should_stop);
  }
  return should_stop;
}

Vote Thread::ShouldReportStop(Event *event_ptr) {
  StateType thread_state = GetResumeState();
  StateType temp_thread_state = GetTemporaryResumeState();

  Log *log = GetLog(LLDBLog::Step);

  if (thread_state == eStateSuspended || thread_state == eStateInvalid) {
    LLDB_LOGF(log,
              "Thread::ShouldReportStop() tid = 0x%4.4" PRIx64
              ": returning vote %i (state was suspended or invalid)",
              GetID(), eVoteNoOpinion);
    return eVoteNoOpinion;
  }

  if (temp_thread_state == eStateSuspended ||
      temp_thread_state == eStateInvalid) {
    LLDB_LOGF(log,
              "Thread::ShouldReportStop() tid = 0x%4.4" PRIx64
              ": returning vote %i (temporary state was suspended or invalid)",
              GetID(), eVoteNoOpinion);
    return eVoteNoOpinion;
  }

  if (!ThreadStoppedForAReason()) {
    LLDB_LOGF(log,
              "Thread::ShouldReportStop() tid = 0x%4.4" PRIx64
              ": returning vote %i (thread didn't stop for a reason.)",
              GetID(), eVoteNoOpinion);
    return eVoteNoOpinion;
  }

  if (GetPlans().AnyCompletedPlans()) {
    // Pass skip_private = false to GetCompletedPlan, since we want to ask
    // the last plan, regardless of whether it is private or not.
    LLDB_LOGF(log,
              "Thread::ShouldReportStop() tid = 0x%4.4" PRIx64
              ": returning vote for complete stack's back plan",
              GetID());
    return GetPlans().GetCompletedPlan(false)->ShouldReportStop(event_ptr);
  } else {
    Vote thread_vote = eVoteNoOpinion;
    ThreadPlan *plan_ptr = GetCurrentPlan();
    while (true) {
      if (plan_ptr->PlanExplainsStop(event_ptr)) {
        thread_vote = plan_ptr->ShouldReportStop(event_ptr);
        break;
      }
      if (plan_ptr->IsBasePlan())
        break;
      else
        plan_ptr = GetPreviousPlan(plan_ptr);
    }
    LLDB_LOGF(log,
              "Thread::ShouldReportStop() tid = 0x%4.4" PRIx64
              ": returning vote %i for current plan",
              GetID(), thread_vote);

    return thread_vote;
  }
}

Vote Thread::ShouldReportRun(Event *event_ptr) {
  StateType thread_state = GetResumeState();

  if (thread_state == eStateSuspended || thread_state == eStateInvalid) {
    return eVoteNoOpinion;
  }

  Log *log = GetLog(LLDBLog::Step);
  if (GetPlans().AnyCompletedPlans()) {
    // Pass skip_private = false to GetCompletedPlan, since we want to ask
    // the last plan, regardless of whether it is private or not.
    LLDB_LOGF(log,
              "Current Plan for thread %d(%p) (0x%4.4" PRIx64
              ", %s): %s being asked whether we should report run.",
              GetIndexID(), static_cast<void *>(this), GetID(),
              StateAsCString(GetTemporaryResumeState()),
              GetCompletedPlan()->GetName());

    return GetPlans().GetCompletedPlan(false)->ShouldReportRun(event_ptr);
  } else {
    LLDB_LOGF(log,
              "Current Plan for thread %d(%p) (0x%4.4" PRIx64
              ", %s): %s being asked whether we should report run.",
              GetIndexID(), static_cast<void *>(this), GetID(),
              StateAsCString(GetTemporaryResumeState()),
              GetCurrentPlan()->GetName());

    return GetCurrentPlan()->ShouldReportRun(event_ptr);
  }
}

bool Thread::MatchesSpec(const ThreadSpec *spec) {
  return (spec == nullptr) ? true : spec->ThreadPassesBasicTests(*this);
}

ThreadPlanStack &Thread::GetPlans() const {
  ThreadPlanStack *plans = GetProcess()->FindThreadPlans(GetID());
  if (plans)
    return *plans;

  // History threads don't have a thread plan, but they do ask get asked to
  // describe themselves, which usually involves pulling out the stop reason.
  // That in turn will check for a completed plan on the ThreadPlanStack.
  // Instead of special-casing at that point, we return a Stack with a
  // ThreadPlanNull as its base plan.  That will give the right answers to the
  // queries GetDescription makes, and only assert if you try to run the thread.
  if (!m_null_plan_stack_up)
    m_null_plan_stack_up = std::make_unique<ThreadPlanStack>(*this, true);
  return *m_null_plan_stack_up;
}

void Thread::PushPlan(ThreadPlanSP thread_plan_sp) {
  assert(thread_plan_sp && "Don't push an empty thread plan.");

  Log *log = GetLog(LLDBLog::Step);
  if (log) {
    StreamString s;
    thread_plan_sp->GetDescription(&s, lldb::eDescriptionLevelFull);
    LLDB_LOGF(log, "Thread::PushPlan(0x%p): \"%s\", tid = 0x%4.4" PRIx64 ".",
              static_cast<void *>(this), s.GetData(),
              thread_plan_sp->GetThread().GetID());
  }

  GetPlans().PushPlan(std::move(thread_plan_sp));
}

void Thread::PopPlan() {
  Log *log = GetLog(LLDBLog::Step);
  ThreadPlanSP popped_plan_sp = GetPlans().PopPlan();
  if (log) {
    LLDB_LOGF(log, "Popping plan: \"%s\", tid = 0x%4.4" PRIx64 ".",
              popped_plan_sp->GetName(), popped_plan_sp->GetThread().GetID());
  }
}

void Thread::DiscardPlan() {
  Log *log = GetLog(LLDBLog::Step);
  ThreadPlanSP discarded_plan_sp = GetPlans().DiscardPlan();

  LLDB_LOGF(log, "Discarding plan: \"%s\", tid = 0x%4.4" PRIx64 ".",
            discarded_plan_sp->GetName(),
            discarded_plan_sp->GetThread().GetID());
}

void Thread::AutoCompleteThreadPlans(CompletionRequest &request) const {
  const ThreadPlanStack &plans = GetPlans();
  if (!plans.AnyPlans())
    return;

  // Iterate from the second plan (index: 1) to skip the base plan.
  ThreadPlanSP p;
  uint32_t i = 1;
  while ((p = plans.GetPlanByIndex(i, false))) {
    StreamString strm;
    p->GetDescription(&strm, eDescriptionLevelInitial);
    request.TryCompleteCurrentArg(std::to_string(i), strm.GetString());
    i++;
  }
}

ThreadPlan *Thread::GetCurrentPlan() const {
  return GetPlans().GetCurrentPlan().get();
}

ThreadPlanSP Thread::GetCompletedPlan() const {
  return GetPlans().GetCompletedPlan();
}

ValueObjectSP Thread::GetReturnValueObject() const {
  return GetPlans().GetReturnValueObject();
}

ExpressionVariableSP Thread::GetExpressionVariable() const {
  return GetPlans().GetExpressionVariable();
}

bool Thread::IsThreadPlanDone(ThreadPlan *plan) const {
  return GetPlans().IsPlanDone(plan);
}

bool Thread::WasThreadPlanDiscarded(ThreadPlan *plan) const {
  return GetPlans().WasPlanDiscarded(plan);
}

bool Thread::CompletedPlanOverridesBreakpoint() const {
  return GetPlans().AnyCompletedPlans();
}

ThreadPlan *Thread::GetPreviousPlan(ThreadPlan *current_plan) const{
  return GetPlans().GetPreviousPlan(current_plan);
}

Status Thread::QueueThreadPlan(ThreadPlanSP &thread_plan_sp,
                               bool abort_other_plans) {
  Status status;
  StreamString s;
  if (!thread_plan_sp->ValidatePlan(&s)) {
    DiscardThreadPlansUpToPlan(thread_plan_sp);
    thread_plan_sp.reset();
    status.SetErrorString(s.GetString());
    return status;
  }

  if (abort_other_plans)
    DiscardThreadPlans(true);

  PushPlan(thread_plan_sp);

  // This seems a little funny, but I don't want to have to split up the
  // constructor and the DidPush in the scripted plan, that seems annoying.
  // That means the constructor has to be in DidPush. So I have to validate the
  // plan AFTER pushing it, and then take it off again...
  if (!thread_plan_sp->ValidatePlan(&s)) {
    DiscardThreadPlansUpToPlan(thread_plan_sp);
    thread_plan_sp.reset();
    status.SetErrorString(s.GetString());
    return status;
  }

  return status;
}

bool Thread::DiscardUserThreadPlansUpToIndex(uint32_t plan_index) {
  // Count the user thread plans from the back end to get the number of the one
  // we want to discard:

  ThreadPlan *up_to_plan_ptr = GetPlans().GetPlanByIndex(plan_index).get();
  if (up_to_plan_ptr == nullptr)
    return false;

  DiscardThreadPlansUpToPlan(up_to_plan_ptr);
  return true;
}

void Thread::DiscardThreadPlansUpToPlan(lldb::ThreadPlanSP &up_to_plan_sp) {
  DiscardThreadPlansUpToPlan(up_to_plan_sp.get());
}

void Thread::DiscardThreadPlansUpToPlan(ThreadPlan *up_to_plan_ptr) {
  Log *log = GetLog(LLDBLog::Step);
  LLDB_LOGF(log,
            "Discarding thread plans for thread tid = 0x%4.4" PRIx64
            ", up to %p",
            GetID(), static_cast<void *>(up_to_plan_ptr));
  GetPlans().DiscardPlansUpToPlan(up_to_plan_ptr);
}

void Thread::DiscardThreadPlans(bool force) {
  Log *log = GetLog(LLDBLog::Step);
  if (log) {
    LLDB_LOGF(log,
              "Discarding thread plans for thread (tid = 0x%4.4" PRIx64
              ", force %d)",
              GetID(), force);
  }

  if (force) {
    GetPlans().DiscardAllPlans();
    return;
  }
  GetPlans().DiscardConsultingControllingPlans();
}

Status Thread::UnwindInnermostExpression() {
  Status error;
  ThreadPlan *innermost_expr_plan = GetPlans().GetInnermostExpression();
  if (!innermost_expr_plan) {
    error.SetErrorString("No expressions currently active on this thread");
    return error;
  }
  DiscardThreadPlansUpToPlan(innermost_expr_plan);
  return error;
}

ThreadPlanSP Thread::QueueBasePlan(bool abort_other_plans) {
  ThreadPlanSP thread_plan_sp(new ThreadPlanBase(*this));
  QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

ThreadPlanSP Thread::QueueThreadPlanForStepSingleInstruction(
    bool step_over, bool abort_other_plans, bool stop_other_threads,
    Status &status) {
  ThreadPlanSP thread_plan_sp(new ThreadPlanStepInstruction(
      *this, step_over, stop_other_threads, eVoteNoOpinion, eVoteNoOpinion));
  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

ThreadPlanSP Thread::QueueThreadPlanForStepOverRange(
    bool abort_other_plans, const AddressRange &range,
    const SymbolContext &addr_context, lldb::RunMode stop_other_threads,
    Status &status, LazyBool step_out_avoids_code_withoug_debug_info) {
  ThreadPlanSP thread_plan_sp;
  thread_plan_sp = std::make_shared<ThreadPlanStepOverRange>(
      *this, range, addr_context, stop_other_threads,
      step_out_avoids_code_withoug_debug_info);

  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

// Call the QueueThreadPlanForStepOverRange method which takes an address
// range.
ThreadPlanSP Thread::QueueThreadPlanForStepOverRange(
    bool abort_other_plans, const LineEntry &line_entry,
    const SymbolContext &addr_context, lldb::RunMode stop_other_threads,
    Status &status, LazyBool step_out_avoids_code_withoug_debug_info) {
  const bool include_inlined_functions = true;
  auto address_range =
      line_entry.GetSameLineContiguousAddressRange(include_inlined_functions);
  return QueueThreadPlanForStepOverRange(
      abort_other_plans, address_range, addr_context, stop_other_threads,
      status, step_out_avoids_code_withoug_debug_info);
}

ThreadPlanSP Thread::QueueThreadPlanForStepInRange(
    bool abort_other_plans, const AddressRange &range,
    const SymbolContext &addr_context, const char *step_in_target,
    lldb::RunMode stop_other_threads, Status &status,
    LazyBool step_in_avoids_code_without_debug_info,
    LazyBool step_out_avoids_code_without_debug_info) {
  ThreadPlanSP thread_plan_sp(new ThreadPlanStepInRange(
      *this, range, addr_context, step_in_target, stop_other_threads,
      step_in_avoids_code_without_debug_info,
      step_out_avoids_code_without_debug_info));
  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

// Call the QueueThreadPlanForStepInRange method which takes an address range.
ThreadPlanSP Thread::QueueThreadPlanForStepInRange(
    bool abort_other_plans, const LineEntry &line_entry,
    const SymbolContext &addr_context, const char *step_in_target,
    lldb::RunMode stop_other_threads, Status &status,
    LazyBool step_in_avoids_code_without_debug_info,
    LazyBool step_out_avoids_code_without_debug_info) {
  const bool include_inlined_functions = false;
  return QueueThreadPlanForStepInRange(
      abort_other_plans,
      line_entry.GetSameLineContiguousAddressRange(include_inlined_functions),
      addr_context, step_in_target, stop_other_threads, status,
      step_in_avoids_code_without_debug_info,
      step_out_avoids_code_without_debug_info);
}

ThreadPlanSP Thread::QueueThreadPlanForStepOut(
    bool abort_other_plans, SymbolContext *addr_context, bool first_insn,
    bool stop_other_threads, Vote report_stop_vote, Vote report_run_vote,
    uint32_t frame_idx, Status &status,
    LazyBool step_out_avoids_code_without_debug_info) {
  ThreadPlanSP thread_plan_sp(new ThreadPlanStepOut(
      *this, addr_context, first_insn, stop_other_threads, report_stop_vote,
      report_run_vote, frame_idx, step_out_avoids_code_without_debug_info));

  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

ThreadPlanSP Thread::QueueThreadPlanForStepOutNoShouldStop(
    bool abort_other_plans, SymbolContext *addr_context, bool first_insn,
    bool stop_other_threads, Vote report_stop_vote, Vote report_run_vote,
    uint32_t frame_idx, Status &status, bool continue_to_next_branch) {
  const bool calculate_return_value =
      false; // No need to calculate the return value here.
  ThreadPlanSP thread_plan_sp(new ThreadPlanStepOut(
      *this, addr_context, first_insn, stop_other_threads, report_stop_vote,
      report_run_vote, frame_idx, eLazyBoolNo, continue_to_next_branch,
      calculate_return_value));

  ThreadPlanStepOut *new_plan =
      static_cast<ThreadPlanStepOut *>(thread_plan_sp.get());
  new_plan->ClearShouldStopHereCallbacks();

  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

ThreadPlanSP Thread::QueueThreadPlanForStepThrough(StackID &return_stack_id,
                                                   bool abort_other_plans,
                                                   bool stop_other_threads,
                                                   Status &status) {
  ThreadPlanSP thread_plan_sp(
      new ThreadPlanStepThrough(*this, return_stack_id, stop_other_threads));
  if (!thread_plan_sp || !thread_plan_sp->ValidatePlan(nullptr))
    return ThreadPlanSP();

  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

ThreadPlanSP Thread::QueueThreadPlanForRunToAddress(bool abort_other_plans,
                                                    Address &target_addr,
                                                    bool stop_other_threads,
                                                    Status &status) {
  ThreadPlanSP thread_plan_sp(
      new ThreadPlanRunToAddress(*this, target_addr, stop_other_threads));

  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

ThreadPlanSP Thread::QueueThreadPlanForStepUntil(
    bool abort_other_plans, lldb::addr_t *address_list, size_t num_addresses,
    bool stop_other_threads, uint32_t frame_idx, Status &status) {
  ThreadPlanSP thread_plan_sp(new ThreadPlanStepUntil(
      *this, address_list, num_addresses, stop_other_threads, frame_idx));

  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

lldb::ThreadPlanSP Thread::QueueThreadPlanForStepScripted(
    bool abort_other_plans, const char *class_name,
    StructuredData::ObjectSP extra_args_sp, bool stop_other_threads,
    Status &status) {

  ThreadPlanSP thread_plan_sp(new ThreadPlanPython(
      *this, class_name, StructuredDataImpl(extra_args_sp)));
  thread_plan_sp->SetStopOthers(stop_other_threads);
  status = QueueThreadPlan(thread_plan_sp, abort_other_plans);
  return thread_plan_sp;
}

uint32_t Thread::GetIndexID() const { return m_index_id; }

TargetSP Thread::CalculateTarget() {
  TargetSP target_sp;
  ProcessSP process_sp(GetProcess());
  if (process_sp)
    target_sp = process_sp->CalculateTarget();
  return target_sp;
}

ProcessSP Thread::CalculateProcess() { return GetProcess(); }

ThreadSP Thread::CalculateThread() { return shared_from_this(); }

StackFrameSP Thread::CalculateStackFrame() { return StackFrameSP(); }

void Thread::CalculateExecutionContext(ExecutionContext &exe_ctx) {
  exe_ctx.SetContext(shared_from_this());
}

StackFrameListSP Thread::GetStackFrameList() {
  std::lock_guard<std::recursive_mutex> guard(m_frame_mutex);

  if (!m_curr_frames_sp)
    m_curr_frames_sp =
        std::make_shared<StackFrameList>(*this, m_prev_frames_sp, true);

  return m_curr_frames_sp;
}

std::optional<addr_t> Thread::GetPreviousFrameZeroPC() {
  return m_prev_framezero_pc;
}

void Thread::ClearStackFrames() {
  std::lock_guard<std::recursive_mutex> guard(m_frame_mutex);

  GetUnwinder().Clear();
  m_prev_framezero_pc.reset();
  if (RegisterContextSP reg_ctx_sp = GetRegisterContext())
    m_prev_framezero_pc = reg_ctx_sp->GetPC();

  // Only store away the old "reference" StackFrameList if we got all its
  // frames:
  // FIXME: At some point we can try to splice in the frames we have fetched
  // into the new frame as we make it, but let's not try that now.
  if (m_curr_frames_sp && m_curr_frames_sp->GetAllFramesFetched())
    m_prev_frames_sp.swap(m_curr_frames_sp);
  m_curr_frames_sp.reset();

  m_extended_info.reset();
  m_extended_info_fetched = false;
}

lldb::StackFrameSP Thread::GetFrameWithConcreteFrameIndex(uint32_t unwind_idx) {
  return GetStackFrameList()->GetFrameWithConcreteFrameIndex(unwind_idx);
}

Status Thread::ReturnFromFrameWithIndex(uint32_t frame_idx,
                                        lldb::ValueObjectSP return_value_sp,
                                        bool broadcast) {
  StackFrameSP frame_sp = GetStackFrameAtIndex(frame_idx);
  Status return_error;

  if (!frame_sp) {
    return_error.SetErrorStringWithFormat(
        "Could not find frame with index %d in thread 0x%" PRIx64 ".",
        frame_idx, GetID());
  }

  return ReturnFromFrame(frame_sp, return_value_sp, broadcast);
}

Status Thread::ReturnFromFrame(lldb::StackFrameSP frame_sp,
                               lldb::ValueObjectSP return_value_sp,
                               bool broadcast) {
  Status return_error;

  if (!frame_sp) {
    return_error.SetErrorString("Can't return to a null frame.");
    return return_error;
  }

  Thread *thread = frame_sp->GetThread().get();
  uint32_t older_frame_idx = frame_sp->GetFrameIndex() + 1;
  StackFrameSP older_frame_sp = thread->GetStackFrameAtIndex(older_frame_idx);
  if (!older_frame_sp) {
    return_error.SetErrorString("No older frame to return to.");
    return return_error;
  }

  if (return_value_sp) {
    lldb::ABISP abi = thread->GetProcess()->GetABI();
    if (!abi) {
      return_error.SetErrorString("Could not find ABI to set return value.");
      return return_error;
    }
    SymbolContext sc = frame_sp->GetSymbolContext(eSymbolContextFunction);

    // FIXME: ValueObject::Cast doesn't currently work correctly, at least not
    // for scalars.
    // Turn that back on when that works.
    if (/* DISABLES CODE */ (false) && sc.function != nullptr) {
      Type *function_type = sc.function->GetType();
      if (function_type) {
        CompilerType return_type =
            sc.function->GetCompilerType().GetFunctionReturnType();
        if (return_type) {
          StreamString s;
          return_type.DumpTypeDescription(&s);
          ValueObjectSP cast_value_sp = return_value_sp->Cast(return_type);
          if (cast_value_sp) {
            cast_value_sp->SetFormat(eFormatHex);
            return_value_sp = cast_value_sp;
          }
        }
      }
    }

    return_error = abi->SetReturnValueObject(older_frame_sp, return_value_sp);
    if (!return_error.Success())
      return return_error;
  }

  // Now write the return registers for the chosen frame: Note, we can't use
  // ReadAllRegisterValues->WriteAllRegisterValues, since the read & write cook
  // their data

  StackFrameSP youngest_frame_sp = thread->GetStackFrameAtIndex(0);
  if (youngest_frame_sp) {
    lldb::RegisterContextSP reg_ctx_sp(youngest_frame_sp->GetRegisterContext());
    if (reg_ctx_sp) {
      bool copy_success = reg_ctx_sp->CopyFromRegisterContext(
          older_frame_sp->GetRegisterContext());
      if (copy_success) {
        thread->DiscardThreadPlans(true);
        thread->ClearStackFrames();
        if (broadcast && EventTypeHasListeners(eBroadcastBitStackChanged)) {
          auto data_sp = std::make_shared<ThreadEventData>(shared_from_this());
          BroadcastEvent(eBroadcastBitStackChanged, data_sp);
        }
      } else {
        return_error.SetErrorString("Could not reset register values.");
      }
    } else {
      return_error.SetErrorString("Frame has no register context.");
    }
  } else {
    return_error.SetErrorString("Returned past top frame.");
  }
  return return_error;
}

static void DumpAddressList(Stream &s, const std::vector<Address> &list,
                            ExecutionContextScope *exe_scope) {
  for (size_t n = 0; n < list.size(); n++) {
    s << "\t";
    list[n].Dump(&s, exe_scope, Address::DumpStyleResolvedDescription,
                 Address::DumpStyleSectionNameOffset);
    s << "\n";
  }
}

Status Thread::JumpToLine(const FileSpec &file, uint32_t line,
                          bool can_leave_function, std::string *warnings) {
  ExecutionContext exe_ctx(GetStackFrameAtIndex(0));
  Target *target = exe_ctx.GetTargetPtr();
  TargetSP target_sp = exe_ctx.GetTargetSP();
  RegisterContext *reg_ctx = exe_ctx.GetRegisterContext();
  StackFrame *frame = exe_ctx.GetFramePtr();
  const SymbolContext &sc = frame->GetSymbolContext(eSymbolContextFunction);

  // Find candidate locations.
  std::vector<Address> candidates, within_function, outside_function;
  target->GetImages().FindAddressesForLine(target_sp, file, line, sc.function,
                                           within_function, outside_function);

  // If possible, we try and stay within the current function. Within a
  // function, we accept multiple locations (optimized code may do this,
  // there's no solution here so we do the best we can). However if we're
  // trying to leave the function, we don't know how to pick the right
  // location, so if there's more than one then we bail.
  if (!within_function.empty())
    candidates = within_function;
  else if (outside_function.size() == 1 && can_leave_function)
    candidates = outside_function;

  // Check if we got anything.
  if (candidates.empty()) {
    if (outside_function.empty()) {
      return Status("Cannot locate an address for %s:%i.",
                    file.GetFilename().AsCString(), line);
    } else if (outside_function.size() == 1) {
      return Status("%s:%i is outside the current function.",
                    file.GetFilename().AsCString(), line);
    } else {
      StreamString sstr;
      DumpAddressList(sstr, outside_function, target);
      return Status("%s:%i has multiple candidate locations:\n%s",
                    file.GetFilename().AsCString(), line, sstr.GetData());
    }
  }

  // Accept the first location, warn about any others.
  Address dest = candidates[0];
  if (warnings && candidates.size() > 1) {
    StreamString sstr;
    sstr.Printf("%s:%i appears multiple times in this function, selecting the "
                "first location:\n",
                file.GetFilename().AsCString(), line);
    DumpAddressList(sstr, candidates, target);
    *warnings = std::string(sstr.GetString());
  }

  if (!reg_ctx->SetPC(dest))
    return Status("Cannot change PC to target address.");

  return Status();
}

bool Thread::DumpUsingFormat(Stream &strm, uint32_t frame_idx,
                             const FormatEntity::Entry *format) {
  ExecutionContext exe_ctx(shared_from_this());
  Process *process = exe_ctx.GetProcessPtr();
  if (!process || !format)
    return false;

  StackFrameSP frame_sp;
  SymbolContext frame_sc;
  if (frame_idx != LLDB_INVALID_FRAME_ID) {
    frame_sp = GetStackFrameAtIndex(frame_idx);
    if (frame_sp) {
      exe_ctx.SetFrameSP(frame_sp);
      frame_sc = frame_sp->GetSymbolContext(eSymbolContextEverything);
    }
  }

  return FormatEntity::Format(*format, strm, frame_sp ? &frame_sc : nullptr,
                              &exe_ctx, nullptr, nullptr, false, false);
}

void Thread::DumpUsingSettingsFormat(Stream &strm, uint32_t frame_idx,
                                     bool stop_format) {
  ExecutionContext exe_ctx(shared_from_this());

  const FormatEntity::Entry *thread_format;
  if (stop_format)
    thread_format = exe_ctx.GetTargetRef().GetDebugger().GetThreadStopFormat();
  else
    thread_format = exe_ctx.GetTargetRef().GetDebugger().GetThreadFormat();

  assert(thread_format);

  DumpUsingFormat(strm, frame_idx, thread_format);
}

void Thread::SettingsInitialize() {}

void Thread::SettingsTerminate() {}

lldb::addr_t Thread::GetThreadPointer() {
  if (m_reg_context_sp)
    return m_reg_context_sp->GetThreadPointer();
  return LLDB_INVALID_ADDRESS;
}

addr_t Thread::GetThreadLocalData(const ModuleSP module,
                                  lldb::addr_t tls_file_addr) {
  // The default implementation is to ask the dynamic loader for it. This can
  // be overridden for specific platforms.
  DynamicLoader *loader = GetProcess()->GetDynamicLoader();
  if (loader)
    return loader->GetThreadLocalData(module, shared_from_this(),
                                      tls_file_addr);
  else
    return LLDB_INVALID_ADDRESS;
}

bool Thread::SafeToCallFunctions() {
  Process *process = GetProcess().get();
  if (process) {
    DynamicLoader *loader = GetProcess()->GetDynamicLoader();
    if (loader && loader->IsFullyInitialized() == false)
      return false;

    SystemRuntime *runtime = process->GetSystemRuntime();
    if (runtime) {
      return runtime->SafeToCallFunctionsOnThisThread(shared_from_this());
    }
  }
  return true;
}

lldb::StackFrameSP
Thread::GetStackFrameSPForStackFramePtr(StackFrame *stack_frame_ptr) {
  return GetStackFrameList()->GetStackFrameSPForStackFramePtr(stack_frame_ptr);
}

std::string Thread::StopReasonAsString(lldb::StopReason reason) {
  switch (reason) {
  case eStopReasonInvalid:
    return "invalid";
  case eStopReasonNone:
    return "none";
  case eStopReasonTrace:
    return "trace";
  case eStopReasonBreakpoint:
    return "breakpoint";
  case eStopReasonWatchpoint:
    return "watchpoint";
  case eStopReasonSignal:
    return "signal";
  case eStopReasonException:
    return "exception";
  case eStopReasonExec:
    return "exec";
  case eStopReasonFork:
    return "fork";
  case eStopReasonVFork:
    return "vfork";
  case eStopReasonVForkDone:
    return "vfork done";
  case eStopReasonPlanComplete:
    return "plan complete";
  case eStopReasonThreadExiting:
    return "thread exiting";
  case eStopReasonInstrumentation:
    return "instrumentation break";
  case eStopReasonProcessorTrace:
    return "processor trace";
  }

  return "StopReason = " + std::to_string(reason);
}

std::string Thread::RunModeAsString(lldb::RunMode mode) {
  switch (mode) {
  case eOnlyThisThread:
    return "only this thread";
  case eAllThreads:
    return "all threads";
  case eOnlyDuringStepping:
    return "only during stepping";
  }

  return "RunMode = " + std::to_string(mode);
}

size_t Thread::GetStatus(Stream &strm, uint32_t start_frame,
                         uint32_t num_frames, uint32_t num_frames_with_source,
                         bool stop_format, bool only_stacks) {

  if (!only_stacks) {
    ExecutionContext exe_ctx(shared_from_this());
    Target *target = exe_ctx.GetTargetPtr();
    Process *process = exe_ctx.GetProcessPtr();
    strm.Indent();
    bool is_selected = false;
    if (process) {
      if (process->GetThreadList().GetSelectedThread().get() == this)
        is_selected = true;
    }
    strm.Printf("%c ", is_selected ? '*' : ' ');
    if (target && target->GetDebugger().GetUseExternalEditor()) {
      StackFrameSP frame_sp = GetStackFrameAtIndex(start_frame);
      if (frame_sp) {
        SymbolContext frame_sc(
            frame_sp->GetSymbolContext(eSymbolContextLineEntry));
        if (frame_sc.line_entry.line != 0 && frame_sc.line_entry.GetFile()) {
          if (llvm::Error e = Host::OpenFileInExternalEditor(
                  target->GetDebugger().GetExternalEditor(),
                  frame_sc.line_entry.GetFile(), frame_sc.line_entry.line)) {
            LLDB_LOG_ERROR(GetLog(LLDBLog::Host), std::move(e),
                           "OpenFileInExternalEditor failed: {0}");
          }
        }
      }
    }

    DumpUsingSettingsFormat(strm, start_frame, stop_format);
  }

  size_t num_frames_shown = 0;
  if (num_frames > 0) {
    strm.IndentMore();

    const bool show_frame_info = true;
    const bool show_frame_unique = only_stacks;
    const char *selected_frame_marker = nullptr;
    if (num_frames == 1 || only_stacks ||
        (GetID() != GetProcess()->GetThreadList().GetSelectedThread()->GetID()))
      strm.IndentMore();
    else
      selected_frame_marker = "* ";

    num_frames_shown = GetStackFrameList()->GetStatus(
        strm, start_frame, num_frames, show_frame_info, num_frames_with_source,
        show_frame_unique, selected_frame_marker);
    if (num_frames == 1)
      strm.IndentLess();
    strm.IndentLess();
  }
  return num_frames_shown;
}

bool Thread::GetDescription(Stream &strm, lldb::DescriptionLevel level,
                            bool print_json_thread, bool print_json_stopinfo) {
  const bool stop_format = false;
  DumpUsingSettingsFormat(strm, 0, stop_format);
  strm.Printf("\n");

  StructuredData::ObjectSP thread_info = GetExtendedInfo();

  if (print_json_thread || print_json_stopinfo) {
    if (thread_info && print_json_thread) {
      thread_info->Dump(strm);
      strm.Printf("\n");
    }

    if (print_json_stopinfo && m_stop_info_sp) {
      StructuredData::ObjectSP stop_info = m_stop_info_sp->GetExtendedInfo();
      if (stop_info) {
        stop_info->Dump(strm);
        strm.Printf("\n");
      }
    }

    return true;
  }

  if (thread_info) {
    StructuredData::ObjectSP activity =
        thread_info->GetObjectForDotSeparatedPath("activity");
    StructuredData::ObjectSP breadcrumb =
        thread_info->GetObjectForDotSeparatedPath("breadcrumb");
    StructuredData::ObjectSP messages =
        thread_info->GetObjectForDotSeparatedPath("trace_messages");

    bool printed_activity = false;
    if (activity && activity->GetType() == eStructuredDataTypeDictionary) {
      StructuredData::Dictionary *activity_dict = activity->GetAsDictionary();
      StructuredData::ObjectSP id = activity_dict->GetValueForKey("id");
      StructuredData::ObjectSP name = activity_dict->GetValueForKey("name");
      if (name && name->GetType() == eStructuredDataTypeString && id &&
          id->GetType() == eStructuredDataTypeInteger) {
        strm.Format("  Activity '{0}', {1:x}\n",
                    name->GetAsString()->GetValue(),
                    id->GetUnsignedIntegerValue());
      }
      printed_activity = true;
    }
    bool printed_breadcrumb = false;
    if (breadcrumb && breadcrumb->GetType() == eStructuredDataTypeDictionary) {
      if (printed_activity)
        strm.Printf("\n");
      StructuredData::Dictionary *breadcrumb_dict =
          breadcrumb->GetAsDictionary();
      StructuredData::ObjectSP breadcrumb_text =
          breadcrumb_dict->GetValueForKey("name");
      if (breadcrumb_text &&
          breadcrumb_text->GetType() == eStructuredDataTypeString) {
        strm.Format("  Current Breadcrumb: {0}\n",
                    breadcrumb_text->GetAsString()->GetValue());
      }
      printed_breadcrumb = true;
    }
    if (messages && messages->GetType() == eStructuredDataTypeArray) {
      if (printed_breadcrumb)
        strm.Printf("\n");
      StructuredData::Array *messages_array = messages->GetAsArray();
      const size_t msg_count = messages_array->GetSize();
      if (msg_count > 0) {
        strm.Printf("  %zu trace messages:\n", msg_count);
        for (size_t i = 0; i < msg_count; i++) {
          StructuredData::ObjectSP message = messages_array->GetItemAtIndex(i);
          if (message && message->GetType() == eStructuredDataTypeDictionary) {
            StructuredData::Dictionary *message_dict =
                message->GetAsDictionary();
            StructuredData::ObjectSP message_text =
                message_dict->GetValueForKey("message");
            if (message_text &&
                message_text->GetType() == eStructuredDataTypeString) {
              strm.Format("    {0}\n", message_text->GetAsString()->GetValue());
            }
          }
        }
      }
    }
  }

  return true;
}

size_t Thread::GetStackFrameStatus(Stream &strm, uint32_t first_frame,
                                   uint32_t num_frames, bool show_frame_info,
                                   uint32_t num_frames_with_source) {
  return GetStackFrameList()->GetStatus(
      strm, first_frame, num_frames, show_frame_info, num_frames_with_source);
}

Unwind &Thread::GetUnwinder() {
  if (!m_unwinder_up)
    m_unwinder_up = std::make_unique<UnwindLLDB>(*this);
  return *m_unwinder_up;
}

void Thread::Flush() {
  ClearStackFrames();
  m_reg_context_sp.reset();
}

bool Thread::IsStillAtLastBreakpointHit() {
  // If we are currently stopped at a breakpoint, always return that stopinfo
  // and don't reset it. This allows threads to maintain their breakpoint
  // stopinfo, such as when thread-stepping in multithreaded programs.
  if (m_stop_info_sp) {
    StopReason stop_reason = m_stop_info_sp->GetStopReason();
    if (stop_reason == lldb::eStopReasonBreakpoint) {
      uint64_t value = m_stop_info_sp->GetValue();
      lldb::RegisterContextSP reg_ctx_sp(GetRegisterContext());
      if (reg_ctx_sp) {
        lldb::addr_t pc = reg_ctx_sp->GetPC();
        BreakpointSiteSP bp_site_sp =
            GetProcess()->GetBreakpointSiteList().FindByAddress(pc);
        if (bp_site_sp && static_cast<break_id_t>(value) == bp_site_sp->GetID())
          return true;
      }
    }
  }
  return false;
}

Status Thread::StepIn(bool source_step,
                      LazyBool step_in_avoids_code_without_debug_info,
                      LazyBool step_out_avoids_code_without_debug_info)

{
  Status error;
  Process *process = GetProcess().get();
  if (StateIsStoppedState(process->GetState(), true)) {
    StackFrameSP frame_sp = GetStackFrameAtIndex(0);
    ThreadPlanSP new_plan_sp;
    const lldb::RunMode run_mode = eOnlyThisThread;
    const bool abort_other_plans = false;

    if (source_step && frame_sp && frame_sp->HasDebugInformation()) {
      SymbolContext sc(frame_sp->GetSymbolContext(eSymbolContextEverything));
      new_plan_sp = QueueThreadPlanForStepInRange(
          abort_other_plans, sc.line_entry, sc, nullptr, run_mode, error,
          step_in_avoids_code_without_debug_info,
          step_out_avoids_code_without_debug_info);
    } else {
      new_plan_sp = QueueThreadPlanForStepSingleInstruction(
          false, abort_other_plans, run_mode, error);
    }

    new_plan_sp->SetIsControllingPlan(true);
    new_plan_sp->SetOkayToDiscard(false);

    // Why do we need to set the current thread by ID here???
    process->GetThreadList().SetSelectedThreadByID(GetID());
    error = process->Resume();
  } else {
    error.SetErrorString("process not stopped");
  }
  return error;
}

Status Thread::StepOver(bool source_step,
                        LazyBool step_out_avoids_code_without_debug_info) {
  Status error;
  Process *process = GetProcess().get();
  if (StateIsStoppedState(process->GetState(), true)) {
    StackFrameSP frame_sp = GetStackFrameAtIndex(0);
    ThreadPlanSP new_plan_sp;

    const lldb::RunMode run_mode = eOnlyThisThread;
    const bool abort_other_plans = false;

    if (source_step && frame_sp && frame_sp->HasDebugInformation()) {
      SymbolContext sc(frame_sp->GetSymbolContext(eSymbolContextEverything));
      new_plan_sp = QueueThreadPlanForStepOverRange(
          abort_other_plans, sc.line_entry, sc, run_mode, error,
          step_out_avoids_code_without_debug_info);
    } else {
      new_plan_sp = QueueThreadPlanForStepSingleInstruction(
          true, abort_other_plans, run_mode, error);
    }

    new_plan_sp->SetIsControllingPlan(true);
    new_plan_sp->SetOkayToDiscard(false);

    // Why do we need to set the current thread by ID here???
    process->GetThreadList().SetSelectedThreadByID(GetID());
    error = process->Resume();
  } else {
    error.SetErrorString("process not stopped");
  }
  return error;
}

Status Thread::StepOut(uint32_t frame_idx) {
  Status error;
  Process *process = GetProcess().get();
  if (StateIsStoppedState(process->GetState(), true)) {
    const bool first_instruction = false;
    const bool stop_other_threads = false;
    const bool abort_other_plans = false;

    ThreadPlanSP new_plan_sp(QueueThreadPlanForStepOut(
        abort_other_plans, nullptr, first_instruction, stop_other_threads,
        eVoteYes, eVoteNoOpinion, frame_idx, error));

    new_plan_sp->SetIsControllingPlan(true);
    new_plan_sp->SetOkayToDiscard(false);

    // Why do we need to set the current thread by ID here???
    process->GetThreadList().SetSelectedThreadByID(GetID());
    error = process->Resume();
  } else {
    error.SetErrorString("process not stopped");
  }
  return error;
}

ValueObjectSP Thread::GetCurrentException() {
  if (auto frame_sp = GetStackFrameAtIndex(0))
    if (auto recognized_frame = frame_sp->GetRecognizedFrame())
      if (auto e = recognized_frame->GetExceptionObject())
        return e;

  // NOTE: Even though this behavior is generalized, only ObjC is actually
  // supported at the moment.
  for (LanguageRuntime *runtime : GetProcess()->GetLanguageRuntimes()) {
    if (auto e = runtime->GetExceptionObjectForThread(shared_from_this()))
      return e;
  }

  return ValueObjectSP();
}

ThreadSP Thread::GetCurrentExceptionBacktrace() {
  ValueObjectSP exception = GetCurrentException();
  if (!exception)
    return ThreadSP();

  // NOTE: Even though this behavior is generalized, only ObjC is actually
  // supported at the moment.
  for (LanguageRuntime *runtime : GetProcess()->GetLanguageRuntimes()) {
    if (auto bt = runtime->GetBacktraceThreadFromException(exception))
      return bt;
  }

  return ThreadSP();
}

lldb::ValueObjectSP Thread::GetSiginfoValue() {
  ProcessSP process_sp = GetProcess();
  assert(process_sp);
  Target &target = process_sp->GetTarget();
  PlatformSP platform_sp = target.GetPlatform();
  assert(platform_sp);
  ArchSpec arch = target.GetArchitecture();

  CompilerType type = platform_sp->GetSiginfoType(arch.GetTriple());
  if (!type.IsValid())
    return ValueObjectConstResult::Create(&target, Status("no siginfo_t for the platform"));

  std::optional<uint64_t> type_size = type.GetByteSize(nullptr);
  assert(type_size);
  llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>> data =
      GetSiginfo(*type_size);
  if (!data)
    return ValueObjectConstResult::Create(&target, Status(data.takeError()));

  DataExtractor data_extractor{data.get()->getBufferStart(), data.get()->getBufferSize(),
    process_sp->GetByteOrder(), arch.GetAddressByteSize()};
  return ValueObjectConstResult::Create(&target, type, ConstString("__lldb_siginfo"), data_extractor);
}
