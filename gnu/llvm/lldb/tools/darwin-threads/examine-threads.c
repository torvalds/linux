#include <ctype.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>

// from System.framework/Versions/B/PrivateHeaders/sys/codesign.h
#define CS_OPS_STATUS 0       /* return status */
#define CS_RESTRICT 0x0000800 /* tell dyld to treat restricted */
int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

/* Step through the process table, find a matching process name, return
   the pid of that matched process.
   If there are multiple processes with that name, issue a warning on stdout
   and return the highest numbered process.
   The proc_pidpath() call is used which gets the full process name including
   directories to the executable and the full (longer than 16 character)
   executable name. */

pid_t get_pid_for_process_name(const char *procname) {
  int process_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0) / sizeof(pid_t);
  if (process_count < 1) {
    printf("Only found %d processes running!\n", process_count);
    exit(1);
  }

  // Allocate a few extra slots in case new processes are spawned
  int all_pids_size = sizeof(pid_t) * (process_count + 3);
  pid_t *all_pids = (pid_t *)malloc(all_pids_size);

  // re-set process_count in case the number of processes changed (got smaller;
  // we won't do bigger)
  process_count =
      proc_listpids(PROC_ALL_PIDS, 0, all_pids, all_pids_size) / sizeof(pid_t);

  int i;
  pid_t highest_pid = 0;
  int match_count = 0;
  for (i = 1; i < process_count; i++) {
    char pidpath[PATH_MAX];
    int pidpath_len = proc_pidpath(all_pids[i], pidpath, sizeof(pidpath));
    if (pidpath_len == 0)
      continue;
    char *j = strrchr(pidpath, '/');
    if ((j == NULL && strcmp(procname, pidpath) == 0) ||
        (j != NULL && strcmp(j + 1, procname) == 0)) {
      match_count++;
      if (all_pids[i] > highest_pid)
        highest_pid = all_pids[i];
    }
  }
  free(all_pids);

  if (match_count == 0) {
    printf("Did not find process '%s'.\n", procname);
    exit(1);
  }
  if (match_count > 1) {
    printf("Warning:  More than one process '%s'!\n", procname);
    printf("          defaulting to the highest-pid one, %d\n", highest_pid);
  }
  return highest_pid;
}

/* Given a pid, get the full executable name (including directory
   paths and the longer-than-16-chars executable name) and return
   the basename of that (i.e. do not include the directory components).
   This function mallocs the memory for the string it returns;
   the caller must free this memory. */

const char *get_process_name_for_pid(pid_t pid) {
  char tmp_name[PATH_MAX];
  if (proc_pidpath(pid, tmp_name, sizeof(tmp_name)) == 0) {
    printf("Could not find process with pid of %d\n", (int)pid);
    exit(1);
  }
  if (strrchr(tmp_name, '/'))
    return strdup(strrchr(tmp_name, '/') + 1);
  else
    return strdup(tmp_name);
}

/* Get a struct kinfo_proc structure for a given pid.
   Process name is required for error printing.
   Gives you the current state of the process and whether it is being debugged
   by anyone.
   memory is malloc()'ed for the returned struct kinfo_proc
   and must be freed by the caller.  */

struct kinfo_proc *get_kinfo_proc_for_pid(pid_t pid, const char *process_name) {
  struct kinfo_proc *kinfo =
      (struct kinfo_proc *)malloc(sizeof(struct kinfo_proc));
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
  size_t len = sizeof(struct kinfo_proc);
  if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), kinfo, &len, NULL, 0) != 0) {
    free((void *)kinfo);
    printf("Could not get kinfo_proc for pid %d\n", (int)pid);
    exit(1);
  }
  return kinfo;
}

/* Get the basic information (thread_basic_info_t) about a given
   thread.
   Gives you the suspend count; thread state; user time; system time; sleep
   time; etc.
   The return value is a pointer to malloc'ed memory - it is the caller's
   responsibility to free it.  */

thread_basic_info_t get_thread_basic_info(thread_t thread) {
  kern_return_t kr;
  integer_t *thinfo = (integer_t *)malloc(sizeof(integer_t) * THREAD_INFO_MAX);
  mach_msg_type_number_t thread_info_count = THREAD_INFO_MAX;
  kr = thread_info(thread, THREAD_BASIC_INFO, (thread_info_t)thinfo,
                   &thread_info_count);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to get basic thread info for a thread\n");
    exit(1);
  }
  return (thread_basic_info_t)thinfo;
}

