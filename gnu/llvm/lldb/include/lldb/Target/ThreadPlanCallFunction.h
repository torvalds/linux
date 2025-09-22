//===-- ThreadPlanCallFunction.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANCALLFUNCTION_H
#define LLDB_TARGET_THREADPLANCALLFUNCTION_H

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"

namespace lldb_private {

class ThreadPlanCallFunction : public ThreadPlan {
  // Create a thread plan to call a function at the address passed in the
  // "function" argument.  If you plan to call GetReturnValueObject, then pass
  // in the return type, otherwise just pass in an invalid CompilerType.
public:
  ThreadPlanCallFunction(Thread &thread, const Address &function,
                         const CompilerType &return_type,
                         llvm::ArrayRef<lldb::addr_t> args,
                         const EvaluateExpressionOptions &options);

  ThreadPlanCallFunction(Thread &thread, const Address &function,
                         const EvaluateExpressionOptions &options);

  ~ThreadPlanCallFunction() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  bool ShouldStop(Event *event_ptr) override;

  Vote ShouldReportStop(Event *event_ptr) override;

  bool StopOthers() override;

  lldb::StateType GetPlanRunState() override;

  void DidPush() override;

  bool WillStop() override;

  bool MischiefManaged() override;

  // To get the return value from a function call you must create a
  // lldb::ValueSP that contains a valid clang type in its context and call
  // RequestReturnValue. The ValueSP will be stored and when the function is
  // done executing, the object will check if there is a requested return
  // value. If there is, the return value will be retrieved using the
  // ABI::GetReturnValue() for the ABI in the process. Then after the thread
  // plan is complete, you can call "GetReturnValue()" to retrieve the value
  // that was extracted.

  lldb::ValueObjectSP GetReturnValueObject() override {
    return m_return_valobj_sp;
  }

  // Return the stack pointer that the function received on entry.  Any stack
  // address below this should be considered invalid after the function has
  // been cleaned up.
  lldb::addr_t GetFunctionStackPointer() { return m_function_sp; }

  // Classes that derive from FunctionCaller, and implement their own DidPop
  // methods should call this so that the thread state gets restored if the
  // plan gets discarded.
  void DidPop() override;

  // If the thread plan stops mid-course, this will be the stop reason that
  // interrupted us. Once DoTakedown is called, this will be the real stop
  // reason at the end of the function call. If it hasn't been set for one or
  // the other of these reasons, we'll return the PrivateStopReason. This is
  // needed because we want the CallFunction thread plans not to show up as the
  // stop reason. But if something bad goes wrong, it is nice to be able to
  // tell the user what really happened.

  virtual lldb::StopInfoSP GetRealStopInfo() {
    if (m_real_stop_info_sp)
      return m_real_stop_info_sp;
    else
      return GetPrivateStopInfo();
  }

  lldb::addr_t GetStopAddress() { return m_stop_address; }

  void RestoreThreadState() override;

  void ThreadDestroyed() override { m_takedown_done = true; }

  void SetStopOthers(bool new_value) override;

protected:
  void ReportRegisterState(const char *message);

  bool DoPlanExplainsStop(Event *event_ptr) override;

  virtual void SetReturnValue();

  bool ConstructorSetup(Thread &thread, ABI *&abi,
                        lldb::addr_t &start_load_addr,
                        lldb::addr_t &function_load_addr);

  virtual void DoTakedown(bool success);

  void SetBreakpoints();

  void ClearBreakpoints();

  bool BreakpointsExplainStop();

  bool m_valid;
  bool m_stop_other_threads;
  bool m_unwind_on_error;
  bool m_ignore_breakpoints;
  bool m_debug_execution;
  bool m_trap_exceptions;
  Address m_function_addr;
  Address m_start_addr;
  lldb::addr_t m_function_sp;
  lldb::ThreadPlanSP m_subplan_sp;
  LanguageRuntime *m_cxx_language_runtime;
  LanguageRuntime *m_objc_language_runtime;
  Thread::ThreadStateCheckpoint m_stored_thread_state;
  lldb::StopInfoSP
      m_real_stop_info_sp; // In general we want to hide call function
                           // thread plans, but for reporting purposes, it's
                           // nice to know the real stop reason. This gets set
                           // in DoTakedown.
  StreamString m_constructor_errors;
  lldb::ValueObjectSP m_return_valobj_sp; // If this contains a valid pointer,
                                          // use the ABI to extract values when
                                          // complete
  bool m_takedown_done; // We want to ensure we only do the takedown once.  This
                        // ensures that.
  bool m_should_clear_objc_exception_bp;
  bool m_should_clear_cxx_exception_bp;
  lldb::addr_t m_stop_address; // This is the address we stopped at.  Also set
                               // in DoTakedown;

private:
  CompilerType m_return_type;
  ThreadPlanCallFunction(const ThreadPlanCallFunction &) = delete;
  const ThreadPlanCallFunction &
  operator=(const ThreadPlanCallFunction &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANCALLFUNCTION_H
