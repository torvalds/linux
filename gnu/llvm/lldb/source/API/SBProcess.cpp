//===-- SBProcess.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBProcess.h"
#include "lldb/Utility/Instrumentation.h"

#include <cinttypes>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "lldb/Core/AddressRangeListImpl.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBMemoryRegionInfoList.h"
#include "lldb/API/SBSaveCoreOptions.h"
#include "lldb/API/SBScriptObject.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBThreadCollection.h"
#include "lldb/API/SBTrace.h"
#include "lldb/API/SBUnixSignals.h"

using namespace lldb;
using namespace lldb_private;

SBProcess::SBProcess() { LLDB_INSTRUMENT_VA(this); }

// SBProcess constructor

SBProcess::SBProcess(const SBProcess &rhs) : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBProcess::SBProcess(const lldb::ProcessSP &process_sp)
    : m_opaque_wp(process_sp) {
  LLDB_INSTRUMENT_VA(this, process_sp);
}

const SBProcess &SBProcess::operator=(const SBProcess &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

// Destructor
SBProcess::~SBProcess() = default;

const char *SBProcess::GetBroadcasterClassName() {
  LLDB_INSTRUMENT();

  return ConstString(Process::GetStaticBroadcasterClass()).AsCString();
}

const char *SBProcess::GetPluginName() {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    return ConstString(process_sp->GetPluginName()).GetCString();
  }
  return "<Unknown>";
}

const char *SBProcess::GetShortPluginName() {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    return ConstString(process_sp->GetPluginName()).GetCString();
  }
  return "<Unknown>";
}

lldb::ProcessSP SBProcess::GetSP() const { return m_opaque_wp.lock(); }

void SBProcess::SetSP(const ProcessSP &process_sp) { m_opaque_wp = process_sp; }

void SBProcess::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_wp.reset();
}

bool SBProcess::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBProcess::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(m_opaque_wp.lock());
  return ((bool)process_sp && process_sp->IsValid());
}

bool SBProcess::RemoteLaunch(char const **argv, char const **envp,
                             const char *stdin_path, const char *stdout_path,
                             const char *stderr_path,
                             const char *working_directory,
                             uint32_t launch_flags, bool stop_at_entry,
                             lldb::SBError &error) {
  LLDB_INSTRUMENT_VA(this, argv, envp, stdin_path, stdout_path, stderr_path,
                     working_directory, launch_flags, stop_at_entry, error);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    if (process_sp->GetState() == eStateConnected) {
      if (stop_at_entry)
        launch_flags |= eLaunchFlagStopAtEntry;
      ProcessLaunchInfo launch_info(FileSpec(stdin_path), FileSpec(stdout_path),
                                    FileSpec(stderr_path),
                                    FileSpec(working_directory), launch_flags);
      Module *exe_module = process_sp->GetTarget().GetExecutableModulePointer();
      if (exe_module)
        launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
      if (argv)
        launch_info.GetArguments().AppendArguments(argv);
      if (envp)
        launch_info.GetEnvironment() = Environment(envp);
      error.SetError(process_sp->Launch(launch_info));
    } else {
      error.SetErrorString("must be in eStateConnected to call RemoteLaunch");
    }
  } else {
    error.SetErrorString("unable to attach pid");
  }

  return error.Success();
}

bool SBProcess::RemoteAttachToProcessWithID(lldb::pid_t pid,
                                            lldb::SBError &error) {
  LLDB_INSTRUMENT_VA(this, pid, error);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    if (process_sp->GetState() == eStateConnected) {
      ProcessAttachInfo attach_info;
      attach_info.SetProcessID(pid);
      error.SetError(process_sp->Attach(attach_info));
    } else {
      error.SetErrorString(
          "must be in eStateConnected to call RemoteAttachToProcessWithID");
    }
  } else {
    error.SetErrorString("unable to attach pid");
  }

  return error.Success();
}

uint32_t SBProcess::GetNumThreads() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t num_threads = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;

    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    num_threads = process_sp->GetThreadList().GetSize(can_update);
  }

  return num_threads;
}

SBThread SBProcess::GetSelectedThread() const {
  LLDB_INSTRUMENT_VA(this);

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->GetThreadList().GetSelectedThread();
    sb_thread.SetThread(thread_sp);
  }

  return sb_thread;
}

