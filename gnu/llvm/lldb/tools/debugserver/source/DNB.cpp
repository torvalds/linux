//===-- DNB.cpp -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 3/23/07.
//
//===----------------------------------------------------------------------===//

#include "DNB.h"
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <libproc.h>
#include <map>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

#define TRY_KQUEUE 1

#ifdef TRY_KQUEUE
#include <sys/event.h>
#include <sys/time.h>
#ifdef NOTE_EXIT_DETAIL
#define USE_KQUEUE
#endif
#endif

#include "CFBundle.h"
#include "CFString.h"
#include "DNBDataRef.h"
#include "DNBLog.h"
#include "DNBThreadResumeActions.h"
#include "DNBTimer.h"
#include "MacOSX/Genealogy.h"
#include "MacOSX/MachProcess.h"
#include "MacOSX/MachTask.h"
#include "MacOSX/ThreadInfo.h"
#include "RNBRemote.h"

typedef std::shared_ptr<MachProcess> MachProcessSP;
typedef std::map<nub_process_t, MachProcessSP> ProcessMap;
typedef ProcessMap::iterator ProcessMapIter;
typedef ProcessMap::const_iterator ProcessMapConstIter;

static size_t
GetAllInfosMatchingName(const char *process_name,
                        std::vector<struct kinfo_proc> &matching_proc_infos);

// A Thread safe singleton to get a process map pointer.
//
// Returns a pointer to the existing process map, or a pointer to a
// newly created process map if CAN_CREATE is non-zero.
static ProcessMap *GetProcessMap(bool can_create) {
  static ProcessMap *g_process_map_ptr = NULL;

  if (can_create && g_process_map_ptr == NULL) {
    static pthread_mutex_t g_process_map_mutex = PTHREAD_MUTEX_INITIALIZER;
    PTHREAD_MUTEX_LOCKER(locker, &g_process_map_mutex);
    if (g_process_map_ptr == NULL)
      g_process_map_ptr = new ProcessMap;
  }
  return g_process_map_ptr;
}

// Add PID to the shared process pointer map.
//
// Return non-zero value if we succeed in adding the process to the map.
// The only time this should fail is if we run out of memory and can't
// allocate a ProcessMap.
static nub_bool_t AddProcessToMap(nub_process_t pid, MachProcessSP &procSP) {
  ProcessMap *process_map = GetProcessMap(true);
  if (process_map) {
    process_map->insert(std::make_pair(pid, procSP));
    return true;
  }
  return false;
}

// Remove the shared pointer for PID from the process map.
//
// Returns the number of items removed from the process map.
// static size_t
// RemoveProcessFromMap (nub_process_t pid)
//{
//    ProcessMap* process_map = GetProcessMap(false);
//    if (process_map)
//    {
//        return process_map->erase(pid);
//    }
//    return 0;
//}

// Get the shared pointer for PID from the existing process map.
//
// Returns true if we successfully find a shared pointer to a
// MachProcess object.
static nub_bool_t GetProcessSP(nub_process_t pid, MachProcessSP &procSP) {
  ProcessMap *process_map = GetProcessMap(false);
  if (process_map != NULL) {
    ProcessMapIter pos = process_map->find(pid);
    if (pos != process_map->end()) {
      procSP = pos->second;
      return true;
    }
  }
  procSP.reset();
  return false;
}

#ifdef USE_KQUEUE
void *kqueue_thread(void *arg) {
  int kq_id = (int)(intptr_t)arg;

#if defined(__APPLE__)
  pthread_setname_np("kqueue thread");
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

  struct kevent death_event;
  while (true) {
    int n_events = kevent(kq_id, NULL, 0, &death_event, 1, NULL);
    if (n_events == -1) {
      if (errno == EINTR)
        continue;
      else {
        DNBLogError("kqueue failed with error: (%d): %s", errno,
                    strerror(errno));
        return NULL;
      }
    } else if (death_event.flags & EV_ERROR) {
      int error_no = static_cast<int>(death_event.data);
      const char *error_str = strerror(error_no);
      if (error_str == NULL)
        error_str = "Unknown error";
      DNBLogError("Failed to initialize kqueue event: (%d): %s", error_no,
                  error_str);
      return NULL;
    } else {
      int status;
      const pid_t pid = (pid_t)death_event.ident;
      const pid_t child_pid = waitpid(pid, &status, 0);

      bool exited = false;
      int signal = 0;
      int exit_status = 0;
      if (WIFSTOPPED(status)) {
        signal = WSTOPSIG(status);
        DNBLogThreadedIf(LOG_PROCESS, "waitpid (%i) -> STOPPED (signal = %i)",
                         child_pid, signal);
      } else if (WIFEXITED(status)) {
        exit_status = WEXITSTATUS(status);
        exited = true;
        DNBLogThreadedIf(LOG_PROCESS, "waitpid (%i) -> EXITED (status = %i)",
                         child_pid, exit_status);
      } else if (WIFSIGNALED(status)) {
        signal = WTERMSIG(status);
        if (child_pid == abs(pid)) {
          DNBLogThreadedIf(LOG_PROCESS,
                           "waitpid (%i) -> SIGNALED and EXITED (signal = %i)",
                           child_pid, signal);
          char exit_info[64];
          ::snprintf(exit_info, sizeof(exit_info),
                     "Terminated due to signal %i", signal);
          DNBProcessSetExitInfo(child_pid, exit_info);
          exited = true;
          exit_status = INT8_MAX;
        } else {
          DNBLogThreadedIf(LOG_PROCESS,
                           "waitpid (%i) -> SIGNALED (signal = %i)", child_pid,
                           signal);
        }
      }

      if (exited) {
        if (death_event.data & NOTE_EXIT_MEMORY)
          DNBProcessSetExitInfo(child_pid, "Terminated due to memory issue");
        else if (death_event.data & NOTE_EXIT_DECRYPTFAIL)
          DNBProcessSetExitInfo(child_pid, "Terminated due to decrypt failure");
        else if (death_event.data & NOTE_EXIT_CSERROR)
          DNBProcessSetExitInfo(child_pid,
                                "Terminated due to code signing error");

        DNBLogThreadedIf(
            LOG_PROCESS,
            "waitpid_process_thread (): setting exit status for pid = %i to %i",
            child_pid, exit_status);
        DNBProcessSetExitStatus(child_pid, status);
        return NULL;
      }
    }
  }
}

static bool spawn_kqueue_thread(pid_t pid) {
  pthread_t thread;
  int kq_id;

  kq_id = kqueue();
  if (kq_id == -1) {
    DNBLogError("Could not get kqueue for pid = %i.", pid);
    return false;
  }

  struct kevent reg_event;

  EV_SET(&reg_event, pid, EVFILT_PROC, EV_ADD,
         NOTE_EXIT | NOTE_EXITSTATUS | NOTE_EXIT_DETAIL, 0, NULL);
  // Register the event:
  int result = kevent(kq_id, &reg_event, 1, NULL, 0, NULL);
  if (result != 0) {
    DNBLogError(
        "Failed to register kqueue NOTE_EXIT event for pid %i, error: %d.", pid,
        result);
    return false;
  }

  int ret =
      ::pthread_create(&thread, NULL, kqueue_thread, (void *)(intptr_t)kq_id);

  // pthread_create returns 0 if successful
  if (ret == 0) {
    ::pthread_detach(thread);
    return true;
  }
  return false;
}
#endif // #if USE_KQUEUE

