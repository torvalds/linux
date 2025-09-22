//===-- MachThread.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHTHREAD_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHTHREAD_H

#include <string>
#include <vector>

#include <libproc.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/signal.h>

#include "DNBArch.h"
#include "DNBRegisterInfo.h"
#include "MachException.h"
#include "PThreadCondition.h"
#include "PThreadMutex.h"

#include "ThreadInfo.h"

class DNBBreakpoint;
class MachProcess;
class MachThreadList;

class MachThread {
public:
  MachThread(MachProcess *process, bool is_64_bit,
             uint64_t unique_thread_id = 0, thread_t mach_port_number = 0);
  ~MachThread();

  MachProcess *Process() { return m_process; }
  const MachProcess *Process() const { return m_process; }
  nub_process_t ProcessID() const;
  void Dump(uint32_t index);
  uint64_t ThreadID() const { return m_unique_id; }
  thread_t MachPortNumber() const { return m_mach_port_number; }
  thread_t InferiorThreadID() const;

  uint32_t SequenceID() const { return m_seq_id; }
  static bool ThreadIDIsValid(
      uint64_t thread); // The 64-bit system-wide unique thread identifier
  static bool MachPortNumberIsValid(thread_t thread); // The mach port # for
                                                      // this thread in
                                                      // debugserver namespace
  void Resume(bool others_stopped);
  void Suspend();
  bool SetSuspendCountBeforeResume(bool others_stopped);
  bool RestoreSuspendCountAfterStop();

  bool GetRegisterState(int flavor, bool force);
  bool SetRegisterState(int flavor);
  uint64_t
  GetPC(uint64_t failValue = INVALID_NUB_ADDRESS); // Get program counter
  bool SetPC(uint64_t value);                      // Set program counter
  uint64_t GetSP(uint64_t failValue = INVALID_NUB_ADDRESS); // Get stack pointer

  DNBBreakpoint *CurrentBreakpoint();
  uint32_t EnableHardwareBreakpoint(const DNBBreakpoint *breakpoint,
                                    bool also_set_on_task);
  uint32_t EnableHardwareWatchpoint(const DNBBreakpoint *watchpoint,
                                    bool also_set_on_task);
  bool DisableHardwareBreakpoint(const DNBBreakpoint *breakpoint,
                                 bool also_set_on_task);
  bool DisableHardwareWatchpoint(const DNBBreakpoint *watchpoint,
                                 bool also_set_on_task);
  uint32_t NumSupportedHardwareWatchpoints() const;
  bool RollbackTransForHWP();
  bool FinishTransForHWP();

  nub_state_t GetState();
  void SetState(nub_state_t state);

  void ThreadWillResume(const DNBThreadResumeAction *thread_action,
                        bool others_stopped = false);
  bool ShouldStop(bool &step_more);
  bool IsStepping();
  bool ThreadDidStop();
  bool NotifyException(MachException::Data &exc);
  const MachException::Data &GetStopException() { return m_stop_exception; }

  nub_size_t GetNumRegistersInSet(nub_size_t regSet) const;
  const char *GetRegisterSetName(nub_size_t regSet) const;
  const DNBRegisterInfo *GetRegisterInfo(nub_size_t regSet,
                                         nub_size_t regIndex) const;
  void DumpRegisterState(nub_size_t regSet);
  const DNBRegisterSetInfo *GetRegisterSetInfo(nub_size_t *num_reg_sets) const;
  bool GetRegisterValue(uint32_t reg_set_idx, uint32_t reg_idx,
                        DNBRegisterValue *reg_value);
  bool SetRegisterValue(uint32_t reg_set_idx, uint32_t reg_idx,
                        const DNBRegisterValue *reg_value);
  nub_size_t GetRegisterContext(void *buf, nub_size_t buf_len);
  nub_size_t SetRegisterContext(const void *buf, nub_size_t buf_len);
  uint32_t SaveRegisterState();
  bool RestoreRegisterState(uint32_t save_id);

  void NotifyBreakpointChanged(const DNBBreakpoint *bp) {}

  bool IsUserReady();
  struct thread_basic_info *GetBasicInfo();
  const char *GetBasicInfoAsString() const;
  const char *GetName();

  DNBArchProtocol *GetArchProtocol() { return m_arch_up.get(); }

  ThreadInfo::QoS GetRequestedQoS(nub_addr_t tsd, uint64_t dti_qos_class_index);
  nub_addr_t GetPThreadT();
  nub_addr_t GetDispatchQueueT();
  nub_addr_t
  GetTSDAddressForThread(uint64_t plo_pthread_tsd_base_address_offset,
                         uint64_t plo_pthread_tsd_base_offset,
                         uint64_t plo_pthread_tsd_entry_size);

  static uint64_t GetGloballyUniqueThreadIDForMachPortID(thread_t mach_port_id);

protected:
  static bool GetBasicInfo(thread_t threadID,
                           struct thread_basic_info *basic_info);

  bool GetIdentifierInfo();

  //    const char *
  //    GetDispatchQueueName();
  //
  MachProcess *m_process; // The process that owns this thread
  uint64_t m_unique_id; // The globally unique ID for this thread (nub_thread_t)
  thread_t m_mach_port_number; // The mach port # for this thread in debugserver
                               // namesp.
  uint32_t m_seq_id;   // A Sequential ID that increments with each new thread
  nub_state_t m_state; // The state of our process
  PThreadMutex m_state_mutex;            // Multithreaded protection for m_state
  struct thread_basic_info m_basic_info; // Basic information for a thread used
                                         // to see if a thread is valid
  int32_t m_suspend_count; // The current suspend count > 0 means we have
                           // suspended m_suspendCount times,
  //                           < 0 means we have resumed it m_suspendCount
  //                           times.
  MachException::Data m_stop_exception; // The best exception that describes why
                                        // this thread is stopped
  std::unique_ptr<DNBArchProtocol>
      m_arch_up; // Arch specific information for register state and more
  const DNBRegisterSetInfo
      *m_reg_sets; // Register set information for this thread
  nub_size_t m_num_reg_sets;
  thread_identifier_info_data_t m_ident_info;
  struct proc_threadinfo m_proc_threadinfo;
  std::string m_dispatch_queue_name;
  bool m_is_64_bit;

  // qos_class_t _pthread_qos_class_decode(pthread_priority_t priority, int *,
  // unsigned long *);
  unsigned int (*m_pthread_qos_class_decode)(unsigned long priority, int *,
                                             unsigned long *);

private:
  friend class MachThreadList;
};

typedef std::shared_ptr<MachThread> MachThreadSP;

#endif