SBThread SBProcess::CreateOSPluginThread(lldb::tid_t tid,
                                         lldb::addr_t context) {
  LLDB_INSTRUMENT_VA(this, tid, context);

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->CreateOSPluginThread(tid, context);
    sb_thread.SetThread(thread_sp);
  }

  return sb_thread;
}

SBTarget SBProcess::GetTarget() const {
  LLDB_INSTRUMENT_VA(this);

  SBTarget sb_target;
  TargetSP target_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    target_sp = process_sp->GetTarget().shared_from_this();
    sb_target.SetSP(target_sp);
  }

  return sb_target;
}

size_t SBProcess::PutSTDIN(const char *src, size_t src_len) {
  LLDB_INSTRUMENT_VA(this, src, src_len);

  size_t ret_val = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    ret_val = process_sp->PutSTDIN(src, src_len, error);
  }

  return ret_val;
}

size_t SBProcess::GetSTDOUT(char *dst, size_t dst_len) const {
  LLDB_INSTRUMENT_VA(this, dst, dst_len);

  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    bytes_read = process_sp->GetSTDOUT(dst, dst_len, error);
  }

  return bytes_read;
}

size_t SBProcess::GetSTDERR(char *dst, size_t dst_len) const {
  LLDB_INSTRUMENT_VA(this, dst, dst_len);

  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    bytes_read = process_sp->GetSTDERR(dst, dst_len, error);
  }

  return bytes_read;
}

size_t SBProcess::GetAsyncProfileData(char *dst, size_t dst_len) const {
  LLDB_INSTRUMENT_VA(this, dst, dst_len);

  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Status error;
    bytes_read = process_sp->GetAsyncProfileData(dst, dst_len, error);
  }

  return bytes_read;
}

void SBProcess::ReportEventState(const SBEvent &event, SBFile out) const {
  LLDB_INSTRUMENT_VA(this, event, out);

  return ReportEventState(event, out.m_opaque_sp);
}

void SBProcess::ReportEventState(const SBEvent &event, FILE *out) const {
  LLDB_INSTRUMENT_VA(this, event, out);
  FileSP outfile = std::make_shared<NativeFile>(out, false);
  return ReportEventState(event, outfile);
}

void SBProcess::ReportEventState(const SBEvent &event, FileSP out) const {

  LLDB_INSTRUMENT_VA(this, event, out);

  if (!out || !out->IsValid())
    return;

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    StreamFile stream(out);
    const StateType event_state = SBProcess::GetStateFromEvent(event);
    stream.Printf("Process %" PRIu64 " %s\n", process_sp->GetID(),
                  SBDebugger::StateAsCString(event_state));
  }
}

void SBProcess::AppendEventStateReport(const SBEvent &event,
                                       SBCommandReturnObject &result) {
  LLDB_INSTRUMENT_VA(this, event, result);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    const StateType event_state = SBProcess::GetStateFromEvent(event);
    char message[1024];
    ::snprintf(message, sizeof(message), "Process %" PRIu64 " %s\n",
               process_sp->GetID(), SBDebugger::StateAsCString(event_state));

    result.AppendMessage(message);
  }
}

bool SBProcess::SetSelectedThread(const SBThread &thread) {
  LLDB_INSTRUMENT_VA(this, thread);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    return process_sp->GetThreadList().SetSelectedThreadByID(
        thread.GetThreadID());
  }
  return false;
}

bool SBProcess::SetSelectedThreadByID(lldb::tid_t tid) {
  LLDB_INSTRUMENT_VA(this, tid);

  bool ret_val = false;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    ret_val = process_sp->GetThreadList().SetSelectedThreadByID(tid);
  }

  return ret_val;
}

bool SBProcess::SetSelectedThreadByIndexID(uint32_t index_id) {
  LLDB_INSTRUMENT_VA(this, index_id);

  bool ret_val = false;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    ret_val = process_sp->GetThreadList().SetSelectedThreadByIndexID(index_id);
  }

  return ret_val;
}

SBThread SBProcess::GetThreadAtIndex(size_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->GetThreadList().GetThreadAtIndex(index, can_update);
    sb_thread.SetThread(thread_sp);
  }

  return sb_thread;
}

