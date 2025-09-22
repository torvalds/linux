//===-- StopInfo.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

StopInfo::StopInfo(Thread &thread, uint64_t value)
    : m_thread_wp(thread.shared_from_this()),
      m_stop_id(thread.GetProcess()->GetStopID()),
      m_resume_id(thread.GetProcess()->GetResumeID()), m_value(value),
      m_description(), m_override_should_notify(eLazyBoolCalculate),
      m_override_should_stop(eLazyBoolCalculate), m_extended_info() {}

bool StopInfo::IsValid() const {
  ThreadSP thread_sp(m_thread_wp.lock());
  if (thread_sp)
    return thread_sp->GetProcess()->GetStopID() == m_stop_id;
  return false;
}

void StopInfo::MakeStopInfoValid() {
  ThreadSP thread_sp(m_thread_wp.lock());
  if (thread_sp) {
    m_stop_id = thread_sp->GetProcess()->GetStopID();
    m_resume_id = thread_sp->GetProcess()->GetResumeID();
  }
}

bool StopInfo::HasTargetRunSinceMe() {
  ThreadSP thread_sp(m_thread_wp.lock());

  if (thread_sp) {
    lldb::StateType ret_type = thread_sp->GetProcess()->GetPrivateState();
    if (ret_type == eStateRunning) {
      return true;
    } else if (ret_type == eStateStopped) {
      // This is a little tricky.  We want to count "run and stopped again
      // before you could ask this question as a "TRUE" answer to
      // HasTargetRunSinceMe.  But we don't want to include any running of the
      // target done for expressions.  So we track both resumes, and resumes
      // caused by expressions, and check if there are any resumes
      // NOT caused
      // by expressions.

      uint32_t curr_resume_id = thread_sp->GetProcess()->GetResumeID();
      uint32_t last_user_expression_id =
          thread_sp->GetProcess()->GetLastUserExpressionResumeID();
      if (curr_resume_id == m_resume_id) {
        return false;
      } else if (curr_resume_id > last_user_expression_id) {
        return true;
      }
    }
  }
  return false;
}

// StopInfoBreakpoint

namespace lldb_private {
class StopInfoBreakpoint : public StopInfo {
public:
  StopInfoBreakpoint(Thread &thread, break_id_t break_id)
      : StopInfo(thread, break_id), m_should_stop(false),
        m_should_stop_is_valid(false), m_should_perform_action(true),
        m_address(LLDB_INVALID_ADDRESS), m_break_id(LLDB_INVALID_BREAK_ID),
        m_was_all_internal(false), m_was_one_shot(false) {
    StoreBPInfo();
  }

  StopInfoBreakpoint(Thread &thread, break_id_t break_id, bool should_stop)
      : StopInfo(thread, break_id), m_should_stop(should_stop),
        m_should_stop_is_valid(true), m_should_perform_action(true),
        m_address(LLDB_INVALID_ADDRESS), m_break_id(LLDB_INVALID_BREAK_ID),
        m_was_all_internal(false), m_was_one_shot(false) {
    StoreBPInfo();
  }

  ~StopInfoBreakpoint() override = default;

  void StoreBPInfo() {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp) {
      BreakpointSiteSP bp_site_sp(
          thread_sp->GetProcess()->GetBreakpointSiteList().FindByID(m_value));
      if (bp_site_sp) {
        uint32_t num_constituents = bp_site_sp->GetNumberOfConstituents();
        if (num_constituents == 1) {
          BreakpointLocationSP bp_loc_sp = bp_site_sp->GetConstituentAtIndex(0);
          if (bp_loc_sp) {
            Breakpoint & bkpt = bp_loc_sp->GetBreakpoint();
            m_break_id = bkpt.GetID();
            m_was_one_shot = bkpt.IsOneShot();
            m_was_all_internal = bkpt.IsInternal();
          }
        } else {
          m_was_all_internal = true;
          for (uint32_t i = 0; i < num_constituents; i++) {
            if (!bp_site_sp->GetConstituentAtIndex(i)
                     ->GetBreakpoint()
                     .IsInternal()) {
              m_was_all_internal = false;
              break;
            }
          }
        }
        m_address = bp_site_sp->GetLoadAddress();
      }
    }
  }

  bool IsValidForOperatingSystemThread(Thread &thread) override {
    ProcessSP process_sp(thread.GetProcess());
    if (process_sp) {
      BreakpointSiteSP bp_site_sp(
          process_sp->GetBreakpointSiteList().FindByID(m_value));
      if (bp_site_sp)
        return bp_site_sp->ValidForThisThread(thread);
    }
    return false;
  }

  StopReason GetStopReason() const override { return eStopReasonBreakpoint; }

