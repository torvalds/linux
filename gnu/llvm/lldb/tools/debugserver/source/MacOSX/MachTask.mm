//===-- MachTask.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//----------------------------------------------------------------------
//
//  MachTask.cpp
//  debugserver
//
//  Created by Greg Clayton on 12/5/08.
//
//===----------------------------------------------------------------------===//

#include "MachTask.h"

// C Includes

#include <mach-o/dyld_images.h>
#include <mach/mach_vm.h>
#import <sys/sysctl.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

// C++ Includes
#include <iomanip>
#include <sstream>

// Other libraries and framework includes
// Project includes
#include "CFUtils.h"
#include "DNB.h"
#include "DNBDataRef.h"
#include "DNBError.h"
#include "DNBLog.h"
#include "MachProcess.h"

#ifdef WITH_SPRINGBOARD

#include <CoreFoundation/CoreFoundation.h>
#include <SpringBoardServices/SBSWatchdogAssertion.h>
#include <SpringBoardServices/SpringBoardServer.h>

#endif

#ifdef WITH_BKS
extern "C" {
#import <BackBoardServices/BKSWatchdogAssertion.h>
#import <BackBoardServices/BackBoardServices.h>
#import <Foundation/Foundation.h>
}
#endif

#include <AvailabilityMacros.h>

#ifdef LLDB_ENERGY
#include <mach/mach_time.h>
#include <pmenergy.h>
#include <pmsample.h>
#endif

extern "C" int
proc_get_cpumon_params(pid_t pid, int *percentage,
                       int *interval); // <libproc_internal.h> SPI

//----------------------------------------------------------------------
// MachTask constructor
//----------------------------------------------------------------------
MachTask::MachTask(MachProcess *process)
    : m_process(process), m_task(TASK_NULL), m_vm_memory(),
      m_exception_thread(0), m_exception_port(MACH_PORT_NULL),
      m_exec_will_be_suspended(false), m_do_double_resume(false) {
  memset(&m_exc_port_info, 0, sizeof(m_exc_port_info));
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
MachTask::~MachTask() { Clear(); }

//----------------------------------------------------------------------
// MachTask::Suspend
//----------------------------------------------------------------------
kern_return_t MachTask::Suspend() {
  DNBError err;
  task_t task = TaskPort();
  err = ::task_suspend(task);
  if (DNBLogCheckLogBit(LOG_TASK) || err.Fail())
    err.LogThreaded("::task_suspend ( target_task = 0x%4.4x )", task);
  return err.Status();
}

//----------------------------------------------------------------------
// MachTask::Resume
//----------------------------------------------------------------------
kern_return_t MachTask::Resume() {
  struct task_basic_info task_info;
  task_t task = TaskPort();
  if (task == TASK_NULL)
    return KERN_INVALID_ARGUMENT;

  DNBError err;
  err = BasicInfo(task, &task_info);

  if (err.Success()) {
    if (m_do_double_resume && task_info.suspend_count == 2) {
      err = ::task_resume(task);
      if (DNBLogCheckLogBit(LOG_TASK) || err.Fail())
        err.LogThreaded("::task_resume double-resume after exec-start-stopped "
                        "( target_task = 0x%4.4x )", task);
    }
    m_do_double_resume = false;
      
    // task_resume isn't counted like task_suspend calls are, are, so if the
    // task is not suspended, don't try and resume it since it is already
    // running
    if (task_info.suspend_count > 0) {
      err = ::task_resume(task);
      if (DNBLogCheckLogBit(LOG_TASK) || err.Fail())
        err.LogThreaded("::task_resume ( target_task = 0x%4.4x )", task);
    }
  }
  return err.Status();
}

//----------------------------------------------------------------------
// MachTask::ExceptionPort
//----------------------------------------------------------------------
mach_port_t MachTask::ExceptionPort() const { return m_exception_port; }

//----------------------------------------------------------------------
// MachTask::ExceptionPortIsValid
//----------------------------------------------------------------------
bool MachTask::ExceptionPortIsValid() const {
  return MACH_PORT_VALID(m_exception_port);
}

//----------------------------------------------------------------------
// MachTask::Clear
//----------------------------------------------------------------------
void MachTask::Clear() {
  // Do any cleanup needed for this task
  if (m_exception_thread)
    ShutDownExcecptionThread();
  m_task = TASK_NULL;
  m_exception_thread = 0;
  m_exception_port = MACH_PORT_NULL;
  m_exec_will_be_suspended = false;
  m_do_double_resume = false;
}

//----------------------------------------------------------------------
// MachTask::SaveExceptionPortInfo
//----------------------------------------------------------------------
kern_return_t MachTask::SaveExceptionPortInfo() {
  return m_exc_port_info.Save(TaskPort());
}

//----------------------------------------------------------------------
// MachTask::RestoreExceptionPortInfo
//----------------------------------------------------------------------
kern_return_t MachTask::RestoreExceptionPortInfo() {
  return m_exc_port_info.Restore(TaskPort());
}

//----------------------------------------------------------------------
// MachTask::ReadMemory
//----------------------------------------------------------------------
nub_size_t MachTask::ReadMemory(nub_addr_t addr, nub_size_t size, void *buf) {
  nub_size_t n = 0;
  task_t task = TaskPort();
  if (task != TASK_NULL) {
    n = m_vm_memory.Read(task, addr, buf, size);

    DNBLogThreadedIf(LOG_MEMORY, "MachTask::ReadMemory ( addr = 0x%8.8llx, "
                                 "size = %llu, buf = %p) => %llu bytes read",
                     (uint64_t)addr, (uint64_t)size, buf, (uint64_t)n);
    if (DNBLogCheckLogBit(LOG_MEMORY_DATA_LONG) ||
        (DNBLogCheckLogBit(LOG_MEMORY_DATA_SHORT) && size <= 8)) {
      DNBDataRef data((uint8_t *)buf, n, false);
      data.Dump(0, static_cast<DNBDataRef::offset_t>(n), addr,
                DNBDataRef::TypeUInt8, 16);
    }
  }
  return n;
}

//----------------------------------------------------------------------
// MachTask::WriteMemory
//----------------------------------------------------------------------
nub_size_t MachTask::WriteMemory(nub_addr_t addr, nub_size_t size,
                                 const void *buf) {
  nub_size_t n = 0;
  task_t task = TaskPort();
  if (task != TASK_NULL) {
    n = m_vm_memory.Write(task, addr, buf, size);
    DNBLogThreadedIf(LOG_MEMORY, "MachTask::WriteMemory ( addr = 0x%8.8llx, "
                                 "size = %llu, buf = %p) => %llu bytes written",
                     (uint64_t)addr, (uint64_t)size, buf, (uint64_t)n);
    if (DNBLogCheckLogBit(LOG_MEMORY_DATA_LONG) ||
        (DNBLogCheckLogBit(LOG_MEMORY_DATA_SHORT) && size <= 8)) {
      DNBDataRef data((const uint8_t *)buf, n, false);
      data.Dump(0, static_cast<DNBDataRef::offset_t>(n), addr,
                DNBDataRef::TypeUInt8, 16);
    }
  }
  return n;
}

//----------------------------------------------------------------------
// MachTask::MemoryRegionInfo
//----------------------------------------------------------------------
int MachTask::GetMemoryRegionInfo(nub_addr_t addr, DNBRegionInfo *region_info) {
  task_t task = TaskPort();
  if (task == TASK_NULL)
    return -1;

  int ret = m_vm_memory.GetMemoryRegionInfo(task, addr, region_info);
  DNBLogThreadedIf(LOG_MEMORY, "MachTask::MemoryRegionInfo ( addr = 0x%8.8llx "
                               ") => %i  (start = 0x%8.8llx, size = 0x%8.8llx, "
                               "permissions = %u)",
                   (uint64_t)addr, ret, (uint64_t)region_info->addr,
                   (uint64_t)region_info->size, region_info->permissions);
  return ret;
}

#define TIME_VALUE_TO_TIMEVAL(a, r)                                            \
  do {                                                                         \
    (r)->tv_sec = (a)->seconds;                                                \
    (r)->tv_usec = (a)->microseconds;                                          \
  } while (0)

// We should consider moving this into each MacThread.
static void get_threads_profile_data(DNBProfileDataScanType scanType,
                                     task_t task, nub_process_t pid,
                                     std::vector<uint64_t> &threads_id,
                                     std::vector<std::string> &threads_name,
                                     std::vector<uint64_t> &threads_used_usec) {
  kern_return_t kr;
  thread_act_array_t threads;
  mach_msg_type_number_t tcnt;

  kr = task_threads(task, &threads, &tcnt);
  if (kr != KERN_SUCCESS)
    return;

  for (mach_msg_type_number_t i = 0; i < tcnt; i++) {
    thread_identifier_info_data_t identifier_info;
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    kr = ::thread_info(threads[i], THREAD_IDENTIFIER_INFO,
                       (thread_info_t)&identifier_info, &count);
    if (kr != KERN_SUCCESS)
      continue;

    thread_basic_info_data_t basic_info;
    count = THREAD_BASIC_INFO_COUNT;
    kr = ::thread_info(threads[i], THREAD_BASIC_INFO,
                       (thread_info_t)&basic_info, &count);
    if (kr != KERN_SUCCESS)
      continue;

    if ((basic_info.flags & TH_FLAGS_IDLE) == 0) {
      nub_thread_t tid =
          MachThread::GetGloballyUniqueThreadIDForMachPortID(threads[i]);
      threads_id.push_back(tid);

      if ((scanType & eProfileThreadName) &&
          (identifier_info.thread_handle != 0)) {
        struct proc_threadinfo proc_threadinfo;
        int len = ::proc_pidinfo(pid, PROC_PIDTHREADINFO,
                                 identifier_info.thread_handle,
                                 &proc_threadinfo, PROC_PIDTHREADINFO_SIZE);
        if (len && proc_threadinfo.pth_name[0]) {
          threads_name.push_back(proc_threadinfo.pth_name);
        } else {
          threads_name.push_back("");
        }
      } else {
        threads_name.push_back("");
      }
      struct timeval tv;
      struct timeval thread_tv;
      TIME_VALUE_TO_TIMEVAL(&basic_info.user_time, &thread_tv);
      TIME_VALUE_TO_TIMEVAL(&basic_info.system_time, &tv);
      timeradd(&thread_tv, &tv, &thread_tv);
      uint64_t used_usec = thread_tv.tv_sec * 1000000ULL + thread_tv.tv_usec;
      threads_used_usec.push_back(used_usec);
    }

    mach_port_deallocate(mach_task_self(), threads[i]);
  }
  mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)(uintptr_t)threads,
                     tcnt * sizeof(*threads));
}