uint32_t SBProcess::GetNumQueues() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t num_queues = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      num_queues = process_sp->GetQueueList().GetSize();
    }
  }

  return num_queues;
}

SBQueue SBProcess::GetQueueAtIndex(size_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  SBQueue sb_queue;
  QueueSP queue_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      queue_sp = process_sp->GetQueueList().GetQueueAtIndex(index);
      sb_queue.SetQueue(queue_sp);
    }
  }

  return sb_queue;
}

uint32_t SBProcess::GetStopID(bool include_expression_stops) {
  LLDB_INSTRUMENT_VA(this, include_expression_stops);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    if (include_expression_stops)
      return process_sp->GetStopID();
    else
      return process_sp->GetLastNaturalStopID();
  }
  return 0;
}

SBEvent SBProcess::GetStopEventForStopID(uint32_t stop_id) {
  LLDB_INSTRUMENT_VA(this, stop_id);

  SBEvent sb_event;
  EventSP event_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    event_sp = process_sp->GetStopEventForStopID(stop_id);
    sb_event.reset(event_sp);
  }

  return sb_event;
}

void SBProcess::ForceScriptedState(StateType new_state) {
  LLDB_INSTRUMENT_VA(this, new_state);

  if (ProcessSP process_sp = GetSP()) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    process_sp->ForceScriptedState(new_state);
  }
}

StateType SBProcess::GetState() {
  LLDB_INSTRUMENT_VA(this);

  StateType ret_val = eStateInvalid;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    ret_val = process_sp->GetState();
  }

  return ret_val;
}

int SBProcess::GetExitStatus() {
  LLDB_INSTRUMENT_VA(this);

  int exit_status = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    exit_status = process_sp->GetExitStatus();
  }

  return exit_status;
}

const char *SBProcess::GetExitDescription() {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());
  if (!process_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      process_sp->GetTarget().GetAPIMutex());
  return ConstString(process_sp->GetExitDescription()).GetCString();
}

lldb::pid_t SBProcess::GetProcessID() {
  LLDB_INSTRUMENT_VA(this);

  lldb::pid_t ret_val = LLDB_INVALID_PROCESS_ID;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    ret_val = process_sp->GetID();

  return ret_val;
}

uint32_t SBProcess::GetUniqueID() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t ret_val = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    ret_val = process_sp->GetUniqueID();
  return ret_val;
}

ByteOrder SBProcess::GetByteOrder() const {
  LLDB_INSTRUMENT_VA(this);

  ByteOrder byteOrder = eByteOrderInvalid;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    byteOrder = process_sp->GetTarget().GetArchitecture().GetByteOrder();

  return byteOrder;
}

uint32_t SBProcess::GetAddressByteSize() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t size = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp)
    size = process_sp->GetTarget().GetArchitecture().GetAddressByteSize();

  return size;
}

SBError SBProcess::Continue() {
  LLDB_INSTRUMENT_VA(this);

  SBError sb_error;
  ProcessSP process_sp(GetSP());

  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());

    if (process_sp->GetTarget().GetDebugger().GetAsyncExecution())
      sb_error.ref() = process_sp->Resume();
    else
      sb_error.ref() = process_sp->ResumeSynchronous(nullptr);
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBError SBProcess::Destroy() {
  LLDB_INSTRUMENT_VA(this);

  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Destroy(false));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBError SBProcess::Stop() {
  LLDB_INSTRUMENT_VA(this);

  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Halt());
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBError SBProcess::Kill() {
  LLDB_INSTRUMENT_VA(this);

  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Destroy(true));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBError SBProcess::Detach() {
  LLDB_INSTRUMENT_VA(this);

  // FIXME: This should come from a process default.
  bool keep_stopped = false;
  return Detach(keep_stopped);
}

SBError SBProcess::Detach(bool keep_stopped) {
  LLDB_INSTRUMENT_VA(this, keep_stopped);

  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Detach(keep_stopped));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBError SBProcess::Signal(int signo) {
  LLDB_INSTRUMENT_VA(this, signo);

  SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    sb_error.SetError(process_sp->Signal(signo));
  } else
    sb_error.SetErrorString("SBProcess is invalid");

  return sb_error;
}

