//===-- Thread.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREAD_H
#define LLDB_TARGET_THREAD_H

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lldb/Core/UserSettingsController.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/RegisterCheckpoint.h"
#include "lldb/Target/StackFrameList.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/UnimplementedError.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"
#include "llvm/Support/MemoryBuffer.h"

#define LLDB_THREAD_MAX_STOP_EXC_DATA 8

namespace lldb_private {

class ThreadPlanStack;

class ThreadProperties : public Properties {
public:
  ThreadProperties(bool is_global);

  ~ThreadProperties() override;

  /// The regular expression returned determines symbols that this
  /// thread won't stop in during "step-in" operations.
  ///
  /// \return
  ///    A pointer to a regular expression to compare against symbols,
  ///    or nullptr if all symbols are allowed.
  ///
  const RegularExpression *GetSymbolsToAvoidRegexp();

  FileSpecList GetLibrariesToAvoid() const;

  bool GetTraceEnabledState() const;

  bool GetStepInAvoidsNoDebug() const;

  bool GetStepOutAvoidsNoDebug() const;

  uint64_t GetMaxBacktraceDepth() const;
};

class Thread : public std::enable_shared_from_this<Thread>,
               public ThreadProperties,
               public UserID,
               public ExecutionContextScope,
               public Broadcaster {
public:
  /// Broadcaster event bits definitions.
  enum {
    eBroadcastBitStackChanged = (1 << 0),
    eBroadcastBitThreadSuspended = (1 << 1),
    eBroadcastBitThreadResumed = (1 << 2),
    eBroadcastBitSelectedFrameChanged = (1 << 3),
    eBroadcastBitThreadSelected = (1 << 4)
  };

  static llvm::StringRef GetStaticBroadcasterClass();

  llvm::StringRef GetBroadcasterClass() const override {
    return GetStaticBroadcasterClass();
  }

  class ThreadEventData : public EventData {
  public:
    ThreadEventData(const lldb::ThreadSP thread_sp);

    ThreadEventData(const lldb::ThreadSP thread_sp, const StackID &stack_id);

    ThreadEventData();

    ~ThreadEventData() override;

    static llvm::StringRef GetFlavorString();

    llvm::StringRef GetFlavor() const override {
      return ThreadEventData::GetFlavorString();
    }

    void Dump(Stream *s) const override;

    static const ThreadEventData *GetEventDataFromEvent(const Event *event_ptr);

    static lldb::ThreadSP GetThreadFromEvent(const Event *event_ptr);

    static StackID GetStackIDFromEvent(const Event *event_ptr);

    static lldb::StackFrameSP GetStackFrameFromEvent(const Event *event_ptr);

    lldb::ThreadSP GetThread() const { return m_thread_sp; }

    StackID GetStackID() const { return m_stack_id; }

  private:
    lldb::ThreadSP m_thread_sp;
    StackID m_stack_id;

    ThreadEventData(const ThreadEventData &) = delete;
    const ThreadEventData &operator=(const ThreadEventData &) = delete;
  };

  struct ThreadStateCheckpoint {
    uint32_t orig_stop_id; // Dunno if I need this yet but it is an interesting
                           // bit of data.
    lldb::StopInfoSP stop_info_sp; // You have to restore the stop info or you
                                   // might continue with the wrong signals.
    size_t m_completed_plan_checkpoint;
    lldb::RegisterCheckpointSP
        register_backup_sp; // You need to restore the registers, of course...
    uint32_t current_inlined_depth;
    lldb::addr_t current_inlined_pc;
  };

  /// Constructor
  ///
  /// \param [in] use_invalid_index_id
  ///     Optional parameter, defaults to false.  The only subclass that
  ///     is likely to set use_invalid_index_id == true is the HistoryThread
  ///     class.  In that case, the Thread we are constructing represents
  ///     a thread from earlier in the program execution.  We may have the
  ///     tid of the original thread that they represent but we don't want
  ///     to reuse the IndexID of that thread, or create a new one.  If a
  ///     client wants to know the original thread's IndexID, they should use
  ///     Thread::GetExtendedBacktraceOriginatingIndexID().
  Thread(Process &process, lldb::tid_t tid, bool use_invalid_index_id = false);

  ~Thread() override;

  static void SettingsInitialize();

  static void SettingsTerminate();

  static ThreadProperties &GetGlobalProperties();

  lldb::ProcessSP GetProcess() const { return m_process_wp.lock(); }

  int GetResumeSignal() const { return m_resume_signal; }

  void SetResumeSignal(int signal) { m_resume_signal = signal; }

  lldb::StateType GetState() const;

  void SetState(lldb::StateType state);

  /// Sets the USER resume state for this thread.  If you set a thread to
  /// suspended with
  /// this API, it won't take part in any of the arbitration for ShouldResume,
  /// and will stay
  /// suspended even when other threads do get to run.
  ///
  /// N.B. This is not the state that is used internally by thread plans to
  /// implement
  /// staying on one thread while stepping over a breakpoint, etc.  The is the
  /// TemporaryResume state, and if you are implementing some bit of strategy in
  /// the stepping
  /// machinery you should be using that state and not the user resume state.
  ///
  /// If you are just preparing all threads to run, you should not override the
  /// threads that are
  /// marked as suspended by the debugger.  In that case, pass override_suspend
  /// = false.  If you want
  /// to force the thread to run (e.g. the "thread continue" command, or are
  /// resetting the state
  /// (e.g. in SBThread::Resume()), then pass true to override_suspend.
  void SetResumeState(lldb::StateType state, bool override_suspend = false) {
    if (m_resume_state == lldb::eStateSuspended && !override_suspend)
      return;
    m_resume_state = state;
  }

  /// Gets the USER resume state for this thread.  This is not the same as what
  /// this thread is going to do for any particular step, however if this thread
  /// returns eStateSuspended, then the process control logic will never allow
  /// this
  /// thread to run.
  ///
  /// \return
  ///    The User resume state for this thread.
  lldb::StateType GetResumeState() const { return m_resume_state; }

  // This function is called on all the threads before "ShouldResume" and
  // "WillResume" in case a thread needs to change its state before the
  // ThreadList polls all the threads to figure out which ones actually will
  // get to run and how.
  void SetupForResume();

  // Do not override this function, it is for thread plan logic only
  bool ShouldResume(lldb::StateType resume_state);

  // Override this to do platform specific tasks before resume.
  virtual void WillResume(lldb::StateType resume_state) {}

  // This clears generic thread state after a resume.  If you subclass this, be
  // sure to call it.
  virtual void DidResume();

  // This notifies the thread when a private stop occurs.
  virtual void DidStop();

  virtual void RefreshStateAfterStop() = 0;

  std::string GetStopDescription();

  std::string GetStopDescriptionRaw();

  void WillStop();

  bool ShouldStop(Event *event_ptr);

  Vote ShouldReportStop(Event *event_ptr);

  Vote ShouldReportRun(Event *event_ptr);

  void Flush();

  // Return whether this thread matches the specification in ThreadSpec.  This
  // is a virtual method because at some point we may extend the thread spec
  // with a platform specific dictionary of attributes, which then only the
  // platform specific Thread implementation would know how to match.  For now,
  // this just calls through to the ThreadSpec's ThreadPassesBasicTests method.
  virtual bool MatchesSpec(const ThreadSpec *spec);

  // Get the current public stop info, calculating it if necessary.
  lldb::StopInfoSP GetStopInfo();

  lldb::StopReason GetStopReason();

  bool StopInfoIsUpToDate() const;

  // This sets the stop reason to a "blank" stop reason, so you can call
  // functions on the thread without having the called function run with
  // whatever stop reason you stopped with.
  void SetStopInfoToNothing();

  bool ThreadStoppedForAReason();

  static std::string RunModeAsString(lldb::RunMode mode);

  static std::string StopReasonAsString(lldb::StopReason reason);

  virtual const char *GetInfo() { return nullptr; }

  /// Retrieve a dictionary of information about this thread
  ///
  /// On Mac OS X systems there may be voucher information.
  /// The top level dictionary returned will have an "activity" key and the
  /// value of the activity is a dictionary.  Keys in that dictionary will
  /// be "name" and "id", among others.
  /// There may also be "trace_messages" (an array) with each entry in that
  /// array
  /// being a dictionary (keys include "message" with the text of the trace
  /// message).
  StructuredData::ObjectSP GetExtendedInfo() {
    if (!m_extended_info_fetched) {
      m_extended_info = FetchThreadExtendedInfo();
      m_extended_info_fetched = true;
    }
    return m_extended_info;
  }

  virtual const char *GetName() { return nullptr; }

  virtual void SetName(const char *name) {}

  /// Whether this thread can be associated with a libdispatch queue
  ///
  /// The Thread may know if it is associated with a libdispatch queue,
  /// it may know definitively that it is NOT associated with a libdispatch
  /// queue, or it may be unknown whether it is associated with a libdispatch
  /// queue.
  ///
  /// \return
  ///     eLazyBoolNo if this thread is definitely not associated with a
  ///     libdispatch queue (e.g. on a non-Darwin system where GCD aka
  ///     libdispatch is not available).
  ///
  ///     eLazyBoolYes this thread is associated with a libdispatch queue.
  ///
  ///     eLazyBoolCalculate this thread may be associated with a libdispatch
  ///     queue but the thread doesn't know one way or the other.
  virtual lldb_private::LazyBool GetAssociatedWithLibdispatchQueue() {
    return eLazyBoolNo;
  }

  virtual void SetAssociatedWithLibdispatchQueue(
      lldb_private::LazyBool associated_with_libdispatch_queue) {}

  /// Retrieve the Queue ID for the queue currently using this Thread
  ///
  /// If this Thread is doing work on behalf of a libdispatch/GCD queue,
  /// retrieve the QueueID.
  ///
  /// This is a unique identifier for the libdispatch/GCD queue in a
  /// process.  Often starting at 1 for the initial system-created
  /// queues and incrementing, a QueueID will not be reused for a
  /// different queue during the lifetime of a process.
  ///
  /// \return
  ///     A QueueID if the Thread subclass implements this, else
  ///     LLDB_INVALID_QUEUE_ID.
  virtual lldb::queue_id_t GetQueueID() { return LLDB_INVALID_QUEUE_ID; }

  virtual void SetQueueID(lldb::queue_id_t new_val) {}

  /// Retrieve the Queue name for the queue currently using this Thread
  ///
  /// If this Thread is doing work on behalf of a libdispatch/GCD queue,
  /// retrieve the Queue name.
  ///
  /// \return
  ///     The Queue name, if the Thread subclass implements this, else
  ///     nullptr.
  virtual const char *GetQueueName() { return nullptr; }

  virtual void SetQueueName(const char *name) {}

  /// Retrieve the Queue kind for the queue currently using this Thread
  ///
  /// If this Thread is doing work on behalf of a libdispatch/GCD queue,
  /// retrieve the Queue kind - either eQueueKindSerial or
  /// eQueueKindConcurrent, indicating that this queue processes work
  /// items serially or concurrently.
  ///
  /// \return
  ///     The Queue kind, if the Thread subclass implements this, else
  ///     eQueueKindUnknown.
  virtual lldb::QueueKind GetQueueKind() { return lldb::eQueueKindUnknown; }

  virtual void SetQueueKind(lldb::QueueKind kind) {}

  /// Retrieve the Queue for this thread, if any.
  ///
  /// \return
  ///     A QueueSP for the queue that is currently associated with this
  ///     thread.
  ///     An empty shared pointer indicates that this thread is not
  ///     associated with a queue, or libdispatch queues are not
  ///     supported on this target.
  virtual lldb::QueueSP GetQueue() { return lldb::QueueSP(); }

  /// Retrieve the address of the libdispatch_queue_t struct for queue
  /// currently using this Thread
  ///
  /// If this Thread is doing work on behalf of a libdispatch/GCD queue,
  /// retrieve the address of the libdispatch_queue_t structure describing
  /// the queue.
  ///
  /// This address may be reused for different queues later in the Process
  /// lifetime and should not be used to identify a queue uniquely.  Use
  /// the GetQueueID() call for that.
  ///
  /// \return
  ///     The Queue's libdispatch_queue_t address if the Thread subclass
  ///     implements this, else LLDB_INVALID_ADDRESS.
  virtual lldb::addr_t GetQueueLibdispatchQueueAddress() {
    return LLDB_INVALID_ADDRESS;
  }

  virtual void SetQueueLibdispatchQueueAddress(lldb::addr_t dispatch_queue_t) {}

  /// Whether this Thread already has all the Queue information cached or not
  ///
  /// A Thread may be associated with a libdispatch work Queue at a given
  /// public stop event.  If so, the thread can satisify requests like
  /// GetQueueLibdispatchQueueAddress, GetQueueKind, GetQueueName, and
  /// GetQueueID
  /// either from information from the remote debug stub when it is initially
  /// created, or it can query the SystemRuntime for that information.
  ///
  /// This method allows the SystemRuntime to discover if a thread has this
  /// information already, instead of calling the thread to get the information
  /// and having the thread call the SystemRuntime again.
  virtual bool ThreadHasQueueInformation() const { return false; }

  /// GetStackFrameCount can be expensive.  Stacks can get very deep, and they
  /// require memory reads for each frame.  So only use GetStackFrameCount when 
  /// you need to know the depth of the stack.  When iterating over frames, its
  /// better to generate the frames one by one with GetFrameAtIndex, and when
  /// that returns NULL, you are at the end of the stack.  That way your loop
  /// will only do the work it needs to, without forcing lldb to realize
  /// StackFrames you weren't going to look at.
  virtual uint32_t GetStackFrameCount() {
    return GetStackFrameList()->GetNumFrames();
  }

  virtual lldb::StackFrameSP GetStackFrameAtIndex(uint32_t idx) {
    return GetStackFrameList()->GetFrameAtIndex(idx);
  }

  virtual lldb::StackFrameSP
  GetFrameWithConcreteFrameIndex(uint32_t unwind_idx);

  bool DecrementCurrentInlinedDepth() {
    return GetStackFrameList()->DecrementCurrentInlinedDepth();
  }

  uint32_t GetCurrentInlinedDepth() {
    return GetStackFrameList()->GetCurrentInlinedDepth();
  }

  Status ReturnFromFrameWithIndex(uint32_t frame_idx,
                                  lldb::ValueObjectSP return_value_sp,
                                  bool broadcast = false);

  Status ReturnFromFrame(lldb::StackFrameSP frame_sp,
                         lldb::ValueObjectSP return_value_sp,
                         bool broadcast = false);

  Status JumpToLine(const FileSpec &file, uint32_t line,
                    bool can_leave_function, std::string *warnings = nullptr);

  virtual lldb::StackFrameSP GetFrameWithStackID(const StackID &stack_id) {
    if (stack_id.IsValid())
      return GetStackFrameList()->GetFrameWithStackID(stack_id);
    return lldb::StackFrameSP();
  }

  // Only pass true to select_most_relevant if you are fulfilling an explicit
  // user request for GetSelectedFrameIndex.  The most relevant frame is only
  // for showing to the user, and can do arbitrary work, so we don't want to
  // call it internally.
  uint32_t GetSelectedFrameIndex(SelectMostRelevant select_most_relevant) {
    return GetStackFrameList()->GetSelectedFrameIndex(select_most_relevant);
  }

  lldb::StackFrameSP
  GetSelectedFrame(SelectMostRelevant select_most_relevant);

  uint32_t SetSelectedFrame(lldb_private::StackFrame *frame,
                            bool broadcast = false);

  bool SetSelectedFrameByIndex(uint32_t frame_idx, bool broadcast = false);

  bool SetSelectedFrameByIndexNoisily(uint32_t frame_idx,
                                      Stream &output_stream);

  void SetDefaultFileAndLineToSelectedFrame() {
    GetStackFrameList()->SetDefaultFileAndLineToSelectedFrame();
  }

  virtual lldb::RegisterContextSP GetRegisterContext() = 0;

  virtual lldb::RegisterContextSP
  CreateRegisterContextForFrame(StackFrame *frame) = 0;

  virtual void ClearStackFrames();

  virtual bool SetBackingThread(const lldb::ThreadSP &thread_sp) {
    return false;
  }

  virtual lldb::ThreadSP GetBackingThread() const { return lldb::ThreadSP(); }

  virtual void ClearBackingThread() {
    // Subclasses can use this function if a thread is actually backed by
    // another thread. This is currently used for the OperatingSystem plug-ins
    // where they might have a thread that is in memory, yet its registers are
    // available through the lldb_private::Thread subclass for the current
    // lldb_private::Process class. Since each time the process stops the
    // backing threads for memory threads can change, we need a way to clear
    // the backing thread for all memory threads each time we stop.
  }

  /// Dump \a count instructions of the thread's \a Trace starting at the \a
  /// start_position position in reverse order.
  ///
  /// The instructions are indexed in reverse order, which means that the \a
  /// start_position 0 represents the last instruction of the trace
  /// chronologically.
  ///
  /// \param[in] s
  ///   The stream object where the instructions are printed.
  ///
  /// \param[in] count
  ///     The number of instructions to print.
  ///
  /// \param[in] start_position
  ///     The position of the first instruction to print.
  void DumpTraceInstructions(Stream &s, size_t count,
                             size_t start_position = 0) const;

  /// Print a description of this thread using the provided thread format.
  ///
  /// \param[out] strm
  ///   The Stream to print the description to.
  ///
  /// \param[in] frame_idx
  ///   If not \b LLDB_INVALID_FRAME_ID, then use this frame index as context to
  ///   generate the description.
  ///
  /// \param[in] format
  ///   The input format.
  ///
  /// \return
  ///   \b true if and only if dumping with the given \p format worked.
  bool DumpUsingFormat(Stream &strm, uint32_t frame_idx,
                       const FormatEntity::Entry *format);

  // If stop_format is true, this will be the form used when we print stop
  // info. If false, it will be the form we use for thread list and co.
  void DumpUsingSettingsFormat(Stream &strm, uint32_t frame_idx,
                               bool stop_format);

  bool GetDescription(Stream &s, lldb::DescriptionLevel level,
                      bool print_json_thread, bool print_json_stopinfo);

  /// Default implementation for stepping into.
  ///
  /// This function is designed to be used by commands where the
  /// process is publicly stopped.
  ///
  /// \param[in] source_step
  ///     If true and the frame has debug info, then do a source level
  ///     step in, else do a single instruction step in.
  ///
  /// \param[in] step_in_avoids_code_without_debug_info
  ///     If \a true, then avoid stepping into code that doesn't have
  ///     debug info, else step into any code regardless of whether it
  ///     has debug info.
  ///
  /// \param[in] step_out_avoids_code_without_debug_info
  ///     If \a true, then if you step out to code with no debug info, keep
  ///     stepping out till you get to code with debug info.
  ///
  /// \return
  ///     An error that describes anything that went wrong
  virtual Status
  StepIn(bool source_step,
         LazyBool step_in_avoids_code_without_debug_info = eLazyBoolCalculate,
         LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  /// Default implementation for stepping over.
  ///
  /// This function is designed to be used by commands where the
  /// process is publicly stopped.
  ///
  /// \param[in] source_step
  ///     If true and the frame has debug info, then do a source level
  ///     step over, else do a single instruction step over.
  ///
  /// \return
  ///     An error that describes anything that went wrong
  virtual Status StepOver(
      bool source_step,
      LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  /// Default implementation for stepping out.
  ///
  /// This function is designed to be used by commands where the
  /// process is publicly stopped.
  ///
  /// \param[in] frame_idx
  ///     The frame index to step out of.
  ///
  /// \return
  ///     An error that describes anything that went wrong
  virtual Status StepOut(uint32_t frame_idx = 0);

  /// Retrieves the per-thread data area.
  /// Most OSs maintain a per-thread pointer (e.g. the FS register on
  /// x64), which we return the value of here.
  ///
  /// \return
  ///     LLDB_INVALID_ADDRESS if not supported, otherwise the thread
  ///     pointer value.
  virtual lldb::addr_t GetThreadPointer();

  /// Retrieves the per-module TLS block for a thread.
  ///
  /// \param[in] module
  ///     The module to query TLS data for.
  ///
  /// \param[in] tls_file_addr
  ///     The thread local address in module
  /// \return
  ///     If the thread has TLS data allocated for the
  ///     module, the address of the TLS block. Otherwise
  ///     LLDB_INVALID_ADDRESS is returned.
  virtual lldb::addr_t GetThreadLocalData(const lldb::ModuleSP module,
                                          lldb::addr_t tls_file_addr);

  /// Check whether this thread is safe to run functions
  ///
  /// The SystemRuntime may know of certain thread states (functions in
  /// process of execution, for instance) which can make it unsafe for
  /// functions to be called.
  ///
  /// \return
  ///     True if it is safe to call functions on this thread.
  ///     False if function calls should be avoided on this thread.
  virtual bool SafeToCallFunctions();

  // Thread Plan Providers:
  // This section provides the basic thread plans that the Process control
  // machinery uses to run the target.  ThreadPlan.h provides more details on
  // how this mechanism works. The thread provides accessors to a set of plans
  // that perform basic operations. The idea is that particular Platform
  // plugins can override these methods to provide the implementation of these
  // basic operations appropriate to their environment.
  //
  // NB: All the QueueThreadPlanXXX providers return Shared Pointers to
  // Thread plans.  This is useful so that you can modify the plans after
  // creation in ways specific to that plan type.  Also, it is often necessary
  // for ThreadPlans that utilize other ThreadPlans to implement their task to
  // keep a shared pointer to the sub-plan. But besides that, the shared
  // pointers should only be held onto by entities who live no longer than the
  // thread containing the ThreadPlan.
  // FIXME: If this becomes a problem, we can make a version that just returns a
  // pointer,
  // which it is clearly unsafe to hold onto, and a shared pointer version, and
  // only allow ThreadPlan and Co. to use the latter.  That is made more
  // annoying to do because there's no elegant way to friend a method to all
  // sub-classes of a given class.
  //

  /// Queues the base plan for a thread.
  /// The version returned by Process does some things that are useful,
  /// like handle breakpoints and signals, so if you return a plugin specific
  /// one you probably want to call through to the Process one for anything
  /// your plugin doesn't explicitly handle.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  lldb::ThreadPlanSP QueueBasePlan(bool abort_other_plans);

  /// Queues the plan used to step one instruction from the current PC of \a
  /// thread.
  ///
  /// \param[in] step_over
  ///    \b true if we step over calls to functions, false if we step in.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepSingleInstruction(
      bool step_over, bool abort_other_plans, bool stop_other_threads,
      Status &status);

  /// Queues the plan used to step through an address range, stepping  over
  /// function calls.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] type
  ///    Type of step to do, only eStepTypeInto and eStepTypeOver are supported
  ///    by this plan.
  ///
  /// \param[in] range
  ///    The address range to step through.
  ///
  /// \param[in] addr_context
  ///    When dealing with stepping through inlined functions the current PC is
  ///    not enough information to know
  ///    what "step" means.  For instance a series of nested inline functions
  ///    might start at the same address.
  //     The \a addr_context provides the current symbol context the step
  ///    is supposed to be out of.
  //   FIXME: Currently unused.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \param[in] step_out_avoids_code_without_debug_info
  ///    If eLazyBoolYes, if the step over steps out it will continue to step
  ///    out till it comes to a frame with debug info.
  ///    If eLazyBoolCalculate, we will consult the default set in the thread.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepOverRange(
      bool abort_other_plans, const AddressRange &range,
      const SymbolContext &addr_context, lldb::RunMode stop_other_threads,
      Status &status,
      LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  // Helper function that takes a LineEntry to step, insted of an AddressRange.
  // This may combine multiple LineEntries of the same source line number to
  // step over a longer address range in a single operation.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepOverRange(
      bool abort_other_plans, const LineEntry &line_entry,
      const SymbolContext &addr_context, lldb::RunMode stop_other_threads,
      Status &status,
      LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  /// Queues the plan used to step through an address range, stepping into
  /// functions.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] type
  ///    Type of step to do, only eStepTypeInto and eStepTypeOver are supported
  ///    by this plan.
  ///
  /// \param[in] range
  ///    The address range to step through.
  ///
  /// \param[in] addr_context
  ///    When dealing with stepping through inlined functions the current PC is
  ///    not enough information to know
  ///    what "step" means.  For instance a series of nested inline functions
  ///    might start at the same address.
  //     The \a addr_context provides the current symbol context the step
  ///    is supposed to be out of.
  //   FIXME: Currently unused.
  ///
  /// \param[in] step_in_target
  ///    Name if function we are trying to step into.  We will step out if we
  ///    don't land in that function.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \param[in] step_in_avoids_code_without_debug_info
  ///    If eLazyBoolYes we will step out if we step into code with no debug
  ///    info.
  ///    If eLazyBoolCalculate we will consult the default set in the thread.
  ///
  /// \param[in] step_out_avoids_code_without_debug_info
  ///    If eLazyBoolYes, if the step over steps out it will continue to step
  ///    out till it comes to a frame with debug info.
  ///    If eLazyBoolCalculate, it will consult the default set in the thread.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepInRange(
      bool abort_other_plans, const AddressRange &range,
      const SymbolContext &addr_context, const char *step_in_target,
      lldb::RunMode stop_other_threads, Status &status,
      LazyBool step_in_avoids_code_without_debug_info = eLazyBoolCalculate,
      LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  // Helper function that takes a LineEntry to step, insted of an AddressRange.
  // This may combine multiple LineEntries of the same source line number to
  // step over a longer address range in a single operation.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepInRange(
      bool abort_other_plans, const LineEntry &line_entry,
      const SymbolContext &addr_context, const char *step_in_target,
      lldb::RunMode stop_other_threads, Status &status,
      LazyBool step_in_avoids_code_without_debug_info = eLazyBoolCalculate,
      LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  /// Queue the plan used to step out of the function at the current PC of
  /// \a thread.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] addr_context
  ///    When dealing with stepping through inlined functions the current PC is
  ///    not enough information to know
  ///    what "step" means.  For instance a series of nested inline functions
  ///    might start at the same address.
  //     The \a addr_context provides the current symbol context the step
  ///    is supposed to be out of.
  //   FIXME: Currently unused.
  ///
  /// \param[in] first_insn
  ///     \b true if this is the first instruction of a function.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[in] report_stop_vote
  ///    See standard meanings for the stop & run votes in ThreadPlan.h.
  ///
  /// \param[in] report_run_vote
  ///    See standard meanings for the stop & run votes in ThreadPlan.h.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \param[in] step_out_avoids_code_without_debug_info
  ///    If eLazyBoolYes, if the step over steps out it will continue to step
  ///    out till it comes to a frame with debug info.
  ///    If eLazyBoolCalculate, it will consult the default set in the thread.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepOut(
      bool abort_other_plans, SymbolContext *addr_context, bool first_insn,
      bool stop_other_threads, Vote report_stop_vote, Vote report_run_vote,
      uint32_t frame_idx, Status &status,
      LazyBool step_out_avoids_code_without_debug_info = eLazyBoolCalculate);

  /// Queue the plan used to step out of the function at the current PC of
  /// a thread.  This version does not consult the should stop here callback,
  /// and should only
  /// be used by other thread plans when they need to retain control of the step
  /// out.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] addr_context
  ///    When dealing with stepping through inlined functions the current PC is
  ///    not enough information to know
  ///    what "step" means.  For instance a series of nested inline functions
  ///    might start at the same address.
  //     The \a addr_context provides the current symbol context the step
  ///    is supposed to be out of.
  //   FIXME: Currently unused.
  ///
  /// \param[in] first_insn
  ///     \b true if this is the first instruction of a function.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[in] report_stop_vote
  ///    See standard meanings for the stop & run votes in ThreadPlan.h.
  ///
  /// \param[in] report_run_vote
  ///    See standard meanings for the stop & run votes in ThreadPlan.h.
  ///
  /// \param[in] frame_idx
  ///     The frame index.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \param[in] continue_to_next_branch
  ///    Normally this will enqueue a plan that will put a breakpoint on the
  ///    return address and continue
  ///    to there.  If continue_to_next_branch is true, this is an operation not
  ///    involving the user --
  ///    e.g. stepping "next" in a source line and we instruction stepped into
  ///    another function --
  ///    so instead of putting a breakpoint on the return address, advance the
  ///    breakpoint to the
  ///    end of the source line that is doing the call, or until the next flow
  ///    control instruction.
  ///    If the return value from the function call is to be retrieved /
  ///    displayed to the user, you must stop
  ///    on the return address.  The return value may be stored in volatile
  ///    registers which are overwritten
  ///    before the next branch instruction.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP QueueThreadPlanForStepOutNoShouldStop(
      bool abort_other_plans, SymbolContext *addr_context, bool first_insn,
      bool stop_other_threads, Vote report_stop_vote, Vote report_run_vote,
      uint32_t frame_idx, Status &status, bool continue_to_next_branch = false);