#define RAW_HEXBASE std::setfill('0') << std::hex << std::right
#define DECIMAL std::dec << std::setfill(' ')
std::string MachTask::GetProfileData(DNBProfileDataScanType scanType) {
  std::string result;

  static int32_t numCPU = -1;
  struct host_cpu_load_info host_info;
  if (scanType & eProfileHostCPU) {
    int32_t mib[] = {CTL_HW, HW_AVAILCPU};
    size_t len = sizeof(numCPU);
    if (numCPU == -1) {
      if (sysctl(mib, sizeof(mib) / sizeof(int32_t), &numCPU, &len, NULL, 0) !=
          0)
        return result;
    }

    mach_port_t localHost = mach_host_self();
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    kern_return_t kr = host_statistics(localHost, HOST_CPU_LOAD_INFO,
                                       (host_info_t)&host_info, &count);
    if (kr != KERN_SUCCESS)
      return result;
  }

  task_t task = TaskPort();
  if (task == TASK_NULL)
    return result;

  pid_t pid = m_process->ProcessID();

  struct task_basic_info task_info;
  DNBError err;
  err = BasicInfo(task, &task_info);

  if (!err.Success())
    return result;

  uint64_t elapsed_usec = 0;
  uint64_t task_used_usec = 0;
  if (scanType & eProfileCPU) {
    // Get current used time.
    struct timeval current_used_time;
    struct timeval tv;
    TIME_VALUE_TO_TIMEVAL(&task_info.user_time, &current_used_time);
    TIME_VALUE_TO_TIMEVAL(&task_info.system_time, &tv);
    timeradd(&current_used_time, &tv, &current_used_time);
    task_used_usec =
        current_used_time.tv_sec * 1000000ULL + current_used_time.tv_usec;

    struct timeval current_elapsed_time;
    int res = gettimeofday(&current_elapsed_time, NULL);
    if (res == 0) {
      elapsed_usec = current_elapsed_time.tv_sec * 1000000ULL +
                     current_elapsed_time.tv_usec;
    }
  }

  std::vector<uint64_t> threads_id;
  std::vector<std::string> threads_name;
  std::vector<uint64_t> threads_used_usec;

  if (scanType & eProfileThreadsCPU) {
    get_threads_profile_data(scanType, task, pid, threads_id, threads_name,
                             threads_used_usec);
  }

  vm_statistics64_data_t vminfo;
  uint64_t physical_memory = 0;
  uint64_t anonymous = 0;
  uint64_t phys_footprint = 0;
  uint64_t memory_cap = 0;
  if (m_vm_memory.GetMemoryProfile(scanType, task, task_info,
                                   m_process->GetCPUType(), pid, vminfo,
                                   physical_memory, anonymous,
                                   phys_footprint, memory_cap)) {
    std::ostringstream profile_data_stream;

    if (scanType & eProfileHostCPU) {
      profile_data_stream << "num_cpu:" << numCPU << ';';
      profile_data_stream << "host_user_ticks:"
                          << host_info.cpu_ticks[CPU_STATE_USER] << ';';
      profile_data_stream << "host_sys_ticks:"
                          << host_info.cpu_ticks[CPU_STATE_SYSTEM] << ';';
      profile_data_stream << "host_idle_ticks:"
                          << host_info.cpu_ticks[CPU_STATE_IDLE] << ';';
    }

    if (scanType & eProfileCPU) {
      profile_data_stream << "elapsed_usec:" << elapsed_usec << ';';
      profile_data_stream << "task_used_usec:" << task_used_usec << ';';
    }

    if (scanType & eProfileThreadsCPU) {
      const size_t num_threads = threads_id.size();
      for (size_t i = 0; i < num_threads; i++) {
        profile_data_stream << "thread_used_id:" << std::hex << threads_id[i]
                            << std::dec << ';';
        profile_data_stream << "thread_used_usec:" << threads_used_usec[i]
                            << ';';

        if (scanType & eProfileThreadName) {
          profile_data_stream << "thread_used_name:";
          const size_t len = threads_name[i].size();
          if (len) {
            const char *thread_name = threads_name[i].c_str();
            // Make sure that thread name doesn't interfere with our delimiter.
            profile_data_stream << RAW_HEXBASE << std::setw(2);
            const uint8_t *ubuf8 = (const uint8_t *)(thread_name);
            for (size_t j = 0; j < len; j++) {
              profile_data_stream << (uint32_t)(ubuf8[j]);
            }
            // Reset back to DECIMAL.
            profile_data_stream << DECIMAL;
          }
          profile_data_stream << ';';
        }
      }
    }

    if (scanType & eProfileHostMemory)
      profile_data_stream << "total:" << physical_memory << ';';

    if (scanType & eProfileMemory) {
      static vm_size_t pagesize = vm_kernel_page_size;

      // This mimicks Activity Monitor.
      uint64_t total_used_count =
          (physical_memory / pagesize) -
          (vminfo.free_count - vminfo.speculative_count) -
          vminfo.external_page_count - vminfo.purgeable_count;
      profile_data_stream << "used:" << total_used_count * pagesize << ';';

      if (scanType & eProfileMemoryAnonymous) {
        profile_data_stream << "anonymous:" << anonymous << ';';
      }
      
      profile_data_stream << "phys_footprint:" << phys_footprint << ';';
    }
    
    if (scanType & eProfileMemoryCap) {
      profile_data_stream << "mem_cap:" << memory_cap << ';';
    }
    
#ifdef LLDB_ENERGY
    if (scanType & eProfileEnergy) {
      struct rusage_info_v2 info;
      int rc = proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t *)&info);
      if (rc == 0) {
        uint64_t now = mach_absolute_time();
        pm_task_energy_data_t pm_energy;
        memset(&pm_energy, 0, sizeof(pm_energy));
        /*
         * Disable most features of pm_sample_pid. It will gather
         * network/GPU/WindowServer information; fill in the rest.
         */
        pm_sample_task_and_pid(task, pid, &pm_energy, now,
                               PM_SAMPLE_ALL & ~PM_SAMPLE_NAME &
                                   ~PM_SAMPLE_INTERVAL & ~PM_SAMPLE_CPU &
                                   ~PM_SAMPLE_DISK);
        pm_energy.sti.total_user = info.ri_user_time;
        pm_energy.sti.total_system = info.ri_system_time;
        pm_energy.sti.task_interrupt_wakeups = info.ri_interrupt_wkups;
        pm_energy.sti.task_platform_idle_wakeups = info.ri_pkg_idle_wkups;
        pm_energy.diskio_bytesread = info.ri_diskio_bytesread;
        pm_energy.diskio_byteswritten = info.ri_diskio_byteswritten;
        pm_energy.pageins = info.ri_pageins;

        uint64_t total_energy =
            (uint64_t)(pm_energy_impact(&pm_energy) * NSEC_PER_SEC);
        // uint64_t process_age = now - info.ri_proc_start_abstime;
        // uint64_t avg_energy = 100.0 * (double)total_energy /
        // (double)process_age;

        profile_data_stream << "energy:" << total_energy << ';';
      }
    }
