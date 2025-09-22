//===-- RNBContext.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBCONTEXT_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBCONTEXT_H

#include "DNBError.h"
#include "PThreadEvent.h"
#include "RNBDefs.h"
#include <string>
#include <vector>

class RNBContext {
public:
  using IgnoredExceptions = std::vector<exception_mask_t>;
  enum {
    event_proc_state_changed = 0x001,
    event_proc_thread_running = 0x002, // Sticky
    event_proc_thread_exiting = 0x004,
    event_proc_stdio_available = 0x008,
    event_proc_profile_data = 0x010,
    event_read_packet_available = 0x020,
    event_read_thread_running = 0x040, // Sticky
    event_read_thread_exiting = 0x080,

    normal_event_bits = event_proc_state_changed | event_proc_thread_exiting |
                        event_proc_stdio_available | event_proc_profile_data |
                        event_read_packet_available |
                        event_read_thread_exiting ,

    sticky_event_bits = event_proc_thread_running | event_read_thread_running,

    all_event_bits = sticky_event_bits | normal_event_bits
  } event_t;
  // Constructors and Destructors
  RNBContext() = default;
  virtual ~RNBContext();

  nub_process_t ProcessID() const { return m_pid; }
  bool HasValidProcessID() const { return m_pid != INVALID_NUB_PROCESS; }
  void SetProcessID(nub_process_t pid);
  nub_size_t GetProcessStopCount() const { return m_pid_stop_count; }
  bool SetProcessStopCount(nub_size_t count) {
    // Returns true if this class' notion of the PID state changed
    if (m_pid_stop_count == count)
      return false; // Didn't change
    m_pid_stop_count = count;
    return true; // The stop count has changed.
  }

  bool ProcessStateRunning() const;
  PThreadEvent &Events() { return m_events; }
  nub_event_t AllEventBits() const { return all_event_bits; }
  nub_event_t NormalEventBits() const { return normal_event_bits; }
  nub_event_t StickyEventBits() const { return sticky_event_bits; }
  const char *EventsAsString(nub_event_t events, std::string &s);

  size_t ArgumentCount() const { return m_arg_vec.size(); }
  const char *ArgumentAtIndex(size_t index);
  void PushArgument(const char *arg) {
    if (arg)
      m_arg_vec.push_back(arg);
  }
  void ClearArgv() { m_arg_vec.erase(m_arg_vec.begin(), m_arg_vec.end()); }

  size_t EnvironmentCount() const { return m_env_vec.size(); }
  const char *EnvironmentAtIndex(size_t index);
  void PushEnvironment(const char *arg) {
    if (arg)
      m_env_vec.push_back(arg);
  }
  void PushEnvironmentIfNeeded(const char *arg);
  void ClearEnvironment() {
    m_env_vec.erase(m_env_vec.begin(), m_env_vec.end());
  }
  DNBError &LaunchStatus() { return m_launch_status; }
  const char *LaunchStatusAsString(std::string &s);
  nub_launch_flavor_t LaunchFlavor() const { return m_launch_flavor; }
  void SetLaunchFlavor(nub_launch_flavor_t flavor) { m_launch_flavor = flavor; }

  const char *GetWorkingDirectory() const {
    if (!m_working_directory.empty())
      return m_working_directory.c_str();
    return NULL;
  }

  bool SetWorkingDirectory(const char *path);

  std::string &GetSTDIN() { return m_stdin; }
  std::string &GetSTDOUT() { return m_stdout; }
  std::string &GetSTDERR() { return m_stderr; }
  std::string &GetWorkingDir() { return m_working_dir; }

  const char *GetSTDINPath() {
    return m_stdin.empty() ? NULL : m_stdin.c_str();
  }
  const char *GetSTDOUTPath() {
    return m_stdout.empty() ? NULL : m_stdout.c_str();
  }
  const char *GetSTDERRPath() {
    return m_stderr.empty() ? NULL : m_stderr.c_str();
  }
  const char *GetWorkingDirPath() {
    return m_working_dir.empty() ? NULL : m_working_dir.c_str();
  }

  void PushProcessEvent(const char *p) { m_process_event.assign(p); }
  const char *GetProcessEvent() { return m_process_event.c_str(); }

  void SetDetachOnError(bool detach) { m_detach_on_error = detach; }
  bool GetDetachOnError() { return m_detach_on_error; }

  bool AddIgnoredException(const char *exception_name);
  
  void AddDefaultIgnoredExceptions();

  const IgnoredExceptions &GetIgnoredExceptions() {
    return m_ignored_exceptions;
  }

protected:
  // Classes that inherit from RNBContext can see and modify these
  nub_process_t m_pid = INVALID_NUB_PROCESS;
  std::string m_stdin;
  std::string m_stdout;
  std::string m_stderr;
  std::string m_working_dir;
  nub_size_t m_pid_stop_count = 0;
  /// Threaded events that we can wait for.
  PThreadEvent m_events{0, all_event_bits};
  pthread_t m_pid_pthread;
  /// How to launch our inferior process.
  nub_launch_flavor_t m_launch_flavor = eLaunchFlavorDefault;
  /// This holds the status from the last launch attempt.
  DNBError m_launch_status;
  std::vector<std::string> m_arg_vec;
  /// This will be unparsed entries FOO=value
  std::vector<std::string> m_env_vec;
  std::string m_working_directory;
  std::string m_process_event;
  bool m_detach_on_error = false;
  IgnoredExceptions m_ignored_exceptions;

  void StartProcessStatusThread();
  void StopProcessStatusThread();
  static void *ThreadFunctionProcessStatus(void *arg);

private:
  RNBContext(const RNBContext &rhs) = delete;
  RNBContext &operator=(const RNBContext &rhs) = delete;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBCONTEXT_H
