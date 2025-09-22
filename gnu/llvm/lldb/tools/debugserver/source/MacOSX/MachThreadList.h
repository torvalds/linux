//===-- MachThreadList.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/19/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHTHREADLIST_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHTHREADLIST_H

#include "MachThread.h"
#include "ThreadInfo.h"

class DNBThreadResumeActions;

class MachThreadList {
public:
  MachThreadList();
  ~MachThreadList();

  void Clear();
  void Dump() const;
  bool GetRegisterValue(nub_thread_t tid, uint32_t set, uint32_t reg,
                        DNBRegisterValue *reg_value) const;
  bool SetRegisterValue(nub_thread_t tid, uint32_t set, uint32_t reg,
                        const DNBRegisterValue *reg_value) const;
  nub_size_t GetRegisterContext(nub_thread_t tid, void *buf, size_t buf_len);
  nub_size_t SetRegisterContext(nub_thread_t tid, const void *buf,
                                size_t buf_len);
  uint32_t SaveRegisterState(nub_thread_t tid);
  bool RestoreRegisterState(nub_thread_t tid, uint32_t save_id);
  const char *GetThreadInfo(nub_thread_t tid) const;
  void ProcessWillResume(MachProcess *process,
                         const DNBThreadResumeActions &thread_actions);
  uint32_t ProcessDidStop(MachProcess *process);
  bool NotifyException(MachException::Data &exc);
  bool ShouldStop(bool &step_more);
  const char *GetName(nub_thread_t tid);
  nub_state_t GetState(nub_thread_t tid);
  nub_thread_t SetCurrentThread(nub_thread_t tid);

  ThreadInfo::QoS GetRequestedQoS(nub_thread_t tid, nub_addr_t tsd,
                                  uint64_t dti_qos_class_index);
  nub_addr_t GetPThreadT(nub_thread_t tid);
  nub_addr_t GetDispatchQueueT(nub_thread_t tid);
  nub_addr_t
  GetTSDAddressForThread(nub_thread_t tid,
                         uint64_t plo_pthread_tsd_base_address_offset,
                         uint64_t plo_pthread_tsd_base_offset,
                         uint64_t plo_pthread_tsd_entry_size);

  bool GetThreadStoppedReason(nub_thread_t tid,
                              struct DNBThreadStopInfo *stop_info) const;
  void DumpThreadStoppedReason(nub_thread_t tid) const;
  bool GetIdentifierInfo(nub_thread_t tid,
                         thread_identifier_info_data_t *ident_info);
  nub_size_t NumThreads() const;
  nub_thread_t ThreadIDAtIndex(nub_size_t idx) const;
  nub_thread_t CurrentThreadID();
  void CurrentThread(MachThreadSP &threadSP);
  void NotifyBreakpointChanged(const DNBBreakpoint *bp);
  uint32_t EnableHardwareBreakpoint(const DNBBreakpoint *bp) const;
  bool DisableHardwareBreakpoint(const DNBBreakpoint *bp) const;
  uint32_t EnableHardwareWatchpoint(const DNBBreakpoint *wp) const;
  bool DisableHardwareWatchpoint(const DNBBreakpoint *wp) const;
  uint32_t NumSupportedHardwareWatchpoints() const;

  uint32_t GetThreadIndexForThreadStoppedWithSignal(const int signo) const;

  MachThreadSP GetThreadByID(nub_thread_t tid) const;

  MachThreadSP GetThreadByMachPortNumber(thread_t mach_port_number) const;
  nub_thread_t GetThreadIDByMachPortNumber(thread_t mach_port_number) const;
  thread_t GetMachPortNumberByThreadID(nub_thread_t globally_unique_id) const;

protected:
  typedef std::vector<MachThreadSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  enum class HardwareBreakpointAction {
    EnableWatchpoint,
    DisableWatchpoint,
    EnableBreakpoint,
    DisableBreakpoint,
  };

  uint32_t DoHardwareBreakpointAction(const DNBBreakpoint *bp,
                                      HardwareBreakpointAction action) const;

  uint32_t UpdateThreadList(MachProcess *process, bool update,
                            collection *num_threads = NULL);
  //  const_iterator  FindThreadByID (thread_t tid) const;

  collection m_threads;
  mutable PThreadMutex m_threads_mutex;
  MachThreadSP m_current_thread;
  bool m_is_64_bit;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHTHREADLIST_H