#endif

    if (scanType & eProfileEnergyCPUCap) {
      int percentage = -1;
      int interval = -1;
      int result = proc_get_cpumon_params(pid, &percentage, &interval);
      if ((result == 0) && (percentage >= 0) && (interval >= 0)) {
        profile_data_stream << "cpu_cap_p:" << percentage << ';';
        profile_data_stream << "cpu_cap_t:" << interval << ';';
      }
    }

    profile_data_stream << "--end--;";

    result = profile_data_stream.str();
  }

  return result;
}

//----------------------------------------------------------------------
// MachTask::TaskPortForProcessID
//----------------------------------------------------------------------
task_t MachTask::TaskPortForProcessID(DNBError &err, bool force) {
  if (((m_task == TASK_NULL) || force) && m_process != NULL)
    m_task = MachTask::TaskPortForProcessID(m_process->ProcessID(), err);
  return m_task;
}

//----------------------------------------------------------------------
// MachTask::TaskPortForProcessID
//----------------------------------------------------------------------
task_t MachTask::TaskPortForProcessID(pid_t pid, DNBError &err,
                                      uint32_t num_retries,
                                      uint32_t usec_interval) {
  if (pid != INVALID_NUB_PROCESS) {
    DNBError err;
    mach_port_t task_self = mach_task_self();
    task_t task = TASK_NULL;
    for (uint32_t i = 0; i < num_retries; i++) {
      DNBLog("[LaunchAttach] (%d) about to task_for_pid(%d)", getpid(), pid);
      err = ::task_for_pid(task_self, pid, &task);

      if (DNBLogCheckLogBit(LOG_TASK) || err.Fail()) {
        char str[1024];
        ::snprintf(str, sizeof(str), "::task_for_pid ( target_tport = 0x%4.4x, "
                                     "pid = %d, &task ) => err = 0x%8.8x (%s)",
                   task_self, pid, err.Status(),
                   err.AsString() ? err.AsString() : "success");
        if (err.Fail()) {
          err.SetErrorString(str);
          DNBLogError(
              "[LaunchAttach] MachTask::TaskPortForProcessID task_for_pid(%d) "
              "failed: %s",
              pid, str);
        }
        err.LogThreaded(str);
      }

      if (err.Success()) {
        DNBLog("[LaunchAttach] (%d) successfully task_for_pid(%d)'ed", getpid(),
               pid);
        return task;
      }

      // Sleep a bit and try again
      ::usleep(usec_interval);
    }
  }
  return TASK_NULL;
}

//----------------------------------------------------------------------
// MachTask::BasicInfo
//----------------------------------------------------------------------
kern_return_t MachTask::BasicInfo(struct task_basic_info *info) {
  return BasicInfo(TaskPort(), info);
}

//----------------------------------------------------------------------
// MachTask::BasicInfo
//----------------------------------------------------------------------
kern_return_t MachTask::BasicInfo(task_t task, struct task_basic_info *info) {
  if (info == NULL)
    return KERN_INVALID_ARGUMENT;

  DNBError err;
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  err = ::task_info(task, TASK_BASIC_INFO, (task_info_t)info, &count);
  const bool log_process = DNBLogCheckLogBit(LOG_TASK);
  if (log_process || err.Fail())
    err.LogThreaded("::task_info ( target_task = 0x%4.4x, flavor = "
                    "TASK_BASIC_INFO, task_info_out => %p, task_info_outCnt => "
                    "%u )",
                    task, info, count);
  if (DNBLogCheckLogBit(LOG_TASK) && DNBLogCheckLogBit(LOG_VERBOSE) &&
      err.Success()) {
    float user = (float)info->user_time.seconds +
                 (float)info->user_time.microseconds / 1000000.0f;
    float system = (float)info->user_time.seconds +
                   (float)info->user_time.microseconds / 1000000.0f;
    DNBLogThreaded("task_basic_info = { suspend_count = %i, virtual_size = "
                   "0x%8.8llx, resident_size = 0x%8.8llx, user_time = %f, "
                   "system_time = %f }",
                   info->suspend_count, (uint64_t)info->virtual_size,
                   (uint64_t)info->resident_size, user, system);
  }
  return err.Status();
}

//----------------------------------------------------------------------
// MachTask::IsValid
//
// Returns true if a task is a valid task port for a current process.
//----------------------------------------------------------------------
bool MachTask::IsValid() const { return MachTask::IsValid(TaskPort()); }

//----------------------------------------------------------------------
// MachTask::IsValid
//
// Returns true if a task is a valid task port for a current process.
//----------------------------------------------------------------------
bool MachTask::IsValid(task_t task) {
  if (task != TASK_NULL) {
    struct task_basic_info task_info;
    return BasicInfo(task, &task_info) == KERN_SUCCESS;
  }
  return false;
}

bool MachTask::StartExceptionThread(
        const RNBContext::IgnoredExceptions &ignored_exceptions, 
        DNBError &err) {
  DNBLogThreadedIf(LOG_EXCEPTIONS, "MachTask::%s ( )", __FUNCTION__);

  task_t task = TaskPortForProcessID(err);
  if (MachTask::IsValid(task)) {
    // Got the mach port for the current process
    mach_port_t task_self = mach_task_self();

    // Allocate an exception port that we will use to track our child process
    err = ::mach_port_allocate(task_self, MACH_PORT_RIGHT_RECEIVE,
                               &m_exception_port);
    if (err.Fail())
      return false;

    // Add the ability to send messages on the new exception port
    err = ::mach_port_insert_right(task_self, m_exception_port,
                                   m_exception_port, MACH_MSG_TYPE_MAKE_SEND);
    if (err.Fail())
      return false;

    // Save the original state of the exception ports for our child process
    SaveExceptionPortInfo();

    // We weren't able to save the info for our exception ports, we must stop...
    if (m_exc_port_info.mask == 0) {
      err.SetErrorString("failed to get exception port info");
      return false;
    }

    if (!ignored_exceptions.empty()) {
      for (exception_mask_t mask : ignored_exceptions)
        m_exc_port_info.mask = m_exc_port_info.mask & ~mask;
    }

    // Set the ability to get all exceptions on this port
    err = ::task_set_exception_ports(
        task, m_exc_port_info.mask, m_exception_port,
        EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);
    if (DNBLogCheckLogBit(LOG_EXCEPTIONS) || err.Fail()) {
      err.LogThreaded("::task_set_exception_ports ( task = 0x%4.4x, "
                      "exception_mask = 0x%8.8x, new_port = 0x%4.4x, behavior "
                      "= 0x%8.8x, new_flavor = 0x%8.8x )",
                      task, m_exc_port_info.mask, m_exception_port,
                      (EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES),
                      THREAD_STATE_NONE);
    }

    if (err.Fail())
      return false;

    // Create the exception thread
    err = ::pthread_create(&m_exception_thread, NULL, MachTask::ExceptionThread,
                           this);
    return err.Success();
  } else {
    DNBLogError("MachTask::%s (): task invalid, exception thread start failed.",
                __FUNCTION__);
  }
  return false;
}

