//===-- ThreadList.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <algorithm>

#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

ThreadList::ThreadList(Process &process)
    : ThreadCollection(), m_process(process), m_stop_id(0),
      m_selected_tid(LLDB_INVALID_THREAD_ID) {}

ThreadList::ThreadList(const ThreadList &rhs)
    : ThreadCollection(), m_process(rhs.m_process), m_stop_id(rhs.m_stop_id),
      m_selected_tid() {
  // Use the assignment operator since it uses the mutex
  *this = rhs;
}

const ThreadList &ThreadList::operator=(const ThreadList &rhs) {
  if (this != &rhs) {
    // We only allow assignments between thread lists describing the same
    // process. Same process implies same mutex, which means it's enough to lock
    // just the current object.
    assert(&m_process == &rhs.m_process);
    assert(&GetMutex() == &rhs.GetMutex());
    std::lock_guard<std::recursive_mutex> guard(GetMutex());

    m_stop_id = rhs.m_stop_id;
    m_threads = rhs.m_threads;
    m_selected_tid = rhs.m_selected_tid;
  }
  return *this;
}

ThreadList::~ThreadList() {
  // Clear the thread list. Clear will take the mutex lock which will ensure
  // that if anyone is using the list they won't get it removed while using it.
  Clear();
}

lldb::ThreadSP ThreadList::GetExpressionExecutionThread() {
  if (m_expression_tid_stack.empty())
    return GetSelectedThread();
  ThreadSP expr_thread_sp = FindThreadByID(m_expression_tid_stack.back());
  if (expr_thread_sp)
    return expr_thread_sp;
  else
    return GetSelectedThread();
}

void ThreadList::PushExpressionExecutionThread(lldb::tid_t tid) {
  m_expression_tid_stack.push_back(tid);
}

void ThreadList::PopExpressionExecutionThread(lldb::tid_t tid) {
  assert(m_expression_tid_stack.back() == tid);
  m_expression_tid_stack.pop_back();
}

uint32_t ThreadList::GetStopID() const { return m_stop_id; }

void ThreadList::SetStopID(uint32_t stop_id) { m_stop_id = stop_id; }

uint32_t ThreadList::GetSize(bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();
  return m_threads.size();
}

ThreadSP ThreadList::GetThreadAtIndex(uint32_t idx, bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();

  ThreadSP thread_sp;
  if (idx < m_threads.size())
    thread_sp = m_threads[idx];
  return thread_sp;
}

ThreadSP ThreadList::FindThreadByID(lldb::tid_t tid, bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();

  ThreadSP thread_sp;
  uint32_t idx = 0;
  const uint32_t num_threads = m_threads.size();
  for (idx = 0; idx < num_threads; ++idx) {
    if (m_threads[idx]->GetID() == tid) {
      thread_sp = m_threads[idx];
      break;
    }
  }
  return thread_sp;
}

ThreadSP ThreadList::FindThreadByProtocolID(lldb::tid_t tid, bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();

  ThreadSP thread_sp;
  uint32_t idx = 0;
  const uint32_t num_threads = m_threads.size();
  for (idx = 0; idx < num_threads; ++idx) {
    if (m_threads[idx]->GetProtocolID() == tid) {
      thread_sp = m_threads[idx];
      break;
    }
  }
  return thread_sp;
}

ThreadSP ThreadList::RemoveThreadByID(lldb::tid_t tid, bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();

  ThreadSP thread_sp;
  uint32_t idx = 0;
  const uint32_t num_threads = m_threads.size();
  for (idx = 0; idx < num_threads; ++idx) {
    if (m_threads[idx]->GetID() == tid) {
      thread_sp = m_threads[idx];
      m_threads.erase(m_threads.begin() + idx);
      break;
    }
  }
  return thread_sp;
}

ThreadSP ThreadList::RemoveThreadByProtocolID(lldb::tid_t tid,
                                              bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();

  ThreadSP thread_sp;
  uint32_t idx = 0;
  const uint32_t num_threads = m_threads.size();
  for (idx = 0; idx < num_threads; ++idx) {
    if (m_threads[idx]->GetProtocolID() == tid) {
      thread_sp = m_threads[idx];
      m_threads.erase(m_threads.begin() + idx);
      break;
    }
  }
  return thread_sp;
}

ThreadSP ThreadList::GetThreadSPForThreadPtr(Thread *thread_ptr) {
  ThreadSP thread_sp;
  if (thread_ptr) {
    std::lock_guard<std::recursive_mutex> guard(GetMutex());

    uint32_t idx = 0;
    const uint32_t num_threads = m_threads.size();
    for (idx = 0; idx < num_threads; ++idx) {
      if (m_threads[idx].get() == thread_ptr) {
        thread_sp = m_threads[idx];
        break;
      }
    }
  }
  return thread_sp;
}

ThreadSP ThreadList::GetBackingThread(const ThreadSP &real_thread) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  ThreadSP thread_sp;
  const uint32_t num_threads = m_threads.size();
  for (uint32_t idx = 0; idx < num_threads; ++idx) {
    if (m_threads[idx]->GetBackingThread() == real_thread) {
      thread_sp = m_threads[idx];
      break;
    }
  }
  return thread_sp;
}

ThreadSP ThreadList::FindThreadByIndexID(uint32_t index_id, bool can_update) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  if (can_update)
    m_process.UpdateThreadListIfNeeded();

  ThreadSP thread_sp;
  const uint32_t num_threads = m_threads.size();
  for (uint32_t idx = 0; idx < num_threads; ++idx) {
    if (m_threads[idx]->GetIndexID() == index_id) {
      thread_sp = m_threads[idx];
      break;
    }
  }
  return thread_sp;
}

bool ThreadList::ShouldStop(Event *event_ptr) {
  // Running events should never stop, obviously...

  Log *log = GetLog(LLDBLog::Step);

  // The ShouldStop method of the threads can do a whole lot of work, figuring
  // out whether the thread plan conditions are met.  So we don't want to keep
  // the ThreadList locked the whole time we are doing this.
  // FIXME: It is possible that running code could cause new threads
  // to be created.  If that happens, we will miss asking them whether they
  // should stop.  This is not a big deal since we haven't had a chance to hang
  // any interesting operations on those threads yet.

  collection threads_copy;
  {
    // Scope for locker
    std::lock_guard<std::recursive_mutex> guard(GetMutex());

    m_process.UpdateThreadListIfNeeded();
    for (lldb::ThreadSP thread_sp : m_threads) {
      // This is an optimization...  If we didn't let a thread run in between
      // the previous stop and this one, we shouldn't have to consult it for
      // ShouldStop.  So just leave it off the list we are going to inspect.
      // If the thread didn't run but had work to do before declaring a public
      // stop, then also include it.
      // On Linux, if a thread-specific conditional breakpoint was hit, it won't
      // necessarily be the thread that hit the breakpoint itself that
      // evaluates the conditional expression, so the thread that hit the
      // breakpoint could still be asked to stop, even though it hasn't been
      // allowed to run since the previous stop.
      if (thread_sp->GetTemporaryResumeState() != eStateSuspended ||
          thread_sp->IsStillAtLastBreakpointHit()
          || thread_sp->ShouldRunBeforePublicStop())
        threads_copy.push_back(thread_sp);
    }

    // It is possible the threads we were allowing to run all exited and then
    // maybe the user interrupted or something, then fall back on looking at
    // all threads:

    if (threads_copy.size() == 0)
      threads_copy = m_threads;
  }

  collection::iterator pos, end = threads_copy.end();

  if (log) {
    log->PutCString("");
    LLDB_LOGF(log,
              "ThreadList::%s: %" PRIu64 " threads, %" PRIu64
              " unsuspended threads",
              __FUNCTION__, (uint64_t)m_threads.size(),
              (uint64_t)threads_copy.size());
  }

  bool did_anybody_stop_for_a_reason = false;

  // If the event is an Interrupt event, then we're going to stop no matter
  // what.  Otherwise, presume we won't stop.
  bool should_stop = false;
  if (Process::ProcessEventData::GetInterruptedFromEvent(event_ptr)) {
    LLDB_LOGF(
        log, "ThreadList::%s handling interrupt event, should stop set to true",
        __FUNCTION__);

    should_stop = true;
  }

  // Now we run through all the threads and get their stop info's.  We want to
  // make sure to do this first before we start running the ShouldStop, because
  // one thread's ShouldStop could destroy information (like deleting a thread
  // specific breakpoint another thread had stopped at) which could lead us to
  // compute the StopInfo incorrectly. We don't need to use it here, we just
  // want to make sure it gets computed.

  for (pos = threads_copy.begin(); pos != end; ++pos) {
    ThreadSP thread_sp(*pos);
    thread_sp->GetStopInfo();
  }

  // If a thread needs to finish some job that can be done just on this thread
  // before broadcastion the stop, it will signal that by returning true for
  // ShouldRunBeforePublicStop.  This variable gathers the results from that.
  bool a_thread_needs_to_run = false;
  for (pos = threads_copy.begin(); pos != end; ++pos) {
    ThreadSP thread_sp(*pos);

    // We should never get a stop for which no thread had a stop reason, but
    // sometimes we do see this - for instance when we first connect to a
    // remote stub.  In that case we should stop, since we can't figure out the
    // right thing to do and stopping gives the user control over what to do in
    // this instance.
    //
    // Note, this causes a problem when you have a thread specific breakpoint,
    // and a bunch of threads hit the breakpoint, but not the thread which we
    // are waiting for.  All the threads that are not "supposed" to hit the
    // breakpoint are marked as having no stop reason, which is right, they
    // should not show a stop reason.  But that triggers this code and causes
    // us to stop seemingly for no reason.
    //
    // Since the only way we ever saw this error was on first attach, I'm only
    // going to trigger set did_anybody_stop_for_a_reason to true unless this
    // is the first stop.
    //
    // If this becomes a problem, we'll have to have another StopReason like
    // "StopInfoHidden" which will look invalid everywhere but at this check.

    if (thread_sp->GetProcess()->GetStopID() > 1)
      did_anybody_stop_for_a_reason = true;
    else
      did_anybody_stop_for_a_reason |= thread_sp->ThreadStoppedForAReason();

    const bool thread_should_stop = thread_sp->ShouldStop(event_ptr);

    if (thread_should_stop)
      should_stop |= true;
    else {
      bool this_thread_forces_run = thread_sp->ShouldRunBeforePublicStop();
      a_thread_needs_to_run |= this_thread_forces_run;
      if (this_thread_forces_run) 
        LLDB_LOG(log,
                 "ThreadList::{0} thread: {1:x}, "
                 "says it needs to run before public stop.",
                 __FUNCTION__, thread_sp->GetID());
    }
  }

  if (a_thread_needs_to_run) {
    should_stop = false;
  } else if (!should_stop && !did_anybody_stop_for_a_reason) {
    should_stop = true;
    LLDB_LOGF(log,
              "ThreadList::%s we stopped but no threads had a stop reason, "
              "overriding should_stop and stopping.",
              __FUNCTION__);
  }

  LLDB_LOGF(log, "ThreadList::%s overall should_stop = %i", __FUNCTION__,
            should_stop);

  if (should_stop) {
    for (pos = threads_copy.begin(); pos != end; ++pos) {
      ThreadSP thread_sp(*pos);
      thread_sp->WillStop();
    }
  }

  return should_stop;
}

