//===-- SBThread.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTHREAD_H
#define LLDB_API_SBTHREAD_H

#include "lldb/API/SBDefines.h"

#include <cstdio>

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class SBFrame;

class LLDB_API SBThread {
public:
  enum {
    eBroadcastBitStackChanged = (1 << 0),
    eBroadcastBitThreadSuspended = (1 << 1),
    eBroadcastBitThreadResumed = (1 << 2),
    eBroadcastBitSelectedFrameChanged = (1 << 3),
    eBroadcastBitThreadSelected = (1 << 4)
  };

  static const char *GetBroadcasterClassName();

  SBThread();

  SBThread(const lldb::SBThread &thread);

  ~SBThread();

  lldb::SBQueue GetQueue() const;

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

  bool GetStopReasonExtendedInfoAsJSON(lldb::SBStream &stream);

  SBThreadCollection
  GetStopReasonExtendedBacktraces(InstrumentationRuntimeType type);

  size_t GetStopDescription(char *dst_or_null, size_t dst_len);

  SBValue GetStopReturnValue();

  lldb::tid_t GetThreadID() const;

  uint32_t GetIndexID() const;

  const char *GetName() const;

  const char *GetQueueName() const;

  lldb::queue_id_t GetQueueID() const;

  bool GetInfoItemByPathAsString(const char *path, SBStream &strm);

  void StepOver(lldb::RunMode stop_other_threads = lldb::eOnlyDuringStepping);

  void StepOver(lldb::RunMode stop_other_threads, SBError &error);

  void StepInto(lldb::RunMode stop_other_threads = lldb::eOnlyDuringStepping);

  void StepInto(const char *target_name,
                lldb::RunMode stop_other_threads = lldb::eOnlyDuringStepping);

  void StepInto(const char *target_name, uint32_t end_line, SBError &error,
                lldb::RunMode stop_other_threads = lldb::eOnlyDuringStepping);

  void StepOut();

  void StepOut(SBError &error);

  void StepOutOfFrame(SBFrame &frame);

  void StepOutOfFrame(SBFrame &frame, SBError &error);

  void StepInstruction(bool step_over);

  void StepInstruction(bool step_over, SBError &error);

  SBError StepOverUntil(lldb::SBFrame &frame, lldb::SBFileSpec &file_spec,
                        uint32_t line);

  SBError StepUsingScriptedThreadPlan(const char *script_class_name);

  SBError StepUsingScriptedThreadPlan(const char *script_class_name,
                                      bool resume_immediately);

  SBError StepUsingScriptedThreadPlan(const char *script_class_name,
                                      lldb::SBStructuredData &args_data,
                                      bool resume_immediately);

  SBError JumpToLine(lldb::SBFileSpec &file_spec, uint32_t line);

  void RunToAddress(lldb::addr_t addr);

  void RunToAddress(lldb::addr_t addr, SBError &error);

  SBError ReturnFromFrame(SBFrame &frame, SBValue &return_value);

  SBError UnwindInnermostExpression();

  /// LLDB currently supports process centric debugging which means when any
  /// thread in a process stops, all other threads are stopped. The Suspend()
  /// call here tells our process to suspend a thread and not let it run when
  /// the other threads in a process are allowed to run. So when
  /// SBProcess::Continue() is called, any threads that aren't suspended will
  /// be allowed to run. If any of the SBThread functions for stepping are
  /// called (StepOver, StepInto, StepOut, StepInstruction, RunToAddress), the
  /// thread will not be allowed to run and these functions will simply return.
  ///
  /// Eventually we plan to add support for thread centric debugging where
  /// each thread is controlled individually and each thread would broadcast
  /// its state, but we haven't implemented this yet.
  ///
  /// Likewise the SBThread::Resume() call will again allow the thread to run
  /// when the process is continued.
  ///
  /// Suspend() and Resume() functions are not currently reference counted, if
  /// anyone has the need for them to be reference counted, please let us
  /// know.
  bool Suspend();

  bool Suspend(SBError &error);

  bool Resume();

  bool Resume(SBError &error);

  bool IsSuspended();

  bool IsStopped();

  uint32_t GetNumFrames();

  lldb::SBFrame GetFrameAtIndex(uint32_t idx);

  lldb::SBFrame GetSelectedFrame();

  lldb::SBFrame SetSelectedFrame(uint32_t frame_idx);

  static bool EventIsThreadEvent(const SBEvent &event);

  static SBFrame GetStackFrameFromEvent(const SBEvent &event);

  static SBThread GetThreadFromEvent(const SBEvent &event);

  lldb::SBProcess GetProcess();

  const lldb::SBThread &operator=(const lldb::SBThread &rhs);

  bool operator==(const lldb::SBThread &rhs) const;

  bool operator!=(const lldb::SBThread &rhs) const;

  bool GetDescription(lldb::SBStream &description) const;

  bool GetDescription(lldb::SBStream &description, bool stop_format) const;

  /// Similar to \a GetDescription() but the format of the description can be
  /// configured via the \p format parameter. See
  /// https://lldb.llvm.org/use/formatting.html for more information on format
  /// strings.
  ///
  /// \param[in] format
  ///   The format to use for generating the description.
  ///
  /// \param[out] output
  ///   The stream where the description will be written to.
  ///
  /// \return
  ///   An error object with an error message in case of failures.
  SBError GetDescriptionWithFormat(const SBFormat &format, SBStream &output);

  bool GetStatus(lldb::SBStream &status) const;

  SBThread GetExtendedBacktraceThread(const char *type);

  uint32_t GetExtendedBacktraceOriginatingIndexID();

  SBValue GetCurrentException();

  SBThread GetCurrentExceptionBacktrace();

  bool SafeToCallFunctions();

  SBValue GetSiginfo();

private:
  friend class SBBreakpoint;
  friend class SBBreakpointLocation;
  friend class SBBreakpointCallbackBaton;
  friend class SBExecutionContext;
  friend class SBFrame;
  friend class SBProcess;
  friend class SBDebugger;
  friend class SBValue;
  friend class lldb_private::QueueImpl;
  friend class SBQueueItem;
  friend class SBThreadCollection;
  friend class SBThreadPlan;
  friend class SBTrace;

  friend class lldb_private::python::SWIGBridge;

  SBThread(const lldb::ThreadSP &lldb_object_sp);

  void SetThread(const lldb::ThreadSP &lldb_object_sp);

  SBError ResumeNewPlan(lldb_private::ExecutionContext &exe_ctx,
                        lldb_private::ThreadPlan *new_plan);

  lldb::ExecutionContextRefSP m_opaque_sp;

  lldb_private::Thread *operator->();

  lldb_private::Thread *get();
};

} // namespace lldb

#endif // LLDB_API_SBTHREAD_H