  /// Gets the plan used to step through the code that steps from a function
  /// call site at the current PC into the actual function call.
  ///
  /// \param[in] return_stack_id
  ///    The stack id that we will return to (by setting backstop breakpoints on
  ///    the return
  ///    address to that frame) if we fail to step through.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP
  QueueThreadPlanForStepThrough(StackID &return_stack_id,
                                bool abort_other_plans, bool stop_other_threads,
                                Status &status);

  /// Gets the plan used to continue from the current PC.
  /// This is a simple plan, mostly useful as a backstop when you are continuing
  /// for some particular purpose.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \param[in] target_addr
  ///    The address to which we're running.
  ///
  /// \param[in] stop_other_threads
  ///    \b true if we will stop other threads while we single step this one.
  ///
  /// \param[out] status
  ///     A status with an error if queuing failed.
  ///
  /// \return
  ///     A shared pointer to the newly queued thread plan, or nullptr if the
  ///     plan could not be queued.
  virtual lldb::ThreadPlanSP
  QueueThreadPlanForRunToAddress(bool abort_other_plans, Address &target_addr,
                                 bool stop_other_threads, Status &status);

  virtual lldb::ThreadPlanSP QueueThreadPlanForStepUntil(
      bool abort_other_plans, lldb::addr_t *address_list, size_t num_addresses,
      bool stop_others, uint32_t frame_idx, Status &status);