kern_return_t MachTask::ShutDownExcecptionThread() {
  DNBError err;

  err = RestoreExceptionPortInfo();

  // NULL our exception port and let our exception thread exit
  mach_port_t exception_port = m_exception_port;
  m_exception_port = 0;

  err.SetError(::pthread_cancel(m_exception_thread), DNBError::POSIX);
  if (DNBLogCheckLogBit(LOG_TASK) || err.Fail())
    err.LogThreaded("::pthread_cancel ( thread = %p )", m_exception_thread);

  err.SetError(::pthread_join(m_exception_thread, NULL), DNBError::POSIX);
  if (DNBLogCheckLogBit(LOG_TASK) || err.Fail())
    err.LogThreaded("::pthread_join ( thread = %p, value_ptr = NULL)",
                    m_exception_thread);

  // Deallocate our exception port that we used to track our child process
  mach_port_t task_self = mach_task_self();
  err = ::mach_port_deallocate(task_self, exception_port);
  if (DNBLogCheckLogBit(LOG_TASK) || err.Fail())
    err.LogThreaded("::mach_port_deallocate ( task = 0x%4.4x, name = 0x%4.4x )",
                    task_self, exception_port);

  m_exec_will_be_suspended = false;
  m_do_double_resume = false;

  return err.Status();
}