  bool ShouldStopSynchronous(Event *event_ptr) override {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp) {
      if (!m_should_stop_is_valid) {
        // Only check once if we should stop at a breakpoint
        BreakpointSiteSP bp_site_sp(
            thread_sp->GetProcess()->GetBreakpointSiteList().FindByID(m_value));
        if (bp_site_sp) {
          ExecutionContext exe_ctx(thread_sp->GetStackFrameAtIndex(0));
          StoppointCallbackContext context(event_ptr, exe_ctx, true);
          bp_site_sp->BumpHitCounts();
          m_should_stop = bp_site_sp->ShouldStop(&context);
        } else {
          Log *log = GetLog(LLDBLog::Process);

          LLDB_LOGF(log,
                    "Process::%s could not find breakpoint site id: %" PRId64
                    "...",
                    __FUNCTION__, m_value);

          m_should_stop = true;
        }
        m_should_stop_is_valid = true;
      }
      return m_should_stop;
    }
    return false;
  }

  bool DoShouldNotify(Event *event_ptr) override {
    return !m_was_all_internal;
  }

  const char *GetDescription() override {
    if (m_description.empty()) {
      ThreadSP thread_sp(m_thread_wp.lock());
      if (thread_sp) {
        BreakpointSiteSP bp_site_sp(
            thread_sp->GetProcess()->GetBreakpointSiteList().FindByID(m_value));
        if (bp_site_sp) {
          StreamString strm;
          // If we have just hit an internal breakpoint, and it has a kind
          // description, print that instead of the full breakpoint printing:
          if (bp_site_sp->IsInternal()) {
            size_t num_constituents = bp_site_sp->GetNumberOfConstituents();
            for (size_t idx = 0; idx < num_constituents; idx++) {
              const char *kind = bp_site_sp->GetConstituentAtIndex(idx)
                                     ->GetBreakpoint()
                                     .GetBreakpointKind();
              if (kind != nullptr) {
                m_description.assign(kind);
                return kind;
              }
            }
          }

          strm.Printf("breakpoint ");
          bp_site_sp->GetDescription(&strm, eDescriptionLevelBrief);
          m_description = std::string(strm.GetString());
        } else {
          StreamString strm;
          if (m_break_id != LLDB_INVALID_BREAK_ID) {
            BreakpointSP break_sp =
                thread_sp->GetProcess()->GetTarget().GetBreakpointByID(
                    m_break_id);
            if (break_sp) {
              if (break_sp->IsInternal()) {
                const char *kind = break_sp->GetBreakpointKind();
                if (kind)
                  strm.Printf("internal %s breakpoint(%d).", kind, m_break_id);
                else
                  strm.Printf("internal breakpoint(%d).", m_break_id);
              } else {
                strm.Printf("breakpoint %d.", m_break_id);
              }
            } else {
              if (m_was_one_shot)
                strm.Printf("one-shot breakpoint %d", m_break_id);
              else
                strm.Printf("breakpoint %d which has been deleted.",
                            m_break_id);
            }
          } else if (m_address == LLDB_INVALID_ADDRESS)
            strm.Printf("breakpoint site %" PRIi64
                        " which has been deleted - unknown address",
                        m_value);
          else
            strm.Printf("breakpoint site %" PRIi64
                        " which has been deleted - was at 0x%" PRIx64,
                        m_value, m_address);

          m_description = std::string(strm.GetString());
        }
      }
    }
    return m_description.c_str();
  }

