//===-- ThreadPlanStepOverRange.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANSTEPOVERRANGE_H
#define LLDB_TARGET_THREADPLANSTEPOVERRANGE_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanStepRange.h"

namespace lldb_private {

class ThreadPlanStepOverRange : public ThreadPlanStepRange,
                                ThreadPlanShouldStopHere {
public:
  ThreadPlanStepOverRange(Thread &thread, const AddressRange &range,
                          const SymbolContext &addr_context,
                          lldb::RunMode stop_others,
                          LazyBool step_out_avoids_no_debug);

  ~ThreadPlanStepOverRange() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ShouldStop(Event *event_ptr) override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;

  void SetFlagsToDefault() override {
    GetFlags().Set(ThreadPlanStepOverRange::s_default_flag_values);
  }

private:
  static uint32_t s_default_flag_values;

  void SetupAvoidNoDebug(LazyBool step_out_avoids_code_without_debug_info);
  bool IsEquivalentContext(const SymbolContext &context);

  bool m_first_resume;

  ThreadPlanStepOverRange(const ThreadPlanStepOverRange &) = delete;
  const ThreadPlanStepOverRange &
  operator=(const ThreadPlanStepOverRange &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANSTEPOVERRANGE_H