void *MachTask::ExceptionThread(void *arg) {
  if (arg == NULL)
    return NULL;

  MachTask *mach_task = (MachTask *)arg;
  MachProcess *mach_proc = mach_task->Process();
  DNBLogThreadedIf(LOG_EXCEPTIONS,
                   "MachTask::%s ( arg = %p ) starting thread...", __FUNCTION__,
                   arg);

#if defined(__APPLE__)
  pthread_setname_np("exception monitoring thread");
#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  struct sched_param thread_param;
  int thread_sched_policy;
  if (pthread_getschedparam(pthread_self(), &thread_sched_policy,
                            &thread_param) == 0) {
    thread_param.sched_priority = 47;
    pthread_setschedparam(pthread_self(), thread_sched_policy, &thread_param);
  }
#endif
#endif

  // We keep a count of the number of consecutive exceptions received so
  // we know to grab all exceptions without a timeout. We do this to get a
  // bunch of related exceptions on our exception port so we can process
  // then together. When we have multiple threads, we can get an exception
  // per thread and they will come in consecutively. The main loop in this
  // thread can stop periodically if needed to service things related to this
  // process.
  // flag set in the options, so we will wait forever for an exception on
  // our exception port. After we get one exception, we then will use the
  // MACH_RCV_TIMEOUT option with a zero timeout to grab all other current
  // exceptions for our process. After we have received the last pending
  // exception, we will get a timeout which enables us to then notify
  // our main thread that we have an exception bundle available. We then wait
  // for the main thread to tell this exception thread to start trying to get
  // exceptions messages again and we start again with a mach_msg read with
  // infinite timeout.
  uint32_t num_exceptions_received = 0;
  DNBError err;
  task_t task = mach_task->TaskPort();
  mach_msg_timeout_t periodic_timeout = 0;

#if defined(WITH_SPRINGBOARD) && !defined(WITH_BKS)
  mach_msg_timeout_t watchdog_elapsed = 0;
  mach_msg_timeout_t watchdog_timeout = 60 * 1000;
  pid_t pid = mach_proc->ProcessID();
  CFReleaser<SBSWatchdogAssertionRef> watchdog;

  if (mach_proc->ProcessUsingSpringBoard()) {
    // Request a renewal for every 60 seconds if we attached using SpringBoard
    watchdog.reset(::SBSWatchdogAssertionCreateForPID(NULL, pid, 60));
    DNBLogThreadedIf(
        LOG_TASK, "::SBSWatchdogAssertionCreateForPID (NULL, %4.4x, 60 ) => %p",
        pid, watchdog.get());

    if (watchdog.get()) {
      ::SBSWatchdogAssertionRenew(watchdog.get());

      CFTimeInterval watchdogRenewalInterval =
          ::SBSWatchdogAssertionGetRenewalInterval(watchdog.get());
      DNBLogThreadedIf(
          LOG_TASK,
          "::SBSWatchdogAssertionGetRenewalInterval ( %p ) => %g seconds",
          watchdog.get(), watchdogRenewalInterval);
      if (watchdogRenewalInterval > 0.0) {
        watchdog_timeout = (mach_msg_timeout_t)watchdogRenewalInterval * 1000;
        if (watchdog_timeout > 3000)
          watchdog_timeout -= 1000; // Give us a second to renew our timeout
        else if (watchdog_timeout > 1000)
          watchdog_timeout -=
              250; // Give us a quarter of a second to renew our timeout
      }
    }
    if (periodic_timeout == 0 || periodic_timeout > watchdog_timeout)
      periodic_timeout = watchdog_timeout;
  }
#endif // #if defined (WITH_SPRINGBOARD) && !defined (WITH_BKS)

#ifdef WITH_BKS
  CFReleaser<BKSWatchdogAssertionRef> watchdog;
  if (mach_proc->ProcessUsingBackBoard()) {
    pid_t pid = mach_proc->ProcessID();
    CFAllocatorRef alloc = kCFAllocatorDefault;
    watchdog.reset(::BKSWatchdogAssertionCreateForPID(alloc, pid));
  }
#endif // #ifdef WITH_BKS

  while (mach_task->ExceptionPortIsValid()) {
    ::pthread_testcancel();

    MachException::Message exception_message;

    if (num_exceptions_received > 0) {
      // No timeout, just receive as many exceptions as we can since we already
      // have one and we want
      // to get all currently available exceptions for this task
      err = exception_message.Receive(
          mach_task->ExceptionPort(),
          MACH_RCV_MSG | MACH_RCV_INTERRUPT | MACH_RCV_TIMEOUT, 1);
    } else if (periodic_timeout > 0) {
      // We need to stop periodically in this loop, so try and get a mach
      // message with a valid timeout (ms)
      err = exception_message.Receive(mach_task->ExceptionPort(),
                                      MACH_RCV_MSG | MACH_RCV_INTERRUPT |
                                          MACH_RCV_TIMEOUT,
                                      periodic_timeout);
    } else {
      // We don't need to parse all current exceptions or stop periodically,
      // just wait for an exception forever.
      err = exception_message.Receive(mach_task->ExceptionPort(),
                                      MACH_RCV_MSG | MACH_RCV_INTERRUPT, 0);
    }

    if (err.Status() == MACH_RCV_INTERRUPTED) {
      // If we have no task port we should exit this thread
      if (!mach_task->ExceptionPortIsValid()) {
        DNBLogThreadedIf(LOG_EXCEPTIONS, "thread cancelled...");
        break;
      }

      // Make sure our task is still valid
      if (MachTask::IsValid(task)) {
        // Task is still ok
        DNBLogThreadedIf(LOG_EXCEPTIONS,
                         "interrupted, but task still valid, continuing...");
        continue;
      } else {
        DNBLogThreadedIf(LOG_EXCEPTIONS, "task has exited...");
        mach_proc->SetState(eStateExited);
        // Our task has died, exit the thread.
        break;
      }
    } else if (err.Status() == MACH_RCV_TIMED_OUT) {
      if (num_exceptions_received > 0) {
        // We were receiving all current exceptions with a timeout of zero
        // it is time to go back to our normal looping mode
        num_exceptions_received = 0;

        // Notify our main thread we have a complete exception message
        // bundle available and get the possibly updated task port back
        // from the process in case we exec'ed and our task port changed
        task = mach_proc->ExceptionMessageBundleComplete();

        // in case we use a timeout value when getting exceptions...
        // Make sure our task is still valid
        if (MachTask::IsValid(task)) {
          // Task is still ok
          DNBLogThreadedIf(LOG_EXCEPTIONS, "got a timeout, continuing...");
          continue;
        } else {
          DNBLogThreadedIf(LOG_EXCEPTIONS, "task has exited...");
          mach_proc->SetState(eStateExited);
          // Our task has died, exit the thread.
          break;
        }
      }

#if defined(WITH_SPRINGBOARD) && !defined(WITH_BKS)
      if (watchdog.get()) {
        watchdog_elapsed += periodic_timeout;
        if (watchdog_elapsed >= watchdog_timeout) {
          DNBLogThreadedIf(LOG_TASK, "SBSWatchdogAssertionRenew ( %p )",
                           watchdog.get());
          ::SBSWatchdogAssertionRenew(watchdog.get());
          watchdog_elapsed = 0;
        }
      }
#endif
    } else if (err.Status() != KERN_SUCCESS) {
      DNBLogThreadedIf(LOG_EXCEPTIONS, "got some other error, do something "
                                       "about it??? nah, continuing for "
                                       "now...");
      // TODO: notify of error?
    } else {
      if (exception_message.CatchExceptionRaise(task)) {
        if (exception_message.state.task_port != task) {
          if (exception_message.state.IsValid()) {
            // We exec'ed and our task port changed on us.
            DNBLogThreadedIf(LOG_EXCEPTIONS,
                             "task port changed from 0x%4.4x to 0x%4.4x",
                             task, exception_message.state.task_port);
            task = exception_message.state.task_port;
            mach_task->TaskPortChanged(exception_message.state.task_port);
          }
        }
        ++num_exceptions_received;
        mach_proc->ExceptionMessageReceived(exception_message);
      }
    }
  }

#if defined(WITH_SPRINGBOARD) && !defined(WITH_BKS)
  if (watchdog.get()) {
    // TODO: change SBSWatchdogAssertionRelease to SBSWatchdogAssertionCancel
    // when we
    // all are up and running on systems that support it. The SBS framework has
    // a #define
    // that will forward SBSWatchdogAssertionRelease to
    // SBSWatchdogAssertionCancel for now
    // so it should still build either way.
    DNBLogThreadedIf(LOG_TASK, "::SBSWatchdogAssertionRelease(%p)",
                     watchdog.get());
    ::SBSWatchdogAssertionRelease(watchdog.get());
  }
#endif // #if defined (WITH_SPRINGBOARD) && !defined (WITH_BKS)

  DNBLogThreadedIf(LOG_EXCEPTIONS, "MachTask::%s (%p): thread exiting...",
                   __FUNCTION__, arg);
  return NULL;
}