protected:
  bool ShouldStop(Event *event_ptr) override {
    // This just reports the work done by PerformAction or the synchronous
    // stop. It should only ever get called after they have had a chance to
    // run.
    assert(m_should_stop_is_valid);
    return m_should_stop;
  }

  void PerformAction(Event *event_ptr) override {
    if (!m_should_perform_action)
      return;
    m_should_perform_action = false;
    bool all_stopping_locs_internal = true;

    ThreadSP thread_sp(m_thread_wp.lock());

    if (thread_sp) {
      Log *log = GetLog(LLDBLog::Breakpoints | LLDBLog::Step);

      if (!thread_sp->IsValid()) {
        // This shouldn't ever happen, but just in case, don't do more harm.
        if (log) {
          LLDB_LOGF(log, "PerformAction got called with an invalid thread.");
        }
        m_should_stop = true;
        m_should_stop_is_valid = true;
        return;
      }

      BreakpointSiteSP bp_site_sp(
          thread_sp->GetProcess()->GetBreakpointSiteList().FindByID(m_value));
      std::unordered_set<break_id_t> precondition_breakpoints;
      // Breakpoints that fail their condition check are not considered to
      // have been hit.  If the only locations at this site have failed their
      // conditions, we should change the stop-info to none.  Otherwise, if we
      // hit another breakpoint on a different thread which does stop, users
      // will see a breakpont hit with a failed condition, which is wrong.
      // Use this variable to tell us if that is true.
      bool actually_hit_any_locations = false;
      if (bp_site_sp) {
        // Let's copy the constituents list out of the site and store them in a
        // local list.  That way if one of the breakpoint actions changes the
        // site, then we won't be operating on a bad list.
        BreakpointLocationCollection site_locations;
        size_t num_constituents =
            bp_site_sp->CopyConstituentsList(site_locations);

        if (num_constituents == 0) {
          m_should_stop = true;
          actually_hit_any_locations = true;  // We're going to stop, don't 
                                              // change the stop info.
        } else {
          // We go through each location, and test first its precondition -
          // this overrides everything.  Note, we only do this once per
          // breakpoint - not once per location... Then check the condition.
          // If the condition says to stop, then we run the callback for that
          // location.  If that callback says to stop as well, then we set
          // m_should_stop to true; we are going to stop. But we still want to
          // give all the breakpoints whose conditions say we are going to stop
          // a chance to run their callbacks. Of course if any callback
          // restarts the target by putting "continue" in the callback, then
          // we're going to restart, without running the rest of the callbacks.
          // And in this case we will end up not stopping even if another
          // location said we should stop. But that's better than not running
          // all the callbacks.

          // There's one other complication here.  We may have run an async
          // breakpoint callback that said we should stop.  We only want to
          // override that if another breakpoint action says we shouldn't
          // stop.  If nobody else has an opinion, then we should stop if the
          // async callback says we should.  An example of this is the async
          // shared library load notification breakpoint and the setting
          // stop-on-sharedlibrary-events.
          // We'll keep the async value in async_should_stop, and track whether
          // anyone said we should NOT stop in actually_said_continue.
          bool async_should_stop = false;
          if (m_should_stop_is_valid)
            async_should_stop = m_should_stop;
          bool actually_said_continue = false;

          m_should_stop = false;

          // We don't select threads as we go through them testing breakpoint
          // conditions and running commands. So we need to set the thread for
          // expression evaluation here:
          ThreadList::ExpressionExecutionThreadPusher thread_pusher(thread_sp);

          ExecutionContext exe_ctx(thread_sp->GetStackFrameAtIndex(0));
          Process *process = exe_ctx.GetProcessPtr();
          if (process->GetModIDRef().IsRunningExpression()) {
            // If we are in the middle of evaluating an expression, don't run
            // asynchronous breakpoint commands or expressions.  That could
            // lead to infinite recursion if the command or condition re-calls
            // the function with this breakpoint.
            // TODO: We can keep a list of the breakpoints we've seen while
            // running expressions in the nested
            // PerformAction calls that can arise when the action runs a
            // function that hits another breakpoint, and only stop running
            // commands when we see the same breakpoint hit a second time.

            m_should_stop_is_valid = true;

            // It is possible that the user has a breakpoint at the same site
            // as the completed plan had (e.g. user has a breakpoint
            // on a module entry point, and `ThreadPlanCallFunction` ends
            // also there). We can't find an internal breakpoint in the loop
            // later because it was already removed on the plan completion.
            // So check if the plan was completed, and stop if so.
            if (thread_sp->CompletedPlanOverridesBreakpoint()) {
              m_should_stop = true;
              thread_sp->ResetStopInfo();
              return;
            }

            LLDB_LOGF(log, "StopInfoBreakpoint::PerformAction - Hit a "
                           "breakpoint while running an expression,"
                           " not running commands to avoid recursion.");
            bool ignoring_breakpoints =
                process->GetIgnoreBreakpointsInExpressions();
            // Internal breakpoints should be allowed to do their job, we
            // can make sure they don't do anything that would cause recursive
            // command execution:
            if (!m_was_all_internal) {
              m_should_stop = !ignoring_breakpoints;
              LLDB_LOGF(log,
                        "StopInfoBreakpoint::PerformAction - in expression, "
                        "continuing: %s.",
                        m_should_stop ? "true" : "false");
              Debugger::ReportWarning(
                  "hit breakpoint while running function, skipping commands "
                  "and conditions to prevent recursion",
                    process->GetTarget().GetDebugger().GetID());
              return;
            }
          }

          StoppointCallbackContext context(event_ptr, exe_ctx, false);

          // For safety's sake let's also grab an extra reference to the
          // breakpoint constituents of the locations we're going to examine,
          // since the locations are going to have to get back to their
          // breakpoints, and the locations don't keep their constituents alive.
          // I'm just sticking the BreakpointSP's in a vector since I'm only
          // using it to locally increment their retain counts.

          std::vector<lldb::BreakpointSP> location_constituents;

          for (size_t j = 0; j < num_constituents; j++) {
            BreakpointLocationSP loc(site_locations.GetByIndex(j));
            location_constituents.push_back(
                loc->GetBreakpoint().shared_from_this());
          }

          for (size_t j = 0; j < num_constituents; j++) {
            lldb::BreakpointLocationSP bp_loc_sp = site_locations.GetByIndex(j);
            StreamString loc_desc;
            if (log) {
              bp_loc_sp->GetDescription(&loc_desc, eDescriptionLevelBrief);
            }
            // If another action disabled this breakpoint or its location, then
            // don't run the actions.
            if (!bp_loc_sp->IsEnabled() ||
                !bp_loc_sp->GetBreakpoint().IsEnabled())
              continue;

            // The breakpoint site may have many locations associated with it,
            // not all of them valid for this thread.  Skip the ones that
            // aren't:
            if (!bp_loc_sp->ValidForThisThread(*thread_sp)) {
              if (log) {
                LLDB_LOGF(log,
                          "Breakpoint %s hit on thread 0x%llx but it was not "
                          "for this thread, continuing.",
                          loc_desc.GetData(),
                          static_cast<unsigned long long>(thread_sp->GetID()));
              }
              continue;
            }

            // First run the precondition, but since the precondition is per
            // breakpoint, only run it once per breakpoint.
            std::pair<std::unordered_set<break_id_t>::iterator, bool> result =
                precondition_breakpoints.insert(
                    bp_loc_sp->GetBreakpoint().GetID());
            if (!result.second)
              continue;

            bool precondition_result =
                bp_loc_sp->GetBreakpoint().EvaluatePrecondition(context);
            if (!precondition_result) {
              actually_said_continue = true;
              continue;
            }
            // Next run the condition for the breakpoint.  If that says we
            // should stop, then we'll run the callback for the breakpoint.  If
            // the callback says we shouldn't stop that will win.

            if (bp_loc_sp->GetConditionText() == nullptr)
              actually_hit_any_locations = true;
            else {
              Status condition_error;
              bool condition_says_stop =
                  bp_loc_sp->ConditionSaysStop(exe_ctx, condition_error);

              if (!condition_error.Success()) {
                // If the condition fails to evaluate, we are going to stop 
                // at it, so the location was hit.
                actually_hit_any_locations = true;
                const char *err_str =
                    condition_error.AsCString("<unknown error>");
                LLDB_LOGF(log, "Error evaluating condition: \"%s\"\n", err_str);

                StreamString strm;
                strm << "stopped due to an error evaluating condition of "
                        "breakpoint ";
                bp_loc_sp->GetDescription(&strm, eDescriptionLevelBrief);
                strm << ": \"" << bp_loc_sp->GetConditionText() << "\"\n";
                strm << err_str;

                Debugger::ReportError(
                    strm.GetString().str(),
                    exe_ctx.GetTargetRef().GetDebugger().GetID());
              } else {
                LLDB_LOGF(log,
                          "Condition evaluated for breakpoint %s on thread "
                          "0x%llx condition_says_stop: %i.",
                          loc_desc.GetData(),
                          static_cast<unsigned long long>(thread_sp->GetID()),
                          condition_says_stop);
                if (condition_says_stop) 
                  actually_hit_any_locations = true;
                else {
                  // We don't want to increment the hit count of breakpoints if
                  // the condition fails. We've already bumped it by the time
                  // we get here, so undo the bump:
                  bp_loc_sp->UndoBumpHitCount();
                  actually_said_continue = true;
                  continue;
                }
              }
            }

            // We've done all the checks whose failure means "we consider lldb
            // not to have hit the breakpoint".  Now we're going to check for
            // conditions that might continue after hitting.  Start with the
            // ignore count:
            if (!bp_loc_sp->IgnoreCountShouldStop()) {
              actually_said_continue = true;
              continue;
            }

            // Check the auto-continue bit on the location, do this before the
            // callback since it may change this, but that would be for the
            // NEXT hit.  Note, you might think you could check auto-continue
            // before the condition, and not evaluate the condition if it says
            // to continue.  But failing the condition means the breakpoint was
            // effectively NOT HIT.  So these two states are different.
            bool auto_continue_says_stop = true;
            if (bp_loc_sp->IsAutoContinue())
            {
              LLDB_LOGF(log,
                        "Continuing breakpoint %s as AutoContinue was set.",
                        loc_desc.GetData());
              // We want this stop reported, so you will know we auto-continued
              // but only for external breakpoints:
              if (!bp_loc_sp->GetBreakpoint().IsInternal())
                thread_sp->SetShouldReportStop(eVoteYes);
              auto_continue_says_stop = false;
            }

            bool callback_says_stop = true;

            // FIXME: For now the callbacks have to run in async mode - the
            // first time we restart we need
            // to get out of there.  So set it here.
            // When we figure out how to nest breakpoint hits then this will
            // change.

            // Don't run async callbacks in PerformAction.  They have already
            // been taken into account with async_should_stop.
            if (!bp_loc_sp->IsCallbackSynchronous()) {
              Debugger &debugger = thread_sp->CalculateTarget()->GetDebugger();
              bool old_async = debugger.GetAsyncExecution();
              debugger.SetAsyncExecution(true);

              callback_says_stop = bp_loc_sp->InvokeCallback(&context);

              debugger.SetAsyncExecution(old_async);

              if (callback_says_stop && auto_continue_says_stop)
                m_should_stop = true;
              else
                actually_said_continue = true;
            }

            if (m_should_stop && !bp_loc_sp->GetBreakpoint().IsInternal())
              all_stopping_locs_internal = false;

            // If we are going to stop for this breakpoint, then remove the
            // breakpoint.
            if (callback_says_stop && bp_loc_sp &&
                bp_loc_sp->GetBreakpoint().IsOneShot()) {
              thread_sp->GetProcess()->GetTarget().RemoveBreakpointByID(
                  bp_loc_sp->GetBreakpoint().GetID());
            }
            // Also make sure that the callback hasn't continued the target. If
            // it did, when we'll set m_should_start to false and get out of
            // here.
            if (HasTargetRunSinceMe()) {
              m_should_stop = false;
              actually_said_continue = true;
              break;
            }
          }
          // At this point if nobody actually told us to continue, we should
          // give the async breakpoint callback a chance to weigh in:
          if (!actually_said_continue && !m_should_stop) {
            m_should_stop = async_should_stop;
          }
        }
        // We've figured out what this stop wants to do, so mark it as valid so
        // we don't compute it again.
        m_should_stop_is_valid = true;
      } else {
        m_should_stop = true;
        m_should_stop_is_valid = true;
        actually_hit_any_locations = true;
        Log *log_process(GetLog(LLDBLog::Process));

        LLDB_LOGF(log_process,
                  "Process::%s could not find breakpoint site id: %" PRId64
                  "...",
                  __FUNCTION__, m_value);
      }

      if ((!m_should_stop || all_stopping_locs_internal) &&
          thread_sp->CompletedPlanOverridesBreakpoint()) {

        // Override should_stop decision when we have completed step plan
        // additionally to the breakpoint
        m_should_stop = true;

        // We know we're stopping for a completed plan and we don't want to
        // show the breakpoint stop, so compute the public stop info immediately
        // here.
        thread_sp->CalculatePublicStopInfo();
      } else if (!actually_hit_any_locations) {
        // In the end, we didn't actually have any locations that passed their
        // "was I hit" checks.  So say we aren't stopped.
        GetThread()->ResetStopInfo();
        LLDB_LOGF(log, "Process::%s all locations failed condition checks.",
          __FUNCTION__);
      }

      LLDB_LOGF(log,
                "Process::%s returning from action with m_should_stop: %d.",
                __FUNCTION__, m_should_stop);
    }
  }