/* Get the thread identifier info (thread_identifier_info_data_t)
   about a given thread.
   Gives you the system-wide unique thread number; the pthread identifier number
*/

thread_identifier_info_data_t get_thread_identifier_info(thread_t thread) {
  kern_return_t kr;
  thread_identifier_info_data_t tident;
  mach_msg_type_number_t tident_count = THREAD_IDENTIFIER_INFO_COUNT;
  kr = thread_info(thread, THREAD_IDENTIFIER_INFO, (thread_info_t)&tident,
                   &tident_count);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to get thread ident for a thread\n");
    exit(1);
  }
  return tident;
}

/* Given a mach port # (in the examine-threads mach port namespace) for a
   thread,
   find the mach port # in the inferior program's port namespace.
   Sets inferior_port if successful.
   Returns true if successful, false if unable to find the port number.  */

bool inferior_namespace_mach_port_num(task_t task,
                                      thread_t examine_threads_port,
                                      thread_t *inferior_port) {
  kern_return_t retval;
  mach_port_name_array_t names;
  mach_msg_type_number_t nameslen;
  mach_port_type_array_t types;
  mach_msg_type_number_t typeslen;

  if (inferior_port == NULL)
    return false;

  retval = mach_port_names(task, &names, &nameslen, &types, &typeslen);
  if (retval != KERN_SUCCESS) {
    printf("Error - unable to get mach port names for inferior.\n");
    return false;
  }
  int i = 0;
  for (i = 0; i < nameslen; i++) {
    mach_port_t local_name;
    mach_msg_type_name_t local_type;
    retval = mach_port_extract_right(task, names[i], MACH_MSG_TYPE_COPY_SEND,
                                     &local_name, &local_type);
    if (retval == KERN_SUCCESS) {
      mach_port_deallocate(mach_task_self(), local_name);
      if (local_name == examine_threads_port) {
        *inferior_port = names[i];
        vm_deallocate(mach_task_self(), (vm_address_t)names,
                      nameslen * sizeof(mach_port_t));
        vm_deallocate(mach_task_self(), (vm_address_t)types,
                      typeslen * sizeof(mach_port_t));
        return true;
      }
    }
  }
  vm_deallocate(mach_task_self(), (vm_address_t)names,
                nameslen * sizeof(mach_port_t));
  vm_deallocate(mach_task_self(), (vm_address_t)types,
                typeslen * sizeof(mach_port_t));
  return false;
}

/* Get the current pc value for a given thread.  */

uint64_t get_current_pc(thread_t thread, int *wordsize) {
  kern_return_t kr;

#if defined(__x86_64__) || defined(__i386__)
  x86_thread_state_t gp_regs;
  mach_msg_type_number_t gp_count = x86_THREAD_STATE_COUNT;
  kr = thread_get_state(thread, x86_THREAD_STATE, (thread_state_t)&gp_regs,
                        &gp_count);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to get registers for a thread\n");
    exit(1);
  }

  if (gp_regs.tsh.flavor == x86_THREAD_STATE64) {
    *wordsize = 8;
    return gp_regs.uts.ts64.__rip;
  } else {
    *wordsize = 4;
    return gp_regs.uts.ts32.__eip;
  }
#endif

#if defined(__arm__)
  arm_thread_state_t gp_regs;
  mach_msg_type_number_t gp_count = ARM_THREAD_STATE_COUNT;
  kr = thread_get_state(thread, ARM_THREAD_STATE, (thread_state_t)&gp_regs,
                        &gp_count);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to get registers for a thread\n");
    exit(1);
  }
  *wordsize = 4;
  return gp_regs.__pc;
#endif

#if defined(__arm64__)
  arm_thread_state64_t gp_regs;
  mach_msg_type_number_t gp_count = ARM_THREAD_STATE64_COUNT;
  kr = thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&gp_regs,
                        &gp_count);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to get registers for a thread\n");
    exit(1);
  }
  *wordsize = 8;
  return gp_regs.__pc;
#endif
}