SBUnixSignals SBProcess::GetUnixSignals() {
  LLDB_INSTRUMENT_VA(this);

  if (auto process_sp = GetSP())
    return SBUnixSignals{process_sp};

  return SBUnixSignals{};
}

void SBProcess::SendAsyncInterrupt() {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    process_sp->SendAsyncInterrupt();
  }
}

SBThread SBProcess::GetThreadByID(tid_t tid) {
  LLDB_INSTRUMENT_VA(this, tid);

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp = process_sp->GetThreadList().FindThreadByID(tid, can_update);
    sb_thread.SetThread(thread_sp);
  }

  return sb_thread;
}

SBThread SBProcess::GetThreadByIndexID(uint32_t index_id) {
  LLDB_INSTRUMENT_VA(this, index_id);

  SBThread sb_thread;
  ThreadSP thread_sp;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    const bool can_update = stop_locker.TryLock(&process_sp->GetRunLock());
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    thread_sp =
        process_sp->GetThreadList().FindThreadByIndexID(index_id, can_update);
    sb_thread.SetThread(thread_sp);
  }

  return sb_thread;
}

StateType SBProcess::GetStateFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  StateType ret_val = Process::ProcessEventData::GetStateFromEvent(event.get());

  return ret_val;
}

bool SBProcess::GetRestartedFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  bool ret_val = Process::ProcessEventData::GetRestartedFromEvent(event.get());

  return ret_val;
}

size_t SBProcess::GetNumRestartedReasonsFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return Process::ProcessEventData::GetNumRestartedReasons(event.get());
}

const char *
SBProcess::GetRestartedReasonAtIndexFromEvent(const lldb::SBEvent &event,
                                              size_t idx) {
  LLDB_INSTRUMENT_VA(event, idx);

  return ConstString(Process::ProcessEventData::GetRestartedReasonAtIndex(
                         event.get(), idx))
      .GetCString();
}

SBProcess SBProcess::GetProcessFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  ProcessSP process_sp =
      Process::ProcessEventData::GetProcessFromEvent(event.get());
  if (!process_sp) {
    // StructuredData events also know the process they come from. Try that.
    process_sp = EventDataStructuredData::GetProcessFromEvent(event.get());
  }

  return SBProcess(process_sp);
}

bool SBProcess::GetInterruptedFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return Process::ProcessEventData::GetInterruptedFromEvent(event.get());
}

lldb::SBStructuredData
SBProcess::GetStructuredDataFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return SBStructuredData(event.GetSP());
}

bool SBProcess::EventIsProcessEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return Process::ProcessEventData::GetEventDataFromEvent(event.get()) !=
         nullptr;
}

bool SBProcess::EventIsStructuredDataEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  EventSP event_sp = event.GetSP();
  EventData *event_data = event_sp ? event_sp->GetData() : nullptr;
  return event_data && (event_data->GetFlavor() ==
                        EventDataStructuredData::GetFlavorString());
}

SBBroadcaster SBProcess::GetBroadcaster() const {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());

  SBBroadcaster broadcaster(process_sp.get(), false);

  return broadcaster;
}

const char *SBProcess::GetBroadcasterClass() {
  LLDB_INSTRUMENT();

  return ConstString(Process::GetStaticBroadcasterClass()).AsCString();
}

lldb::SBAddressRangeList SBProcess::FindRangesInMemory(
    const void *buf, uint64_t size, const SBAddressRangeList &ranges,
    uint32_t alignment, uint32_t max_matches, SBError &error) {
  LLDB_INSTRUMENT_VA(this, buf, size, ranges, alignment, max_matches, error);

  lldb::SBAddressRangeList matches;

  ProcessSP process_sp(GetSP());
  if (!process_sp) {
    error.SetErrorString("SBProcess is invalid");
    return matches;
  }
  Process::StopLocker stop_locker;
  if (!stop_locker.TryLock(&process_sp->GetRunLock())) {
    error.SetErrorString("process is running");
    return matches;
  }
  std::lock_guard<std::recursive_mutex> guard(
      process_sp->GetTarget().GetAPIMutex());
  matches.m_opaque_up->ref() = process_sp->FindRangesInMemory(
      reinterpret_cast<const uint8_t *>(buf), size, ranges.ref().ref(),
      alignment, max_matches, error.ref());
  return matches;
}