static void *waitpid_thread(void *arg) {
  const pid_t pid = (pid_t)(intptr_t)arg;
  int status;

#if defined(__APPLE__)
  pthread_setname_np("waitpid thread");
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

  while (true) {
    pid_t child_pid = waitpid(pid, &status, 0);
    DNBLogThreadedIf(LOG_PROCESS, "waitpid_thread (): waitpid (pid = %i, "
                                  "&status, 0) => %i, status = %i, errno = %i",
                     pid, child_pid, status, errno);

    if (child_pid < 0) {
      if (errno == EINTR)
        continue;
      break;
    } else {
      if (WIFSTOPPED(status)) {
        continue;
      } else // if (WIFEXITED(status) || WIFSIGNALED(status))
      {
        DNBLogThreadedIf(
            LOG_PROCESS,
            "waitpid_thread (): setting exit status for pid = %i to %i",
            child_pid, status);
        DNBProcessSetExitStatus(child_pid, status);
        return NULL;
      }
    }
  }

  // We should never exit as long as our child process is alive, so if we
  // do something else went wrong and we should exit...
  DNBLogThreadedIf(LOG_PROCESS, "waitpid_thread (): main loop exited, setting "
                                "exit status to an invalid value (-1) for pid "
                                "%i",
                   pid);
  DNBProcessSetExitStatus(pid, -1);
  return NULL;
}
static bool spawn_waitpid_thread(pid_t pid) {
#ifdef USE_KQUEUE
  bool success = spawn_kqueue_thread(pid);
  if (success)
    return true;
#endif

  pthread_t thread;
  int ret =
      ::pthread_create(&thread, NULL, waitpid_thread, (void *)(intptr_t)pid);
  // pthread_create returns 0 if successful
  if (ret == 0) {
    ::pthread_detach(thread);
    return true;
  }
  return false;
}

nub_process_t DNBProcessLaunch(
    RNBContext *ctx, const char *path, char const *argv[], const char *envp[],
    const char *working_directory, // NULL => don't change, non-NULL => set
                                   // working directory for inferior to this
    const char *stdin_path, const char *stdout_path, const char *stderr_path,
    bool no_stdio, int disable_aslr, const char *event_data, char *err_str,
    size_t err_len) {
  DNBLogThreadedIf(LOG_PROCESS,
                   "%s ( path='%s', argv = %p, envp = %p, "
                   "working_dir=%s, stdin=%s, stdout=%s, "
                   "stderr=%s, no-stdio=%i, launch_flavor = %u, "
                   "disable_aslr = %d, err = %p, err_len = "
                   "%llu) called...",
                   __FUNCTION__, path, static_cast<void *>(argv),
                   static_cast<void *>(envp), working_directory, stdin_path,
                   stdout_path, stderr_path, no_stdio, ctx->LaunchFlavor(),
                   disable_aslr, static_cast<void *>(err_str),
                   static_cast<uint64_t>(err_len));

  if (err_str && err_len > 0)
    err_str[0] = '\0';
  struct stat path_stat;
  if (::stat(path, &path_stat) == -1) {
    char stat_error[256];
    ::strerror_r(errno, stat_error, sizeof(stat_error));
    snprintf(err_str, err_len, "%s (%s)", stat_error, path);
    return INVALID_NUB_PROCESS;
  }

  MachProcessSP processSP(new MachProcess);
  if (processSP.get()) {
    DNBError launch_err;
    pid_t pid = processSP->LaunchForDebug(
        path, argv, envp, working_directory, stdin_path, stdout_path,
        stderr_path, no_stdio, ctx->LaunchFlavor(), disable_aslr, event_data,
        ctx->GetIgnoredExceptions(), launch_err);
    if (err_str) {
      *err_str = '\0';
      if (launch_err.Fail()) {
        const char *launch_err_str = launch_err.AsString();
        if (launch_err_str) {
          strlcpy(err_str, launch_err_str, err_len - 1);
          err_str[err_len - 1] =
              '\0'; // Make sure the error string is terminated
        }
      }
    }

    DNBLogThreadedIf(LOG_PROCESS, "(DebugNub) new pid is %d...", pid);

    if (pid != INVALID_NUB_PROCESS) {
      // Spawn a thread to reap our child inferior process...
      spawn_waitpid_thread(pid);

      if (processSP->Task().TaskPortForProcessID(launch_err) == TASK_NULL) {
        // We failed to get the task for our process ID which is bad.
        // Kill our process otherwise it will be stopped at the entry
        // point and get reparented to someone else and never go away.
        DNBLog("Could not get task port for process, sending SIGKILL and "
               "exiting.");
        kill(SIGKILL, pid);

        if (err_str && err_len > 0) {
          if (launch_err.AsString()) {
            ::snprintf(err_str, err_len,
                       "failed to get the task for process %i: %s", pid,
                       launch_err.AsString());
          } else {

            const char *ent_name =
#if TARGET_OS_OSX
              "com.apple.security.get-task-allow";
#else
              "get-task-allow";
#endif
            ::snprintf(err_str, err_len,
                       "failed to get the task for process %i: this likely "
                       "means the process cannot be debugged, either because "
                       "it's a system process or because the process is "
                       "missing the %s entitlement.",
                       pid, ent_name);
          }
        }
      } else {
        bool res = AddProcessToMap(pid, processSP);
        UNUSED_IF_ASSERT_DISABLED(res);
        assert(res && "Couldn't add process to map!");
        return pid;
      }
    }
  }
  return INVALID_NUB_PROCESS;
}

// If there is one process with a given name, return the pid for that process.
nub_process_t DNBProcessGetPIDByName(const char *name) {
  std::vector<struct kinfo_proc> matching_proc_infos;
  size_t num_matching_proc_infos =
      GetAllInfosMatchingName(name, matching_proc_infos);
  if (num_matching_proc_infos == 1) {
    return matching_proc_infos[0].kp_proc.p_pid;
  }
  return INVALID_NUB_PROCESS;
}

nub_process_t DNBProcessAttachByName(const char *name, struct timespec *timeout,
                                     const RNBContext::IgnoredExceptions 
                                             &ignored_exceptions, char *err_str,
                                     size_t err_len) {
  if (err_str && err_len > 0)
    err_str[0] = '\0';
  std::vector<struct kinfo_proc> matching_proc_infos;
  size_t num_matching_proc_infos =
      GetAllInfosMatchingName(name, matching_proc_infos);
  if (num_matching_proc_infos == 0) {
    DNBLogError("error: no processes match '%s'\n", name);
    return INVALID_NUB_PROCESS;
  }
  if (num_matching_proc_infos > 1) {
    DNBLogError("error: %llu processes match '%s':\n",
                (uint64_t)num_matching_proc_infos, name);
    size_t i;
    for (i = 0; i < num_matching_proc_infos; ++i)
      DNBLogError("%6u - %s\n", matching_proc_infos[i].kp_proc.p_pid,
                  matching_proc_infos[i].kp_proc.p_comm);
    return INVALID_NUB_PROCESS;
  }

  return DNBProcessAttach(matching_proc_infos[0].kp_proc.p_pid, timeout,
                          ignored_exceptions, err_str, err_len);
}

