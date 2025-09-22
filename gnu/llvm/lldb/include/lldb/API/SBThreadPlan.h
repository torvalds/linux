//===-- SBThreadPlan.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTHREADPLAN_H
#define LLDB_API_SBTHREADPLAN_H

#include "lldb/API/SBDefines.h"

#include <cstdio>

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBThreadPlan {

public:
  SBThreadPlan();

  SBThreadPlan(const lldb::SBThreadPlan &threadPlan);

  SBThreadPlan(lldb::SBThread &thread, const char *class_name);

  SBThreadPlan(lldb::SBThread &thread, const char *class_name, 
               lldb::SBStructuredData &args_data);

  ~SBThreadPlan();

  explicit operator bool() const;

  bool IsValid() const;

  void Clear();

  lldb::StopReason GetStopReason();

  /// Get the number of words associated with the stop reason.
  /// See also GetStopReasonDataAtIndex().
  size_t GetStopReasonDataCount();

  /// Get information associated with a stop reason.
  ///
  /// Breakpoint stop reasons will have data that consists of pairs of
  /// breakpoint IDs followed by the breakpoint location IDs (they always come
  /// in pairs).
  ///
  /// Stop Reason              Count Data Type
  /// ======================== ===== =========================================
  /// eStopReasonNone          0
  /// eStopReasonTrace         0
  /// eStopReasonBreakpoint    N     duple: {breakpoint id, location id}
  /// eStopReasonWatchpoint    1     watchpoint id
  /// eStopReasonSignal        1     unix signal number
  /// eStopReasonException     N     exception data
  /// eStopReasonExec          0
  /// eStopReasonFork          1     pid of the child process
  /// eStopReasonVFork         1     pid of the child process
  /// eStopReasonVForkDone     0
  /// eStopReasonPlanComplete  0
  uint64_t GetStopReasonDataAtIndex(uint32_t idx);

  SBThread GetThread() const;

  const lldb::SBThreadPlan &operator=(const lldb::SBThreadPlan &rhs);

  bool GetDescription(lldb::SBStream &description) const;

  void SetPlanComplete(bool success);

  bool IsPlanComplete();

  bool IsPlanStale();

  bool IsValid();

  bool GetStopOthers();

  void SetStopOthers(bool stop_others);

  // This section allows an SBThreadPlan to push another of the common types of
  // plans...
  SBThreadPlan QueueThreadPlanForStepOverRange(SBAddress &start_address,
                                               lldb::addr_t range_size);
  SBThreadPlan QueueThreadPlanForStepOverRange(SBAddress &start_address,
                                               lldb::addr_t range_size,
                                               SBError &error);

  SBThreadPlan QueueThreadPlanForStepInRange(SBAddress &start_address,
                                             lldb::addr_t range_size);
  SBThreadPlan QueueThreadPlanForStepInRange(SBAddress &start_address,
                                             lldb::addr_t range_size,
                                             SBError &error);

  SBThreadPlan QueueThreadPlanForStepOut(uint32_t frame_idx_to_step_to,
                                         bool first_insn = false);
  SBThreadPlan QueueThreadPlanForStepOut(uint32_t frame_idx_to_step_to,
                                         bool first_insn, SBError &error);

  SBThreadPlan QueueThreadPlanForRunToAddress(SBAddress address);
  SBThreadPlan QueueThreadPlanForRunToAddress(SBAddress address,
                                              SBError &error);

  SBThreadPlan QueueThreadPlanForStepScripted(const char *script_class_name);
  SBThreadPlan QueueThreadPlanForStepScripted(const char *script_class_name,
                                              SBError &error);
  SBThreadPlan QueueThreadPlanForStepScripted(const char *script_class_name,
                                              lldb::SBStructuredData &args_data,
                                              SBError &error);

protected:
  friend class lldb_private::python::SWIGBridge;

  SBThreadPlan(const lldb::ThreadPlanSP &lldb_object_sp);

private:
  friend class SBBreakpoint;
  friend class SBBreakpointLocation;
  friend class SBFrame;
  friend class SBProcess;
  friend class SBDebugger;
  friend class SBValue;
  friend class lldb_private::QueueImpl;
  friend class SBQueueItem;

  lldb::ThreadPlanSP GetSP() const { return m_opaque_wp.lock(); }
  lldb_private::ThreadPlan *get() const { return GetSP().get(); }
  void SetThreadPlan(const lldb::ThreadPlanSP &lldb_object_sp);

  lldb::ThreadPlanWP m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_API_SBTHREADPLAN_H
