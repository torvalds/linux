//===-- RNBContext.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 12/12/07.
//
//===----------------------------------------------------------------------===//

#include "RNBContext.h"

#include <sstream>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

#include "CFString.h"
#include "DNB.h"
#include "DNBLog.h"
#include "RNBRemote.h"
#include "MacOSX/MachException.h"

// Destructor
RNBContext::~RNBContext() { SetProcessID(INVALID_NUB_PROCESS); }

// RNBContext constructor

const char *RNBContext::EnvironmentAtIndex(size_t index) {
  if (index < m_env_vec.size())
    return m_env_vec[index].c_str();
  else
    return NULL;
}

static std::string GetEnvironmentKey(const std::string &env) {
  std::string key = env.substr(0, env.find('='));
  if (!key.empty() && key.back() == '=')
    key.pop_back();
  return key;
}

void RNBContext::PushEnvironmentIfNeeded(const char *arg) {
  if (!arg)
    return;
  std::string arg_key = GetEnvironmentKey(arg);

  for (const std::string &entry: m_env_vec) {
    if (arg_key == GetEnvironmentKey(entry))
      return;
  }
  m_env_vec.push_back(arg);
}

const char *RNBContext::ArgumentAtIndex(size_t index) {
  if (index < m_arg_vec.size())
    return m_arg_vec[index].c_str();
  else
    return NULL;
}

bool RNBContext::SetWorkingDirectory(const char *path) {
  struct stat working_directory_stat;
  if (::stat(path, &working_directory_stat) != 0) {
    m_working_directory.clear();
    return false;
  }
  m_working_directory.assign(path);
  return true;
}

void RNBContext::SetProcessID(nub_process_t pid) {
  // Delete and events we created
  if (m_pid != INVALID_NUB_PROCESS) {
    StopProcessStatusThread();
    // Unregister this context as a client of the process's events.
  }
  // Assign our new process ID
  m_pid = pid;

  if (pid != INVALID_NUB_PROCESS) {
    StartProcessStatusThread();
  }
}

void RNBContext::StartProcessStatusThread() {
  DNBLogThreadedIf(LOG_RNB_PROC, "RNBContext::%s called", __FUNCTION__);
  if ((m_events.GetEventBits() & event_proc_thread_running) == 0) {
    int err = ::pthread_create(&m_pid_pthread, NULL,
                               ThreadFunctionProcessStatus, this);
    if (err == 0) {
      // Our thread was successfully kicked off, wait for it to
      // set the started event so we can safely continue
      m_events.WaitForSetEvents(event_proc_thread_running);
      DNBLogThreadedIf(LOG_RNB_PROC, "RNBContext::%s thread got started!",
                       __FUNCTION__);
    } else {
      DNBLogThreadedIf(LOG_RNB_PROC,
                       "RNBContext::%s thread failed to start: err = %i",
                       __FUNCTION__, err);
      m_events.ResetEvents(event_proc_thread_running);
      m_events.SetEvents(event_proc_thread_exiting);
    }
  }
}

void RNBContext::StopProcessStatusThread() {
  DNBLogThreadedIf(LOG_RNB_PROC, "RNBContext::%s called", __FUNCTION__);
  if ((m_events.GetEventBits() & event_proc_thread_running) ==
      event_proc_thread_running) {
    struct timespec timeout_abstime;
    DNBTimer::OffsetTimeOfDay(&timeout_abstime, 2, 0);
    // Wait for 2 seconds for the rx thread to exit
    if (m_events.WaitForSetEvents(RNBContext::event_proc_thread_exiting,
                                  &timeout_abstime) ==
        RNBContext::event_proc_thread_exiting) {
      DNBLogThreadedIf(LOG_RNB_PROC,
                       "RNBContext::%s thread stopped as requeseted",
                       __FUNCTION__);
    } else {
      DNBLogThreadedIf(LOG_RNB_PROC,
                       "RNBContext::%s thread did not stop in 2 seconds...",
                       __FUNCTION__);
      // Kill the RX thread???
    }
  }
}

