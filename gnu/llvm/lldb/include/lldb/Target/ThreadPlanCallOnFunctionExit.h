//===-- ThreadPlanCallOnFunctionExit.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANCALLONFUNCTIONEXIT_H
#define LLDB_TARGET_THREADPLANCALLONFUNCTIONEXIT_H

#include "lldb/Target/ThreadPlan.h"

#include <functional>

namespace lldb_private {

// =============================================================================
/// This thread plan calls a function object when the current function exits.
// =============================================================================

class ThreadPlanCallOnFunctionExit : public ThreadPlan {
public:
  /// Definition for the callback made when the currently executing thread
  /// finishes executing its function.
  using Callback = std::function<void()>;

  ThreadPlanCallOnFunctionExit(Thread &thread, const Callback &callback);

  void DidPush() override;

  // ThreadPlan API

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  bool ShouldStop(Event *event_ptr) override;

  bool WillStop() override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

  lldb::StateType GetPlanRunState() override;

private:
  Callback m_callback;
  lldb::ThreadPlanSP m_step_out_threadplan_sp;
};
}

#endif // LLDB_TARGET_THREADPLANCALLONFUNCTIONEXIT_H
