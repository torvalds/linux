//===-- ThreadPlanStack.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStack.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

static void PrintPlanElement(Stream &s, const ThreadPlanSP &plan,
                             lldb::DescriptionLevel desc_level,
                             int32_t elem_idx) {
  s.IndentMore();
  s.Indent();
  s.Printf("Element %d: ", elem_idx);
  plan->GetDescription(&s, desc_level);
  s.EOL();
  s.IndentLess();
}

ThreadPlanStack::ThreadPlanStack(const Thread &thread, bool make_null) {
  if (make_null) {
    // The ThreadPlanNull doesn't do anything to the Thread, so this is actually
    // still a const operation.
    m_plans.push_back(
        ThreadPlanSP(new ThreadPlanNull(const_cast<Thread &>(thread))));
  }
}

void ThreadPlanStack::DumpThreadPlans(Stream &s,
                                      lldb::DescriptionLevel desc_level,
                                      bool include_internal) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  s.IndentMore();
  PrintOneStack(s, "Active plan stack", m_plans, desc_level, include_internal);
  PrintOneStack(s, "Completed plan stack", m_completed_plans, desc_level,
                include_internal);
  PrintOneStack(s, "Discarded plan stack", m_discarded_plans, desc_level,
                include_internal);
  s.IndentLess();
}

void ThreadPlanStack::PrintOneStack(Stream &s, llvm::StringRef stack_name,
                                    const PlanStack &stack,
                                    lldb::DescriptionLevel desc_level,
                                    bool include_internal) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  // If the stack is empty, just exit:
  if (stack.empty())
    return;

  // Make sure there are public completed plans:
  bool any_public = false;
  if (!include_internal) {
    for (auto plan : stack) {
      if (!plan->GetPrivate()) {
        any_public = true;
        break;
      }
    }
  }

  if (include_internal || any_public) {
    int print_idx = 0;
    s.Indent();
    s << stack_name << ":\n";
    for (auto plan : stack) {
      if (!include_internal && plan->GetPrivate())
        continue;
      PrintPlanElement(s, plan, desc_level, print_idx++);
    }
  }
}

size_t ThreadPlanStack::CheckpointCompletedPlans() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  m_completed_plan_checkpoint++;
  m_completed_plan_store.insert(
      std::make_pair(m_completed_plan_checkpoint, m_completed_plans));
  return m_completed_plan_checkpoint;
}

void ThreadPlanStack::RestoreCompletedPlanCheckpoint(size_t checkpoint) {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  auto result = m_completed_plan_store.find(checkpoint);
  assert(result != m_completed_plan_store.end() &&
         "Asked for a checkpoint that didn't exist");
  m_completed_plans.swap((*result).second);
  m_completed_plan_store.erase(result);
}

void ThreadPlanStack::DiscardCompletedPlanCheckpoint(size_t checkpoint) {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  m_completed_plan_store.erase(checkpoint);
}

void ThreadPlanStack::ThreadDestroyed(Thread *thread) {
  // Tell the plan stacks that this thread is going away:
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  for (ThreadPlanSP plan : m_plans)
    plan->ThreadDestroyed();

  for (ThreadPlanSP plan : m_discarded_plans)
    plan->ThreadDestroyed();

  for (ThreadPlanSP plan : m_completed_plans)
    plan->ThreadDestroyed();

  // Now clear the current plan stacks:
  m_plans.clear();
  m_discarded_plans.clear();
  m_completed_plans.clear();

  // Push a ThreadPlanNull on the plan stack.  That way we can continue
  // assuming that the plan stack is never empty, but if somebody errantly asks
  // questions of a destroyed thread without checking first whether it is
  // destroyed, they won't crash.
  if (thread != nullptr) {
    lldb::ThreadPlanSP null_plan_sp(new ThreadPlanNull(*thread));
    m_plans.push_back(null_plan_sp);
  }
}