nub_process_t DNBProcessAttach(nub_process_t attach_pid,
                               struct timespec *timeout, 
                               const RNBContext::IgnoredExceptions 
                                       &ignored_exceptions,
                               char *err_str, size_t err_len) {
  if (err_str && err_len > 0)
    err_str[0] = '\0';

  if (getenv("LLDB_DEBUGSERVER_PATH") == NULL) {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID,
                 static_cast<int>(attach_pid)};
    struct kinfo_proc processInfo;
    size_t bufsize = sizeof(processInfo);
    if (sysctl(mib, (unsigned)(sizeof(mib) / sizeof(int)), &processInfo,
               &bufsize, NULL, 0) == 0 &&
        bufsize > 0) {

      if ((processInfo.kp_proc.p_flag & P_TRANSLATED) == P_TRANSLATED) {
        const char *translated_debugserver =
            "/Library/Apple/usr/libexec/oah/debugserver";
        char fdstr[16];
        char pidstr[16];
        extern int communication_fd;

        if (communication_fd == -1) {
          fprintf(stderr, "Trying to attach to a translated process with the "
                          "native debugserver, exiting...\n");
          exit(1);
        }

        snprintf(fdstr, sizeof(fdstr), "--fd=%d", communication_fd);
        snprintf(pidstr, sizeof(pidstr), "--attach=%d", attach_pid);
        execl(translated_debugserver, translated_debugserver, "--native-regs",
              "--setsid", fdstr, "--handoff-attach-from-native", pidstr,
              (char *)0);
        DNBLogThreadedIf(LOG_PROCESS, "Failed to launch debugserver for "
                         "translated process: ", errno, strerror(errno));
        __builtin_trap();
      }
    }
  }

  if (DNBDebugserverIsTranslated()) {
    return INVALID_NUB_PROCESS_ARCH;
  }

  pid_t pid = INVALID_NUB_PROCESS;
  MachProcessSP processSP(new MachProcess);
  if (processSP.get()) {
    DNBLogThreadedIf(LOG_PROCESS, "(DebugNub) attaching to pid %d...",
                     attach_pid);
    pid =
        processSP->AttachForDebug(attach_pid, ignored_exceptions, err_str, 
                                  err_len);

    if (pid != INVALID_NUB_PROCESS) {
      bool res = AddProcessToMap(pid, processSP);
      UNUSED_IF_ASSERT_DISABLED(res);
      assert(res && "Couldn't add process to map!");
      spawn_waitpid_thread(pid);
    }
  }

  while (pid != INVALID_NUB_PROCESS) {
    // Wait for process to start up and hit entry point
    DNBLogThreadedIf(LOG_PROCESS, "%s DNBProcessWaitForEvent (%4.4x, "
                                  "eEventProcessRunningStateChanged | "
                                  "eEventProcessStoppedStateChanged, true, "
                                  "INFINITE)...",
                     __FUNCTION__, pid);
    nub_event_t set_events =
        DNBProcessWaitForEvents(pid, eEventProcessRunningStateChanged |
                                         eEventProcessStoppedStateChanged,
                                true, timeout);

    DNBLogThreadedIf(LOG_PROCESS, "%s DNBProcessWaitForEvent (%4.4x, "
                                  "eEventProcessRunningStateChanged | "
                                  "eEventProcessStoppedStateChanged, true, "
                                  "INFINITE) => 0x%8.8x",
                     __FUNCTION__, pid, set_events);

    if (set_events == 0) {
      if (err_str && err_len > 0)
        snprintf(err_str, err_len,
                 "attached to process, but could not pause execution; attach "
                 "failed");
      pid = INVALID_NUB_PROCESS;
    } else {
      if (set_events & (eEventProcessRunningStateChanged |
                        eEventProcessStoppedStateChanged)) {
        nub_state_t pid_state = DNBProcessGetState(pid);
        DNBLogThreadedIf(
            LOG_PROCESS,
            "%s process %4.4x state changed (eEventProcessStateChanged): %s",
            __FUNCTION__, pid, DNBStateAsString(pid_state));

        switch (pid_state) {
        case eStateInvalid:
        case eStateUnloaded:
        case eStateAttaching:
        case eStateLaunching:
        case eStateSuspended:
          break; // Ignore

        case eStateRunning:
        case eStateStepping:
          // Still waiting to stop at entry point...
          break;

        case eStateStopped:
        case eStateCrashed:
          return pid;

        case eStateDetached:
        case eStateExited:
          if (err_str && err_len > 0)
            snprintf(err_str, err_len, "process exited");
          return INVALID_NUB_PROCESS;
        }
      }

      DNBProcessResetEvents(pid, set_events);
    }
  }

  return INVALID_NUB_PROCESS;
}

size_t DNBGetAllInfos(std::vector<struct kinfo_proc> &proc_infos) {
  size_t size = 0;
  int name[] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
  u_int namelen = sizeof(name) / sizeof(int);
  int err;

  // Try to find out how many processes are around so we can
  // size the buffer appropriately.  sysctl's man page specifically suggests
  // this approach, and says it returns a bit larger size than needed to
  // handle any new processes created between then and now.

  err = ::sysctl(name, namelen, NULL, &size, NULL, 0);

  if ((err < 0) && (err != ENOMEM)) {
    proc_infos.clear();
    perror("sysctl (mib, miblen, NULL, &num_processes, NULL, 0)");
    return 0;
  }

  // Increase the size of the buffer by a few processes in case more have
  // been spawned
  proc_infos.resize(size / sizeof(struct kinfo_proc));
  size = proc_infos.size() *
         sizeof(struct kinfo_proc); // Make sure we don't exceed our resize...
  err = ::sysctl(name, namelen, &proc_infos[0], &size, NULL, 0);
  if (err < 0) {
    proc_infos.clear();
    return 0;
  }

  // Trim down our array to fit what we actually got back
  proc_infos.resize(size / sizeof(struct kinfo_proc));
  return proc_infos.size();
}

JSONGenerator::ObjectSP DNBGetDyldProcessState(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetDyldProcessState();
  }
  return {};
}

static size_t
GetAllInfosMatchingName(const char *full_process_name,
                        std::vector<struct kinfo_proc> &matching_proc_infos) {

  matching_proc_infos.clear();
  if (full_process_name && full_process_name[0]) {
    // We only get the process name, not the full path, from the proc_info.  So
    // just take the
    // base name of the process name...
    const char *process_name;
    process_name = strrchr(full_process_name, '/');
    if (process_name == NULL)
      process_name = full_process_name;
    else
      process_name++;

    const size_t process_name_len = strlen(process_name);
    std::vector<struct kinfo_proc> proc_infos;
    const size_t num_proc_infos = DNBGetAllInfos(proc_infos);
    if (num_proc_infos > 0) {
      uint32_t i;
      for (i = 0; i < num_proc_infos; i++) {
        // Skip zombie processes and processes with unset status
        if (proc_infos[i].kp_proc.p_stat == 0 ||
            proc_infos[i].kp_proc.p_stat == SZOMB)
          continue;

        // Check for process by name. We only check the first MAXCOMLEN
        // chars as that is all that kp_proc.p_comm holds.

        if (::strncasecmp(process_name, proc_infos[i].kp_proc.p_comm,
                          MAXCOMLEN) == 0) {
          if (process_name_len > MAXCOMLEN) {
            // We found a matching process name whose first MAXCOMLEN
            // characters match, but there is more to the name than
            // this. We need to get the full process name.  Use proc_pidpath,
            // which will get
            // us the full path to the executed process.

            char proc_path_buf[PATH_MAX];

            int return_val = proc_pidpath(proc_infos[i].kp_proc.p_pid,
                                          proc_path_buf, PATH_MAX);
            if (return_val > 0) {
              // Okay, now search backwards from that to see if there is a
              // slash in the name.  Note, even though we got all the args we
              // don't care
              // because the list data is just a bunch of concatenated null
              // terminated strings
              // so strrchr will start from the end of argv0.

              const char *argv_basename = strrchr(proc_path_buf, '/');
              if (argv_basename) {
                // Skip the '/'
                ++argv_basename;
              } else {
                // We didn't find a directory delimiter in the process argv[0],
                // just use what was in there
                argv_basename = proc_path_buf;
              }

              if (argv_basename) {
                if (::strncasecmp(process_name, argv_basename, PATH_MAX) == 0) {
                  matching_proc_infos.push_back(proc_infos[i]);
                }
              }
            }
          } else {
            // We found a matching process, add it to our list
            matching_proc_infos.push_back(proc_infos[i]);
          }
        }
      }
    }
  }
  // return the newly added matches.
  return matching_proc_infos.size();
}

nub_process_t
DNBProcessAttachWait(RNBContext *ctx, const char *waitfor_process_name,
                     bool ignore_existing, struct timespec *timeout_abstime,
                     useconds_t waitfor_interval, char *err_str, size_t err_len,
                     DNBShouldCancelCallback should_cancel_callback,
                     void *callback_data) {
  DNBError prepare_error;
  std::vector<struct kinfo_proc> exclude_proc_infos;
  size_t num_exclude_proc_infos;

  nub_launch_flavor_t launch_flavor = ctx->LaunchFlavor();

  // If the PrepareForAttach returns a valid token, use  MachProcess to check
  // for the process, otherwise scan the process table.

  const void *attach_token = MachProcess::PrepareForAttach(
      waitfor_process_name, launch_flavor, true, prepare_error);

  if (prepare_error.Fail()) {
    DNBLogError("Error in PrepareForAttach: %s", prepare_error.AsString());
    return INVALID_NUB_PROCESS;
  }

  if (attach_token == NULL) {
    if (ignore_existing)
      num_exclude_proc_infos =
          GetAllInfosMatchingName(waitfor_process_name, exclude_proc_infos);
    else
      num_exclude_proc_infos = 0;
  }

  DNBLogThreadedIf(LOG_PROCESS, "Waiting for '%s' to appear...\n",
                   waitfor_process_name);

  // Loop and try to find the process by name
  nub_process_t waitfor_pid = INVALID_NUB_PROCESS;

  while (waitfor_pid == INVALID_NUB_PROCESS) {
    if (attach_token != NULL) {
      nub_process_t pid;
      pid = MachProcess::CheckForProcess(attach_token, launch_flavor);
      if (pid != INVALID_NUB_PROCESS) {
        waitfor_pid = pid;
        break;
      }
    } else {
      // Get the current process list, and check for matches that
      // aren't in our original list. If anyone wants to attach
      // to an existing process by name, they should do it with
      // --attach=PROCNAME. Else we will wait for the first matching
      // process that wasn't in our exclusion list.
      std::vector<struct kinfo_proc> proc_infos;
      const size_t num_proc_infos =
          GetAllInfosMatchingName(waitfor_process_name, proc_infos);
      for (size_t i = 0; i < num_proc_infos; i++) {
        nub_process_t curr_pid = proc_infos[i].kp_proc.p_pid;
        for (size_t j = 0; j < num_exclude_proc_infos; j++) {
          if (curr_pid == exclude_proc_infos[j].kp_proc.p_pid) {
            // This process was in our exclusion list, don't use it.
            curr_pid = INVALID_NUB_PROCESS;
            break;
          }
        }

        // If we didn't find CURR_PID in our exclusion list, then use it.
        if (curr_pid != INVALID_NUB_PROCESS) {
          // We found our process!
          waitfor_pid = curr_pid;
          break;
        }
      }
    }

    // If we haven't found our process yet, check for a timeout
    // and then sleep for a bit until we poll again.
    if (waitfor_pid == INVALID_NUB_PROCESS) {
      if (timeout_abstime != NULL) {
        // Check to see if we have a waitfor-duration option that
        // has timed out?
        if (DNBTimer::TimeOfDayLaterThan(*timeout_abstime)) {
          if (err_str && err_len > 0)
            snprintf(err_str, err_len, "operation timed out");
          DNBLogError("error: waiting for process '%s' timed out.\n",
                      waitfor_process_name);
          return INVALID_NUB_PROCESS;
        }
      }

      // Call the should cancel callback as well...

      if (should_cancel_callback != NULL &&
          should_cancel_callback(callback_data)) {
        DNBLogThreadedIf(
            LOG_PROCESS,
            "DNBProcessAttachWait cancelled by should_cancel callback.");
        waitfor_pid = INVALID_NUB_PROCESS;
        break;
      }

      // Now we're going to wait a while before polling again.  But we also
      // need to check whether we've gotten an event from the debugger  
      // telling us to interrupt the wait.  So we'll use the wait for a possible
      // next event to also be our short pause...
      struct timespec short_timeout;
      DNBTimer::OffsetTimeOfDay(&short_timeout, 0, waitfor_interval);
      uint32_t event_mask = RNBContext::event_read_packet_available 
          | RNBContext::event_read_thread_exiting;
      nub_event_t set_events = ctx->Events().WaitForSetEvents(event_mask, 
          &short_timeout);
      if (set_events & RNBContext::event_read_packet_available) {
        // If we get any packet from the debugger while waiting on the async,
        // it has to be telling us to interrupt.  So always exit here.
        // Over here in DNB land we can see that there was a packet, but all
        // the methods to actually handle it are protected.  It's not worth
        // rearranging all that just to get which packet we were sent...
        DNBLogError("Interrupted by packet while waiting for '%s' to appear.\n",
                   waitfor_process_name);
        break;
      }
      if (set_events & RNBContext::event_read_thread_exiting) {
        // The packet thread is shutting down, get out of here...
        DNBLogError("Interrupted by packet thread shutdown while waiting for "
                    "%s to appear.\n", waitfor_process_name);
        break;
      }
      
    }
  }

  if (waitfor_pid != INVALID_NUB_PROCESS) {
    DNBLogThreadedIf(LOG_PROCESS, "Attaching to %s with pid %i...\n",
                     waitfor_process_name, waitfor_pid);
    // In some cases, we attempt to attach during the transition from
    // /usr/lib/dyld to the dyld in the shared cache. If that happens, we may
    // end up in a state where there is no dyld in the process and from there
    // the debugging session is doomed.
    // In an attempt to make this scenario much less likely, we sleep
    // for an additional `waitfor_interval` number of microseconds before
    // attaching.
    ::usleep(waitfor_interval);
    waitfor_pid = DNBProcessAttach(waitfor_pid, timeout_abstime,
                                   ctx->GetIgnoredExceptions(), err_str, 
                                   err_len);
  }

  bool success = waitfor_pid != INVALID_NUB_PROCESS;
  MachProcess::CleanupAfterAttach(attach_token, launch_flavor, success,
                                  prepare_error);

  return waitfor_pid;
}

nub_bool_t DNBProcessDetach(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    const bool remove = true;
    DNBLogThreaded(
        "Disabling breakpoints and watchpoints, and detaching from %d.", pid);
    procSP->DisableAllBreakpoints(remove);
    procSP->DisableAllWatchpoints(remove);
    return procSP->Detach();
  }
  return false;
}