// This thread's sole purpose is to watch for any status changes in the
// child process.
void *RNBContext::ThreadFunctionProcessStatus(void *arg) {
  RNBRemoteSP remoteSP(g_remoteSP);
  RNBRemote *remote = remoteSP.get();
  if (remote == NULL)
    return NULL;
  RNBContext &ctx = remote->Context();

  nub_process_t pid = ctx.ProcessID();
  DNBLogThreadedIf(LOG_RNB_PROC,
                   "RNBContext::%s (arg=%p, pid=%4.4x): thread starting...",
                   __FUNCTION__, arg, pid);
  ctx.Events().SetEvents(RNBContext::event_proc_thread_running);

#if defined(__APPLE__)
  pthread_setname_np("child process status watcher thread");
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

  bool done = false;
  while (!done) {
    DNBLogThreadedIf(LOG_RNB_PROC,
                     "RNBContext::%s calling DNBProcessWaitForEvent(pid, "
                     "eEventProcessRunningStateChanged | "
                     "eEventProcessStoppedStateChanged | eEventStdioAvailable "
                     "| eEventProfileDataAvailable, true)...",
                     __FUNCTION__);
    nub_event_t pid_status_event = DNBProcessWaitForEvents(
        pid,
        eEventProcessRunningStateChanged | eEventProcessStoppedStateChanged |
            eEventStdioAvailable | eEventProfileDataAvailable,
        true, NULL);
    DNBLogThreadedIf(LOG_RNB_PROC,
                     "RNBContext::%s calling DNBProcessWaitForEvent(pid, "
                     "eEventProcessRunningStateChanged | "
                     "eEventProcessStoppedStateChanged | eEventStdioAvailable "
                     "| eEventProfileDataAvailable, true) => 0x%8.8x",
                     __FUNCTION__, pid_status_event);

    if (pid_status_event == 0) {
      DNBLogThreadedIf(LOG_RNB_PROC, "RNBContext::%s (pid=%4.4x) got ZERO back "
                                     "from DNBProcessWaitForEvent....",
                       __FUNCTION__, pid);
      //    done = true;
    } else {
      if (pid_status_event & eEventStdioAvailable) {
        DNBLogThreadedIf(
            LOG_RNB_PROC,
            "RNBContext::%s (pid=%4.4x) got stdio available event....",
            __FUNCTION__, pid);
        ctx.Events().SetEvents(RNBContext::event_proc_stdio_available);
        // Wait for the main thread to consume this notification if it requested
        // we wait for it
        ctx.Events().WaitForResetAck(RNBContext::event_proc_stdio_available);
      }

      if (pid_status_event & eEventProfileDataAvailable) {
        DNBLogThreadedIf(
            LOG_RNB_PROC,
            "RNBContext::%s (pid=%4.4x) got profile data event....",
            __FUNCTION__, pid);
        ctx.Events().SetEvents(RNBContext::event_proc_profile_data);
        // Wait for the main thread to consume this notification if it requested
        // we wait for it
        ctx.Events().WaitForResetAck(RNBContext::event_proc_profile_data);
      }

      if (pid_status_event & (eEventProcessRunningStateChanged |
                              eEventProcessStoppedStateChanged)) {
        nub_state_t pid_state = DNBProcessGetState(pid);
        DNBLogThreadedIf(
            LOG_RNB_PROC,
            "RNBContext::%s (pid=%4.4x) got process state change: %s",
            __FUNCTION__, pid, DNBStateAsString(pid_state));

        // Let the main thread know there is a process state change to see
        ctx.Events().SetEvents(RNBContext::event_proc_state_changed);
        // Wait for the main thread to consume this notification if it requested
        // we wait for it
        ctx.Events().WaitForResetAck(RNBContext::event_proc_state_changed);

        switch (pid_state) {
        case eStateStopped:
          break;

        case eStateInvalid:
        case eStateExited:
        case eStateDetached:
          done = true;
          break;
        default:
          break;
        }
      }

      // Reset any events that we consumed.
      DNBProcessResetEvents(pid, pid_status_event);
    }
  }
  DNBLogThreadedIf(LOG_RNB_PROC,
                   "RNBContext::%s (arg=%p, pid=%4.4x): thread exiting...",
                   __FUNCTION__, arg, pid);
  ctx.Events().ResetEvents(event_proc_thread_running);
  ctx.Events().SetEvents(event_proc_thread_exiting);
  return NULL;
}

const char *RNBContext::EventsAsString(nub_event_t events, std::string &s) {
  s.clear();
  if (events & event_proc_state_changed)
    s += "proc_state_changed ";
  if (events & event_proc_thread_running)
    s += "proc_thread_running ";
  if (events & event_proc_thread_exiting)
    s += "proc_thread_exiting ";
  if (events & event_proc_stdio_available)
    s += "proc_stdio_available ";
  if (events & event_proc_profile_data)
    s += "proc_profile_data ";
  if (events & event_read_packet_available)
    s += "read_packet_available ";
  if (events & event_read_thread_running)
    s += "read_thread_running ";
  if (events & event_read_thread_running)
    s += "read_thread_running ";
  return s.c_str();
}

const char *RNBContext::LaunchStatusAsString(std::string &s) {
  s.clear();

  const char *err_str = m_launch_status.AsString();
  if (err_str)
    s = err_str;
  else {
    char error_num_str[64];
    snprintf(error_num_str, sizeof(error_num_str), "%u",
             m_launch_status.Status());
    s = error_num_str;
  }
  return s.c_str();
}

bool RNBContext::ProcessStateRunning() const {
  nub_state_t pid_state = DNBProcessGetState(m_pid);
  return pid_state == eStateRunning || pid_state == eStateStepping;
}

bool RNBContext::AddIgnoredException(const char *exception_name) {
  exception_mask_t exc_mask = MachException::ExceptionMask(exception_name);
  if (exc_mask == 0)
    return false;
  m_ignored_exceptions.push_back(exc_mask);
  return true;
}

void RNBContext::AddDefaultIgnoredExceptions() {
  m_ignored_exceptions.push_back(EXC_MASK_BAD_ACCESS);
  m_ignored_exceptions.push_back(EXC_MASK_BAD_INSTRUCTION);
  m_ignored_exceptions.push_back(EXC_MASK_ARITHMETIC);
}