  virtual lldb::ThreadPlanSP
  QueueThreadPlanForStepScripted(bool abort_other_plans, const char *class_name,
                                 StructuredData::ObjectSP extra_args_sp,
                                 bool stop_other_threads, Status &status);

  // Thread Plan accessors:

  /// Format the thread plan information for auto completion.
  ///
  /// \param[in] request
  ///     The reference to the completion handler.
  void AutoCompleteThreadPlans(CompletionRequest &request) const;

  /// Gets the plan which will execute next on the plan stack.
  ///
  /// \return
  ///     A pointer to the next executed plan.
  ThreadPlan *GetCurrentPlan() const;

  /// Unwinds the thread stack for the innermost expression plan currently
  /// on the thread plan stack.
  ///
  /// \return
  ///     An error if the thread plan could not be unwound.

  Status UnwindInnermostExpression();

  /// Gets the outer-most plan that was popped off the plan stack in the
  /// most recent stop.  Useful for printing the stop reason accurately.
  ///
  /// \return
  ///     A pointer to the last completed plan.
  lldb::ThreadPlanSP GetCompletedPlan() const;

  /// Gets the outer-most return value from the completed plans
  ///
  /// \return
  ///     A ValueObjectSP, either empty if there is no return value,
  ///     or containing the return value.
  lldb::ValueObjectSP GetReturnValueObject() const;