Vote ThreadList::ShouldReportStop(Event *event_ptr) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  Vote result = eVoteNoOpinion;
  m_process.UpdateThreadListIfNeeded();
  collection::iterator pos, end = m_threads.end();

  Log *log = GetLog(LLDBLog::Step);

  LLDB_LOGF(log, "ThreadList::%s %" PRIu64 " threads", __FUNCTION__,
            (uint64_t)m_threads.size());

  // Run through the threads and ask whether we should report this event. For
  // stopping, a YES vote wins over everything.  A NO vote wins over NO
  // opinion.  The exception is if a thread has work it needs to force before
  // a public stop, which overrides everyone else's opinion:
  for (pos = m_threads.begin(); pos != end; ++pos) {
    ThreadSP thread_sp(*pos);
    if (thread_sp->ShouldRunBeforePublicStop()) {
      LLDB_LOG(log, "Thread {0:x} has private business to complete, overrode "
               "the should report stop.", thread_sp->GetID());
      result = eVoteNo;
      break;
    }

    const Vote vote = thread_sp->ShouldReportStop(event_ptr);
    switch (vote) {
    case eVoteNoOpinion:
      continue;

    case eVoteYes:
      result = eVoteYes;
      break;

    case eVoteNo:
      if (result == eVoteNoOpinion) {
        result = eVoteNo;
      } else {
        LLDB_LOG(log,
          "Thread {0:x} voted {1}, but lost out because result was {2}",
          thread_sp->GetID(), vote, result);
      }
      break;
    }
  }
  LLDB_LOG(log, "Returning {0}", result);
  return result;
}

void ThreadList::SetShouldReportStop(Vote vote) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  m_process.UpdateThreadListIfNeeded();
  collection::iterator pos, end = m_threads.end();
  for (pos = m_threads.begin(); pos != end; ++pos) {
    ThreadSP thread_sp(*pos);
    thread_sp->SetShouldReportStop(vote);
  }
}