nub_bool_t DNBProcessKill(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->Kill();
  }
  return false;
}

nub_bool_t DNBProcessSignal(nub_process_t pid, int signal) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->Signal(signal);
  }
  return false;
}

nub_bool_t DNBProcessInterrupt(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->Interrupt();
  return false;
}

nub_bool_t DNBProcessSendEvent(nub_process_t pid, const char *event) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    // FIXME: Do something with the error...
    DNBError send_error;
    return procSP->SendEvent(event, send_error);
  }
  return false;
}

nub_bool_t DNBProcessIsAlive(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return MachTask::IsValid(procSP->Task().TaskPort());
  }
  return eStateInvalid;
}

// Process and Thread state information
nub_state_t DNBProcessGetState(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetState();
  }
  return eStateInvalid;
}

// Process and Thread state information
nub_bool_t DNBProcessGetExitStatus(nub_process_t pid, int *status) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetExitStatus(status);
  }
  return false;
}

nub_bool_t DNBProcessSetExitStatus(nub_process_t pid, int status) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    procSP->SetExitStatus(status);
    return true;
  }
  return false;
}

const char *DNBProcessGetExitInfo(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetExitInfo();
  }
  return NULL;
}

nub_bool_t DNBProcessSetExitInfo(nub_process_t pid, const char *info) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    procSP->SetExitInfo(info);
    return true;
  }
  return false;
}

const char *DNBThreadGetName(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->ThreadGetName(tid);
  return NULL;
}

nub_bool_t
DNBThreadGetIdentifierInfo(nub_process_t pid, nub_thread_t tid,
                           thread_identifier_info_data_t *ident_info) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetThreadList().GetIdentifierInfo(tid, ident_info);
  return false;
}

nub_state_t DNBThreadGetState(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->ThreadGetState(tid);
  }
  return eStateInvalid;
}

const char *DNBStateAsString(nub_state_t state) {
  switch (state) {
  case eStateInvalid:
    return "Invalid";
  case eStateUnloaded:
    return "Unloaded";
  case eStateAttaching:
    return "Attaching";
  case eStateLaunching:
    return "Launching";
  case eStateStopped:
    return "Stopped";
  case eStateRunning:
    return "Running";
  case eStateStepping:
    return "Stepping";
  case eStateCrashed:
    return "Crashed";
  case eStateDetached:
    return "Detached";
  case eStateExited:
    return "Exited";
  case eStateSuspended:
    return "Suspended";
  }
  return "nub_state_t ???";
}

Genealogy::ThreadActivitySP DNBGetGenealogyInfoForThread(nub_process_t pid,
                                                         nub_thread_t tid,
                                                         bool &timed_out) {
  Genealogy::ThreadActivitySP thread_activity_sp;
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    thread_activity_sp = procSP->GetGenealogyInfoForThread(tid, timed_out);
  return thread_activity_sp;
}

Genealogy::ProcessExecutableInfoSP DNBGetGenealogyImageInfo(nub_process_t pid,
                                                            size_t idx) {
  Genealogy::ProcessExecutableInfoSP image_info_sp;
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    image_info_sp = procSP->GetGenealogyImageInfo(idx);
  }
  return image_info_sp;
}

ThreadInfo::QoS DNBGetRequestedQoSForThread(nub_process_t pid, nub_thread_t tid,
                                            nub_addr_t tsd,
                                            uint64_t dti_qos_class_index) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetRequestedQoS(tid, tsd, dti_qos_class_index);
  }
  return ThreadInfo::QoS();
}

nub_addr_t DNBGetPThreadT(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetPThreadT(tid);
  }
  return INVALID_NUB_ADDRESS;
}

nub_addr_t DNBGetDispatchQueueT(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetDispatchQueueT(tid);
  }
  return INVALID_NUB_ADDRESS;
}

nub_addr_t
DNBGetTSDAddressForThread(nub_process_t pid, nub_thread_t tid,
                          uint64_t plo_pthread_tsd_base_address_offset,
                          uint64_t plo_pthread_tsd_base_offset,
                          uint64_t plo_pthread_tsd_entry_size) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetTSDAddressForThread(
        tid, plo_pthread_tsd_base_address_offset, plo_pthread_tsd_base_offset,
        plo_pthread_tsd_entry_size);
  }
  return INVALID_NUB_ADDRESS;
}

std::optional<std::pair<cpu_type_t, cpu_subtype_t>>
DNBGetMainBinaryCPUTypes(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetMainBinaryCPUTypes(pid);
  return {};
}

JSONGenerator::ObjectSP
DNBGetAllLoadedLibrariesInfos(nub_process_t pid, bool report_load_commands) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetAllLoadedLibrariesInfos(pid, report_load_commands);
  }
  return JSONGenerator::ObjectSP();
}

JSONGenerator::ObjectSP
DNBGetLibrariesInfoForAddresses(nub_process_t pid,
                                std::vector<uint64_t> &macho_addresses) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetLibrariesInfoForAddresses(pid, macho_addresses);
  }
  return JSONGenerator::ObjectSP();
}

JSONGenerator::ObjectSP DNBGetSharedCacheInfo(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->GetSharedCacheInfo(pid);
  }
  return JSONGenerator::ObjectSP();
}

const char *DNBProcessGetExecutablePath(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->Path();
  }
  return NULL;
}

nub_size_t DNBProcessGetArgumentCount(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->ArgumentCount();
  }
  return 0;
}

const char *DNBProcessGetArgumentAtIndex(nub_process_t pid, nub_size_t idx) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->ArgumentAtIndex(idx);
  }
  return NULL;
}

// Execution control
nub_bool_t DNBProcessResume(nub_process_t pid,
                            const DNBThreadResumeAction *actions,
                            size_t num_actions) {
  DNBLogThreadedIf(LOG_PROCESS, "%s(pid = %4.4x)", __FUNCTION__, pid);
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    DNBThreadResumeActions thread_actions(actions, num_actions);

    // Below we add a default thread plan just in case one wasn't
    // provided so all threads always know what they were supposed to do
    if (thread_actions.IsEmpty()) {
      // No thread plans were given, so the default it to run all threads
      thread_actions.SetDefaultThreadActionIfNeeded(eStateRunning, 0);
    } else {
      // Some thread plans were given which means anything that wasn't
      // specified should remain stopped.
      thread_actions.SetDefaultThreadActionIfNeeded(eStateStopped, 0);
    }
    return procSP->Resume(thread_actions);
  }
  return false;
}

nub_bool_t DNBProcessHalt(nub_process_t pid) {
  DNBLogThreadedIf(LOG_PROCESS, "%s(pid = %4.4x)", __FUNCTION__, pid);
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->Signal(SIGSTOP);
  return false;
}
//
// nub_bool_t
// DNBThreadResume (nub_process_t pid, nub_thread_t tid, nub_bool_t step)
//{
//    DNBLogThreadedIf(LOG_THREAD, "%s(pid = %4.4x, tid = %4.4x, step = %u)",
//    __FUNCTION__, pid, tid, (uint32_t)step);
//    MachProcessSP procSP;
//    if (GetProcessSP (pid, procSP))
//    {
//        return procSP->Resume(tid, step, 0);
//    }
//    return false;
//}
//
// nub_bool_t
// DNBThreadResumeWithSignal (nub_process_t pid, nub_thread_t tid, nub_bool_t
// step, int signal)
//{
//    DNBLogThreadedIf(LOG_THREAD, "%s(pid = %4.4x, tid = %4.4x, step = %u,
//    signal = %i)", __FUNCTION__, pid, tid, (uint32_t)step, signal);
//    MachProcessSP procSP;
//    if (GetProcessSP (pid, procSP))
//    {
//        return procSP->Resume(tid, step, signal);
//    }
//    return false;
//}