  /// Gets the outer-most expression variable from the completed plans
  ///
  /// \return
  ///     A ExpressionVariableSP, either empty if there is no
  ///     plan completed an expression during the current stop
  ///     or the expression variable that was made for the completed expression.
  lldb::ExpressionVariableSP GetExpressionVariable() const;

  ///  Checks whether the given plan is in the completed plans for this
  ///  stop.
  ///
  /// \param[in] plan
  ///     Pointer to the plan you're checking.
  ///
  /// \return
  ///     Returns true if the input plan is in the completed plan stack,
  ///     false otherwise.
  bool IsThreadPlanDone(ThreadPlan *plan) const;

  ///  Checks whether the given plan is in the discarded plans for this
  ///  stop.
  ///
  /// \param[in] plan
  ///     Pointer to the plan you're checking.
  ///
  /// \return
  ///     Returns true if the input plan is in the discarded plan stack,
  ///     false otherwise.
  bool WasThreadPlanDiscarded(ThreadPlan *plan) const;

  /// Check if we have completed plan to override breakpoint stop reason
  ///
  /// \return
  ///     Returns true if completed plan stack is not empty
  ///     false otherwise.
  bool CompletedPlanOverridesBreakpoint() const;

  /// Queues a generic thread plan.
  ///
  /// \param[in] plan_sp
  ///    The plan to queue.
  ///
  /// \param[in] abort_other_plans
  ///    \b true if we discard the currently queued plans and replace them with
  ///    this one.
  ///    Otherwise this plan will go on the end of the plan stack.
  ///
  /// \return
  ///     A pointer to the last completed plan.
  Status QueueThreadPlan(lldb::ThreadPlanSP &plan_sp, bool abort_other_plans);

