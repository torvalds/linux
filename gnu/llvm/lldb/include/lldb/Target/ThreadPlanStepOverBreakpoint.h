//===-- ThreadPlanStepOverBreakpoint.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANSTEPOVERBREAKPOINT_H
#define LLDB_TARGET_THREADPLANSTEPOVERBREAKPOINT_H

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"

namespace lldb_private {

class ThreadPlanStepOverBreakpoint : public ThreadPlan {
public:
  ThreadPlanStepOverBreakpoint(Thread &thread);

  ~ThreadPlanStepOverBreakpoint() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ValidatePlan(Stream *error) override;
  bool ShouldStop(Event *event_ptr) override;
  bool StopOthers() override;
  lldb::StateType GetPlanRunState() override;
  bool WillStop() override;
  void DidPop() override;
  bool MischiefManaged() override;
  void ThreadDestroyed() override;
  void SetAutoContinue(bool do_it);
  bool ShouldAutoContinue(Event *event_ptr) override;
  bool IsPlanStale() override;

  lldb::addr_t GetBreakpointLoadAddress() const { return m_breakpoint_addr; }

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;

  void ReenableBreakpointSite();

private:
  lldb::addr_t m_breakpoint_addr;
  lldb::user_id_t m_breakpoint_site_id;
  bool m_auto_continue;
  bool m_reenabled_breakpoint_site;

  ThreadPlanStepOverBreakpoint(const ThreadPlanStepOverBreakpoint &) = delete;
  const ThreadPlanStepOverBreakpoint &
  operator=(const ThreadPlanStepOverBreakpoint &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANSTEPOVERBREAKPOINT_H
