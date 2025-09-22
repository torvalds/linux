//===-- Genealogy.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <Availability.h>
#include <dlfcn.h>
#include <string>
#include <uuid/uuid.h>

#include "DNBDefs.h"
#include "Genealogy.h"
#include "GenealogySPI.h"
#include "MachThreadList.h"

/// Constructor

Genealogy::Genealogy()
    : m_os_activity_diagnostic_for_pid(nullptr),
      m_os_activity_iterate_processes(nullptr),
      m_os_activity_iterate_breadcrumbs(nullptr),
      m_os_activity_iterate_messages(nullptr),
      m_os_activity_iterate_activities(nullptr), m_os_trace_get_type(nullptr),
      m_os_trace_copy_formatted_message(nullptr),
      m_os_activity_for_thread(nullptr), m_os_activity_for_task_thread(nullptr),
      m_thread_activities(), m_process_executable_infos(),
      m_diagnosticd_call_timed_out(false) {
  m_os_activity_diagnostic_for_pid =
      (bool (*)(pid_t, os_activity_t, uint32_t, os_diagnostic_block_t))dlsym(
          RTLD_DEFAULT, "os_activity_diagnostic_for_pid");
  m_os_activity_iterate_processes =
      (void (*)(os_activity_process_list_t, bool (^)(os_activity_process_t)))
          dlsym(RTLD_DEFAULT, "os_activity_iterate_processes");
  m_os_activity_iterate_breadcrumbs =
      (void (*)(os_activity_process_t, bool (^)(os_activity_breadcrumb_t)))
          dlsym(RTLD_DEFAULT, "os_activity_iterate_breadcrumbs");
  m_os_activity_iterate_messages = (void (*)(
      os_trace_message_list_t, os_activity_process_t,
      bool (^)(os_trace_message_t)))dlsym(RTLD_DEFAULT,
                                          "os_activity_iterate_messages");
  m_os_activity_iterate_activities = (void (*)(
      os_activity_list_t, os_activity_process_t,
      bool (^)(os_activity_entry_t)))dlsym(RTLD_DEFAULT,
                                           "os_activity_iterate_activities");
  m_os_trace_get_type =
      (uint8_t(*)(os_trace_message_t))dlsym(RTLD_DEFAULT, "os_trace_get_type");
  m_os_trace_copy_formatted_message = (char *(*)(os_trace_message_t))dlsym(
      RTLD_DEFAULT, "os_trace_copy_formatted_message");
  m_os_activity_for_thread =
      (os_activity_t(*)(os_activity_process_t, uint64_t))dlsym(
          RTLD_DEFAULT, "os_activity_for_thread");
  m_os_activity_for_task_thread = (os_activity_t(*)(task_t, uint64_t))dlsym(
      RTLD_DEFAULT, "os_activity_for_task_thread");
  m_os_activity_messages_for_thread = (os_trace_message_list_t(*)(
      os_activity_process_t process, os_activity_t activity,
      uint64_t thread_id))dlsym(RTLD_DEFAULT,
                                "os_activity_messages_for_thread");
}

Genealogy::ThreadActivitySP
Genealogy::GetGenealogyInfoForThread(pid_t pid, nub_thread_t tid,
                                     const MachThreadList &thread_list,
                                     task_t task, bool &timed_out) {
  ThreadActivitySP activity;
  //
  // if we've timed out trying to get the activities, don't try again at this
  // process stop.
  // (else we'll need to hit the timeout for every thread we're asked about.)
  // We'll try again at the next public stop.

  if (m_thread_activities.size() == 0 && !m_diagnosticd_call_timed_out) {
    GetActivities(pid, thread_list, task);
  }
  std::map<nub_thread_t, ThreadActivitySP>::const_iterator search;
  search = m_thread_activities.find(tid);
  if (search != m_thread_activities.end()) {
    activity = search->second;
  }
  timed_out = m_diagnosticd_call_timed_out;
  return activity;
}

void Genealogy::Clear() {
  m_thread_activities.clear();
  m_diagnosticd_call_timed_out = false;
}