private:
  bool m_should_stop;
  bool m_should_stop_is_valid;
  bool m_should_perform_action; // Since we are trying to preserve the "state"
                                // of the system even if we run functions
  // etc. behind the users backs, we need to make sure we only REALLY perform
  // the action once.
  lldb::addr_t m_address; // We use this to capture the breakpoint site address
                          // when we create the StopInfo,
  // in case somebody deletes it between the time the StopInfo is made and the
  // description is asked for.
  lldb::break_id_t m_break_id;
  bool m_was_all_internal;
  bool m_was_one_shot;
};

// StopInfoWatchpoint

class StopInfoWatchpoint : public StopInfo {
public:
  // Make sure watchpoint is properly disabled and subsequently enabled while
  // performing watchpoint actions.
  class WatchpointSentry {
  public:
    WatchpointSentry(ProcessSP p_sp, WatchpointSP w_sp) : process_sp(p_sp),
                     watchpoint_sp(w_sp) {
      if (process_sp && watchpoint_sp) {
        const bool notify = false;
        watchpoint_sp->TurnOnEphemeralMode();
        process_sp->DisableWatchpoint(watchpoint_sp, notify);
        process_sp->AddPreResumeAction(SentryPreResumeAction, this);
      }
    }

    void DoReenable() {
      if (process_sp && watchpoint_sp) {
        bool was_disabled = watchpoint_sp->IsDisabledDuringEphemeralMode();
        watchpoint_sp->TurnOffEphemeralMode();
        const bool notify = false;
        if (was_disabled) {
          process_sp->DisableWatchpoint(watchpoint_sp, notify);
        } else {
          process_sp->EnableWatchpoint(watchpoint_sp, notify);
        }
      }
    }

    ~WatchpointSentry() {
        DoReenable();
        if (process_sp)
            process_sp->ClearPreResumeAction(SentryPreResumeAction, this);
    }

    static bool SentryPreResumeAction(void *sentry_void) {
        WatchpointSentry *sentry = (WatchpointSentry *) sentry_void;
        sentry->DoReenable();
        return true;
    }

  private:
    ProcessSP process_sp;
    WatchpointSP watchpoint_sp;
  };

  StopInfoWatchpoint(Thread &thread, break_id_t watch_id, bool silently_skip_wp)
      : StopInfo(thread, watch_id), m_silently_skip_wp(silently_skip_wp) {}

  ~StopInfoWatchpoint() override = default;

  StopReason GetStopReason() const override { return eStopReasonWatchpoint; }

  const char *GetDescription() override {
    if (m_description.empty()) {
      StreamString strm;
      strm.Printf("watchpoint %" PRIi64, m_value);
      m_description = std::string(strm.GetString());
    }
    return m_description.c_str();
  }

protected:
  using StopInfoWatchpointSP = std::shared_ptr<StopInfoWatchpoint>;
  // This plan is used to orchestrate stepping over the watchpoint for
  // architectures (e.g. ARM) that report the watch before running the watched
  // access.  This is the sort of job you have to defer to the thread plans,
  // if you try to do it directly in the stop info and there are other threads
  // that needed to process this stop you will have yanked control away from
  // them and they won't behave correctly.
  class ThreadPlanStepOverWatchpoint : public ThreadPlanStepInstruction {
  public:
    ThreadPlanStepOverWatchpoint(Thread &thread, 
                                 StopInfoWatchpointSP stop_info_sp,
                                 WatchpointSP watch_sp)
        : ThreadPlanStepInstruction(thread, false, true, eVoteNoOpinion,
                                    eVoteNoOpinion),
          m_stop_info_sp(stop_info_sp), m_watch_sp(watch_sp) {
      assert(watch_sp);
    }