  /// Discards the plans queued on the plan stack of the current thread.  This
  /// is
  /// arbitrated by the "Controlling" ThreadPlans, using the "OkayToDiscard"
  /// call.
  //  But if \a force is true, all thread plans are discarded.
  void DiscardThreadPlans(bool force);

  /// Discards the plans queued on the plan stack of the current thread up to
  /// and
  /// including up_to_plan_sp.
  //
  // \param[in] up_to_plan_sp
  //   Discard all plans up to and including this one.
  void DiscardThreadPlansUpToPlan(lldb::ThreadPlanSP &up_to_plan_sp);

  void DiscardThreadPlansUpToPlan(ThreadPlan *up_to_plan_ptr);

  /// Discards the plans queued on the plan stack of the current thread up to
  /// and
  /// including the plan in that matches \a thread_index counting only
  /// the non-Private plans.
  ///
  /// \param[in] thread_index
  ///   Discard all plans up to and including this user plan given by this
  ///   index.
  ///
  /// \return
  ///    \b true if there was a thread plan with that user index, \b false
  ///    otherwise.
  bool DiscardUserThreadPlansUpToIndex(uint32_t thread_index);

  virtual bool CheckpointThreadState(ThreadStateCheckpoint &saved_state);

  virtual bool
  RestoreRegisterStateFromCheckpoint(ThreadStateCheckpoint &saved_state);

