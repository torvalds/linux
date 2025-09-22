//===-- ThreadPlanBase.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANBASE_H
#define LLDB_TARGET_THREADPLANBASE_H

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"

namespace lldb_private {

//  Base thread plans:
//  This is the generic version of the bottom most plan on the plan stack.  It
//  should
//  be able to handle generic breakpoint hitting, and signals and exceptions.

class ThreadPlanBase : public ThreadPlan {
  friend class Process; // RunThreadPlan manages "stopper" base plans.
public:
  ~ThreadPlanBase() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ValidatePlan(Stream *error) override;
  bool ShouldStop(Event *event_ptr) override;
  Vote ShouldReportStop(Event *event_ptr) override;
  bool StopOthers() override;
  lldb::StateType GetPlanRunState() override;
  bool WillStop() override;
  bool MischiefManaged() override;

  bool OkayToDiscard() override { return false; }

  bool IsBasePlan() override { return true; }

protected:
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;
  bool DoPlanExplainsStop(Event *event_ptr) override;
  ThreadPlanBase(Thread &thread);

private:
  friend lldb::ThreadPlanSP Thread::QueueBasePlan(bool abort_other_plans);

  ThreadPlanBase(const ThreadPlanBase &) = delete;
  const ThreadPlanBase &operator=(const ThreadPlanBase &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANBASE_H