lldb::addr_t SBProcess::FindInMemory(const void *buf, uint64_t size,
                                     const SBAddressRange &range,
                                     uint32_t alignment, SBError &error) {
  LLDB_INSTRUMENT_VA(this, buf, size, range, alignment, error);

  ProcessSP process_sp(GetSP());

  if (!process_sp) {
    error.SetErrorString("SBProcess is invalid");
    return LLDB_INVALID_ADDRESS;
  }

  Process::StopLocker stop_locker;
  if (!stop_locker.TryLock(&process_sp->GetRunLock())) {
    error.SetErrorString("process is running");
    return LLDB_INVALID_ADDRESS;
  }

  std::lock_guard<std::recursive_mutex> guard(
      process_sp->GetTarget().GetAPIMutex());
  return process_sp->FindInMemory(reinterpret_cast<const uint8_t *>(buf), size,
                                  range.ref(), alignment, error.ref());
}

size_t SBProcess::ReadMemory(addr_t addr, void *dst, size_t dst_len,
                             SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, addr, dst, dst_len, sb_error);

  if (!dst) {
    sb_error.SetErrorStringWithFormat(
        "no buffer provided to read %zu bytes into", dst_len);
    return 0;
  }

  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());


  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      bytes_read = process_sp->ReadMemory(addr, dst, dst_len, sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }

  return bytes_read;
}

size_t SBProcess::ReadCStringFromMemory(addr_t addr, void *buf, size_t size,
                                        lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, addr, buf, size, sb_error);

  size_t bytes_read = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      bytes_read = process_sp->ReadCStringFromMemory(addr, (char *)buf, size,
                                                     sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return bytes_read;
}

uint64_t SBProcess::ReadUnsignedFromMemory(addr_t addr, uint32_t byte_size,
                                           lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, addr, byte_size, sb_error);

  uint64_t value = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      value = process_sp->ReadUnsignedIntegerFromMemory(addr, byte_size, 0,
                                                        sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return value;
}

lldb::addr_t SBProcess::ReadPointerFromMemory(addr_t addr,
                                              lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, addr, sb_error);

  lldb::addr_t ptr = LLDB_INVALID_ADDRESS;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      ptr = process_sp->ReadPointerFromMemory(addr, sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return ptr;
}

size_t SBProcess::WriteMemory(addr_t addr, const void *src, size_t src_len,
                              SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, addr, src, src_len, sb_error);

  size_t bytes_written = 0;

  ProcessSP process_sp(GetSP());

  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      bytes_written =
          process_sp->WriteMemory(addr, src, src_len, sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  }

  return bytes_written;
}

void SBProcess::GetStatus(SBStream &status) {
  LLDB_INSTRUMENT_VA(this, status);

  ProcessSP process_sp(GetSP());
  if (process_sp)
    process_sp->GetStatus(status.ref());
}

bool SBProcess::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    char path[PATH_MAX];
    GetTarget().GetExecutable().GetPath(path, sizeof(path));
    Module *exe_module = process_sp->GetTarget().GetExecutableModulePointer();
    const char *exe_name = nullptr;
    if (exe_module)
      exe_name = exe_module->GetFileSpec().GetFilename().AsCString();

    strm.Printf("SBProcess: pid = %" PRIu64 ", state = %s, threads = %d%s%s",
                process_sp->GetID(), lldb_private::StateAsCString(GetState()),
                GetNumThreads(), exe_name ? ", executable = " : "",
                exe_name ? exe_name : "");
  } else
    strm.PutCString("No value");

  return true;
}

SBStructuredData SBProcess::GetExtendedCrashInformation() {
  LLDB_INSTRUMENT_VA(this);
  SBStructuredData data;
  ProcessSP process_sp(GetSP());
  if (!process_sp)
    return data;

  PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();

  if (!platform_sp)
    return data;

  auto expected_data =
      platform_sp->FetchExtendedCrashInformation(*process_sp.get());

  if (!expected_data)
    return data;

  StructuredData::ObjectSP fetched_data = *expected_data;
  data.m_impl_up->SetObjectSP(fetched_data);
  return data;
}