  void RestoreThreadStateFromCheckpoint(ThreadStateCheckpoint &saved_state);

  // Get the thread index ID. The index ID that is guaranteed to not be re-used
  // by a process. They start at 1 and increase with each new thread. This
  // allows easy command line access by a unique ID that is easier to type than
  // the actual system thread ID.
  uint32_t GetIndexID() const;

  // Get the originating thread's index ID.
  // In the case of an "extended" thread -- a thread which represents the stack
  // that enqueued/spawned work that is currently executing -- we need to
  // provide the IndexID of the thread that actually did this work.  We don't
  // want to just masquerade as that thread's IndexID by using it in our own
  // IndexID because that way leads to madness - but the driver program which
  // is iterating over extended threads may ask for the OriginatingThreadID to
  // display that information to the user.
  // Normal threads will return the same thing as GetIndexID();
  virtual uint32_t GetExtendedBacktraceOriginatingIndexID() {
    return GetIndexID();
  }

  // The API ID is often the same as the Thread::GetID(), but not in all cases.
  // Thread::GetID() is the user visible thread ID that clients would want to
  // see. The API thread ID is the thread ID that is used when sending data
  // to/from the debugging protocol.
  virtual lldb::user_id_t GetProtocolID() const { return GetID(); }

  // lldb::ExecutionContextScope pure virtual functions
  lldb::TargetSP CalculateTarget() override;

