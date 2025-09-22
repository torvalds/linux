//===-- ThreadPlan.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLAN_H
#define LLDB_TARGET_THREADPLAN_H

#include <mutex>
#include <string>

#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanTracer.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//  ThreadPlan:
//
//  This is the pure virtual base class for thread plans.
//
//  The thread plans provide the "atoms" of behavior that all the logical
//  process control, either directly from commands or through more complex
//  composite plans will rely on.
//
//  Plan Stack:
//
//  The thread maintaining a thread plan stack, and you program the actions of
//  a particular thread by pushing plans onto the plan stack.  There is always
//  a "Current" plan, which is the top of the plan stack, though in some cases
//  a plan may defer to plans higher in the stack for some piece of information
//  (let us define that the plan stack grows downwards).
//
//  The plan stack is never empty, there is always a Base Plan which persists
//  through the life of the running process.
//
//
//  Creating Plans:
//
//  The thread plan is generally created and added to the plan stack through
//  the QueueThreadPlanFor... API in lldb::Thread.  Those API's will return the
//  plan that performs the named operation in a manner appropriate for the
//  current process.  The plans in lldb/source/Target are generic
//  implementations, but a Process plugin can override them.
//
//  ValidatePlan is then called.  If it returns false, the plan is unshipped.
//  This is a little convenience which keeps us from having to error out of the
//  constructor.
//
//  Then the plan is added to the plan stack.  When the plan is added to the
//  plan stack its DidPush will get called.  This is useful if a plan wants to
//  push any additional plans as it is constructed, since you need to make sure
//  you're already on the stack before you push additional plans.
//
//  Completed Plans:
//
//  When the target process stops the plans are queried, among other things,
//  for whether their job is done.  If it is they are moved from the plan stack
//  to the Completed Plan stack in reverse order from their position on the
//  plan stack (since multiple plans may be done at a given stop.)  This is
//  used primarily so that the lldb::Thread::StopInfo for the thread can be set
//  properly.  If one plan pushes another to achieve part of its job, but it
//  doesn't want that sub-plan to be the one that sets the StopInfo, then call
//  SetPrivate on the sub-plan when you create it, and the Thread will pass
//  over that plan in reporting the reason for the stop.
//
//  Discarded plans:
//
//  Your plan may also get discarded, i.e. moved from the plan stack to the
//  "discarded plan stack".  This can happen, for instance, if the plan is
//  calling a function and the function call crashes and you want to unwind the
//  attempt to call.  So don't assume that your plan will always successfully
//  stop.  Which leads to:
//
//  Cleaning up after your plans:
//
//  When the plan is moved from the plan stack its DidPop method is always
//  called, no matter why.  Once it is moved off the plan stack it is done, and
//  won't get a chance to run again.  So you should undo anything that affects
//  target state in this method.  But be sure to leave the plan able to
//  correctly fill the StopInfo, however.  N.B. Don't wait to do clean up
//  target state till the destructor, since that will usually get called when
//  the target resumes, and you want to leave the target state correct for new
//  plans in the time between when your plan gets unshipped and the next
//  resume.
//
//  Thread State Checkpoint:
//
//  Note that calling functions on target process (ThreadPlanCallFunction)
//  changes current thread state. The function can be called either by direct
//  user demand or internally, for example lldb allocates memory on device to
//  calculate breakpoint condition expression - on Linux it is performed by
//  calling mmap on device.  ThreadStateCheckpoint saves Thread state (stop
//  info and completed plan stack) to restore it after completing function
//  call.
//
//  Over the lifetime of the plan, various methods of the ThreadPlan are then
//  called in response to changes of state in the process we are debugging as
//  follows:
//
//  Resuming:
//
//  When the target process is about to be restarted, the plan's WillResume
//  method is called, giving the plan a chance to prepare for the run.  If
//  WillResume returns false, then the process is not restarted.  Be sure to
//  set an appropriate error value in the Process if you have to do this.
//  Note, ThreadPlans actually implement DoWillResume, WillResume wraps that
//  call.
//
//  Next the "StopOthers" method of all the threads are polled, and if one
//  thread's Current plan returns "true" then only that thread gets to run.  If
//  more than one returns "true" the threads that want to run solo get run one
//  by one round robin fashion.  Otherwise all are let to run.
//
//  Note, the way StopOthers is implemented, the base class implementation just
//  asks the previous plan.  So if your plan has no opinion about whether it
//  should run stopping others or not, just don't implement StopOthers, and the
//  parent will be asked.
//
//  Finally, for each thread that is running, it run state is set to the return
//  of RunState from the thread's Current plan.
//
//  Responding to a stop:
//
//  When the target process stops, the plan is called in the following stages:
//
//  First the thread asks the Current Plan if it can handle this stop by
//  calling PlanExplainsStop.  If the Current plan answers "true" then it is
//  asked if the stop should percolate all the way to the user by calling the
//  ShouldStop method.  If the current plan doesn't explain the stop, then we
//  query up the plan stack for a plan that does explain the stop.  The plan
//  that does explain the stop then needs to figure out what to do about the
//  plans below it in the stack.  If the stop is recoverable, then the plan
//  that understands it can just do what it needs to set up to restart, and
//  then continue.  Otherwise, the plan that understood the stop should call
//  DiscardPlanStack to clean up the stack below it.  Note, plans actually
//  implement DoPlanExplainsStop, the result is cached in PlanExplainsStop so
//  the DoPlanExplainsStop itself will only get called once per stop.
//
//  Controlling plans:
//
//  In the normal case, when we decide to stop, we will  collapse the plan
//  stack up to the point of the plan that understood the stop reason.
//  However, if a plan wishes to stay on the stack after an event it didn't
//  directly handle it can designate itself a "Controlling" plan by responding
//  true to IsControllingPlan, and then if it wants not to be discarded, it can
//  return false to OkayToDiscard, and it and all its dependent plans will be
//  preserved when we resume execution.
//
//  The other effect of being a controlling plan is that when the Controlling
//  plan is
//  done , if it has set "OkayToDiscard" to false, then it will be popped &
//  execution will stop and return to the user.  Remember that if OkayToDiscard
//  is false, the plan will be popped and control will be given to the next
//  plan above it on the stack  So setting OkayToDiscard to false means the
//  user will regain control when the ControllingPlan is completed.
//
//  Between these two controls this allows things like: a
//  ControllingPlan/DontDiscard Step Over to hit a breakpoint, stop and return
//  control to the user, but then when the user continues, the step out
//  succeeds.  Even more tricky, when the breakpoint is hit, the user can
//  continue to step in/step over/etc, and finally when they continue, they
//  will finish up the Step Over.
//
//  FIXME: ControllingPlan & OkayToDiscard aren't really orthogonal.
//  ControllingPlan
//  designation means that this plan controls it's fate and the fate of plans
//  below it.  OkayToDiscard tells whether the ControllingPlan wants to stay on
//  the stack.  I originally thought "ControllingPlan-ness" would need to be a
//  fixed
//  characteristic of a ThreadPlan, in which case you needed the extra control.
//  But that doesn't seem to be true.  So we should be able to convert to only
//  ControllingPlan status to mean the current "ControllingPlan/DontDiscard".
//  Then no plans would be ControllingPlans by default, and you would set the
//  ones you wanted to be "user level" in this way.
//
//
//  Actually Stopping:
//
//  If a plan says responds "true" to ShouldStop, then it is asked if it's job
//  is complete by calling MischiefManaged.  If that returns true, the plan is
//  popped from the plan stack and added to the Completed Plan Stack.  Then the
//  next plan in the stack is asked if it ShouldStop, and  it returns "true",
//  it is asked if it is done, and if yes popped, and so on till we reach a
//  plan that is not done.
//
//  Since you often know in the ShouldStop method whether your plan is
//  complete, as a convenience you can call SetPlanComplete and the ThreadPlan
//  implementation of MischiefManaged will return "true", without your having
//  to redo the calculation when your sub-classes MischiefManaged is called.
//  If you call SetPlanComplete, you can later use IsPlanComplete to determine
//  whether the plan is complete.  This is only a convenience for sub-classes,
//  the logic in lldb::Thread will only call MischiefManaged.
//
//  One slightly tricky point is you have to be careful using SetPlanComplete
//  in PlanExplainsStop because you are not guaranteed that PlanExplainsStop
//  for a plan will get called before ShouldStop gets called.  If your sub-plan
//  explained the stop and then popped itself, only your ShouldStop will get
//  called.
//
//  If ShouldStop for any thread returns "true", then the WillStop method of
//  the Current plan of all threads will be called, the stop event is placed on
//  the Process's public broadcaster, and control returns to the upper layers
//  of the debugger.
//
//  Reporting the stop:
//
//  When the process stops, the thread is given a StopReason, in the form of a
//  StopInfo object.  If there is a completed plan corresponding to the stop,
//  then the "actual" stop reason can be suppressed, and instead a
//  StopInfoThreadPlan object will be cons'ed up from the top completed plan in
//  the stack.  However, if the plan doesn't want to be the stop reason, then
//  it can call SetPlanComplete and pass in "false" for the "success"
//  parameter.  In that case, the real stop reason will be used instead.  One
//  example of this is the "StepRangeStepIn" thread plan.  If it stops because
//  of a crash or breakpoint hit, it wants to unship itself, because it isn't
//  so useful to have step in keep going after a breakpoint hit.  But it can't
//  be the reason for the stop or no-one would see that they had hit a
//  breakpoint.
//
//  Cleaning up the plan stack:
//
//  One of the complications of ControllingPlans is that you may get past the
//  limits
//  of a plan without triggering it to clean itself up.  For instance, if you
//  are doing a ControllingPlan StepOver, and hit a breakpoint in a called
//  function,
//  then step over enough times to step out of the initial StepOver range, each
//  of the step overs will explain the stop & take themselves off the stack,
//  but control would never be returned to the original StepOver.  Eventually,
//  the user will continue, and when that continue stops, the old stale
//  StepOver plan that was left on the stack will get woken up and notice it is
//  done. But that can leave junk on the stack for a while.  To avoid that, the
//  plans implement a "IsPlanStale" method, that can check whether it is
//  relevant anymore.  On stop, after the regular plan negotiation, the
//  remaining plan stack is consulted and if any plan says it is stale, it and
//  the plans below it are discarded from the stack.
//
//  Automatically Resuming:
//
//  If ShouldStop for all threads returns "false", then the target process will
//  resume.  This then cycles back to Resuming above.
//
//  Reporting eStateStopped events when the target is restarted:
//
//  If a plan decides to auto-continue the target by returning "false" from
//  ShouldStop, then it will be asked whether the Stopped event should still be
//  reported.  For instance, if you hit a breakpoint that is a User set
//  breakpoint, but the breakpoint callback said to continue the target
//  process, you might still want to inform the upper layers of lldb that the
//  stop had happened.  The way this works is every thread gets to vote on
//  whether to report the stop.  If all votes are eVoteNoOpinion, then the
//  thread list will decide what to do (at present it will pretty much always
//  suppress these stopped events.) If there is an eVoteYes, then the event
//  will be reported regardless of the other votes.  If there is an eVoteNo and
//  no eVoteYes's, then the event won't be reported.
//
//  One other little detail here, sometimes a plan will push another plan onto
//  the plan stack to do some part of the first plan's job, and it would be
//  convenient to tell that plan how it should respond to ShouldReportStop.
//  You can do that by setting the report_stop_vote in the child plan when you
//  create it.
//
//  Suppressing the initial eStateRunning event:
//
//  The private process running thread will take care of ensuring that only one
//  "eStateRunning" event will be delivered to the public Process broadcaster
//  per public eStateStopped event.  However there are some cases where the
//  public state of this process is eStateStopped, but a thread plan needs to
//  restart the target, but doesn't want the running event to be publicly
//  broadcast.  The obvious example of this is running functions by hand as
//  part of expression evaluation.  To suppress the running event return
//  eVoteNo from ShouldReportStop, to force a running event to be reported
//  return eVoteYes, in general though you should return eVoteNoOpinion which
//  will allow the ThreadList to figure out the right thing to do.  The
//  report_run_vote argument to the constructor works like report_stop_vote, and
//  is a way for a plan to instruct a sub-plan on how to respond to
//  ShouldReportStop.

