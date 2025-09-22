//===-- ThreadPlanStepInRange.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANSTEPINRANGE_H
#define LLDB_TARGET_THREADPLANSTEPINRANGE_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanShouldStopHere.h"
#include "lldb/Target/ThreadPlanStepRange.h"

namespace lldb_private {

class ThreadPlanStepInRange : public ThreadPlanStepRange,
                              public ThreadPlanShouldStopHere {
public:
  ThreadPlanStepInRange(Thread &thread, const AddressRange &range,
                        const SymbolContext &addr_context,
                        const char *step_into_target, lldb::RunMode stop_others,
                        LazyBool step_in_avoids_code_without_debug_info,
                        LazyBool step_out_avoids_code_without_debug_info);

  ~ThreadPlanStepInRange() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ShouldStop(Event *event_ptr) override;

  void SetAvoidRegexp(const char *name);

  static void SetDefaultFlagValue(uint32_t new_value);

  bool IsVirtualStep() override;

  // Plans that are implementing parts of a step in might need to follow the
  // behavior of this plan w.r.t. StepThrough.  They can get that from here.
  static uint32_t GetDefaultFlagsValue() {
    return s_default_flag_values;
  }

protected:
  static bool DefaultShouldStopHereCallback(ThreadPlan *current_plan,
                                            Flags &flags,
                                            lldb::FrameComparison operation,
                                            Status &status, void *baton);

  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;

  bool DoPlanExplainsStop(Event *event_ptr) override;

  void SetFlagsToDefault() override {
    GetFlags().Set(ThreadPlanStepInRange::s_default_flag_values);
  }

  void SetCallbacks() {
    ThreadPlanShouldStopHere::ThreadPlanShouldStopHereCallbacks callbacks(
        ThreadPlanStepInRange::DefaultShouldStopHereCallback, nullptr);
    SetShouldStopHereCallbacks(&callbacks, nullptr);
  }

  bool FrameMatchesAvoidCriteria();

private:
  void SetupAvoidNoDebug(LazyBool step_in_avoids_code_without_debug_info,
                         LazyBool step_out_avoids_code_without_debug_info);
  // Need an appropriate marker for the current stack so we can tell step out
  // from step in.

  static uint32_t s_default_flag_values; // These are the default flag values
                                         // for the ThreadPlanStepThrough.
  lldb::ThreadPlanSP m_sub_plan_sp;      // Keep track of the last plan we were
                                    // running.  If it fails, we should stop.
  std::unique_ptr<RegularExpression> m_avoid_regexp_up;
  bool m_step_past_prologue; // FIXME: For now hard-coded to true, we could put
                             // a switch in for this if there's
                             // demand for that.
  bool m_virtual_step; // true if we've just done a "virtual step", i.e. just
                       // moved the inline stack depth.
  ConstString m_step_into_target;
  ThreadPlanStepInRange(const ThreadPlanStepInRange &) = delete;
  const ThreadPlanStepInRange &
  operator=(const ThreadPlanStepInRange &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANSTEPINRANGE_H