    bool DoWillResume(lldb::StateType resume_state,
                      bool current_plan) override {
      if (resume_state == eStateSuspended)
        return true;

      if (!m_did_disable_wp) {
        GetThread().GetProcess()->DisableWatchpoint(m_watch_sp, false);
        m_did_disable_wp = true;
      }
      return true;
    }
    
    bool DoPlanExplainsStop(Event *event_ptr) override {
      if (ThreadPlanStepInstruction::DoPlanExplainsStop(event_ptr))
        return true;
      StopInfoSP stop_info_sp = GetThread().GetPrivateStopInfo();
      // lldb-server resets the stop info for threads that didn't get to run,
      // so we might have not gotten to run, but still have a watchpoint stop
      // reason, in which case this will indeed be for us.
      if (stop_info_sp 
          && stop_info_sp->GetStopReason() == eStopReasonWatchpoint)
        return true;
      return false;
    }

    void DidPop() override {
      // Don't artifically keep the watchpoint alive.
      m_watch_sp.reset();
    }
    
    bool ShouldStop(Event *event_ptr) override {
      bool should_stop = ThreadPlanStepInstruction::ShouldStop(event_ptr);
      bool plan_done = MischiefManaged();
      if (plan_done) {
        m_stop_info_sp->SetStepOverPlanComplete();
        GetThread().SetStopInfo(m_stop_info_sp);
        ResetWatchpoint();
      }
      return should_stop;
    }
    
    bool ShouldRunBeforePublicStop() override {
        return true;
    }

  protected:
    void ResetWatchpoint() {
      if (!m_did_disable_wp)
        return;
      m_did_disable_wp = true;
      GetThread().GetProcess()->EnableWatchpoint(m_watch_sp, true);
    }

  private:
    StopInfoWatchpointSP m_stop_info_sp;
    WatchpointSP m_watch_sp;
    bool m_did_disable_wp = false;
  };

  bool ShouldStopSynchronous(Event *event_ptr) override {
    // If we are running our step-over the watchpoint plan, stop if it's done
    // and continue if it's not:
    if (m_should_stop_is_valid)
      return m_should_stop;

    // If we are running our step over plan, then stop here and let the regular
    // ShouldStop figure out what we should do:  Otherwise, give our plan
    // more time to get run:
    if (m_using_step_over_plan)
      return m_step_over_plan_complete;

    Log *log = GetLog(LLDBLog::Process);
    ThreadSP thread_sp(m_thread_wp.lock());
    assert(thread_sp);
    
    if (thread_sp->GetTemporaryResumeState() == eStateSuspended) {
      // This is the second firing of a watchpoint so don't process it again.
      LLDB_LOG(log, "We didn't run but stopped with a StopInfoWatchpoint, we "
               "have already handled this one, don't do it again.");
      m_should_stop = false;
      m_should_stop_is_valid = true;
      return m_should_stop;
    }
    
    WatchpointSP wp_sp(
        thread_sp->CalculateTarget()->GetWatchpointList().FindByID(GetValue()));
    // If we can no longer find the watchpoint, we just have to stop:
    if (!wp_sp) {

      LLDB_LOGF(log,
                "Process::%s could not find watchpoint location id: %" PRId64
                "...",
                __FUNCTION__, GetValue());

      m_should_stop = true;
      m_should_stop_is_valid = true;
      return true;
    }

    ExecutionContext exe_ctx(thread_sp->GetStackFrameAtIndex(0));
    StoppointCallbackContext context(event_ptr, exe_ctx, true);
    m_should_stop = wp_sp->ShouldStop(&context);
    if (!m_should_stop) {
      // This won't happen at present because we only allow one watchpoint per
      // watched range.  So we won't stop at a watched address with a disabled
      // watchpoint.  If we start allowing overlapping watchpoints, then we
      // will have to make watchpoints be real "WatchpointSite" and delegate to
      // all the watchpoints sharing the site.  In that case, the code below
      // would be the right thing to do.
      m_should_stop_is_valid = true;
      return m_should_stop;
    }
    // If this is a system where we need to execute the watchpoint by hand
    // after the hit, queue a thread plan to do that, and then say not to stop.
    // Otherwise, let the async action figure out whether the watchpoint should
    // stop

    ProcessSP process_sp = exe_ctx.GetProcessSP();
    bool wp_triggers_after = process_sp->GetWatchpointReportedAfter();

    if (!wp_triggers_after) {
      // We have to step over the watchpoint before we know what to do:   
      StopInfoWatchpointSP me_as_siwp_sp 
          = std::static_pointer_cast<StopInfoWatchpoint>(shared_from_this());
      ThreadPlanSP step_over_wp_sp(new ThreadPlanStepOverWatchpoint(
          *(thread_sp.get()), me_as_siwp_sp, wp_sp));
      // When this plan is done we want to stop, so set this as a Controlling
      // plan.    
      step_over_wp_sp->SetIsControllingPlan(true);
      step_over_wp_sp->SetOkayToDiscard(false);

      Status error;
      error = thread_sp->QueueThreadPlan(step_over_wp_sp, false);
      // If we couldn't push the thread plan, just stop here:
      if (!error.Success()) {
        LLDB_LOGF(log, "Could not push our step over watchpoint plan: %s", 
            error.AsCString());

        m_should_stop = true;
        m_should_stop_is_valid = true;
        return true;
      } else {
      // Otherwise, don't set m_should_stop, we don't know that yet.  Just 
      // say we should continue, and tell the thread we really should do so:
        thread_sp->SetShouldRunBeforePublicStop(true);
        m_using_step_over_plan = true;
        return false;
      }
    } else {
      // We didn't have to do anything special
      m_should_stop_is_valid = true;
      return m_should_stop;
    }
    
    return m_should_stop;
  }

  bool ShouldStop(Event *event_ptr) override {
    // This just reports the work done by PerformAction or the synchronous
    // stop. It should only ever get called after they have had a chance to
    // run.
    assert(m_should_stop_is_valid);
    return m_should_stop;
  }