Vote ThreadList::ShouldReportRun(Event *event_ptr) {

  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  Vote result = eVoteNoOpinion;
  m_process.UpdateThreadListIfNeeded();
  collection::iterator pos, end = m_threads.end();

  // Run through the threads and ask whether we should report this event. The
  // rule is NO vote wins over everything, a YES vote wins over no opinion.

  Log *log = GetLog(LLDBLog::Step);

  for (pos = m_threads.begin(); pos != end; ++pos) {
    if ((*pos)->GetResumeState() != eStateSuspended) {
      switch ((*pos)->ShouldReportRun(event_ptr)) {
      case eVoteNoOpinion:
        continue;
      case eVoteYes:
        if (result == eVoteNoOpinion)
          result = eVoteYes;
        break;
      case eVoteNo:
        LLDB_LOGF(log,
                  "ThreadList::ShouldReportRun() thread %d (0x%4.4" PRIx64
                  ") says don't report.",
                  (*pos)->GetIndexID(), (*pos)->GetID());
        result = eVoteNo;
        break;
      }
    }
  }
  return result;
}

void ThreadList::Clear() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  m_stop_id = 0;
  m_threads.clear();
  m_selected_tid = LLDB_INVALID_THREAD_ID;
}

void ThreadList::Destroy() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  const uint32_t num_threads = m_threads.size();
  for (uint32_t idx = 0; idx < num_threads; ++idx) {
    m_threads[idx]->DestroyThread();
  }
}

void ThreadList::RefreshStateAfterStop() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  m_process.UpdateThreadListIfNeeded();

  Log *log = GetLog(LLDBLog::Step);
  if (log && log->GetVerbose())
    LLDB_LOGF(log,
              "Turning off notification of new threads while single stepping "
              "a thread.");

  collection::iterator pos, end = m_threads.end();
  for (pos = m_threads.begin(); pos != end; ++pos)
    (*pos)->RefreshStateAfterStop();
}

void ThreadList::DiscardThreadPlans() {
  // You don't need to update the thread list here, because only threads that
  // you currently know about have any thread plans.
  std::lock_guard<std::recursive_mutex> guard(GetMutex());

  collection::iterator pos, end = m_threads.end();
  for (pos = m_threads.begin(); pos != end; ++pos)
    (*pos)->DiscardThreadPlans(true);
}