class ThreadPlan : public std::enable_shared_from_this<ThreadPlan>,
                   public UserID {
public:
  // We use these enums so that we can cast a base thread plan to it's real
  // type without having to resort to dynamic casting.
  enum ThreadPlanKind {
    eKindGeneric,
    eKindNull,
    eKindBase,
    eKindCallFunction,
    eKindPython,
    eKindStepInstruction,
    eKindStepOut,
    eKindStepOverBreakpoint,
    eKindStepOverRange,
    eKindStepInRange,
    eKindRunToAddress,
    eKindStepThrough,
    eKindStepUntil
  };

  virtual ~ThreadPlan();

  /// Returns the name of this thread plan.
  ///
  /// \return
  ///   A const char * pointer to the thread plan's name.
  const char *GetName() const { return m_name.c_str(); }

  /// Returns the Thread that is using this thread plan.
  ///
  /// \return
  ///   A  pointer to the thread plan's owning thread.
  Thread &GetThread();

  Target &GetTarget();

  const Target &GetTarget() const;

  /// Clear the Thread* cache.
  ///
  /// This is useful in situations like when a new Thread list is being
  /// generated.
  void ClearThreadCache();

  /// Print a description of this thread to the stream \a s.
  /// \a thread.  Don't expect that the result of GetThread is valid in
  /// the description method.  This might get called when the underlying
  /// Thread has not been reported, so we only know the TID and not the thread.
  ///
  /// \param[in] s
  ///    The stream to which to print the description.
  ///
  /// \param[in] level
  ///    The level of description desired.  Note that eDescriptionLevelBrief
  ///    will be used in the stop message printed when the plan is complete.
  virtual void GetDescription(Stream *s, lldb::DescriptionLevel level) = 0;

  /// Returns whether this plan could be successfully created.
  ///
  /// \param[in] error
  ///    A stream to which to print some reason why the plan could not be
  ///    created.
  ///    Can be NULL.
  ///
  /// \return
  ///   \b true if the plan should be queued, \b false otherwise.
  virtual bool ValidatePlan(Stream *error) = 0;

  bool TracerExplainsStop() {
    if (!m_tracer_sp)
      return false;
    else
      return m_tracer_sp->TracerExplainsStop();
  }

  lldb::StateType RunState();

  bool PlanExplainsStop(Event *event_ptr);

  virtual bool ShouldStop(Event *event_ptr) = 0;

  /// Returns whether this thread plan overrides the `ShouldStop` of
  /// subsequently processed plans.
  ///
  /// When processing the thread plan stack, this function gives plans the
  /// ability to continue - even when subsequent plans return true from
  /// `ShouldStop`. \see Thread::ShouldStop
  virtual bool ShouldAutoContinue(Event *event_ptr) { return false; }

  // Whether a "stop class" event should be reported to the "outside world".
  // In general if a thread plan is active, events should not be reported.

  virtual Vote ShouldReportStop(Event *event_ptr);

  Vote ShouldReportRun(Event *event_ptr);

  virtual void SetStopOthers(bool new_value);

  virtual bool StopOthers();
  
  virtual bool ShouldRunBeforePublicStop() { return false; }

  // This is the wrapper for DoWillResume that does generic ThreadPlan logic,
  // then calls DoWillResume.
  bool WillResume(lldb::StateType resume_state, bool current_plan);

  virtual bool WillStop() = 0;

  bool IsControllingPlan() { return m_is_controlling_plan; }

  bool SetIsControllingPlan(bool value) {
    bool old_value = m_is_controlling_plan;
    m_is_controlling_plan = value;
    return old_value;
  }

  virtual bool OkayToDiscard();

  void SetOkayToDiscard(bool value) { m_okay_to_discard = value; }

  // The base class MischiefManaged does some cleanup - so you have to call it
  // in your MischiefManaged derived class.
  virtual bool MischiefManaged();

  virtual void ThreadDestroyed() {
    // Any cleanup that a plan might want to do in case the thread goes away in
    // the middle of the plan being queued on a thread can be done here.
  }

  bool GetPrivate() { return m_plan_private; }

  void SetPrivate(bool input) { m_plan_private = input; }

  virtual void DidPush();

  virtual void DidPop();

  ThreadPlanKind GetKind() const { return m_kind; }

  bool IsPlanComplete();

  void SetPlanComplete(bool success = true);

  virtual bool IsPlanStale() { return false; }

  bool PlanSucceeded() { return m_plan_succeeded; }

  virtual bool IsBasePlan() { return false; }

  lldb::ThreadPlanTracerSP &GetThreadPlanTracer() { return m_tracer_sp; }

  void SetThreadPlanTracer(lldb::ThreadPlanTracerSP new_tracer_sp) {
    m_tracer_sp = new_tracer_sp;
  }

  void DoTraceLog() {
    if (m_tracer_sp && m_tracer_sp->TracingEnabled())
      m_tracer_sp->Log();
  }

  // If the completion of the thread plan stepped out of a function, the return
  // value of the function might have been captured by the thread plan
  // (currently only ThreadPlanStepOut does this.) If so, the ReturnValueObject
  // can be retrieved from here.

  virtual lldb::ValueObjectSP GetReturnValueObject() {
    return lldb::ValueObjectSP();
  }

  // If the thread plan managing the evaluation of a user expression lives
  // longer than the command that instigated the expression (generally because
  // the expression evaluation hit a breakpoint, and the user regained control
  // at that point) a subsequent process control command step/continue/etc.
  // might complete the expression evaluations.  If so, the result of the
  // expression evaluation will show up here.

  virtual lldb::ExpressionVariableSP GetExpressionVariable() {
    return lldb::ExpressionVariableSP();
  }

  // If a thread plan stores the state before it was run, then you might want
  // to restore the state when it is done.  This will do that job. This is
  // mostly useful for artificial plans like CallFunction plans.

  virtual void RestoreThreadState() {}

  virtual bool IsVirtualStep() { return false; }

  bool SetIterationCount(size_t count) {
    if (m_takes_iteration_count) {
      // Don't tell me to do something 0 times...
      if (count == 0)
        return false;
      m_iteration_count = count;
    }
    return m_takes_iteration_count;
  }

protected:
  // Constructors and Destructors
  ThreadPlan(ThreadPlanKind kind, const char *name, Thread &thread,
             Vote report_stop_vote, Vote report_run_vote);

  // Classes that inherit from ThreadPlan can see and modify these

  virtual bool DoWillResume(lldb::StateType resume_state, bool current_plan) {
    return true;
  }

  virtual bool DoPlanExplainsStop(Event *event_ptr) = 0;

  // This pushes a plan onto the plan stack of the current plan's thread.
  // Also sets the plans to private and not controlling plans.  A plan pushed by
  // another thread plan is never either of the above.
  void PushPlan(lldb::ThreadPlanSP &thread_plan_sp) {
    GetThread().PushPlan(thread_plan_sp);
    thread_plan_sp->SetPrivate(true);
    thread_plan_sp->SetIsControllingPlan(false);
  }

  // This gets the previous plan to the current plan (for forwarding requests).
  // This is mostly a formal requirement, it allows us to make the Thread's
  // GetPreviousPlan protected, but only friend ThreadPlan to thread.

  ThreadPlan *GetPreviousPlan() { return GetThread().GetPreviousPlan(this); }

  // This forwards the private Thread::GetPrivateStopInfo which is generally
  // what ThreadPlan's need to know.

  lldb::StopInfoSP GetPrivateStopInfo() {
    return GetThread().GetPrivateStopInfo();
  }

  void SetStopInfo(lldb::StopInfoSP stop_reason_sp) {
    GetThread().SetStopInfo(stop_reason_sp);
  }

  virtual lldb::StateType GetPlanRunState() = 0;

  bool IsUsuallyUnexplainedStopReason(lldb::StopReason);

  Status m_status;
  Process &m_process;
  lldb::tid_t m_tid;
  Vote m_report_stop_vote;
  Vote m_report_run_vote;
  bool m_takes_iteration_count;
  bool m_could_not_resolve_hw_bp;
  int32_t m_iteration_count = 1;

private:
  void CachePlanExplainsStop(bool does_explain) {
    m_cached_plan_explains_stop = does_explain ? eLazyBoolYes : eLazyBoolNo;
  }

  // For ThreadPlan only
  static lldb::user_id_t GetNextID();

  Thread *m_thread; // Stores a cached value of the thread, which is set to
                    // nullptr when the thread resumes.  Don't use this anywhere
                    // but ThreadPlan::GetThread().
  ThreadPlanKind m_kind;
  std::string m_name;
  std::recursive_mutex m_plan_complete_mutex;
  LazyBool m_cached_plan_explains_stop;
  bool m_plan_complete;
  bool m_plan_private;
  bool m_okay_to_discard;
  bool m_is_controlling_plan;
  bool m_plan_succeeded;

  lldb::ThreadPlanTracerSP m_tracer_sp;

  ThreadPlan(const ThreadPlan &) = delete;
  const ThreadPlan &operator=(const ThreadPlan &) = delete;
};

// ThreadPlanNull:
// Threads are assumed to always have at least one plan on the plan stack. This
// is put on the plan stack when a thread is destroyed so that if you
// accidentally access a thread after it is destroyed you won't crash. But
// asking questions of the ThreadPlanNull is definitely an error.

class ThreadPlanNull : public ThreadPlan {
public:
  ThreadPlanNull(Thread &thread);
  ~ThreadPlanNull() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  bool ShouldStop(Event *event_ptr) override;

  bool MischiefManaged() override;

  bool WillStop() override;

  bool IsBasePlan() override { return true; }

  bool OkayToDiscard() override { return false; }

  const Status &GetStatus() { return m_status; }

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

  lldb::StateType GetPlanRunState() override;

  ThreadPlanNull(const ThreadPlanNull &) = delete;
  const ThreadPlanNull &operator=(const ThreadPlanNull &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLAN_H