  void PerformAction(Event *event_ptr) override {
    Log *log = GetLog(LLDBLog::Watchpoints);
    // We're going to calculate if we should stop or not in some way during the
    // course of this code.  Also by default we're going to stop, so set that
    // here.
    m_should_stop = true;


    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp) {

      WatchpointSP wp_sp(
          thread_sp->CalculateTarget()->GetWatchpointList().FindByID(
              GetValue()));
      if (wp_sp) {
        // This sentry object makes sure the current watchpoint is disabled
        // while performing watchpoint actions, and it is then enabled after we
        // are finished.
        ExecutionContext exe_ctx(thread_sp->GetStackFrameAtIndex(0));
        ProcessSP process_sp = exe_ctx.GetProcessSP();

        WatchpointSentry sentry(process_sp, wp_sp);

        if (m_silently_skip_wp) {
          m_should_stop = false;
          wp_sp->UndoHitCount();
        }

        if (wp_sp->GetHitCount() <= wp_sp->GetIgnoreCount()) {
          m_should_stop = false;
          m_should_stop_is_valid = true;
        }

        Debugger &debugger = exe_ctx.GetTargetRef().GetDebugger();

        if (m_should_stop && wp_sp->GetConditionText() != nullptr) {
          // We need to make sure the user sees any parse errors in their
          // condition, so we'll hook the constructor errors up to the
          // debugger's Async I/O.
          ExpressionResults result_code;
          EvaluateExpressionOptions expr_options;
          expr_options.SetUnwindOnError(true);
          expr_options.SetIgnoreBreakpoints(true);
          ValueObjectSP result_value_sp;
          Status error;
          result_code = UserExpression::Evaluate(
              exe_ctx, expr_options, wp_sp->GetConditionText(),
              llvm::StringRef(), result_value_sp, error);

          if (result_code == eExpressionCompleted) {
            if (result_value_sp) {
              Scalar scalar_value;
              if (result_value_sp->ResolveValue(scalar_value)) {
                if (scalar_value.ULongLong(1) == 0) {
                  // The condition failed, which we consider "not having hit
                  // the watchpoint" so undo the hit count here.
                  wp_sp->UndoHitCount();
                  m_should_stop = false;
                } else
                  m_should_stop = true;
                LLDB_LOGF(log,
                          "Condition successfully evaluated, result is %s.\n",
                          m_should_stop ? "true" : "false");
              } else {
                m_should_stop = true;
                LLDB_LOGF(
                    log,
                    "Failed to get an integer result from the expression.");
              }
            }
          } else {
            const char *err_str = error.AsCString("<unknown error>");
            LLDB_LOGF(log, "Error evaluating condition: \"%s\"\n", err_str);

            StreamString strm;
            strm << "stopped due to an error evaluating condition of "
                    "watchpoint ";
            wp_sp->GetDescription(&strm, eDescriptionLevelBrief);
            strm << ": \"" << wp_sp->GetConditionText() << "\"\n";
            strm << err_str;

            Debugger::ReportError(strm.GetString().str(),
                                  exe_ctx.GetTargetRef().GetDebugger().GetID());
          }
        }

        // If the condition says to stop, we run the callback to further decide
        // whether to stop.
        if (m_should_stop) {
            // FIXME: For now the callbacks have to run in async mode - the
            // first time we restart we need
            // to get out of there.  So set it here.
            // When we figure out how to nest watchpoint hits then this will
            // change.

          bool old_async = debugger.GetAsyncExecution();
          debugger.SetAsyncExecution(true);

          StoppointCallbackContext context(event_ptr, exe_ctx, false);
          bool stop_requested = wp_sp->InvokeCallback(&context);

          debugger.SetAsyncExecution(old_async);

          // Also make sure that the callback hasn't continued the target. If
          // it did, when we'll set m_should_stop to false and get out of here.
          if (HasTargetRunSinceMe())
            m_should_stop = false;

          if (m_should_stop && !stop_requested) {
            // We have been vetoed by the callback mechanism.
            m_should_stop = false;
          }
        }

        // Don't stop if the watched region value is unmodified, and
        // this is a Modify-type watchpoint.
        if (m_should_stop && !wp_sp->WatchedValueReportable(exe_ctx)) {
          wp_sp->UndoHitCount();
          m_should_stop = false;
        }

        // Finally, if we are going to stop, print out the new & old values:
        if (m_should_stop) {
          wp_sp->CaptureWatchedValue(exe_ctx);

          Debugger &debugger = exe_ctx.GetTargetRef().GetDebugger();
          StreamSP output_sp = debugger.GetAsyncOutputStream();
          if (wp_sp->DumpSnapshots(output_sp.get())) {
            output_sp->EOL();
            output_sp->Flush();
          }
        }

      } else {
        Log *log_process(GetLog(LLDBLog::Process));

        LLDB_LOGF(log_process,
                  "Process::%s could not find watchpoint id: %" PRId64 "...",
                  __FUNCTION__, m_value);
      }
      LLDB_LOGF(log,
                "Process::%s returning from action with m_should_stop: %d.",
                __FUNCTION__, m_should_stop);

      m_should_stop_is_valid = true;
    }
  }

private:
  void SetStepOverPlanComplete() {
    assert(m_using_step_over_plan);
    m_step_over_plan_complete = true;
  }
  
  bool m_should_stop = false;
  bool m_should_stop_is_valid = false;
  // A false watchpoint hit has happened -
  // the thread stopped with a watchpoint
  // hit notification, but the watched region
  // was not actually accessed (as determined
  // by the gdb stub we're talking to).
  // Continue past this watchpoint without
  // notifying the user; on some targets this
  // may mean disable wp, instruction step,
  // re-enable wp, continue.
  // On others, just continue.
  bool m_silently_skip_wp = false;
  bool m_step_over_plan_complete = false;
  bool m_using_step_over_plan = false;
};

// StopInfoUnixSignal

class StopInfoUnixSignal : public StopInfo {
public:
  StopInfoUnixSignal(Thread &thread, int signo, const char *description,
                     std::optional<int> code)
      : StopInfo(thread, signo), m_code(code) {
    SetDescription(description);
  }

  ~StopInfoUnixSignal() override = default;

  StopReason GetStopReason() const override { return eStopReasonSignal; }

  bool ShouldStopSynchronous(Event *event_ptr) override {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      return thread_sp->GetProcess()->GetUnixSignals()->GetShouldStop(m_value);
    return false;
  }

  bool ShouldStop(Event *event_ptr) override {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      return thread_sp->GetProcess()->GetUnixSignals()->GetShouldStop(m_value);
    return false;
  }