bool ThreadList::WillResume() {
  // Run through the threads and perform their momentary actions. But we only
  // do this for threads that are running, user suspended threads stay where
  // they are.

  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  m_process.UpdateThreadListIfNeeded();

  collection::iterator pos, end = m_threads.end();

  // See if any thread wants to run stopping others.  If it does, then we won't
  // setup the other threads for resume, since they aren't going to get a
  // chance to run.  This is necessary because the SetupForResume might add
  // "StopOthers" plans which would then get to be part of the who-gets-to-run
  // negotiation, but they're coming in after the fact, and the threads that
  // are already set up should take priority.

  bool wants_solo_run = false;

  for (pos = m_threads.begin(); pos != end; ++pos) {
    lldbassert((*pos)->GetCurrentPlan() &&
               "thread should not have null thread plan");
    if ((*pos)->GetResumeState() != eStateSuspended &&
        (*pos)->GetCurrentPlan()->StopOthers()) {
      if ((*pos)->IsOperatingSystemPluginThread() &&
          !(*pos)->GetBackingThread())
        continue;
      wants_solo_run = true;
      break;
    }
  }

  if (wants_solo_run) {
    Log *log = GetLog(LLDBLog::Step);
    if (log && log->GetVerbose())
      LLDB_LOGF(log, "Turning on notification of new threads while single "
                     "stepping a thread.");
    m_process.StartNoticingNewThreads();
  } else {
    Log *log = GetLog(LLDBLog::Step);
    if (log && log->GetVerbose())
      LLDB_LOGF(log, "Turning off notification of new threads while single "
                     "stepping a thread.");
    m_process.StopNoticingNewThreads();
  }

  // Give all the threads that are likely to run a last chance to set up their
  // state before we negotiate who is actually going to get a chance to run...
  // Don't set to resume suspended threads, and if any thread wanted to stop
  // others, only call setup on the threads that request StopOthers...

  for (pos = m_threads.begin(); pos != end; ++pos) {
    if ((*pos)->GetResumeState() != eStateSuspended &&
        (!wants_solo_run || (*pos)->GetCurrentPlan()->StopOthers())) {
      if ((*pos)->IsOperatingSystemPluginThread() &&
          !(*pos)->GetBackingThread())
        continue;
      (*pos)->SetupForResume();
    }
  }

  // Now go through the threads and see if any thread wants to run just itself.
  // if so then pick one and run it.

  ThreadList run_me_only_list(m_process);

  run_me_only_list.SetStopID(m_process.GetStopID());

  // One or more threads might want to "Stop Others".  We want to handle all
  // those requests first.  And if there is a thread that wanted to "resume
  // before a public stop", let it get the first crack:
  // There are two special kinds of thread that have priority for "StopOthers":
  // a "ShouldRunBeforePublicStop thread, or the currently selected thread.  If
  // we find one satisfying that critereon, put it here.
  ThreadSP stop_others_thread_sp;

  for (pos = m_threads.begin(); pos != end; ++pos) {
    ThreadSP thread_sp(*pos);
    if (thread_sp->GetResumeState() != eStateSuspended &&
        thread_sp->GetCurrentPlan()->StopOthers()) {
      if ((*pos)->IsOperatingSystemPluginThread() &&
          !(*pos)->GetBackingThread())
        continue;

      // You can't say "stop others" and also want yourself to be suspended.
      assert(thread_sp->GetCurrentPlan()->RunState() != eStateSuspended);
      run_me_only_list.AddThread(thread_sp);

      if (thread_sp == GetSelectedThread())
        stop_others_thread_sp = thread_sp;
        
      if (thread_sp->ShouldRunBeforePublicStop()) {
        // This takes precedence, so if we find one of these, service it:
        stop_others_thread_sp = thread_sp;
        break;
      }
    }
  }

  bool need_to_resume = true;

  if (run_me_only_list.GetSize(false) == 0) {
    // Everybody runs as they wish:
    for (pos = m_threads.begin(); pos != end; ++pos) {
      ThreadSP thread_sp(*pos);
      StateType run_state;
      if (thread_sp->GetResumeState() != eStateSuspended)
        run_state = thread_sp->GetCurrentPlan()->RunState();
      else
        run_state = eStateSuspended;
      if (!thread_sp->ShouldResume(run_state))
        need_to_resume = false;
    }
  } else {
    ThreadSP thread_to_run;

    if (stop_others_thread_sp) {
      thread_to_run = stop_others_thread_sp;
    } else if (run_me_only_list.GetSize(false) == 1) {
      thread_to_run = run_me_only_list.GetThreadAtIndex(0);
    } else {
      int random_thread =
          (int)((run_me_only_list.GetSize(false) * (double)rand()) /
                (RAND_MAX + 1.0));
      thread_to_run = run_me_only_list.GetThreadAtIndex(random_thread);
    }

    for (pos = m_threads.begin(); pos != end; ++pos) {
      ThreadSP thread_sp(*pos);
      if (thread_sp == thread_to_run) {
        // Note, a thread might be able to fulfil it's plan w/o actually
        // resuming.  An example of this is a step that changes the current
        // inlined function depth w/o moving the PC.  Check that here:
        if (!thread_sp->ShouldResume(thread_sp->GetCurrentPlan()->RunState()))
          need_to_resume = false;
      } else
        thread_sp->ShouldResume(eStateSuspended);
    }
  }

  return need_to_resume;
}

void ThreadList::DidResume() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  collection::iterator pos, end = m_threads.end();
  for (pos = m_threads.begin(); pos != end; ++pos) {
    // Don't clear out threads that aren't going to get a chance to run, rather
    // leave their state for the next time around.
    ThreadSP thread_sp(*pos);
    if (thread_sp->GetTemporaryResumeState() != eStateSuspended)
      thread_sp->DidResume();
  }
}

void ThreadList::DidStop() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  collection::iterator pos, end = m_threads.end();
  for (pos = m_threads.begin(); pos != end; ++pos) {
    // Notify threads that the process just stopped. Note, this currently
    // assumes that all threads in the list stop when the process stops.  In
    // the future we will want to support a debugging model where some threads
    // continue to run while others are stopped.  We either need to handle that
    // somehow here or create a special thread list containing only threads
    // which will stop in the code that calls this method (currently
    // Process::SetPrivateState).
    ThreadSP thread_sp(*pos);
    if (StateIsRunningState(thread_sp->GetState()))
      thread_sp->DidStop();
  }
}