// So the TASK_DYLD_INFO used to just return the address of the all image infos
// as a single member called "all_image_info". Then someone decided it would be
// a good idea to rename this first member to "all_image_info_addr" and add a
// size member called "all_image_info_size". This of course can not be detected
// using code or #defines. So to hack around this problem, we define our own
// version of the TASK_DYLD_INFO structure so we can guarantee what is inside
// it.

struct hack_task_dyld_info {
  mach_vm_address_t all_image_info_addr;
  mach_vm_size_t all_image_info_size;
};

nub_addr_t MachTask::GetDYLDAllImageInfosAddress(DNBError &err) {
  struct hack_task_dyld_info dyld_info;
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  // Make sure that COUNT isn't bigger than our hacked up struct
  // hack_task_dyld_info.
  // If it is, then make COUNT smaller to match.
  if (count > (sizeof(struct hack_task_dyld_info) / sizeof(natural_t)))
    count = (sizeof(struct hack_task_dyld_info) / sizeof(natural_t));

  task_t task = TaskPortForProcessID(err);
  if (err.Success()) {
    err = ::task_info(task, TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
    if (err.Success()) {
      // We now have the address of the all image infos structure
      return dyld_info.all_image_info_addr;
    }
  }
  return INVALID_NUB_ADDRESS;
}

//----------------------------------------------------------------------
// MachTask::AllocateMemory
//----------------------------------------------------------------------
nub_addr_t MachTask::AllocateMemory(size_t size, uint32_t permissions) {
  mach_vm_address_t addr;
  task_t task = TaskPort();
  if (task == TASK_NULL)
    return INVALID_NUB_ADDRESS;

  DNBError err;
  err = ::mach_vm_allocate(task, &addr, size, TRUE);
  if (err.Status() == KERN_SUCCESS) {
    // Set the protections:
    vm_prot_t mach_prot = VM_PROT_NONE;
    if (permissions & eMemoryPermissionsReadable)
      mach_prot |= VM_PROT_READ;
    if (permissions & eMemoryPermissionsWritable)
      mach_prot |= VM_PROT_WRITE;
    if (permissions & eMemoryPermissionsExecutable)
      mach_prot |= VM_PROT_EXECUTE;

    err = ::mach_vm_protect(task, addr, size, 0, mach_prot);
    if (err.Status() == KERN_SUCCESS) {
      m_allocations.insert(std::make_pair(addr, size));
      return addr;
    }
    ::mach_vm_deallocate(task, addr, size);
  }
  return INVALID_NUB_ADDRESS;
}

//----------------------------------------------------------------------
// MachTask::DeallocateMemory
//----------------------------------------------------------------------
nub_bool_t MachTask::DeallocateMemory(nub_addr_t addr) {
  task_t task = TaskPort();
  if (task == TASK_NULL)
    return false;

  // We have to stash away sizes for the allocations...
  allocation_collection::iterator pos, end = m_allocations.end();
  for (pos = m_allocations.begin(); pos != end; pos++) {
    if ((*pos).first == addr) {
      size_t size = (*pos).second;
      m_allocations.erase(pos);
#define ALWAYS_ZOMBIE_ALLOCATIONS 0
      if (ALWAYS_ZOMBIE_ALLOCATIONS ||
          getenv("DEBUGSERVER_ZOMBIE_ALLOCATIONS")) {
        ::mach_vm_protect(task, addr, size, 0, VM_PROT_NONE);
        return true;
      } else
        return ::mach_vm_deallocate(task, addr, size) == KERN_SUCCESS;
    }
  }
  return false;
}

//----------------------------------------------------------------------
// MachTask::ClearAllocations
//----------------------------------------------------------------------
void MachTask::ClearAllocations() {
  m_allocations.clear();
}

void MachTask::TaskPortChanged(task_t task)
{
  m_task = task;

  // If we've just exec'd to a new process, and it
  // is started suspended, we'll need to do two
  // task_resume's to get the inferior process to
  // continue.
  if (m_exec_will_be_suspended)
    m_do_double_resume = true;
  else
    m_do_double_resume = false;
  m_exec_will_be_suspended = false;
}