  // If should stop returns false, check if we should notify of this event
  bool DoShouldNotify(Event *event_ptr) override {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp) {
      bool should_notify =
          thread_sp->GetProcess()->GetUnixSignals()->GetShouldNotify(m_value);
      if (should_notify) {
        StreamString strm;
        strm.Format(
            "thread {0:d} received signal: {1}", thread_sp->GetIndexID(),
            thread_sp->GetProcess()->GetUnixSignals()->GetSignalAsStringRef(
                m_value));
        Process::ProcessEventData::AddRestartedReason(event_ptr,
                                                      strm.GetData());
      }
      return should_notify;
    }
    return true;
  }

  void WillResume(lldb::StateType resume_state) override {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp) {
      if (!thread_sp->GetProcess()->GetUnixSignals()->GetShouldSuppress(
              m_value))
        thread_sp->SetResumeSignal(m_value);
    }
  }

  const char *GetDescription() override {
    if (m_description.empty()) {
      ThreadSP thread_sp(m_thread_wp.lock());
      if (thread_sp) {
        UnixSignalsSP unix_signals = thread_sp->GetProcess()->GetUnixSignals();
        StreamString strm;
        strm << "signal ";

        std::string signal_name =
            unix_signals->GetSignalDescription(m_value, m_code);
        if (signal_name.size())
          strm << signal_name;
        else
          strm.Printf("%" PRIi64, m_value);

        m_description = std::string(strm.GetString());
      }
    }
    return m_description.c_str();
  }

private:
  // In siginfo_t terms, if m_value is si_signo, m_code is si_code.
  std::optional<int> m_code;
};

// StopInfoTrace

class StopInfoTrace : public StopInfo {
public:
  StopInfoTrace(Thread &thread) : StopInfo(thread, LLDB_INVALID_UID) {}

  ~StopInfoTrace() override = default;

  StopReason GetStopReason() const override { return eStopReasonTrace; }

  const char *GetDescription() override {
    if (m_description.empty())
      return "trace";
    else
      return m_description.c_str();
  }
};

// StopInfoException

class StopInfoException : public StopInfo {
public:
  StopInfoException(Thread &thread, const char *description)
      : StopInfo(thread, LLDB_INVALID_UID) {
    if (description)
      SetDescription(description);
  }

  ~StopInfoException() override = default;

  StopReason GetStopReason() const override { return eStopReasonException; }

  const char *GetDescription() override {
    if (m_description.empty())
      return "exception";
    else
      return m_description.c_str();
  }
};

// StopInfoProcessorTrace

class StopInfoProcessorTrace : public StopInfo {
public:
  StopInfoProcessorTrace(Thread &thread, const char *description)
      : StopInfo(thread, LLDB_INVALID_UID) {
    if (description)
      SetDescription(description);
  }

  ~StopInfoProcessorTrace() override = default;

  StopReason GetStopReason() const override {
    return eStopReasonProcessorTrace;
  }

  const char *GetDescription() override {
    if (m_description.empty())
      return "processor trace event";
    else
      return m_description.c_str();
  }
};

// StopInfoThreadPlan

class StopInfoThreadPlan : public StopInfo {
public:
  StopInfoThreadPlan(ThreadPlanSP &plan_sp, ValueObjectSP &return_valobj_sp,
                     ExpressionVariableSP &expression_variable_sp)
      : StopInfo(plan_sp->GetThread(), LLDB_INVALID_UID), m_plan_sp(plan_sp),
        m_return_valobj_sp(return_valobj_sp),
        m_expression_variable_sp(expression_variable_sp) {}

  ~StopInfoThreadPlan() override = default;

  StopReason GetStopReason() const override { return eStopReasonPlanComplete; }

  const char *GetDescription() override {
    if (m_description.empty()) {
      StreamString strm;
      m_plan_sp->GetDescription(&strm, eDescriptionLevelBrief);
      m_description = std::string(strm.GetString());
    }
    return m_description.c_str();
  }

  ValueObjectSP GetReturnValueObject() { return m_return_valobj_sp; }

  ExpressionVariableSP GetExpressionVariable() {
    return m_expression_variable_sp;
  }

protected:
  bool ShouldStop(Event *event_ptr) override {
    if (m_plan_sp)
      return m_plan_sp->ShouldStop(event_ptr);
    else
      return StopInfo::ShouldStop(event_ptr);
  }

private:
  ThreadPlanSP m_plan_sp;
  ValueObjectSP m_return_valobj_sp;
  ExpressionVariableSP m_expression_variable_sp;
};

// StopInfoExec

class StopInfoExec : public StopInfo {
public:
  StopInfoExec(Thread &thread) : StopInfo(thread, LLDB_INVALID_UID) {}

  ~StopInfoExec() override = default;

  bool ShouldStop(Event *event_ptr) override {
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      return thread_sp->GetProcess()->GetStopOnExec();
    return false;
  }

  StopReason GetStopReason() const override { return eStopReasonExec; }

  const char *GetDescription() override { return "exec"; }

protected:
  void PerformAction(Event *event_ptr) override {
    // Only perform the action once
    if (m_performed_action)
      return;
    m_performed_action = true;
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      thread_sp->GetProcess()->DidExec();
  }

  bool m_performed_action = false;
};

// StopInfoFork

class StopInfoFork : public StopInfo {
public:
  StopInfoFork(Thread &thread, lldb::pid_t child_pid, lldb::tid_t child_tid)
      : StopInfo(thread, child_pid), m_child_pid(child_pid),
        m_child_tid(child_tid) {}

  ~StopInfoFork() override = default;

  bool ShouldStop(Event *event_ptr) override { return false; }

  StopReason GetStopReason() const override { return eStopReasonFork; }

  const char *GetDescription() override { return "fork"; }

protected:
  void PerformAction(Event *event_ptr) override {
    // Only perform the action once
    if (m_performed_action)
      return;
    m_performed_action = true;
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      thread_sp->GetProcess()->DidFork(m_child_pid, m_child_tid);
  }

  bool m_performed_action = false;

private:
  lldb::pid_t m_child_pid;
  lldb::tid_t m_child_tid;
};

// StopInfoVFork

class StopInfoVFork : public StopInfo {
public:
  StopInfoVFork(Thread &thread, lldb::pid_t child_pid, lldb::tid_t child_tid)
      : StopInfo(thread, child_pid), m_child_pid(child_pid),
        m_child_tid(child_tid) {}