ThreadSP ThreadList::GetSelectedThread() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  ThreadSP thread_sp = FindThreadByID(m_selected_tid);
  if (!thread_sp.get()) {
    if (m_threads.size() == 0)
      return thread_sp;
    m_selected_tid = m_threads[0]->GetID();
    thread_sp = m_threads[0];
  }
  return thread_sp;
}

bool ThreadList::SetSelectedThreadByID(lldb::tid_t tid, bool notify) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  ThreadSP selected_thread_sp(FindThreadByID(tid));
  if (selected_thread_sp) {
    m_selected_tid = tid;
    selected_thread_sp->SetDefaultFileAndLineToSelectedFrame();
  } else
    m_selected_tid = LLDB_INVALID_THREAD_ID;

  if (notify)
    NotifySelectedThreadChanged(m_selected_tid);

  return m_selected_tid != LLDB_INVALID_THREAD_ID;
}

bool ThreadList::SetSelectedThreadByIndexID(uint32_t index_id, bool notify) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  ThreadSP selected_thread_sp(FindThreadByIndexID(index_id));
  if (selected_thread_sp.get()) {
    m_selected_tid = selected_thread_sp->GetID();
    selected_thread_sp->SetDefaultFileAndLineToSelectedFrame();
  } else
    m_selected_tid = LLDB_INVALID_THREAD_ID;

  if (notify)
    NotifySelectedThreadChanged(m_selected_tid);

  return m_selected_tid != LLDB_INVALID_THREAD_ID;
}

void ThreadList::NotifySelectedThreadChanged(lldb::tid_t tid) {
  ThreadSP selected_thread_sp(FindThreadByID(tid));
  if (selected_thread_sp->EventTypeHasListeners(
          Thread::eBroadcastBitThreadSelected)) {
    auto data_sp =
        std::make_shared<Thread::ThreadEventData>(selected_thread_sp);
    selected_thread_sp->BroadcastEvent(Thread::eBroadcastBitThreadSelected,
                                       data_sp);
  }
}

void ThreadList::Update(ThreadList &rhs) {
  if (this != &rhs) {
    // We only allow assignments between thread lists describing the same
    // process. Same process implies same mutex, which means it's enough to lock
    // just the current object.
    assert(&m_process == &rhs.m_process);
    assert(&GetMutex() == &rhs.GetMutex());
    std::lock_guard<std::recursive_mutex> guard(GetMutex());

    m_stop_id = rhs.m_stop_id;
    m_threads.swap(rhs.m_threads);
    m_selected_tid = rhs.m_selected_tid;

    // Now we look for threads that we are done with and make sure to clear
    // them up as much as possible so anyone with a shared pointer will still
    // have a reference, but the thread won't be of much use. Using
    // std::weak_ptr for all backward references (such as a thread to a
    // process) will eventually solve this issue for us, but for now, we need
    // to work around the issue
    collection::iterator rhs_pos, rhs_end = rhs.m_threads.end();
    for (rhs_pos = rhs.m_threads.begin(); rhs_pos != rhs_end; ++rhs_pos) {
      // If this thread has already been destroyed, we don't need to look for
      // it to destroy it again.
      if (!(*rhs_pos)->IsValid())
        continue;

      const lldb::tid_t tid = (*rhs_pos)->GetID();
      bool thread_is_alive = false;
      const uint32_t num_threads = m_threads.size();
      for (uint32_t idx = 0; idx < num_threads; ++idx) {
        ThreadSP backing_thread = m_threads[idx]->GetBackingThread();
        if (m_threads[idx]->GetID() == tid ||
            (backing_thread && backing_thread->GetID() == tid)) {
          thread_is_alive = true;
          break;
        }
      }
      if (!thread_is_alive) {
        (*rhs_pos)->DestroyThread();
      }
    }
  }
}

void ThreadList::Flush() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  collection::iterator pos, end = m_threads.end();
  for (pos = m_threads.begin(); pos != end; ++pos)
    (*pos)->Flush();
}

std::recursive_mutex &ThreadList::GetMutex() const {
  return m_process.m_thread_mutex;
}

ThreadList::ExpressionExecutionThreadPusher::ExpressionExecutionThreadPusher(
    lldb::ThreadSP thread_sp)
    : m_thread_list(nullptr), m_tid(LLDB_INVALID_THREAD_ID) {
  if (thread_sp) {
    m_tid = thread_sp->GetID();
    m_thread_list = &thread_sp->GetProcess()->GetThreadList();
    m_thread_list->PushExpressionExecutionThread(m_tid);
  }
}