nub_event_t DNBProcessWaitForEvents(nub_process_t pid, nub_event_t event_mask,
                                    bool wait_for_set,
                                    struct timespec *timeout) {
  nub_event_t result = 0;
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    if (wait_for_set)
      result = procSP->Events().WaitForSetEvents(event_mask, timeout);
    else
      result = procSP->Events().WaitForEventsToReset(event_mask, timeout);
  }
  return result;
}

void DNBProcessResetEvents(nub_process_t pid, nub_event_t event_mask) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    procSP->Events().ResetEvents(event_mask);
}

// Breakpoints
nub_bool_t DNBBreakpointSet(nub_process_t pid, nub_addr_t addr, nub_size_t size,
                            nub_bool_t hardware) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->CreateBreakpoint(addr, size, hardware) != NULL;
  return false;
}

nub_bool_t DNBBreakpointClear(nub_process_t pid, nub_addr_t addr) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->DisableBreakpoint(addr, true);
  return false; // Failed
}

// Watchpoints
nub_bool_t DNBWatchpointSet(nub_process_t pid, nub_addr_t addr, nub_size_t size,
                            uint32_t watch_flags, nub_bool_t hardware) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->CreateWatchpoint(addr, size, watch_flags, hardware) != NULL;
  return false;
}

nub_bool_t DNBWatchpointClear(nub_process_t pid, nub_addr_t addr) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->DisableWatchpoint(addr, true);
  return false; // Failed
}

// Return the number of supported hardware watchpoints.
uint32_t DNBWatchpointGetNumSupportedHWP(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetNumSupportedHardwareWatchpoints();
  return 0;
}

// Read memory in the address space of process PID. This call will take
// care of setting and restoring permissions and breaking up the memory
// read into multiple chunks as required.
//
// RETURNS: number of bytes actually read
nub_size_t DNBProcessMemoryRead(nub_process_t pid, nub_addr_t addr,
                                nub_size_t size, void *buf) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->ReadMemory(addr, size, buf);
  return 0;
}

uint64_t DNBProcessMemoryReadInteger(nub_process_t pid, nub_addr_t addr,
                                     nub_size_t integer_size,
                                     uint64_t fail_value) {
  union Integers {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
  };

  if (integer_size <= sizeof(uint64_t)) {
    Integers ints;
    if (DNBProcessMemoryRead(pid, addr, integer_size, &ints) == integer_size) {
      switch (integer_size) {
      case 1:
        return ints.u8;
      case 2:
        return ints.u16;
      case 3:
        return ints.u32 & 0xffffffu;
      case 4:
        return ints.u32;
      case 5:
        return ints.u32 & 0x000000ffffffffffull;
      case 6:
        return ints.u32 & 0x0000ffffffffffffull;
      case 7:
        return ints.u32 & 0x00ffffffffffffffull;
      case 8:
        return ints.u64;
      }
    }
  }
  return fail_value;
}

nub_addr_t DNBProcessMemoryReadPointer(nub_process_t pid, nub_addr_t addr) {
  cpu_type_t cputype = DNBProcessGetCPUType(pid);
  if (cputype) {
    const nub_size_t pointer_size = (cputype & CPU_ARCH_ABI64) ? 8 : 4;
    return DNBProcessMemoryReadInteger(pid, addr, pointer_size, 0);
  }
  return 0;
}

std::string DNBProcessMemoryReadCString(nub_process_t pid, nub_addr_t addr) {
  std::string cstr;
  char buffer[256];
  const nub_size_t max_buffer_cstr_length = sizeof(buffer) - 1;
  buffer[max_buffer_cstr_length] = '\0';
  nub_size_t length = 0;
  nub_addr_t curr_addr = addr;
  do {
    nub_size_t bytes_read =
        DNBProcessMemoryRead(pid, curr_addr, max_buffer_cstr_length, buffer);
    if (bytes_read == 0)
      break;
    length = strlen(buffer);
    cstr.append(buffer, length);
    curr_addr += length;
  } while (length == max_buffer_cstr_length);
  return cstr;
}

std::string DNBProcessMemoryReadCStringFixed(nub_process_t pid, nub_addr_t addr,
                                             nub_size_t fixed_length) {
  std::string cstr;
  char buffer[fixed_length + 1];
  buffer[fixed_length] = '\0';
  nub_size_t bytes_read = DNBProcessMemoryRead(pid, addr, fixed_length, buffer);
  if (bytes_read > 0)
    cstr.assign(buffer);
  return cstr;
}

// Write memory to the address space of process PID. This call will take
// care of setting and restoring permissions and breaking up the memory
// write into multiple chunks as required.
//
// RETURNS: number of bytes actually written
nub_size_t DNBProcessMemoryWrite(nub_process_t pid, nub_addr_t addr,
                                 nub_size_t size, const void *buf) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->WriteMemory(addr, size, buf);
  return 0;
}

nub_addr_t DNBProcessMemoryAllocate(nub_process_t pid, nub_size_t size,
                                    uint32_t permissions) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->Task().AllocateMemory(size, permissions);
  return 0;
}

nub_bool_t DNBProcessMemoryDeallocate(nub_process_t pid, nub_addr_t addr) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->Task().DeallocateMemory(addr);
  return 0;
}

// Find attributes of the memory region that contains ADDR for process PID,
// if possible, and return a string describing those attributes.
//
// Returns 1 if we could find attributes for this region and OUTBUF can
// be sent to the remote debugger.
//
// Returns 0 if we couldn't find the attributes for a region of memory at
// that address and OUTBUF should not be sent.
//
// Returns -1 if this platform cannot look up information about memory regions
// or if we do not yet have a valid launched process.
//
int DNBProcessMemoryRegionInfo(nub_process_t pid, nub_addr_t addr,
                               DNBRegionInfo *region_info) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->Task().GetMemoryRegionInfo(addr, region_info);

  return -1;
}

std::string DNBProcessGetProfileData(nub_process_t pid,
                                     DNBProfileDataScanType scanType) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->Task().GetProfileData(scanType);

  return std::string("");
}

nub_bool_t DNBProcessSetEnableAsyncProfiling(nub_process_t pid,
                                             nub_bool_t enable,
                                             uint64_t interval_usec,
                                             DNBProfileDataScanType scan_type) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    procSP->SetEnableAsyncProfiling(enable, interval_usec, scan_type);
    return true;
  }

  return false;
}

// Get the number of threads for the specified process.
nub_size_t DNBProcessGetNumThreads(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetNumThreads();
  return 0;
}

// Get the thread ID of the current thread.
nub_thread_t DNBProcessGetCurrentThread(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetCurrentThread();
  return 0;
}

// Get the mach port number of the current thread.
nub_thread_t DNBProcessGetCurrentThreadMachPort(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetCurrentThreadMachPort();
  return 0;
}

// Change the current thread.
nub_thread_t DNBProcessSetCurrentThread(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->SetCurrentThread(tid);
  return INVALID_NUB_THREAD;
}