void ThreadPlanStack::PushPlan(lldb::ThreadPlanSP new_plan_sp) {
  // If the thread plan doesn't already have a tracer, give it its parent's
  // tracer:
  // The first plan has to be a base plan:
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  assert((m_plans.size() > 0 || new_plan_sp->IsBasePlan()) &&
         "Zeroth plan must be a base plan");

  if (!new_plan_sp->GetThreadPlanTracer()) {
    assert(!m_plans.empty());
    new_plan_sp->SetThreadPlanTracer(m_plans.back()->GetThreadPlanTracer());
  }
  m_plans.push_back(new_plan_sp);
  new_plan_sp->DidPush();
}

lldb::ThreadPlanSP ThreadPlanStack::PopPlan() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  assert(m_plans.size() > 1 && "Can't pop the base thread plan");

  // Note that moving the top element of the vector would leave it in an
  // undefined state, and break the guarantee that the stack's thread plans are
  // all valid.
  lldb::ThreadPlanSP plan_sp = m_plans.back();
  m_plans.pop_back();
  m_completed_plans.push_back(plan_sp);
  plan_sp->DidPop();
  return plan_sp;
}

lldb::ThreadPlanSP ThreadPlanStack::DiscardPlan() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  assert(m_plans.size() > 1 && "Can't discard the base thread plan");

  // Note that moving the top element of the vector would leave it in an
  // undefined state, and break the guarantee that the stack's thread plans are
  // all valid.
  lldb::ThreadPlanSP plan_sp = m_plans.back();
  m_plans.pop_back();
  m_discarded_plans.push_back(plan_sp);
  plan_sp->DidPop();
  return plan_sp;
}

// If the input plan is nullptr, discard all plans.  Otherwise make sure this
// plan is in the stack, and if so discard up to and including it.
void ThreadPlanStack::DiscardPlansUpToPlan(ThreadPlan *up_to_plan_ptr) {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  int stack_size = m_plans.size();

  if (up_to_plan_ptr == nullptr) {
    for (int i = stack_size - 1; i > 0; i--)
      DiscardPlan();
    return;
  }

  bool found_it = false;
  for (int i = stack_size - 1; i > 0; i--) {
    if (m_plans[i].get() == up_to_plan_ptr) {
      found_it = true;
      break;
    }
  }

  if (found_it) {
    bool last_one = false;
    for (int i = stack_size - 1; i > 0 && !last_one; i--) {
      if (GetCurrentPlan().get() == up_to_plan_ptr)
        last_one = true;
      DiscardPlan();
    }
  }
}

void ThreadPlanStack::DiscardAllPlans() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  int stack_size = m_plans.size();
  for (int i = stack_size - 1; i > 0; i--) {
    DiscardPlan();
  }
}

void ThreadPlanStack::DiscardConsultingControllingPlans() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  while (true) {
    int controlling_plan_idx;
    bool discard = true;

    // Find the first controlling plan, see if it wants discarding, and if yes
    // discard up to it.
    for (controlling_plan_idx = m_plans.size() - 1; controlling_plan_idx >= 0;
         controlling_plan_idx--) {
      if (m_plans[controlling_plan_idx]->IsControllingPlan()) {
        discard = m_plans[controlling_plan_idx]->OkayToDiscard();
        break;
      }
    }

    // If the controlling plan doesn't want to get discarded, then we're done.
    if (!discard)
      return;

    // First pop all the dependent plans:
    for (int i = m_plans.size() - 1; i > controlling_plan_idx; i--) {
      DiscardPlan();
    }

    // Now discard the controlling plan itself.
    // The bottom-most plan never gets discarded.  "OkayToDiscard" for it
    // means discard it's dependent plans, but not it...
    if (controlling_plan_idx > 0) {
      DiscardPlan();
    }
  }
}

lldb::ThreadPlanSP ThreadPlanStack::GetCurrentPlan() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  assert(m_plans.size() != 0 && "There will always be a base plan.");
  return m_plans.back();
}

lldb::ThreadPlanSP ThreadPlanStack::GetCompletedPlan(bool skip_private) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  if (m_completed_plans.empty())
    return {};

  if (!skip_private)
    return m_completed_plans.back();

  for (int i = m_completed_plans.size() - 1; i >= 0; i--) {
    lldb::ThreadPlanSP completed_plan_sp;
    completed_plan_sp = m_completed_plans[i];
    if (!completed_plan_sp->GetPrivate())
      return completed_plan_sp;
  }
  return {};
}