uint32_t
SBProcess::GetNumSupportedHardwareWatchpoints(lldb::SBError &sb_error) const {
  LLDB_INSTRUMENT_VA(this, sb_error);

  uint32_t num = 0;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
    std::optional<uint32_t> actual_num = process_sp->GetWatchpointSlotCount();
    if (actual_num) {
      num = *actual_num;
    } else {
      sb_error.SetErrorString("Unable to determine number of watchpoints");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return num;
}

uint32_t SBProcess::LoadImage(lldb::SBFileSpec &sb_remote_image_spec,
                              lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, sb_remote_image_spec, sb_error);

  return LoadImage(SBFileSpec(), sb_remote_image_spec, sb_error);
}

uint32_t SBProcess::LoadImage(const lldb::SBFileSpec &sb_local_image_spec,
                              const lldb::SBFileSpec &sb_remote_image_spec,
                              lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, sb_local_image_spec, sb_remote_image_spec, sb_error);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
      PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();
      return platform_sp->LoadImage(process_sp.get(), *sb_local_image_spec,
                                    *sb_remote_image_spec, sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("process is invalid");
  }
  return LLDB_INVALID_IMAGE_TOKEN;
}

uint32_t SBProcess::LoadImageUsingPaths(const lldb::SBFileSpec &image_spec,
                                        SBStringList &paths,
                                        lldb::SBFileSpec &loaded_path,
                                        lldb::SBError &error) {
  LLDB_INSTRUMENT_VA(this, image_spec, paths, loaded_path, error);

  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());
      PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();
      size_t num_paths = paths.GetSize();
      std::vector<std::string> paths_vec;
      paths_vec.reserve(num_paths);
      for (size_t i = 0; i < num_paths; i++)
        paths_vec.push_back(paths.GetStringAtIndex(i));
      FileSpec loaded_spec;

      uint32_t token = platform_sp->LoadImageUsingPaths(
          process_sp.get(), *image_spec, paths_vec, error.ref(), &loaded_spec);
      if (token != LLDB_INVALID_IMAGE_TOKEN)
        loaded_path = loaded_spec;
      return token;
    } else {
      error.SetErrorString("process is running");
    }
  } else {
    error.SetErrorString("process is invalid");
  }

  return LLDB_INVALID_IMAGE_TOKEN;
}

lldb::SBError SBProcess::UnloadImage(uint32_t image_token) {
  LLDB_INSTRUMENT_VA(this, image_token);

  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      PlatformSP platform_sp = process_sp->GetTarget().GetPlatform();
      sb_error.SetError(
          platform_sp->UnloadImage(process_sp.get(), image_token));
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else
    sb_error.SetErrorString("invalid process");
  return sb_error;
}

lldb::SBError SBProcess::SendEventData(const char *event_data) {
  LLDB_INSTRUMENT_VA(this, event_data);

  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      sb_error.SetError(process_sp->SendEventData(event_data));
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else
    sb_error.SetErrorString("invalid process");
  return sb_error;
}

uint32_t SBProcess::GetNumExtendedBacktraceTypes() {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());
  if (process_sp && process_sp->GetSystemRuntime()) {
    SystemRuntime *runtime = process_sp->GetSystemRuntime();
    return runtime->GetExtendedBacktraceTypes().size();
  }
  return 0;
}

const char *SBProcess::GetExtendedBacktraceTypeAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  ProcessSP process_sp(GetSP());
  if (process_sp && process_sp->GetSystemRuntime()) {
    SystemRuntime *runtime = process_sp->GetSystemRuntime();
    const std::vector<ConstString> &names =
        runtime->GetExtendedBacktraceTypes();
    if (idx < names.size()) {
      return names[idx].AsCString();
    }
  }
  return nullptr;
}

SBThreadCollection SBProcess::GetHistoryThreads(addr_t addr) {
  LLDB_INSTRUMENT_VA(this, addr);

  ProcessSP process_sp(GetSP());
  SBThreadCollection threads;
  if (process_sp) {
    threads = SBThreadCollection(process_sp->GetHistoryThreads(addr));
  }
  return threads;
}

