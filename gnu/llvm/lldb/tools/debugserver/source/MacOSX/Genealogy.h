//===-- Genealogy.h ---------------------------------------------*- C++ -*-===//
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_GENEALOGY_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_GENEALOGY_H

#include <mach/task.h>
#include <map>
#include <pthread.h>
#include <string>
#include <vector>

#include "GenealogySPI.h"
#include "MachThreadList.h"

class Genealogy {
public:
  Genealogy();

  ~Genealogy() {}

  void Clear();

  struct Breadcrumb {
    uint32_t breadcrumb_id;
    uint64_t activity_id;
    uint64_t timestamp;
    std::string name;
  };

  struct Activity {
    uint64_t activity_start;
    uint64_t activity_id;
    uint64_t parent_id;
    std::string activity_name;
    std::string reason;
  };

  struct Message {
    uint64_t timestamp;
    uint64_t activity_id;
    uint64_t trace_id;
    uint64_t thread;
    uint8_t type;                // OS_TRACE_TYPE_RELEASE, OS_TRACE_TYPE_DEBUG,
                                 // OS_TRACE_TYPE_ERROR, OS_TRACE_TYPE_FAULT
    uint32_t process_info_index; // index # of the image uuid/file path, 0 means
                                 // unknown
    std::string message;
  };

  typedef std::vector<Message> MessageList;
  typedef std::vector<Breadcrumb> BreadcrumbList;
  typedef std::vector<Activity> ActivityList;

  struct ThreadActivity {
    Activity current_activity;
    MessageList messages;
    BreadcrumbList breadcrumbs; // should be 0 or 1 breadcrumbs; no more than 1
                                // BC for any given activity
  };

  typedef std::shared_ptr<ThreadActivity> ThreadActivitySP;

  ThreadActivitySP GetGenealogyInfoForThread(pid_t pid, nub_thread_t tid,
                                             const MachThreadList &thread_list,
                                             task_t task, bool &timed_out);

  struct ProcessExecutableInfo {
    std::string image_path;
    uuid_t image_uuid;
  };

  typedef std::shared_ptr<ProcessExecutableInfo> ProcessExecutableInfoSP;

  ProcessExecutableInfoSP GetProcessExecutableInfosAtIndex(size_t idx);

  uint32_t AddProcessExecutableInfo(ProcessExecutableInfoSP process_exe_info);

private:
  void GetActivities(pid_t pid, const MachThreadList &thread_list, task_t task);

  // the spi we need to call into libtrace - look them up via dlsym at runtime
  bool (*m_os_activity_diagnostic_for_pid)(pid_t pid, os_activity_t activity,
                                           uint32_t flags,
                                           os_diagnostic_block_t block);
  void (*m_os_activity_iterate_processes)(
      os_activity_process_list_t processes,
      bool (^iterator)(os_activity_process_t process_info));
  void (*m_os_activity_iterate_breadcrumbs)(
      os_activity_process_t process_info,
      bool (^iterator)(os_activity_breadcrumb_t breadcrumb));
  void (*m_os_activity_iterate_messages)(
      os_trace_message_list_t messages, os_activity_process_t process_info,
      bool (^iterator)(os_trace_message_t tracemsg));
  void (*m_os_activity_iterate_activities)(
      os_activity_list_t activities, os_activity_process_t process_info,
      bool (^iterator)(os_activity_entry_t activity));
  uint8_t (*m_os_trace_get_type)(os_trace_message_t trace_msg);
  char *(*m_os_trace_copy_formatted_message)(os_trace_message_t trace_msg);
  os_activity_t (*m_os_activity_for_thread)(os_activity_process_t process,
                                            uint64_t thread_id);
  os_activity_t (*m_os_activity_for_task_thread)(task_t target,
                                                 uint64_t thread_id);
  os_trace_message_list_t (*m_os_activity_messages_for_thread)(
      os_activity_process_t process, os_activity_t activity,
      uint64_t thread_id);

  std::map<nub_thread_t, ThreadActivitySP> m_thread_activities;
  std::vector<ProcessExecutableInfoSP> m_process_executable_infos;
  bool m_diagnosticd_call_timed_out;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_GENEALOGY_H