// Dump a string describing a thread's stop reason to the specified file
// handle
nub_bool_t DNBThreadGetStopReason(nub_process_t pid, nub_thread_t tid,
                                  struct DNBThreadStopInfo *stop_info) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetThreadStoppedReason(tid, stop_info);
  return false;
}

// Return string description for the specified thread.
//
// RETURNS: NULL if the thread isn't valid, else a NULL terminated C
// string from a static buffer that must be copied prior to subsequent
// calls.
const char *DNBThreadGetInfo(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetThreadInfo(tid);
  return NULL;
}

// Get the thread ID given a thread index.
nub_thread_t DNBProcessGetThreadAtIndex(nub_process_t pid, size_t thread_idx) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetThreadAtIndex(thread_idx);
  return INVALID_NUB_THREAD;
}

// Do whatever is needed to sync the thread's register state with it's kernel
// values.
nub_bool_t DNBProcessSyncThreadState(nub_process_t pid, nub_thread_t tid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->SyncThreadState(tid);
  return false;
}

nub_addr_t DNBProcessGetSharedLibraryInfoAddress(nub_process_t pid) {
  MachProcessSP procSP;
  DNBError err;
  if (GetProcessSP(pid, procSP))
    return procSP->Task().GetDYLDAllImageInfosAddress(err);
  return INVALID_NUB_ADDRESS;
}

nub_bool_t DNBProcessSharedLibrariesUpdated(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    procSP->SharedLibrariesUpdated();
    return true;
  }
  return false;
}

std::optional<std::string>
DNBGetDeploymentInfo(nub_process_t pid, bool is_executable,
                     const struct load_command &lc,
                     uint64_t load_command_address, uint32_t &major_version,
                     uint32_t &minor_version, uint32_t &patch_version) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    // FIXME: This doesn't return the correct result when xctest (a
    // macOS binary) is loaded with the macCatalyst dyld platform
    // override. The image info corrects for this, but qProcessInfo
    // will return what is in the binary.
    auto info =
        procSP->GetDeploymentInfo(lc, load_command_address, is_executable);
    major_version = info.major_version;
    minor_version = info.minor_version;
    patch_version = info.patch_version;
    // MachProcess::DeploymentInfo has a bool operator to tell whether we have
    // set the platform.  If that's not true, don't report out the platform:
    if (!info)
      return {};
    return procSP->GetPlatformString(info.platform);
  }
  return {};
}

// Get the current shared library information for a process. Only return
// the shared libraries that have changed since the last shared library
// state changed event if only_changed is non-zero.
nub_size_t
DNBProcessGetSharedLibraryInfo(nub_process_t pid, nub_bool_t only_changed,
                               struct DNBExecutableImageInfo **image_infos) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->CopyImageInfos(image_infos, only_changed);

  // If we have no process, then return NULL for the shared library info
  // and zero for shared library count
  *image_infos = NULL;
  return 0;
}

uint32_t DNBGetRegisterCPUType() {
  return DNBArchProtocol::GetRegisterCPUType();
}
// Get the register set information for a specific thread.
const DNBRegisterSetInfo *DNBGetRegisterSetInfo(nub_size_t *num_reg_sets) {
  return DNBArchProtocol::GetRegisterSetInfo(num_reg_sets);
}

// Read a register value by register set and register index.
nub_bool_t DNBThreadGetRegisterValueByID(nub_process_t pid, nub_thread_t tid,
                                         uint32_t set, uint32_t reg,
                                         DNBRegisterValue *value) {
  MachProcessSP procSP;
  ::bzero(value, sizeof(DNBRegisterValue));
  if (GetProcessSP(pid, procSP)) {
    if (tid != INVALID_NUB_THREAD)
      return procSP->GetRegisterValue(tid, set, reg, value);
  }
  return false;
}

nub_bool_t DNBThreadSetRegisterValueByID(nub_process_t pid, nub_thread_t tid,
                                         uint32_t set, uint32_t reg,
                                         const DNBRegisterValue *value) {
  if (tid != INVALID_NUB_THREAD) {
    MachProcessSP procSP;
    if (GetProcessSP(pid, procSP))
      return procSP->SetRegisterValue(tid, set, reg, value);
  }
  return false;
}

nub_size_t DNBThreadGetRegisterContext(nub_process_t pid, nub_thread_t tid,
                                       void *buf, size_t buf_len) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    if (tid != INVALID_NUB_THREAD)
      return procSP->GetThreadList().GetRegisterContext(tid, buf, buf_len);
  }
  ::bzero(buf, buf_len);
  return 0;
}

nub_size_t DNBThreadSetRegisterContext(nub_process_t pid, nub_thread_t tid,
                                       const void *buf, size_t buf_len) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    if (tid != INVALID_NUB_THREAD)
      return procSP->GetThreadList().SetRegisterContext(tid, buf, buf_len);
  }
  return 0;
}

uint32_t DNBThreadSaveRegisterState(nub_process_t pid, nub_thread_t tid) {
  if (tid != INVALID_NUB_THREAD) {
    MachProcessSP procSP;
    if (GetProcessSP(pid, procSP))
      return procSP->GetThreadList().SaveRegisterState(tid);
  }
  return 0;
}
nub_bool_t DNBThreadRestoreRegisterState(nub_process_t pid, nub_thread_t tid,
                                         uint32_t save_id) {
  if (tid != INVALID_NUB_THREAD) {
    MachProcessSP procSP;
    if (GetProcessSP(pid, procSP))
      return procSP->GetThreadList().RestoreRegisterState(tid, save_id);
  }
  return false;
}

// Read a register value by name.
nub_bool_t DNBThreadGetRegisterValueByName(nub_process_t pid, nub_thread_t tid,
                                           uint32_t reg_set,
                                           const char *reg_name,
                                           DNBRegisterValue *value) {
  MachProcessSP procSP;
  ::bzero(value, sizeof(DNBRegisterValue));
  if (GetProcessSP(pid, procSP)) {
    const struct DNBRegisterSetInfo *set_info;
    nub_size_t num_reg_sets = 0;
    set_info = DNBGetRegisterSetInfo(&num_reg_sets);
    if (set_info) {
      uint32_t set = reg_set;
      uint32_t reg;
      if (set == REGISTER_SET_ALL) {
        for (set = 1; set < num_reg_sets; ++set) {
          for (reg = 0; reg < set_info[set].num_registers; ++reg) {
            if (strcasecmp(reg_name, set_info[set].registers[reg].name) == 0)
              return procSP->GetRegisterValue(tid, set, reg, value);
          }
        }
      } else {
        for (reg = 0; reg < set_info[set].num_registers; ++reg) {
          if (strcasecmp(reg_name, set_info[set].registers[reg].name) == 0)
            return procSP->GetRegisterValue(tid, set, reg, value);
        }
      }
    }
  }
  return false;
}

// Read a register set and register number from the register name.
nub_bool_t DNBGetRegisterInfoByName(const char *reg_name,
                                    DNBRegisterInfo *info) {
  const struct DNBRegisterSetInfo *set_info;
  nub_size_t num_reg_sets = 0;
  set_info = DNBGetRegisterSetInfo(&num_reg_sets);
  if (set_info) {
    uint32_t set, reg;
    for (set = 1; set < num_reg_sets; ++set) {
      for (reg = 0; reg < set_info[set].num_registers; ++reg) {
        if (strcasecmp(reg_name, set_info[set].registers[reg].name) == 0) {
          *info = set_info[set].registers[reg];
          return true;
        }
      }
    }

    for (set = 1; set < num_reg_sets; ++set) {
      uint32_t reg;
      for (reg = 0; reg < set_info[set].num_registers; ++reg) {
        if (set_info[set].registers[reg].alt == NULL)
          continue;

        if (strcasecmp(reg_name, set_info[set].registers[reg].alt) == 0) {
          *info = set_info[set].registers[reg];
          return true;
        }
      }
    }
  }

  ::bzero(info, sizeof(DNBRegisterInfo));
  return false;
}