/* Get the proc_threadinfo for a given thread.
   Gives you the thread name, if set; current and max priorities.
   Returns 1 if successful
   Returns 0 if proc_pidinfo() failed
*/

int get_proc_threadinfo(pid_t pid, uint64_t thread_handle,
                        struct proc_threadinfo *pth) {
  pth->pth_name[0] = '\0';
  int ret = proc_pidinfo(pid, PROC_PIDTHREADINFO, thread_handle, pth,
                         sizeof(struct proc_threadinfo));
  if (ret != 0)
    return 1;
  else
    return 0;
}

int main(int argc, char **argv) {
  kern_return_t kr;
  task_t task;
  pid_t pid = 0;
  char *procname = NULL;
  int arg_is_procname = 0;
  int do_loop = 0;
  int verbose = 0;
  int resume_when_done = 0;
  mach_port_t mytask = mach_task_self();

  if (argc != 2 && argc != 3 && argc != 4 && argc != 5) {
    printf("Usage: tdump [-l] [-v] [-r] pid/procname\n");
    exit(1);
  }

  if (argc == 3 || argc == 4) {
    int i = 1;
    while (i < argc - 1) {
      if (strcmp(argv[i], "-l") == 0)
        do_loop = 1;
      if (strcmp(argv[i], "-v") == 0)
        verbose = 1;
      if (strcmp(argv[i], "-r") == 0)
        resume_when_done++;
      i++;
    }
  }

  char *c = argv[argc - 1];
  if (*c == '\0') {
    printf("Usage: tdump [-l] [-v] pid/procname\n");
    exit(1);
  }
  while (*c != '\0') {
    if (!isdigit(*c)) {
      arg_is_procname = 1;
      procname = argv[argc - 1];
      break;
    }
    c++;
  }

  if (arg_is_procname && procname) {
    pid = get_pid_for_process_name(procname);
  } else {
    errno = 0;
    pid = (pid_t)strtol(argv[argc - 1], NULL, 10);
    if (pid == 0 && errno == EINVAL) {
      printf("Usage: tdump [-l] [-v] pid/procname\n");
      exit(1);
    }
  }

  const char *process_name = get_process_name_for_pid(pid);

  // At this point "pid" is the process id and "process_name" is the process
  // name
  // Now we have to get the process list from the kernel (which only has the
  // truncated
  // 16 char names)

  struct kinfo_proc *kinfo = get_kinfo_proc_for_pid(pid, process_name);

  printf("pid %d (%s) is currently ", pid, process_name);
  switch (kinfo->kp_proc.p_stat) {
  case SIDL:
    printf("being created by fork");
    break;
  case SRUN:
    printf("runnable");
    break;
  case SSLEEP:
    printf("sleeping on an address");
    break;
  case SSTOP:
    printf("suspended");
    break;
  case SZOMB:
    printf("zombie state - awaiting collection by parent");
    break;
  default:
    printf("unknown");
  }
  if (kinfo->kp_proc.p_flag & P_TRACED)
    printf(" and is being debugged.");
  free((void *)kinfo);

  printf("\n");

  int csops_flags = 0;
  if (csops(pid, CS_OPS_STATUS, &csops_flags, sizeof(csops_flags)) != -1 &&
      (csops_flags & CS_RESTRICT)) {
    printf("pid %d (%s) is restricted so nothing can attach to it.\n", pid,
           process_name);
  }

  kr = task_for_pid(mach_task_self(), pid, &task);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to task_for_pid()\n");
    exit(1);
  }

  struct task_basic_info info;
  unsigned int info_count = TASK_BASIC_INFO_COUNT;

  kr = task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &info_count);
  if (kr != KERN_SUCCESS) {
    printf("Error - unable to call task_info.\n");
    exit(1);
  }
  printf("Task suspend count: %d.\n", info.suspend_count);

  struct timespec *rqtp = (struct timespec *)malloc(sizeof(struct timespec));
  rqtp->tv_sec = 0;
  rqtp->tv_nsec = 150000000;

  int loop_cnt = 1;
  do {
    int i;
    if (do_loop)
      printf("Iteration %d:\n", loop_cnt++);
    thread_array_t thread_list;
    mach_msg_type_number_t thread_count;

    kr = task_threads(task, &thread_list, &thread_count);
    if (kr != KERN_SUCCESS) {
      printf("Error - unable to get thread list\n");
      exit(1);
    }
    printf("pid %d has %d threads\n", pid, thread_count);
    if (verbose)
      printf("\n");

    for (i = 0; i < thread_count; i++) {
      thread_basic_info_t basic_info = get_thread_basic_info(thread_list[i]);

      thread_identifier_info_data_t identifier_info =
          get_thread_identifier_info(thread_list[i]);

      int wordsize;
      uint64_t pc = get_current_pc(thread_list[i], &wordsize);

      printf("thread #%d, system-wide-unique-tid 0x%llx, suspend count is %d, ",
             i, identifier_info.thread_id, basic_info->suspend_count);
      if (wordsize == 8)
        printf("pc 0x%016llx, ", pc);
      else
        printf("pc 0x%08llx, ", pc);
      printf("run state is ");
      switch (basic_info->run_state) {
      case TH_STATE_RUNNING:
        puts("running");
        break;
      case TH_STATE_STOPPED:
        puts("stopped");
        break;
      case TH_STATE_WAITING:
        puts("waiting");
        break;
      case TH_STATE_UNINTERRUPTIBLE:
        puts("uninterruptible");
        break;
      case TH_STATE_HALTED:
        puts("halted");
        break;
      default:
        puts("");
      }

      printf("           pthread handle id 0x%llx (not the same value as "
             "pthread_self() returns)\n",
             (uint64_t)identifier_info.thread_handle);

      struct proc_threadinfo pth;
      int proc_threadinfo_succeeded =
          get_proc_threadinfo(pid, identifier_info.thread_handle, &pth);

      if (proc_threadinfo_succeeded && pth.pth_name[0] != '\0')
        printf("           thread name '%s'\n", pth.pth_name);

      printf("           libdispatch qaddr 0x%llx (not the same as the "
             "dispatch_queue_t token)\n",
             (uint64_t)identifier_info.dispatch_qaddr);

      if (verbose) {
        printf(
            "           (examine-threads port namespace) mach port # 0x%4.4x\n",
            (int)thread_list[i]);
        thread_t mach_port_inferior_namespace;
        if (inferior_namespace_mach_port_num(task, thread_list[i],
                                             &mach_port_inferior_namespace))
          printf("           (inferior port namepsace) mach port # 0x%4.4x\n",
                 (int)mach_port_inferior_namespace);
        printf("           user %d.%06ds, system %d.%06ds",
               basic_info->user_time.seconds,
               basic_info->user_time.microseconds,
               basic_info->system_time.seconds,
               basic_info->system_time.microseconds);
        if (basic_info->cpu_usage > 0) {
          float cpu_percentage = basic_info->cpu_usage / 10.0;
          printf(", using %.1f%% cpu currently", cpu_percentage);
        }
        if (basic_info->sleep_time > 0)
          printf(", this thread has slept for %d seconds",
                 basic_info->sleep_time);

        printf("\n           ");
        printf("scheduling policy %d", basic_info->policy);

        if (basic_info->flags != 0) {
          printf(", flags %d", basic_info->flags);
          if ((basic_info->flags | TH_FLAGS_SWAPPED) == TH_FLAGS_SWAPPED)
            printf(" (thread is swapped out)");
          if ((basic_info->flags | TH_FLAGS_IDLE) == TH_FLAGS_IDLE)
            printf(" (thread is idle)");
        }
        if (proc_threadinfo_succeeded)
          printf(", current pri %d, max pri %d", pth.pth_curpri,
                 pth.pth_maxpriority);

        printf("\n\n");
      }

      free((void *)basic_info);
    }
    if (do_loop)
      printf("\n");
    vm_deallocate(mytask, (vm_address_t)thread_list,
                  thread_count * sizeof(thread_act_t));
    nanosleep(rqtp, NULL);
  } while (do_loop);

  while (resume_when_done > 0) {
    kern_return_t err = task_resume(task);
    if (err != KERN_SUCCESS)
      printf("Error resuming task: %d.", err);
    resume_when_done--;
  }

  vm_deallocate(mytask, (vm_address_t)task, sizeof(task_t));
  free((void *)process_name);

  return 0;
}