lldb::ThreadPlanSP ThreadPlanStack::GetPlanByIndex(uint32_t plan_idx,
                                                   bool skip_private) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  uint32_t idx = 0;

  for (lldb::ThreadPlanSP plan_sp : m_plans) {
    if (skip_private && plan_sp->GetPrivate())
      continue;
    if (idx == plan_idx)
      return plan_sp;
    idx++;
  }
  return {};
}

lldb::ValueObjectSP ThreadPlanStack::GetReturnValueObject() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  if (m_completed_plans.empty())
    return {};

  for (int i = m_completed_plans.size() - 1; i >= 0; i--) {
    lldb::ValueObjectSP return_valobj_sp;
    return_valobj_sp = m_completed_plans[i]->GetReturnValueObject();
    if (return_valobj_sp)
      return return_valobj_sp;
  }
  return {};
}

lldb::ExpressionVariableSP ThreadPlanStack::GetExpressionVariable() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  if (m_completed_plans.empty())
    return {};

  for (int i = m_completed_plans.size() - 1; i >= 0; i--) {
    lldb::ExpressionVariableSP expression_variable_sp;
    expression_variable_sp = m_completed_plans[i]->GetExpressionVariable();
    if (expression_variable_sp)
      return expression_variable_sp;
  }
  return {};
}
bool ThreadPlanStack::AnyPlans() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  // There is always a base plan...
  return m_plans.size() > 1;
}

bool ThreadPlanStack::AnyCompletedPlans() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  return !m_completed_plans.empty();
}

bool ThreadPlanStack::AnyDiscardedPlans() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  return !m_discarded_plans.empty();
}

bool ThreadPlanStack::IsPlanDone(ThreadPlan *in_plan) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  for (auto plan : m_completed_plans) {
    if (plan.get() == in_plan)
      return true;
  }
  return false;
}

bool ThreadPlanStack::WasPlanDiscarded(ThreadPlan *in_plan) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  for (auto plan : m_discarded_plans) {
    if (plan.get() == in_plan)
      return true;
  }
  return false;
}

ThreadPlan *ThreadPlanStack::GetPreviousPlan(ThreadPlan *current_plan) const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  if (current_plan == nullptr)
    return nullptr;

  // Look first in the completed plans, if the plan is here and there is
  // a completed plan above it, return that.
  int stack_size = m_completed_plans.size();
  for (int i = stack_size - 1; i > 0; i--) {
    if (current_plan == m_completed_plans[i].get())
      return m_completed_plans[i - 1].get();
  }

  // If this is the first completed plan, the previous one is the
  // bottom of the regular plan stack.
  if (stack_size > 0 && m_completed_plans[0].get() == current_plan) {
    return GetCurrentPlan().get();
  }

  // Otherwise look for it in the regular plans.
  stack_size = m_plans.size();
  for (int i = stack_size - 1; i > 0; i--) {
    if (current_plan == m_plans[i].get())
      return m_plans[i - 1].get();
  }
  return nullptr;
}

ThreadPlan *ThreadPlanStack::GetInnermostExpression() const {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  int stack_size = m_plans.size();

  for (int i = stack_size - 1; i > 0; i--) {
    if (m_plans[i]->GetKind() == ThreadPlan::eKindCallFunction)
      return m_plans[i].get();
  }
  return nullptr;
}

void ThreadPlanStack::ClearThreadCache() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  for (lldb::ThreadPlanSP thread_plan_sp : m_plans)
    thread_plan_sp->ClearThreadCache();
}

void ThreadPlanStack::WillResume() {
  std::lock_guard<std::recursive_mutex> guard(m_stack_mutex);
  m_completed_plans.clear();
  m_discarded_plans.clear();
}

