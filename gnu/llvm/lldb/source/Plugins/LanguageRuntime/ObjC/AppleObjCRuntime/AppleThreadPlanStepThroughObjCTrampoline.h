//===-- AppleThreadPlanStepThroughObjCTrampoline.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLETHREADPLANSTEPTHROUGHOBJCTRAMPOLINE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLETHREADPLANSTEPTHROUGHOBJCTRAMPOLINE_H

#include "AppleObjCTrampolineHandler.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanShouldStopHere.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class AppleThreadPlanStepThroughObjCTrampoline : public ThreadPlan {
public:
  AppleThreadPlanStepThroughObjCTrampoline(
      Thread &thread, AppleObjCTrampolineHandler &trampoline_handler,
      ValueList &values, lldb::addr_t isa_addr, lldb::addr_t sel_addr,
      lldb::addr_t sel_str_addr, llvm::StringRef sel_str);

  ~AppleThreadPlanStepThroughObjCTrampoline() override;

  static bool PreResumeInitializeFunctionCaller(void *myself);

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  lldb::StateType GetPlanRunState() override;

  bool ShouldStop(Event *event_ptr) override;

  // The step through code might have to fill in the cache, so it is not safe
  // to run only one thread.
  bool StopOthers() override { return false; }

  // The base class MischiefManaged does some cleanup - so you have to call it
  // in your MischiefManaged derived class.
  bool MischiefManaged() override;

  void DidPush() override;

  bool WillStop() override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

private:
  bool InitializeFunctionCaller();

  AppleObjCTrampolineHandler &m_trampoline_handler; /// The handler itself.
  lldb::addr_t m_args_addr; /// Stores the address for our step through function
                            /// result structure.
  ValueList m_input_values;
  lldb::addr_t m_isa_addr; /// isa_addr and sel_addr are the keys we will use to
                           /// cache the implementation.
  lldb::addr_t m_sel_addr;
  lldb::ThreadPlanSP m_func_sp; /// This is the function call plan.  We fill it
                                /// at start, then set it to NULL when this plan
                                /// is done.  That way we know to go on to:
  lldb::ThreadPlanSP m_run_to_sp;  /// The plan that runs to the target.
  FunctionCaller *m_impl_function; /// This is a pointer to a impl function that
                                   /// is owned by the client that pushes this
                                   /// plan.
  lldb::addr_t m_sel_str_addr; /// If this is not LLDB_INVALID_ADDRESS then it
                               /// is the address we wrote the selector string
                               /// to.  We need to deallocate it when the
                               /// function call is done.
  std::string m_sel_str;       /// This is the string we wrote to memory - we
                               /// use it for caching, but only if
                               /// m_sel_str_addr is non-null.
};

class AppleThreadPlanStepThroughDirectDispatch: public ThreadPlanStepOut {
public:
  AppleThreadPlanStepThroughDirectDispatch(Thread &thread,
                                           AppleObjCTrampolineHandler &handler,
                                           llvm::StringRef dispatch_func_name);

  ~AppleThreadPlanStepThroughDirectDispatch() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ShouldStop(Event *event_ptr) override;

  bool StopOthers() override { return false; }

  bool MischiefManaged() override;

  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;

  void SetFlagsToDefault() override {
          GetFlags().Set(ThreadPlanStepInRange::GetDefaultFlagsValue());
  }

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

  AppleObjCTrampolineHandler &m_trampoline_handler;
  std::string m_dispatch_func_name;  /// Which dispatch function we're stepping
                                     /// through.
  lldb::ThreadPlanSP m_objc_step_through_sp; /// When we hit an objc_msgSend,
                                             /// we'll use this plan to get to
                                             /// its target.
  std::vector<lldb::BreakpointSP> m_msgSend_bkpts; /// Breakpoints on the objc
                                                   /// dispatch functions.
  bool m_at_msg_send;  /// Are we currently handling an msg_send

};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLETHREADPLANSTEPTHROUGHOBJCTRAMPOLINE_H