void Genealogy::GetActivities(pid_t pid, const MachThreadList &thread_list,
                              task_t task) {
  if (m_os_activity_diagnostic_for_pid != nullptr &&
      m_os_activity_iterate_processes != nullptr &&
      m_os_activity_iterate_breadcrumbs != nullptr &&
      m_os_activity_iterate_messages != nullptr &&
      m_os_activity_iterate_activities != nullptr &&
      m_os_trace_get_type != nullptr &&
      m_os_trace_copy_formatted_message != nullptr &&
      (m_os_activity_for_thread != nullptr ||
       m_os_activity_for_task_thread != nullptr)) {
    __block dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block BreadcrumbList breadcrumbs;
    __block ActivityList activities;
    __block MessageList messages;
    __block std::map<nub_thread_t, uint64_t> thread_activity_mapping;

    os_activity_diagnostic_flag_t flags =
        OS_ACTIVITY_DIAGNOSTIC_ALL_ACTIVITIES |
        OS_ACTIVITY_DIAGNOSTIC_PROCESS_ONLY;
    if (m_os_activity_diagnostic_for_pid(
            pid, 0, flags, ^(os_activity_process_list_t processes, int error) {
              if (error == 0) {
                m_os_activity_iterate_processes(processes, ^bool(
                                                    os_activity_process_t
                                                        process_info) {
                  if (pid == process_info->pid) {
                    // Collect all the Breadcrumbs
                    m_os_activity_iterate_breadcrumbs(
                        process_info,
                        ^bool(os_activity_breadcrumb_t breadcrumb) {
                          Breadcrumb bc;
                          bc.breadcrumb_id = breadcrumb->breadcrumb_id;
                          bc.activity_id = breadcrumb->activity_id;
                          bc.timestamp = breadcrumb->timestamp;
                          if (breadcrumb->name)
                            bc.name = breadcrumb->name;
                          breadcrumbs.push_back(bc);
                          return true;
                        });

                    // Collect all the Activities
                    m_os_activity_iterate_activities(
                        process_info->activities, process_info,
                        ^bool(os_activity_entry_t activity) {
                          Activity ac;
                          ac.activity_start = activity->activity_start;
                          ac.activity_id = activity->activity_id;
                          ac.parent_id = activity->parent_id;
                          if (activity->activity_name)
                            ac.activity_name = activity->activity_name;
                          if (activity->reason)
                            ac.reason = activity->reason;
                          activities.push_back(ac);
                          return true;
                        });

                    // Collect all the Messages -- messages not associated with
                    // any thread
                    m_os_activity_iterate_messages(
                        process_info->messages, process_info,
                        ^bool(os_trace_message_t trace_msg) {
                          Message msg;
                          msg.timestamp = trace_msg->timestamp;
                          msg.trace_id = trace_msg->trace_id;
                          msg.thread = trace_msg->thread;
                          msg.type = m_os_trace_get_type(trace_msg);
                          msg.activity_id = 0;
                          if (trace_msg->image_uuid && trace_msg->image_path) {
                            ProcessExecutableInfoSP process_info_sp(
                                new ProcessExecutableInfo());
                            uuid_copy(process_info_sp->image_uuid,
                                      trace_msg->image_uuid);
                            process_info_sp->image_path = trace_msg->image_path;
                            msg.process_info_index =
                                AddProcessExecutableInfo(process_info_sp);
                          }
                          const char *message_text =
                              m_os_trace_copy_formatted_message(trace_msg);
                          if (message_text)
                            msg.message = message_text;
                          messages.push_back(msg);
                          return true;
                        });

                    // Discover which activities are said to be running on
                    // threads currently
                    const nub_size_t num_threads = thread_list.NumThreads();
                    for (nub_size_t i = 0; i < num_threads; ++i) {
                      nub_thread_t thread_id = thread_list.ThreadIDAtIndex(i);
                      os_activity_t act = 0;
                      if (m_os_activity_for_task_thread != nullptr) {
                        act = m_os_activity_for_task_thread(task, thread_id);
                      } else if (m_os_activity_for_thread != nullptr) {
                        act = m_os_activity_for_thread(process_info, thread_id);
                      }
                      if (act != 0)
                        thread_activity_mapping[thread_id] = act;
                    }

                    // Collect all Messages -- messages associated with a thread

                    // When there's no genealogy information, an early version
                    // of os_activity_messages_for_thread
                    // can crash in rare circumstances.  Check to see if this
                    // process has any activities before
                    // making the call to get messages.
                    if (process_info->activities != nullptr &&
                        thread_activity_mapping.size() > 0) {
                      std::map<nub_thread_t, uint64_t>::const_iterator iter;
                      for (iter = thread_activity_mapping.begin();
                           iter != thread_activity_mapping.end(); ++iter) {
                        nub_thread_t thread_id = iter->first;
                        os_activity_t act = iter->second;
                        os_trace_message_list_t this_thread_messages =
                            m_os_activity_messages_for_thread(process_info, act,
                                                              thread_id);
                        m_os_activity_iterate_messages(
                            this_thread_messages, process_info,
                            ^bool(os_trace_message_t trace_msg) {
                              Message msg;
                              msg.timestamp = trace_msg->timestamp;
                              msg.trace_id = trace_msg->trace_id;
                              msg.thread = trace_msg->thread;
                              msg.type = m_os_trace_get_type(trace_msg);
                              msg.activity_id = act;
                              if (trace_msg->image_uuid &&
                                  trace_msg->image_path) {
                                ProcessExecutableInfoSP process_info_sp(
                                    new ProcessExecutableInfo());
                                uuid_copy(process_info_sp->image_uuid,
                                          trace_msg->image_uuid);
                                process_info_sp->image_path =
                                    trace_msg->image_path;
                                msg.process_info_index =
                                    AddProcessExecutableInfo(process_info_sp);
                              }
                              const char *message_text =
                                  m_os_trace_copy_formatted_message(trace_msg);
                              if (message_text)
                                msg.message = message_text;
                              messages.push_back(msg);
                              return true;
                            });
                      }
                    }
                  }
                  return true;
                });
              }
              dispatch_semaphore_signal(semaphore);
            }) == true) {
      // Wait for the diagnosticd xpc calls to all finish up -- or half a second
      // to elapse.
      dispatch_time_t timeout =
          dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2);
      bool success = dispatch_semaphore_wait(semaphore, timeout) == 0;
      if (!success) {
        m_diagnosticd_call_timed_out = true;
        return;
      }
    }

    // breadcrumbs, activities, and messages have all now been filled in.

    std::map<nub_thread_t, uint64_t>::const_iterator iter;
    for (iter = thread_activity_mapping.begin();
         iter != thread_activity_mapping.end(); ++iter) {
      nub_thread_t thread_id = iter->first;
      uint64_t activity_id = iter->second;
      ActivityList::const_iterator activity_search;
      for (activity_search = activities.begin();
           activity_search != activities.end(); ++activity_search) {
        if (activity_search->activity_id == activity_id) {
          ThreadActivitySP thread_activity_sp(new ThreadActivity());
          thread_activity_sp->current_activity = *activity_search;

          BreadcrumbList::const_iterator breadcrumb_search;
          for (breadcrumb_search = breadcrumbs.begin();
               breadcrumb_search != breadcrumbs.end(); ++breadcrumb_search) {
            if (breadcrumb_search->activity_id == activity_id) {
              thread_activity_sp->breadcrumbs.push_back(*breadcrumb_search);
            }
          }
          MessageList::const_iterator message_search;
          for (message_search = messages.begin();
               message_search != messages.end(); ++message_search) {
            if (message_search->thread == thread_id) {
              thread_activity_sp->messages.push_back(*message_search);
            }
          }

          m_thread_activities[thread_id] = thread_activity_sp;
          break;
        }
      }
    }
  }
}

uint32_t
Genealogy::AddProcessExecutableInfo(ProcessExecutableInfoSP process_exe_info) {
  const uint32_t info_size =
      static_cast<uint32_t>(m_process_executable_infos.size());
  for (uint32_t idx = 0; idx < info_size; ++idx) {
    if (uuid_compare(m_process_executable_infos[idx]->image_uuid,
                     process_exe_info->image_uuid) == 0) {
      return idx + 1;
    }
  }
  m_process_executable_infos.push_back(process_exe_info);
  return info_size + 1;
}

Genealogy::ProcessExecutableInfoSP
Genealogy::GetProcessExecutableInfosAtIndex(size_t idx) {
  ProcessExecutableInfoSP info_sp;
  if (idx > 0) {
    idx--;
    if (idx <= m_process_executable_infos.size()) {
      info_sp = m_process_executable_infos[idx];
    }
  }
  return info_sp;
}