void ThreadPlanStackMap::Update(ThreadList &current_threads,
                                bool delete_missing,
                                bool check_for_new) {

  std::lock_guard<std::recursive_mutex> guard(m_stack_map_mutex);
  // Now find all the new threads and add them to the map:
  if (check_for_new) {
    for (auto thread : current_threads.Threads()) {
      lldb::tid_t cur_tid = thread->GetID();
      if (!Find(cur_tid)) {
        AddThread(*thread);
        thread->QueueBasePlan(true);
      }
    }
  }

  // If we aren't reaping missing threads at this point,
  // we are done.
  if (!delete_missing)
    return;
  // Otherwise scan for absent TID's.
  std::vector<lldb::tid_t> missing_threads;
  // If we are going to delete plans from the plan stack,
  // then scan for absent TID's:
  for (auto &thread_plans : m_plans_list) {
    lldb::tid_t cur_tid = thread_plans.first;
    ThreadSP thread_sp = current_threads.FindThreadByID(cur_tid);
    if (!thread_sp)
      missing_threads.push_back(cur_tid);
  }
  for (lldb::tid_t tid : missing_threads) {
    RemoveTID(tid);
  }
}

void ThreadPlanStackMap::DumpPlans(Stream &strm,
                                   lldb::DescriptionLevel desc_level,
                                   bool internal, bool condense_if_trivial,
                                   bool skip_unreported) {
  std::lock_guard<std::recursive_mutex> guard(m_stack_map_mutex);
  for (auto &elem : m_plans_list) {
    lldb::tid_t tid = elem.first;
    uint32_t index_id = 0;
    ThreadSP thread_sp = m_process.GetThreadList().FindThreadByID(tid);

    if (skip_unreported) {
      if (!thread_sp)
        continue;
    }
    if (thread_sp)
      index_id = thread_sp->GetIndexID();

    if (condense_if_trivial) {
      if (!elem.second.AnyPlans() && !elem.second.AnyCompletedPlans() &&
          !elem.second.AnyDiscardedPlans()) {
        strm.Printf("thread #%u: tid = 0x%4.4" PRIx64 "\n", index_id, tid);
        strm.IndentMore();
        strm.Indent();
        strm.Printf("No active thread plans\n");
        strm.IndentLess();
        return;
      }
    }

    strm.Indent();
    strm.Printf("thread #%u: tid = 0x%4.4" PRIx64 ":\n", index_id, tid);

    elem.second.DumpThreadPlans(strm, desc_level, internal);
  }
}

bool ThreadPlanStackMap::DumpPlansForTID(Stream &strm, lldb::tid_t tid,
                                         lldb::DescriptionLevel desc_level,
                                         bool internal,
                                         bool condense_if_trivial,
                                         bool skip_unreported) {
  std::lock_guard<std::recursive_mutex> guard(m_stack_map_mutex);
  uint32_t index_id = 0;
  ThreadSP thread_sp = m_process.GetThreadList().FindThreadByID(tid);

  if (skip_unreported) {
    if (!thread_sp) {
      strm.Format("Unknown TID: {0}", tid);
      return false;
    }
  }

  if (thread_sp)
    index_id = thread_sp->GetIndexID();
  ThreadPlanStack *stack = Find(tid);
  if (!stack) {
    strm.Format("Unknown TID: {0}\n", tid);
    return false;
  }

  if (condense_if_trivial) {
    if (!stack->AnyPlans() && !stack->AnyCompletedPlans() &&
        !stack->AnyDiscardedPlans()) {
      strm.Printf("thread #%u: tid = 0x%4.4" PRIx64 "\n", index_id, tid);
      strm.IndentMore();
      strm.Indent();
      strm.Printf("No active thread plans\n");
      strm.IndentLess();
      return true;
    }
  }

  strm.Indent();
  strm.Printf("thread #%u: tid = 0x%4.4" PRIx64 ":\n", index_id, tid);

  stack->DumpThreadPlans(strm, desc_level, internal);
  return true;
}

bool ThreadPlanStackMap::PrunePlansForTID(lldb::tid_t tid) {
  // We only remove the plans for unreported TID's.
  std::lock_guard<std::recursive_mutex> guard(m_stack_map_mutex);
  ThreadSP thread_sp = m_process.GetThreadList().FindThreadByID(tid);
  if (thread_sp)
    return false;

  return RemoveTID(tid);
}