// Set the name to address callback function that this nub can use
// for any name to address lookups that are needed.
nub_bool_t DNBProcessSetNameToAddressCallback(nub_process_t pid,
                                              DNBCallbackNameToAddress callback,
                                              void *baton) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    procSP->SetNameToAddressCallback(callback, baton);
    return true;
  }
  return false;
}

// Set the name to address callback function that this nub can use
// for any name to address lookups that are needed.
nub_bool_t DNBProcessSetSharedLibraryInfoCallback(
    nub_process_t pid, DNBCallbackCopyExecutableImageInfos callback,
    void *baton) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    procSP->SetSharedLibraryInfoCallback(callback, baton);
    return true;
  }
  return false;
}

nub_addr_t DNBProcessLookupAddress(nub_process_t pid, const char *name,
                                   const char *shlib) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP)) {
    return procSP->LookupSymbol(name, shlib);
  }
  return INVALID_NUB_ADDRESS;
}

nub_size_t DNBProcessGetAvailableSTDOUT(nub_process_t pid, char *buf,
                                        nub_size_t buf_size) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetAvailableSTDOUT(buf, buf_size);
  return 0;
}

nub_size_t DNBProcessGetAvailableSTDERR(nub_process_t pid, char *buf,
                                        nub_size_t buf_size) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetAvailableSTDERR(buf, buf_size);
  return 0;
}

nub_size_t DNBProcessGetAvailableProfileData(nub_process_t pid, char *buf,
                                             nub_size_t buf_size) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetAsyncProfileData(buf, buf_size);
  return 0;
}

nub_size_t DNBProcessGetStopCount(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->StopCount();
  return 0;
}

uint32_t DNBProcessGetCPUType(nub_process_t pid) {
  MachProcessSP procSP;
  if (GetProcessSP(pid, procSP))
    return procSP->GetCPUType();
  return 0;
}

nub_bool_t DNBResolveExecutablePath(const char *path, char *resolved_path,
                                    size_t resolved_path_size) {
  if (path == NULL || path[0] == '\0')
    return false;

  char max_path[PATH_MAX];
  std::string result;
  CFString::GlobPath(path, result);

  if (result.empty())
    result = path;

  struct stat path_stat;
  if (::stat(path, &path_stat) == 0) {
    if ((path_stat.st_mode & S_IFMT) == S_IFDIR) {
      CFBundle bundle(path);
      CFReleaser<CFURLRef> url(bundle.CopyExecutableURL());
      if (url.get()) {
        if (::CFURLGetFileSystemRepresentation(
                url.get(), true, (UInt8 *)resolved_path, resolved_path_size))
          return true;
      }
    }
  }

  if (realpath(path, max_path)) {
    // Found the path relatively...
    ::strlcpy(resolved_path, max_path, resolved_path_size);
    return strlen(resolved_path) + 1 < resolved_path_size;
  } else {
    // Not a relative path, check the PATH environment variable if the
    const char *PATH = getenv("PATH");
    if (PATH) {
      const char *curr_path_start = PATH;
      const char *curr_path_end;
      while (curr_path_start && *curr_path_start) {
        curr_path_end = strchr(curr_path_start, ':');
        if (curr_path_end == NULL) {
          result.assign(curr_path_start);
          curr_path_start = NULL;
        } else if (curr_path_end > curr_path_start) {
          size_t len = curr_path_end - curr_path_start;
          result.assign(curr_path_start, len);
          curr_path_start += len + 1;
        } else
          break;

        result += '/';
        result += path;
        struct stat s;
        if (stat(result.c_str(), &s) == 0) {
          ::strlcpy(resolved_path, result.c_str(), resolved_path_size);
          return result.size() + 1 < resolved_path_size;
        }
      }
    }
  }
  return false;
}

bool DNBGetOSVersionNumbers(uint64_t *major, uint64_t *minor, uint64_t *patch) {
  return MachProcess::GetOSVersionNumbers(major, minor, patch);
}

std::string DNBGetMacCatalystVersionString() {
  return MachProcess::GetMacCatalystVersionString();
}

void DNBInitialize() {
  DNBLogThreadedIf(LOG_PROCESS, "DNBInitialize ()");
#if defined(__i386__) || defined(__x86_64__)
  DNBArchImplX86_64::Initialize();
#elif defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  DNBArchMachARM64::Initialize();
#endif
}

void DNBTerminate() {}

nub_bool_t DNBSetArchitecture(const char *arch) {
  if (arch && arch[0]) {
    if (strcasecmp(arch, "i386") == 0)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_I386);
    else if (strcasecmp(arch, "x86_64") == 0)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_X86_64,
                                              CPU_SUBTYPE_X86_64_ALL);
    else if (strcasecmp(arch, "x86_64h") == 0)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_X86_64,
                                              CPU_SUBTYPE_X86_64_H);
    else if (strstr(arch, "arm64_32") == arch ||
             strstr(arch, "aarch64_32") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM64_32);
    else if (strstr(arch, "arm64e") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM64,
                                              CPU_SUBTYPE_ARM64E);
    else if (strstr(arch, "arm64") == arch || strstr(arch, "aarch64") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM64,
                                              CPU_SUBTYPE_ARM64_ALL);
    else if (strstr(arch, "armv8") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM64,
                                              CPU_SUBTYPE_ARM64_V8);
    else if (strstr(arch, "armv7em") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V7EM);
    else if (strstr(arch, "armv7m") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V7M);
    else if (strstr(arch, "armv7k") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V7K);
    else if (strstr(arch, "armv7s") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V7S);
    else if (strstr(arch, "armv7") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7);
    else if (strstr(arch, "armv6m") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V6M);
    else if (strstr(arch, "armv6") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V6);
    else if (strstr(arch, "armv5") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V5TEJ);
    else if (strstr(arch, "armv4t") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_V4T);
    else if (strstr(arch, "arm") == arch)
      return DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM,
                                              CPU_SUBTYPE_ARM_ALL);
  }
  return false;
}

bool DNBDebugserverIsTranslated() {
  int ret = 0;
  size_t size = sizeof(ret);
  if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1)
    return false;
  return ret == 1;
}

bool DNBGetAddressingBits(uint32_t &addressing_bits) {
  static uint32_t g_addressing_bits = 0;
  static std::once_flag g_once_flag;
  std::call_once(g_once_flag, [&](){
    size_t len = sizeof(uint32_t);
    if (::sysctlbyname("machdep.virtual_address_size", &g_addressing_bits, &len,
                       NULL, 0) != 0) {
      g_addressing_bits = 0;
    }
  });

  addressing_bits = g_addressing_bits;

  return addressing_bits > 0;
}

nub_process_t DNBGetParentProcessID(nub_process_t child_pid) {
  return MachProcess::GetParentProcessID(child_pid);
}

bool DNBProcessIsBeingDebugged(nub_process_t pid) {
  return MachProcess::ProcessIsBeingDebugged(pid);
}