  lldb::ProcessSP CalculateProcess() override;

  lldb::ThreadSP CalculateThread() override;

  lldb::StackFrameSP CalculateStackFrame() override;

  void CalculateExecutionContext(ExecutionContext &exe_ctx) override;

  lldb::StackFrameSP
  GetStackFrameSPForStackFramePtr(StackFrame *stack_frame_ptr);

  size_t GetStatus(Stream &strm, uint32_t start_frame, uint32_t num_frames,
                   uint32_t num_frames_with_source, bool stop_format,
                   bool only_stacks = false);

  size_t GetStackFrameStatus(Stream &strm, uint32_t first_frame,
                             uint32_t num_frames, bool show_frame_info,
                             uint32_t num_frames_with_source);

  // We need a way to verify that even though we have a thread in a shared
  // pointer that the object itself is still valid. Currently this won't be the
  // case if DestroyThread() was called. DestroyThread is called when a thread
  // has been removed from the Process' thread list.
  bool IsValid() const { return !m_destroy_called; }

  // Sets and returns a valid stop info based on the process stop ID and the
  // current thread plan. If the thread stop ID does not match the process'
  // stop ID, the private stop reason is not set and an invalid StopInfoSP may
  // be returned.
  //
  // NOTE: This function must be called before the current thread plan is
  // moved to the completed plan stack (in Thread::ShouldStop()).
  //
  // NOTE: If subclasses override this function, ensure they do not overwrite
  // the m_actual_stop_info if it is valid.  The stop info may be a
  // "checkpointed and restored" stop info, so if it is still around it is
  // right even if you have not calculated this yourself, or if it disagrees
  // with what you might have calculated.
  virtual lldb::StopInfoSP GetPrivateStopInfo(bool calculate = true);

  // Calculate the stop info that will be shown to lldb clients.  For instance,
  // a "step out" is implemented by running to a breakpoint on the function
  // return PC, so the process plugin initially sets the stop info to a
  // StopInfoBreakpoint. But once we've run the ShouldStop machinery, we
  // discover that there's a completed ThreadPlanStepOut, and that's really
  // the StopInfo we want to show.  That will happen naturally the next
  // time GetStopInfo is called, but if you want to force the replacement,
  // you can call this.

  void CalculatePublicStopInfo();

  /// Ask the thread subclass to set its stop info.
  ///
  /// Thread subclasses should call Thread::SetStopInfo(...) with the reason the
  /// thread stopped.
  ///
  /// A thread that is sitting at a breakpoint site, but has not yet executed
  /// the breakpoint instruction, should have a breakpoint-hit StopInfo set.
  /// When execution is resumed, any thread sitting at a breakpoint site will
  /// instruction-step over the breakpoint instruction silently, and we will
  /// never record this breakpoint as being hit, updating the hit count,
  /// possibly executing breakpoint commands or conditions.
  ///
  /// \return
  ///      True if Thread::SetStopInfo(...) was called, false otherwise.
  virtual bool CalculateStopInfo() = 0;

  // Gets the temporary resume state for a thread.
  //
  // This value gets set in each thread by complex debugger logic in
  // Thread::ShouldResume() and an appropriate thread resume state will get set
  // in each thread every time the process is resumed prior to calling
  // Process::DoResume(). The lldb_private::Process subclass should adhere to
  // the thread resume state request which will be one of:
  //
  //  eStateRunning   - thread will resume when process is resumed
  //  eStateStepping  - thread should step 1 instruction and stop when process
  //                    is resumed
  //  eStateSuspended - thread should not execute any instructions when
  //                    process is resumed
  lldb::StateType GetTemporaryResumeState() const {
    return m_temporary_resume_state;
  }

  void SetStopInfo(const lldb::StopInfoSP &stop_info_sp);

  void ResetStopInfo();

  void SetShouldReportStop(Vote vote);
  
  void SetShouldRunBeforePublicStop(bool newval) { 
      m_should_run_before_public_stop = newval; 
  }
  