bool SBProcess::IsInstrumentationRuntimePresent(
    InstrumentationRuntimeType type) {
  LLDB_INSTRUMENT_VA(this, type);

  ProcessSP process_sp(GetSP());
  if (!process_sp)
    return false;

  std::lock_guard<std::recursive_mutex> guard(
      process_sp->GetTarget().GetAPIMutex());

  InstrumentationRuntimeSP runtime_sp =
      process_sp->GetInstrumentationRuntime(type);

  if (!runtime_sp.get())
    return false;

  return runtime_sp->IsActive();
}

lldb::SBError SBProcess::SaveCore(const char *file_name) {
  LLDB_INSTRUMENT_VA(this, file_name);
  SBSaveCoreOptions options;
  options.SetOutputFile(SBFileSpec(file_name));
  options.SetStyle(SaveCoreStyle::eSaveCoreFull);
  return SaveCore(options);
}

lldb::SBError SBProcess::SaveCore(const char *file_name,
                                  const char *flavor,
                                  SaveCoreStyle core_style) {
  LLDB_INSTRUMENT_VA(this, file_name, flavor, core_style);
  SBSaveCoreOptions options;
  options.SetOutputFile(SBFileSpec(file_name));
  options.SetStyle(core_style);
  SBError error = options.SetPluginName(flavor);
  if (error.Fail())
    return error;
  return SaveCore(options);
}

lldb::SBError SBProcess::SaveCore(SBSaveCoreOptions &options) {

  LLDB_INSTRUMENT_VA(this, options);

  lldb::SBError error;
  ProcessSP process_sp(GetSP());
  if (!process_sp) {
    error.SetErrorString("SBProcess is invalid");
    return error;
  }

  std::lock_guard<std::recursive_mutex> guard(
      process_sp->GetTarget().GetAPIMutex());

  if (process_sp->GetState() != eStateStopped) {
    error.SetErrorString("the process is not stopped");
    return error;
  }

  error.ref() = PluginManager::SaveCore(process_sp, options.ref());

  return error;
}

lldb::SBError
SBProcess::GetMemoryRegionInfo(lldb::addr_t load_addr,
                               SBMemoryRegionInfo &sb_region_info) {
  LLDB_INSTRUMENT_VA(this, load_addr, sb_region_info);

  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());

      sb_error.ref() =
          process_sp->GetMemoryRegionInfo(load_addr, sb_region_info.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return sb_error;
}

lldb::SBMemoryRegionInfoList SBProcess::GetMemoryRegions() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBMemoryRegionInfoList sb_region_list;

  ProcessSP process_sp(GetSP());
  Process::StopLocker stop_locker;
  if (process_sp && stop_locker.TryLock(&process_sp->GetRunLock())) {
    std::lock_guard<std::recursive_mutex> guard(
        process_sp->GetTarget().GetAPIMutex());

    process_sp->GetMemoryRegions(sb_region_list.ref());
  }

  return sb_region_list;
}

lldb::SBProcessInfo SBProcess::GetProcessInfo() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBProcessInfo sb_proc_info;
  ProcessSP process_sp(GetSP());
  ProcessInstanceInfo proc_info;
  if (process_sp && process_sp->GetProcessInfo(proc_info)) {
    sb_proc_info.SetProcessInfo(proc_info);
  }
  return sb_proc_info;
}

lldb::SBFileSpec SBProcess::GetCoreFile() {
  LLDB_INSTRUMENT_VA(this);

  ProcessSP process_sp(GetSP());
  FileSpec core_file;
  if (process_sp) {
    core_file = process_sp->GetCoreFile();
  }
  return SBFileSpec(core_file);
}

addr_t SBProcess::GetAddressMask(AddressMaskType type,
                                 AddressMaskRange addr_range) {
  LLDB_INSTRUMENT_VA(this, type, addr_range);

  if (ProcessSP process_sp = GetSP()) {
    switch (type) {
    case eAddressMaskTypeCode:
      if (addr_range == eAddressMaskRangeHigh)
        return process_sp->GetHighmemCodeAddressMask();
      else
        return process_sp->GetCodeAddressMask();
    case eAddressMaskTypeData:
      if (addr_range == eAddressMaskRangeHigh)
        return process_sp->GetHighmemDataAddressMask();
      else
        return process_sp->GetDataAddressMask();
    case eAddressMaskTypeAny:
      if (addr_range == eAddressMaskRangeHigh)
        return process_sp->GetHighmemDataAddressMask();
      else
        return process_sp->GetDataAddressMask();
    }
  }
  return LLDB_INVALID_ADDRESS_MASK;
}