  ~StopInfoVFork() override = default;

  bool ShouldStop(Event *event_ptr) override { return false; }

  StopReason GetStopReason() const override { return eStopReasonVFork; }

  const char *GetDescription() override { return "vfork"; }

protected:
  void PerformAction(Event *event_ptr) override {
    // Only perform the action once
    if (m_performed_action)
      return;
    m_performed_action = true;
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      thread_sp->GetProcess()->DidVFork(m_child_pid, m_child_tid);
  }

  bool m_performed_action = false;

private:
  lldb::pid_t m_child_pid;
  lldb::tid_t m_child_tid;
};

// StopInfoVForkDone

class StopInfoVForkDone : public StopInfo {
public:
  StopInfoVForkDone(Thread &thread) : StopInfo(thread, 0) {}

  ~StopInfoVForkDone() override = default;

  bool ShouldStop(Event *event_ptr) override { return false; }

  StopReason GetStopReason() const override { return eStopReasonVForkDone; }

  const char *GetDescription() override { return "vforkdone"; }

protected:
  void PerformAction(Event *event_ptr) override {
    // Only perform the action once
    if (m_performed_action)
      return;
    m_performed_action = true;
    ThreadSP thread_sp(m_thread_wp.lock());
    if (thread_sp)
      thread_sp->GetProcess()->DidVForkDone();
  }

  bool m_performed_action = false;
};

} // namespace lldb_private

StopInfoSP StopInfo::CreateStopReasonWithBreakpointSiteID(Thread &thread,
                                                          break_id_t break_id) {
  return StopInfoSP(new StopInfoBreakpoint(thread, break_id));
}

StopInfoSP StopInfo::CreateStopReasonWithBreakpointSiteID(Thread &thread,
                                                          break_id_t break_id,
                                                          bool should_stop) {
  return StopInfoSP(new StopInfoBreakpoint(thread, break_id, should_stop));
}

// LWP_TODO: We'll need a CreateStopReasonWithWatchpointResourceID akin
// to CreateStopReasonWithBreakpointSiteID
StopInfoSP StopInfo::CreateStopReasonWithWatchpointID(Thread &thread,
                                                      break_id_t watch_id,
                                                      bool silently_continue) {
  return StopInfoSP(
      new StopInfoWatchpoint(thread, watch_id, silently_continue));
}

StopInfoSP StopInfo::CreateStopReasonWithSignal(Thread &thread, int signo,
                                                const char *description,
                                                std::optional<int> code) {
  thread.GetProcess()->GetUnixSignals()->IncrementSignalHitCount(signo);
  return StopInfoSP(new StopInfoUnixSignal(thread, signo, description, code));
}

StopInfoSP StopInfo::CreateStopReasonToTrace(Thread &thread) {
  return StopInfoSP(new StopInfoTrace(thread));
}

StopInfoSP StopInfo::CreateStopReasonWithPlan(
    ThreadPlanSP &plan_sp, ValueObjectSP return_valobj_sp,
    ExpressionVariableSP expression_variable_sp) {
  return StopInfoSP(new StopInfoThreadPlan(plan_sp, return_valobj_sp,
                                           expression_variable_sp));
}

StopInfoSP StopInfo::CreateStopReasonWithException(Thread &thread,
                                                   const char *description) {
  return StopInfoSP(new StopInfoException(thread, description));
}

StopInfoSP StopInfo::CreateStopReasonProcessorTrace(Thread &thread,
                                                    const char *description) {
  return StopInfoSP(new StopInfoProcessorTrace(thread, description));
}

StopInfoSP StopInfo::CreateStopReasonWithExec(Thread &thread) {
  return StopInfoSP(new StopInfoExec(thread));
}

StopInfoSP StopInfo::CreateStopReasonFork(Thread &thread,
                                          lldb::pid_t child_pid,
                                          lldb::tid_t child_tid) {
  return StopInfoSP(new StopInfoFork(thread, child_pid, child_tid));
}


StopInfoSP StopInfo::CreateStopReasonVFork(Thread &thread,
                                           lldb::pid_t child_pid,
                                           lldb::tid_t child_tid) {
  return StopInfoSP(new StopInfoVFork(thread, child_pid, child_tid));
}

StopInfoSP StopInfo::CreateStopReasonVForkDone(Thread &thread) {
  return StopInfoSP(new StopInfoVForkDone(thread));
}

ValueObjectSP StopInfo::GetReturnValueObject(StopInfoSP &stop_info_sp) {
  if (stop_info_sp &&
      stop_info_sp->GetStopReason() == eStopReasonPlanComplete) {
    StopInfoThreadPlan *plan_stop_info =
        static_cast<StopInfoThreadPlan *>(stop_info_sp.get());
    return plan_stop_info->GetReturnValueObject();
  } else
    return ValueObjectSP();
}

ExpressionVariableSP StopInfo::GetExpressionVariable(StopInfoSP &stop_info_sp) {
  if (stop_info_sp &&
      stop_info_sp->GetStopReason() == eStopReasonPlanComplete) {
    StopInfoThreadPlan *plan_stop_info =
        static_cast<StopInfoThreadPlan *>(stop_info_sp.get());
    return plan_stop_info->GetExpressionVariable();
  } else
    return ExpressionVariableSP();
}

lldb::ValueObjectSP
StopInfo::GetCrashingDereference(StopInfoSP &stop_info_sp,
                                 lldb::addr_t *crashing_address) {
  if (!stop_info_sp) {
    return ValueObjectSP();
  }

  const char *description = stop_info_sp->GetDescription();
  if (!description) {
    return ValueObjectSP();
  }

  ThreadSP thread_sp = stop_info_sp->GetThread();
  if (!thread_sp) {
    return ValueObjectSP();
  }

  StackFrameSP frame_sp =
      thread_sp->GetSelectedFrame(DoNoSelectMostRelevantFrame);

  if (!frame_sp) {
    return ValueObjectSP();
  }

  const char address_string[] = "address=";

  const char *address_loc = strstr(description, address_string);
  if (!address_loc) {
    return ValueObjectSP();
  }

  address_loc += (sizeof(address_string) - 1);

  uint64_t address = strtoull(address_loc, nullptr, 0);
  if (crashing_address) {
    *crashing_address = address;
  }

  return frame_sp->GuessValueForAddress(address);
}