  bool ShouldRunBeforePublicStop() {
      return m_should_run_before_public_stop;
  }

  /// Sets the extended backtrace token for this thread
  ///
  /// Some Thread subclasses may maintain a token to help with providing
  /// an extended backtrace.  The SystemRuntime plugin will set/request this.
  ///
  /// \param [in] token The extended backtrace token.
  virtual void SetExtendedBacktraceToken(uint64_t token) {}

  /// Gets the extended backtrace token for this thread
  ///
  /// Some Thread subclasses may maintain a token to help with providing
  /// an extended backtrace.  The SystemRuntime plugin will set/request this.
  ///
  /// \return
  ///     The token needed by the SystemRuntime to create an extended backtrace.
  ///     LLDB_INVALID_ADDRESS is returned if no token is available.
  virtual uint64_t GetExtendedBacktraceToken() { return LLDB_INVALID_ADDRESS; }

  lldb::ValueObjectSP GetCurrentException();

  lldb::ThreadSP GetCurrentExceptionBacktrace();

  lldb::ValueObjectSP GetSiginfoValue();

  /// Request the pc value the thread had when previously stopped.
  ///
  /// When the thread performs execution, it copies the current RegisterContext
  /// GetPC() value.  This method returns that value, if it is available.
  ///
  /// \return
  ///     The PC value before execution was resumed.  May not be available;
  ///     an empty std::optional is returned in that case.
  std::optional<lldb::addr_t> GetPreviousFrameZeroPC();

protected:
  friend class ThreadPlan;
  friend class ThreadList;
  friend class ThreadEventData;
  friend class StackFrameList;
  friend class StackFrame;
  friend class OperatingSystem;

  // This is necessary to make sure thread assets get destroyed while the
  // thread is still in good shape to call virtual thread methods.  This must
  // be called by classes that derive from Thread in their destructor.
  virtual void DestroyThread();

  ThreadPlanStack &GetPlans() const;

  void PushPlan(lldb::ThreadPlanSP plan_sp);

  void PopPlan();

  void DiscardPlan();

  ThreadPlan *GetPreviousPlan(ThreadPlan *plan) const;

  virtual Unwind &GetUnwinder();

  // Check to see whether the thread is still at the last breakpoint hit that
  // stopped it.
  virtual bool IsStillAtLastBreakpointHit();

  // Some threads are threads that are made up by OperatingSystem plugins that
  // are threads that exist and are context switched out into memory. The
  // OperatingSystem plug-in need a ways to know if a thread is "real" or made
  // up.
  virtual bool IsOperatingSystemPluginThread() const { return false; }

  // Subclasses that have a way to get an extended info dictionary for this
  // thread should fill
  virtual lldb_private::StructuredData::ObjectSP FetchThreadExtendedInfo() {
    return StructuredData::ObjectSP();
  }

  lldb::StackFrameListSP GetStackFrameList();

  void SetTemporaryResumeState(lldb::StateType new_state) {
    m_temporary_resume_state = new_state;
  }

  void FrameSelectedCallback(lldb_private::StackFrame *frame);

  virtual llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
  GetSiginfo(size_t max_size) const {
    return llvm::make_error<UnimplementedError>();
  }

  // Classes that inherit from Process can see and modify these
  lldb::ProcessWP m_process_wp;    ///< The process that owns this thread.
  lldb::StopInfoSP m_stop_info_sp; ///< The private stop reason for this thread
  uint32_t m_stop_info_stop_id; // This is the stop id for which the StopInfo is
                                // valid.  Can use this so you know that
  // the thread's m_stop_info_sp is current and you don't have to fetch it
  // again
  uint32_t m_stop_info_override_stop_id; // The stop ID containing the last time
                                         // the stop info was checked against
                                         // the stop info override
  bool m_should_run_before_public_stop;  // If this thread has "stop others" 
                                         // private work to do, then it will
                                         // set this.
  const uint32_t m_index_id; ///< A unique 1 based index assigned to each thread
                             /// for easy UI/command line access.
  lldb::RegisterContextSP m_reg_context_sp; ///< The register context for this
                                            ///thread's current register state.
  lldb::StateType m_state;                  ///< The state of our process.
  mutable std::recursive_mutex
      m_state_mutex;       ///< Multithreaded protection for m_state.
  mutable std::recursive_mutex
      m_frame_mutex; ///< Multithreaded protection for m_state.
  lldb::StackFrameListSP m_curr_frames_sp; ///< The stack frames that get lazily
                                           ///populated after a thread stops.
  lldb::StackFrameListSP m_prev_frames_sp; ///< The previous stack frames from
                                           ///the last time this thread stopped.
  std::optional<lldb::addr_t>
      m_prev_framezero_pc; ///< Frame 0's PC the last
                           /// time this thread was stopped.
  int m_resume_signal; ///< The signal that should be used when continuing this
                       ///thread.
  lldb::StateType m_resume_state; ///< This state is used to force a thread to
                                  ///be suspended from outside the ThreadPlan
                                  ///logic.
  lldb::StateType m_temporary_resume_state; ///< This state records what the
                                            ///thread was told to do by the
                                            ///thread plan logic for the current
                                            ///resume.
  /// It gets set in Thread::ShouldResume.
  std::unique_ptr<lldb_private::Unwind> m_unwinder_up;
  bool m_destroy_called; // This is used internally to make sure derived Thread
                         // classes call DestroyThread.
  LazyBool m_override_should_notify;
  mutable std::unique_ptr<ThreadPlanStack> m_null_plan_stack_up;

private:
  bool m_extended_info_fetched; // Have we tried to retrieve the m_extended_info
                                // for this thread?
  StructuredData::ObjectSP m_extended_info; // The extended info for this thread

  void BroadcastSelectedFrameChange(StackID &new_frame_id);

  Thread(const Thread &) = delete;
  const Thread &operator=(const Thread &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREAD_H