void SBProcess::SetAddressMask(AddressMaskType type, addr_t mask,
                               AddressMaskRange addr_range) {
  LLDB_INSTRUMENT_VA(this, type, mask, addr_range);

  if (ProcessSP process_sp = GetSP()) {
    switch (type) {
    case eAddressMaskTypeCode:
      if (addr_range == eAddressMaskRangeAll) {
        process_sp->SetCodeAddressMask(mask);
        process_sp->SetHighmemCodeAddressMask(mask);
      } else if (addr_range == eAddressMaskRangeHigh) {
        process_sp->SetHighmemCodeAddressMask(mask);
      } else {
        process_sp->SetCodeAddressMask(mask);
      }
      break;
    case eAddressMaskTypeData:
      if (addr_range == eAddressMaskRangeAll) {
        process_sp->SetDataAddressMask(mask);
        process_sp->SetHighmemDataAddressMask(mask);
      } else if (addr_range == eAddressMaskRangeHigh) {
        process_sp->SetHighmemDataAddressMask(mask);
      } else {
        process_sp->SetDataAddressMask(mask);
      }
      break;
    case eAddressMaskTypeAll:
      if (addr_range == eAddressMaskRangeAll) {
        process_sp->SetCodeAddressMask(mask);
        process_sp->SetDataAddressMask(mask);
        process_sp->SetHighmemCodeAddressMask(mask);
        process_sp->SetHighmemDataAddressMask(mask);
      } else if (addr_range == eAddressMaskRangeHigh) {
        process_sp->SetHighmemCodeAddressMask(mask);
        process_sp->SetHighmemDataAddressMask(mask);
      } else {
        process_sp->SetCodeAddressMask(mask);
        process_sp->SetDataAddressMask(mask);
      }
      break;
    }
  }
}

void SBProcess::SetAddressableBits(AddressMaskType type, uint32_t num_bits,
                                   AddressMaskRange addr_range) {
  LLDB_INSTRUMENT_VA(this, type, num_bits, addr_range);

  SetAddressMask(type, AddressableBits::AddressableBitToMask(num_bits),
                 addr_range);
}

addr_t SBProcess::FixAddress(addr_t addr, AddressMaskType type) {
  LLDB_INSTRUMENT_VA(this, addr, type);

  if (ProcessSP process_sp = GetSP()) {
    if (type == eAddressMaskTypeAny)
      return process_sp->FixAnyAddress(addr);
    else if (type == eAddressMaskTypeData)
      return process_sp->FixDataAddress(addr);
    else if (type == eAddressMaskTypeCode)
      return process_sp->FixCodeAddress(addr);
  }
  return addr;
}

lldb::addr_t SBProcess::AllocateMemory(size_t size, uint32_t permissions,
                                       lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, size, permissions, sb_error);

  lldb::addr_t addr = LLDB_INVALID_ADDRESS;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      addr = process_sp->AllocateMemory(size, permissions, sb_error.ref());
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return addr;
}

lldb::SBError SBProcess::DeallocateMemory(lldb::addr_t ptr) {
  LLDB_INSTRUMENT_VA(this, ptr);

  lldb::SBError sb_error;
  ProcessSP process_sp(GetSP());
  if (process_sp) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process_sp->GetRunLock())) {
      std::lock_guard<std::recursive_mutex> guard(
          process_sp->GetTarget().GetAPIMutex());
      Status error = process_sp->DeallocateMemory(ptr);
      sb_error.SetError(error);
    } else {
      sb_error.SetErrorString("process is running");
    }
  } else {
    sb_error.SetErrorString("SBProcess is invalid");
  }
  return sb_error;
}

lldb::SBScriptObject SBProcess::GetScriptedImplementation() {
  LLDB_INSTRUMENT_VA(this);
  ProcessSP process_sp(GetSP());
  return lldb::SBScriptObject((process_sp) ? process_sp->GetImplementation()
                                           : nullptr,
                              eScriptLanguageDefault);
}
