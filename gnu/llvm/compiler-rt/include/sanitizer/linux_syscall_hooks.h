//===-- linux_syscall_hooks.h ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of public sanitizer interface.
//
// System call handlers.
//
// Interface methods declared in this header implement pre- and post- syscall
// actions for the active sanitizer.
// Usage:
//   __sanitizer_syscall_pre_getfoo(...args...);
//   long res = syscall(__NR_getfoo, ...args...);
//   __sanitizer_syscall_post_getfoo(res, ...args...);
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_LINUX_SYSCALL_HOOKS_H
#define SANITIZER_LINUX_SYSCALL_HOOKS_H

#define __sanitizer_syscall_pre_time(tloc)                                     \
  __sanitizer_syscall_pre_impl_time((long)(tloc))
#define __sanitizer_syscall_post_time(res, tloc)                               \
  __sanitizer_syscall_post_impl_time(res, (long)(tloc))
#define __sanitizer_syscall_pre_stime(tptr)                                    \
  __sanitizer_syscall_pre_impl_stime((long)(tptr))
#define __sanitizer_syscall_post_stime(res, tptr)                              \
  __sanitizer_syscall_post_impl_stime(res, (long)(tptr))
#define __sanitizer_syscall_pre_gettimeofday(tv, tz)                           \
  __sanitizer_syscall_pre_impl_gettimeofday((long)(tv), (long)(tz))
#define __sanitizer_syscall_post_gettimeofday(res, tv, tz)                     \
  __sanitizer_syscall_post_impl_gettimeofday(res, (long)(tv), (long)(tz))
#define __sanitizer_syscall_pre_settimeofday(tv, tz)                           \
  __sanitizer_syscall_pre_impl_settimeofday((long)(tv), (long)(tz))
#define __sanitizer_syscall_post_settimeofday(res, tv, tz)                     \
  __sanitizer_syscall_post_impl_settimeofday(res, (long)(tv), (long)(tz))
#define __sanitizer_syscall_pre_adjtimex(txc_p)                                \
  __sanitizer_syscall_pre_impl_adjtimex((long)(txc_p))
#define __sanitizer_syscall_post_adjtimex(res, txc_p)                          \
  __sanitizer_syscall_post_impl_adjtimex(res, (long)(txc_p))
#define __sanitizer_syscall_pre_times(tbuf)                                    \
  __sanitizer_syscall_pre_impl_times((long)(tbuf))
#define __sanitizer_syscall_post_times(res, tbuf)                              \
  __sanitizer_syscall_post_impl_times(res, (long)(tbuf))
#define __sanitizer_syscall_pre_gettid() __sanitizer_syscall_pre_impl_gettid()
#define __sanitizer_syscall_post_gettid(res)                                   \
  __sanitizer_syscall_post_impl_gettid(res)
#define __sanitizer_syscall_pre_nanosleep(rqtp, rmtp)                          \
  __sanitizer_syscall_pre_impl_nanosleep((long)(rqtp), (long)(rmtp))
#define __sanitizer_syscall_post_nanosleep(res, rqtp, rmtp)                    \
  __sanitizer_syscall_post_impl_nanosleep(res, (long)(rqtp), (long)(rmtp))
#define __sanitizer_syscall_pre_alarm(seconds)                                 \
  __sanitizer_syscall_pre_impl_alarm((long)(seconds))
#define __sanitizer_syscall_post_alarm(res, seconds)                           \
  __sanitizer_syscall_post_impl_alarm(res, (long)(seconds))
#define __sanitizer_syscall_pre_getpid() __sanitizer_syscall_pre_impl_getpid()
#define __sanitizer_syscall_post_getpid(res)                                   \
  __sanitizer_syscall_post_impl_getpid(res)
#define __sanitizer_syscall_pre_getppid() __sanitizer_syscall_pre_impl_getppid()
#define __sanitizer_syscall_post_getppid(res)                                  \
  __sanitizer_syscall_post_impl_getppid(res)
#define __sanitizer_syscall_pre_getuid() __sanitizer_syscall_pre_impl_getuid()
#define __sanitizer_syscall_post_getuid(res)                                   \
  __sanitizer_syscall_post_impl_getuid(res)
#define __sanitizer_syscall_pre_geteuid() __sanitizer_syscall_pre_impl_geteuid()
#define __sanitizer_syscall_post_geteuid(res)                                  \
  __sanitizer_syscall_post_impl_geteuid(res)
#define __sanitizer_syscall_pre_getgid() __sanitizer_syscall_pre_impl_getgid()
#define __sanitizer_syscall_post_getgid(res)                                   \
  __sanitizer_syscall_post_impl_getgid(res)
#define __sanitizer_syscall_pre_getegid() __sanitizer_syscall_pre_impl_getegid()
#define __sanitizer_syscall_post_getegid(res)                                  \
  __sanitizer_syscall_post_impl_getegid(res)
#define __sanitizer_syscall_pre_getresuid(ruid, euid, suid)                    \
  __sanitizer_syscall_pre_impl_getresuid((long)(ruid), (long)(euid),           \
                                         (long)(suid))
#define __sanitizer_syscall_post_getresuid(res, ruid, euid, suid)              \
  __sanitizer_syscall_post_impl_getresuid(res, (long)(ruid), (long)(euid),     \
                                          (long)(suid))
#define __sanitizer_syscall_pre_getresgid(rgid, egid, sgid)                    \
  __sanitizer_syscall_pre_impl_getresgid((long)(rgid), (long)(egid),           \
                                         (long)(sgid))
#define __sanitizer_syscall_post_getresgid(res, rgid, egid, sgid)              \
  __sanitizer_syscall_post_impl_getresgid(res, (long)(rgid), (long)(egid),     \
                                          (long)(sgid))
#define __sanitizer_syscall_pre_getpgid(pid)                                   \
  __sanitizer_syscall_pre_impl_getpgid((long)(pid))
#define __sanitizer_syscall_post_getpgid(res, pid)                             \
  __sanitizer_syscall_post_impl_getpgid(res, (long)(pid))
#define __sanitizer_syscall_pre_getpgrp() __sanitizer_syscall_pre_impl_getpgrp()
#define __sanitizer_syscall_post_getpgrp(res)                                  \
  __sanitizer_syscall_post_impl_getpgrp(res)
#define __sanitizer_syscall_pre_getsid(pid)                                    \
  __sanitizer_syscall_pre_impl_getsid((long)(pid))
#define __sanitizer_syscall_post_getsid(res, pid)                              \
  __sanitizer_syscall_post_impl_getsid(res, (long)(pid))
#define __sanitizer_syscall_pre_getgroups(gidsetsize, grouplist)               \
  __sanitizer_syscall_pre_impl_getgroups((long)(gidsetsize), (long)(grouplist))
#define __sanitizer_syscall_post_getgroups(res, gidsetsize, grouplist)         \
  __sanitizer_syscall_post_impl_getgroups(res, (long)(gidsetsize),             \
                                          (long)(grouplist))
#define __sanitizer_syscall_pre_setregid(rgid, egid)                           \
  __sanitizer_syscall_pre_impl_setregid((long)(rgid), (long)(egid))
#define __sanitizer_syscall_post_setregid(res, rgid, egid)                     \
  __sanitizer_syscall_post_impl_setregid(res, (long)(rgid), (long)(egid))
#define __sanitizer_syscall_pre_setgid(gid)                                    \
  __sanitizer_syscall_pre_impl_setgid((long)(gid))
#define __sanitizer_syscall_post_setgid(res, gid)                              \
  __sanitizer_syscall_post_impl_setgid(res, (long)(gid))
#define __sanitizer_syscall_pre_setreuid(ruid, euid)                           \
  __sanitizer_syscall_pre_impl_setreuid((long)(ruid), (long)(euid))
#define __sanitizer_syscall_post_setreuid(res, ruid, euid)                     \
  __sanitizer_syscall_post_impl_setreuid(res, (long)(ruid), (long)(euid))
#define __sanitizer_syscall_pre_setuid(uid)                                    \
  __sanitizer_syscall_pre_impl_setuid((long)(uid))
#define __sanitizer_syscall_post_setuid(res, uid)                              \
  __sanitizer_syscall_post_impl_setuid(res, (long)(uid))
#define __sanitizer_syscall_pre_setresuid(ruid, euid, suid)                    \
  __sanitizer_syscall_pre_impl_setresuid((long)(ruid), (long)(euid),           \
                                         (long)(suid))
#define __sanitizer_syscall_post_setresuid(res, ruid, euid, suid)              \
  __sanitizer_syscall_post_impl_setresuid(res, (long)(ruid), (long)(euid),     \
                                          (long)(suid))
#define __sanitizer_syscall_pre_setresgid(rgid, egid, sgid)                    \
  __sanitizer_syscall_pre_impl_setresgid((long)(rgid), (long)(egid),           \
                                         (long)(sgid))
#define __sanitizer_syscall_post_setresgid(res, rgid, egid, sgid)              \
  __sanitizer_syscall_post_impl_setresgid(res, (long)(rgid), (long)(egid),     \
                                          (long)(sgid))
#define __sanitizer_syscall_pre_setfsuid(uid)                                  \
  __sanitizer_syscall_pre_impl_setfsuid((long)(uid))
#define __sanitizer_syscall_post_setfsuid(res, uid)                            \
  __sanitizer_syscall_post_impl_setfsuid(res, (long)(uid))
#define __sanitizer_syscall_pre_setfsgid(gid)                                  \
  __sanitizer_syscall_pre_impl_setfsgid((long)(gid))
#define __sanitizer_syscall_post_setfsgid(res, gid)                            \
  __sanitizer_syscall_post_impl_setfsgid(res, (long)(gid))
#define __sanitizer_syscall_pre_setpgid(pid, pgid)                             \
  __sanitizer_syscall_pre_impl_setpgid((long)(pid), (long)(pgid))
#define __sanitizer_syscall_post_setpgid(res, pid, pgid)                       \
  __sanitizer_syscall_post_impl_setpgid(res, (long)(pid), (long)(pgid))
#define __sanitizer_syscall_pre_setsid() __sanitizer_syscall_pre_impl_setsid()
#define __sanitizer_syscall_post_setsid(res)                                   \
  __sanitizer_syscall_post_impl_setsid(res)
#define __sanitizer_syscall_pre_setgroups(gidsetsize, grouplist)               \
  __sanitizer_syscall_pre_impl_setgroups((long)(gidsetsize), (long)(grouplist))
#define __sanitizer_syscall_post_setgroups(res, gidsetsize, grouplist)         \
  __sanitizer_syscall_post_impl_setgroups(res, (long)(gidsetsize),             \
                                          (long)(grouplist))
#define __sanitizer_syscall_pre_acct(name)                                     \
  __sanitizer_syscall_pre_impl_acct((long)(name))
#define __sanitizer_syscall_post_acct(res, name)                               \
  __sanitizer_syscall_post_impl_acct(res, (long)(name))
#define __sanitizer_syscall_pre_capget(header, dataptr)                        \
  __sanitizer_syscall_pre_impl_capget((long)(header), (long)(dataptr))
#define __sanitizer_syscall_post_capget(res, header, dataptr)                  \
  __sanitizer_syscall_post_impl_capget(res, (long)(header), (long)(dataptr))
#define __sanitizer_syscall_pre_capset(header, data)                           \
  __sanitizer_syscall_pre_impl_capset((long)(header), (long)(data))
#define __sanitizer_syscall_post_capset(res, header, data)                     \
  __sanitizer_syscall_post_impl_capset(res, (long)(header), (long)(data))
#define __sanitizer_syscall_pre_personality(personality)                       \
  __sanitizer_syscall_pre_impl_personality((long)(personality))
#define __sanitizer_syscall_post_personality(res, personality)                 \
  __sanitizer_syscall_post_impl_personality(res, (long)(personality))
#define __sanitizer_syscall_pre_sigpending(set)                                \
  __sanitizer_syscall_pre_impl_sigpending((long)(set))
#define __sanitizer_syscall_post_sigpending(res, set)                          \
  __sanitizer_syscall_post_impl_sigpending(res, (long)(set))
#define __sanitizer_syscall_pre_sigprocmask(how, set, oset)                    \
  __sanitizer_syscall_pre_impl_sigprocmask((long)(how), (long)(set),           \
                                           (long)(oset))
#define __sanitizer_syscall_post_sigprocmask(res, how, set, oset)              \
  __sanitizer_syscall_post_impl_sigprocmask(res, (long)(how), (long)(set),     \
                                            (long)(oset))
#define __sanitizer_syscall_pre_getitimer(which, value)                        \
  __sanitizer_syscall_pre_impl_getitimer((long)(which), (long)(value))
#define __sanitizer_syscall_post_getitimer(res, which, value)                  \
  __sanitizer_syscall_post_impl_getitimer(res, (long)(which), (long)(value))
#define __sanitizer_syscall_pre_setitimer(which, value, ovalue)                \
  __sanitizer_syscall_pre_impl_setitimer((long)(which), (long)(value),         \
                                         (long)(ovalue))
#define __sanitizer_syscall_post_setitimer(res, which, value, ovalue)          \
  __sanitizer_syscall_post_impl_setitimer(res, (long)(which), (long)(value),   \
                                          (long)(ovalue))
#define __sanitizer_syscall_pre_timer_create(which_clock, timer_event_spec,    \
                                             created_timer_id)                 \
  __sanitizer_syscall_pre_impl_timer_create(                                   \
      (long)(which_clock), (long)(timer_event_spec), (long)(created_timer_id))
#define __sanitizer_syscall_post_timer_create(                                 \
    res, which_clock, timer_event_spec, created_timer_id)                      \
  __sanitizer_syscall_post_impl_timer_create(res, (long)(which_clock),         \
                                             (long)(timer_event_spec),         \
                                             (long)(created_timer_id))
#define __sanitizer_syscall_pre_timer_gettime(timer_id, setting)               \
  __sanitizer_syscall_pre_impl_timer_gettime((long)(timer_id), (long)(setting))
#define __sanitizer_syscall_post_timer_gettime(res, timer_id, setting)         \
  __sanitizer_syscall_post_impl_timer_gettime(res, (long)(timer_id),           \
                                              (long)(setting))
#define __sanitizer_syscall_pre_timer_getoverrun(timer_id)                     \
  __sanitizer_syscall_pre_impl_timer_getoverrun((long)(timer_id))
#define __sanitizer_syscall_post_timer_getoverrun(res, timer_id)               \
  __sanitizer_syscall_post_impl_timer_getoverrun(res, (long)(timer_id))
#define __sanitizer_syscall_pre_timer_settime(timer_id, flags, new_setting,    \
                                              old_setting)                     \
  __sanitizer_syscall_pre_impl_timer_settime((long)(timer_id), (long)(flags),  \
                                             (long)(new_setting),              \
                                             (long)(old_setting))
#define __sanitizer_syscall_post_timer_settime(res, timer_id, flags,           \
                                               new_setting, old_setting)       \
  __sanitizer_syscall_post_impl_timer_settime(                                 \
      res, (long)(timer_id), (long)(flags), (long)(new_setting),               \
      (long)(old_setting))
#define __sanitizer_syscall_pre_timer_delete(timer_id)                         \
  __sanitizer_syscall_pre_impl_timer_delete((long)(timer_id))
#define __sanitizer_syscall_post_timer_delete(res, timer_id)                   \
  __sanitizer_syscall_post_impl_timer_delete(res, (long)(timer_id))
#define __sanitizer_syscall_pre_clock_settime(which_clock, tp)                 \
  __sanitizer_syscall_pre_impl_clock_settime((long)(which_clock), (long)(tp))
#define __sanitizer_syscall_post_clock_settime(res, which_clock, tp)           \
  __sanitizer_syscall_post_impl_clock_settime(res, (long)(which_clock),        \
                                              (long)(tp))
#define __sanitizer_syscall_pre_clock_gettime(which_clock, tp)                 \
  __sanitizer_syscall_pre_impl_clock_gettime((long)(which_clock), (long)(tp))
#define __sanitizer_syscall_post_clock_gettime(res, which_clock, tp)           \
  __sanitizer_syscall_post_impl_clock_gettime(res, (long)(which_clock),        \
                                              (long)(tp))
#define __sanitizer_syscall_pre_clock_adjtime(which_clock, tx)                 \
  __sanitizer_syscall_pre_impl_clock_adjtime((long)(which_clock), (long)(tx))
#define __sanitizer_syscall_post_clock_adjtime(res, which_clock, tx)           \
  __sanitizer_syscall_post_impl_clock_adjtime(res, (long)(which_clock),        \
                                              (long)(tx))
#define __sanitizer_syscall_pre_clock_getres(which_clock, tp)                  \
  __sanitizer_syscall_pre_impl_clock_getres((long)(which_clock), (long)(tp))
#define __sanitizer_syscall_post_clock_getres(res, which_clock, tp)            \
  __sanitizer_syscall_post_impl_clock_getres(res, (long)(which_clock),         \
                                             (long)(tp))
#define __sanitizer_syscall_pre_clock_nanosleep(which_clock, flags, rqtp,      \
                                                rmtp)                          \
  __sanitizer_syscall_pre_impl_clock_nanosleep(                                \
      (long)(which_clock), (long)(flags), (long)(rqtp), (long)(rmtp))
#define __sanitizer_syscall_post_clock_nanosleep(res, which_clock, flags,      \
                                                 rqtp, rmtp)                   \
  __sanitizer_syscall_post_impl_clock_nanosleep(                               \
      res, (long)(which_clock), (long)(flags), (long)(rqtp), (long)(rmtp))
#define __sanitizer_syscall_pre_nice(increment)                                \
  __sanitizer_syscall_pre_impl_nice((long)(increment))
#define __sanitizer_syscall_post_nice(res, increment)                          \
  __sanitizer_syscall_post_impl_nice(res, (long)(increment))
#define __sanitizer_syscall_pre_sched_setscheduler(pid, policy, param)         \
  __sanitizer_syscall_pre_impl_sched_setscheduler((long)(pid), (long)(policy), \
                                                  (long)(param))
#define __sanitizer_syscall_post_sched_setscheduler(res, pid, policy, param)   \
  __sanitizer_syscall_post_impl_sched_setscheduler(                            \
      res, (long)(pid), (long)(policy), (long)(param))
#define __sanitizer_syscall_pre_sched_setparam(pid, param)                     \
  __sanitizer_syscall_pre_impl_sched_setparam((long)(pid), (long)(param))
#define __sanitizer_syscall_post_sched_setparam(res, pid, param)               \
  __sanitizer_syscall_post_impl_sched_setparam(res, (long)(pid), (long)(param))
#define __sanitizer_syscall_pre_sched_getscheduler(pid)                        \
  __sanitizer_syscall_pre_impl_sched_getscheduler((long)(pid))
#define __sanitizer_syscall_post_sched_getscheduler(res, pid)                  \
  __sanitizer_syscall_post_impl_sched_getscheduler(res, (long)(pid))
#define __sanitizer_syscall_pre_sched_getparam(pid, param)                     \
  __sanitizer_syscall_pre_impl_sched_getparam((long)(pid), (long)(param))
#define __sanitizer_syscall_post_sched_getparam(res, pid, param)               \
  __sanitizer_syscall_post_impl_sched_getparam(res, (long)(pid), (long)(param))
#define __sanitizer_syscall_pre_sched_setaffinity(pid, len, user_mask_ptr)     \
  __sanitizer_syscall_pre_impl_sched_setaffinity((long)(pid), (long)(len),     \
                                                 (long)(user_mask_ptr))
#define __sanitizer_syscall_post_sched_setaffinity(res, pid, len,              \
                                                   user_mask_ptr)              \
  __sanitizer_syscall_post_impl_sched_setaffinity(                             \
      res, (long)(pid), (long)(len), (long)(user_mask_ptr))
#define __sanitizer_syscall_pre_sched_getaffinity(pid, len, user_mask_ptr)     \
  __sanitizer_syscall_pre_impl_sched_getaffinity((long)(pid), (long)(len),     \
                                                 (long)(user_mask_ptr))
#define __sanitizer_syscall_post_sched_getaffinity(res, pid, len,              \
                                                   user_mask_ptr)              \
  __sanitizer_syscall_post_impl_sched_getaffinity(                             \
      res, (long)(pid), (long)(len), (long)(user_mask_ptr))
#define __sanitizer_syscall_pre_sched_yield()                                  \
  __sanitizer_syscall_pre_impl_sched_yield()
#define __sanitizer_syscall_post_sched_yield(res)                              \
  __sanitizer_syscall_post_impl_sched_yield(res)
#define __sanitizer_syscall_pre_sched_get_priority_max(policy)                 \
  __sanitizer_syscall_pre_impl_sched_get_priority_max((long)(policy))
#define __sanitizer_syscall_post_sched_get_priority_max(res, policy)           \
  __sanitizer_syscall_post_impl_sched_get_priority_max(res, (long)(policy))
#define __sanitizer_syscall_pre_sched_get_priority_min(policy)                 \
  __sanitizer_syscall_pre_impl_sched_get_priority_min((long)(policy))
#define __sanitizer_syscall_post_sched_get_priority_min(res, policy)           \
  __sanitizer_syscall_post_impl_sched_get_priority_min(res, (long)(policy))
#define __sanitizer_syscall_pre_sched_rr_get_interval(pid, interval)           \
  __sanitizer_syscall_pre_impl_sched_rr_get_interval((long)(pid),              \
                                                     (long)(interval))
#define __sanitizer_syscall_post_sched_rr_get_interval(res, pid, interval)     \
  __sanitizer_syscall_post_impl_sched_rr_get_interval(res, (long)(pid),        \
                                                      (long)(interval))
#define __sanitizer_syscall_pre_setpriority(which, who, niceval)               \
  __sanitizer_syscall_pre_impl_setpriority((long)(which), (long)(who),         \
                                           (long)(niceval))
#define __sanitizer_syscall_post_setpriority(res, which, who, niceval)         \
  __sanitizer_syscall_post_impl_setpriority(res, (long)(which), (long)(who),   \
                                            (long)(niceval))
#define __sanitizer_syscall_pre_getpriority(which, who)                        \
  __sanitizer_syscall_pre_impl_getpriority((long)(which), (long)(who))
#define __sanitizer_syscall_post_getpriority(res, which, who)                  \
  __sanitizer_syscall_post_impl_getpriority(res, (long)(which), (long)(who))
#define __sanitizer_syscall_pre_shutdown(arg0, arg1)                           \
  __sanitizer_syscall_pre_impl_shutdown((long)(arg0), (long)(arg1))
#define __sanitizer_syscall_post_shutdown(res, arg0, arg1)                     \
  __sanitizer_syscall_post_impl_shutdown(res, (long)(arg0), (long)(arg1))
#define __sanitizer_syscall_pre_reboot(magic1, magic2, cmd, arg)               \
  __sanitizer_syscall_pre_impl_reboot((long)(magic1), (long)(magic2),          \
                                      (long)(cmd), (long)(arg))
#define __sanitizer_syscall_post_reboot(res, magic1, magic2, cmd, arg)         \
  __sanitizer_syscall_post_impl_reboot(res, (long)(magic1), (long)(magic2),    \
                                       (long)(cmd), (long)(arg))
#define __sanitizer_syscall_pre_restart_syscall()                              \
  __sanitizer_syscall_pre_impl_restart_syscall()
#define __sanitizer_syscall_post_restart_syscall(res)                          \
  __sanitizer_syscall_post_impl_restart_syscall(res)
#define __sanitizer_syscall_pre_kexec_load(entry, nr_segments, segments,       \
                                           flags)                              \
  __sanitizer_syscall_pre_impl_kexec_load((long)(entry), (long)(nr_segments),  \
                                          (long)(segments), (long)(flags))
#define __sanitizer_syscall_post_kexec_load(res, entry, nr_segments, segments, \
                                            flags)                             \
  __sanitizer_syscall_post_impl_kexec_load(res, (long)(entry),                 \
                                           (long)(nr_segments),                \
                                           (long)(segments), (long)(flags))
#define __sanitizer_syscall_pre_exit(error_code)                               \
  __sanitizer_syscall_pre_impl_exit((long)(error_code))
#define __sanitizer_syscall_post_exit(res, error_code)                         \
  __sanitizer_syscall_post_impl_exit(res, (long)(error_code))
#define __sanitizer_syscall_pre_exit_group(error_code)                         \
  __sanitizer_syscall_pre_impl_exit_group((long)(error_code))
#define __sanitizer_syscall_post_exit_group(res, error_code)                   \
  __sanitizer_syscall_post_impl_exit_group(res, (long)(error_code))
#define __sanitizer_syscall_pre_wait4(pid, stat_addr, options, ru)             \
  __sanitizer_syscall_pre_impl_wait4((long)(pid), (long)(stat_addr),           \
                                     (long)(options), (long)(ru))
#define __sanitizer_syscall_post_wait4(res, pid, stat_addr, options, ru)       \
  __sanitizer_syscall_post_impl_wait4(res, (long)(pid), (long)(stat_addr),     \
                                      (long)(options), (long)(ru))
#define __sanitizer_syscall_pre_waitid(which, pid, infop, options, ru)         \
  __sanitizer_syscall_pre_impl_waitid(                                         \
      (long)(which), (long)(pid), (long)(infop), (long)(options), (long)(ru))
#define __sanitizer_syscall_post_waitid(res, which, pid, infop, options, ru)   \
  __sanitizer_syscall_post_impl_waitid(res, (long)(which), (long)(pid),        \
                                       (long)(infop), (long)(options),         \
                                       (long)(ru))
#define __sanitizer_syscall_pre_waitpid(pid, stat_addr, options)               \
  __sanitizer_syscall_pre_impl_waitpid((long)(pid), (long)(stat_addr),         \
                                       (long)(options))
#define __sanitizer_syscall_post_waitpid(res, pid, stat_addr, options)         \
  __sanitizer_syscall_post_impl_waitpid(res, (long)(pid), (long)(stat_addr),   \
                                        (long)(options))
#define __sanitizer_syscall_pre_set_tid_address(tidptr)                        \
  __sanitizer_syscall_pre_impl_set_tid_address((long)(tidptr))
#define __sanitizer_syscall_post_set_tid_address(res, tidptr)                  \
  __sanitizer_syscall_post_impl_set_tid_address(res, (long)(tidptr))
#define __sanitizer_syscall_pre_init_module(umod, len, uargs)                  \
  __sanitizer_syscall_pre_impl_init_module((long)(umod), (long)(len),          \
                                           (long)(uargs))
#define __sanitizer_syscall_post_init_module(res, umod, len, uargs)            \
  __sanitizer_syscall_post_impl_init_module(res, (long)(umod), (long)(len),    \
                                            (long)(uargs))
#define __sanitizer_syscall_pre_delete_module(name_user, flags)                \
  __sanitizer_syscall_pre_impl_delete_module((long)(name_user), (long)(flags))
#define __sanitizer_syscall_post_delete_module(res, name_user, flags)          \
  __sanitizer_syscall_post_impl_delete_module(res, (long)(name_user),          \
                                              (long)(flags))
#define __sanitizer_syscall_pre_rt_sigprocmask(how, set, oset, sigsetsize)     \
  __sanitizer_syscall_pre_impl_rt_sigprocmask(                                 \
      (long)(how), (long)(set), (long)(oset), (long)(sigsetsize))
#define __sanitizer_syscall_post_rt_sigprocmask(res, how, set, oset,           \
                                                sigsetsize)                    \
  __sanitizer_syscall_post_impl_rt_sigprocmask(                                \
      res, (long)(how), (long)(set), (long)(oset), (long)(sigsetsize))
#define __sanitizer_syscall_pre_rt_sigpending(set, sigsetsize)                 \
  __sanitizer_syscall_pre_impl_rt_sigpending((long)(set), (long)(sigsetsize))
#define __sanitizer_syscall_post_rt_sigpending(res, set, sigsetsize)           \
  __sanitizer_syscall_post_impl_rt_sigpending(res, (long)(set),                \
                                              (long)(sigsetsize))
#define __sanitizer_syscall_pre_rt_sigtimedwait(uthese, uinfo, uts,            \
                                                sigsetsize)                    \
  __sanitizer_syscall_pre_impl_rt_sigtimedwait(                                \
      (long)(uthese), (long)(uinfo), (long)(uts), (long)(sigsetsize))
#define __sanitizer_syscall_post_rt_sigtimedwait(res, uthese, uinfo, uts,      \
                                                 sigsetsize)                   \
  __sanitizer_syscall_post_impl_rt_sigtimedwait(                               \
      res, (long)(uthese), (long)(uinfo), (long)(uts), (long)(sigsetsize))
#define __sanitizer_syscall_pre_rt_tgsigqueueinfo(tgid, pid, sig, uinfo)       \
  __sanitizer_syscall_pre_impl_rt_tgsigqueueinfo((long)(tgid), (long)(pid),    \
                                                 (long)(sig), (long)(uinfo))
#define __sanitizer_syscall_post_rt_tgsigqueueinfo(res, tgid, pid, sig, uinfo) \
  __sanitizer_syscall_post_impl_rt_tgsigqueueinfo(                             \
      res, (long)(tgid), (long)(pid), (long)(sig), (long)(uinfo))
#define __sanitizer_syscall_pre_kill(pid, sig)                                 \
  __sanitizer_syscall_pre_impl_kill((long)(pid), (long)(sig))
#define __sanitizer_syscall_post_kill(res, pid, sig)                           \
  __sanitizer_syscall_post_impl_kill(res, (long)(pid), (long)(sig))
#define __sanitizer_syscall_pre_tgkill(tgid, pid, sig)                         \
  __sanitizer_syscall_pre_impl_tgkill((long)(tgid), (long)(pid), (long)(sig))
#define __sanitizer_syscall_post_tgkill(res, tgid, pid, sig)                   \
  __sanitizer_syscall_post_impl_tgkill(res, (long)(tgid), (long)(pid),         \
                                       (long)(sig))
#define __sanitizer_syscall_pre_tkill(pid, sig)                                \
  __sanitizer_syscall_pre_impl_tkill((long)(pid), (long)(sig))
#define __sanitizer_syscall_post_tkill(res, pid, sig)                          \
  __sanitizer_syscall_post_impl_tkill(res, (long)(pid), (long)(sig))
#define __sanitizer_syscall_pre_rt_sigqueueinfo(pid, sig, uinfo)               \
  __sanitizer_syscall_pre_impl_rt_sigqueueinfo((long)(pid), (long)(sig),       \
                                               (long)(uinfo))
#define __sanitizer_syscall_post_rt_sigqueueinfo(res, pid, sig, uinfo)         \
  __sanitizer_syscall_post_impl_rt_sigqueueinfo(res, (long)(pid), (long)(sig), \
                                                (long)(uinfo))
#define __sanitizer_syscall_pre_sgetmask()                                     \
  __sanitizer_syscall_pre_impl_sgetmask()
#define __sanitizer_syscall_post_sgetmask(res)                                 \
  __sanitizer_syscall_post_impl_sgetmask(res)
#define __sanitizer_syscall_pre_ssetmask(newmask)                              \
  __sanitizer_syscall_pre_impl_ssetmask((long)(newmask))
#define __sanitizer_syscall_post_ssetmask(res, newmask)                        \
  __sanitizer_syscall_post_impl_ssetmask(res, (long)(newmask))
#define __sanitizer_syscall_pre_signal(sig, handler)                           \
  __sanitizer_syscall_pre_impl_signal((long)(sig), (long)(handler))
#define __sanitizer_syscall_post_signal(res, sig, handler)                     \
  __sanitizer_syscall_post_impl_signal(res, (long)(sig), (long)(handler))
#define __sanitizer_syscall_pre_pause() __sanitizer_syscall_pre_impl_pause()
#define __sanitizer_syscall_post_pause(res)                                    \
  __sanitizer_syscall_post_impl_pause(res)
#define __sanitizer_syscall_pre_sync() __sanitizer_syscall_pre_impl_sync()
#define __sanitizer_syscall_post_sync(res)                                     \
  __sanitizer_syscall_post_impl_sync(res)
#define __sanitizer_syscall_pre_fsync(fd)                                      \
  __sanitizer_syscall_pre_impl_fsync((long)(fd))
#define __sanitizer_syscall_post_fsync(res, fd)                                \
  __sanitizer_syscall_post_impl_fsync(res, (long)(fd))
#define __sanitizer_syscall_pre_fdatasync(fd)                                  \
  __sanitizer_syscall_pre_impl_fdatasync((long)(fd))
#define __sanitizer_syscall_post_fdatasync(res, fd)                            \
  __sanitizer_syscall_post_impl_fdatasync(res, (long)(fd))
#define __sanitizer_syscall_pre_bdflush(func, data)                            \
  __sanitizer_syscall_pre_impl_bdflush((long)(func), (long)(data))
#define __sanitizer_syscall_post_bdflush(res, func, data)                      \
  __sanitizer_syscall_post_impl_bdflush(res, (long)(func), (long)(data))
#define __sanitizer_syscall_pre_mount(dev_name, dir_name, type, flags, data)   \
  __sanitizer_syscall_pre_impl_mount((long)(dev_name), (long)(dir_name),       \
                                     (long)(type), (long)(flags),              \
                                     (long)(data))
#define __sanitizer_syscall_post_mount(res, dev_name, dir_name, type, flags,   \
                                       data)                                   \
  __sanitizer_syscall_post_impl_mount(res, (long)(dev_name), (long)(dir_name), \
                                      (long)(type), (long)(flags),             \
                                      (long)(data))
#define __sanitizer_syscall_pre_umount(name, flags)                            \
  __sanitizer_syscall_pre_impl_umount((long)(name), (long)(flags))
#define __sanitizer_syscall_post_umount(res, name, flags)                      \
  __sanitizer_syscall_post_impl_umount(res, (long)(name), (long)(flags))
#define __sanitizer_syscall_pre_oldumount(name)                                \
  __sanitizer_syscall_pre_impl_oldumount((long)(name))
#define __sanitizer_syscall_post_oldumount(res, name)                          \
  __sanitizer_syscall_post_impl_oldumount(res, (long)(name))
#define __sanitizer_syscall_pre_truncate(path, length)                         \
  __sanitizer_syscall_pre_impl_truncate((long)(path), (long)(length))
#define __sanitizer_syscall_post_truncate(res, path, length)                   \
  __sanitizer_syscall_post_impl_truncate(res, (long)(path), (long)(length))
#define __sanitizer_syscall_pre_ftruncate(fd, length)                          \
  __sanitizer_syscall_pre_impl_ftruncate((long)(fd), (long)(length))
#define __sanitizer_syscall_post_ftruncate(res, fd, length)                    \
  __sanitizer_syscall_post_impl_ftruncate(res, (long)(fd), (long)(length))
#define __sanitizer_syscall_pre_stat(filename, statbuf)                        \
  __sanitizer_syscall_pre_impl_stat((long)(filename), (long)(statbuf))
#define __sanitizer_syscall_post_stat(res, filename, statbuf)                  \
  __sanitizer_syscall_post_impl_stat(res, (long)(filename), (long)(statbuf))
#define __sanitizer_syscall_pre_statfs(path, buf)                              \
  __sanitizer_syscall_pre_impl_statfs((long)(path), (long)(buf))
#define __sanitizer_syscall_post_statfs(res, path, buf)                        \
  __sanitizer_syscall_post_impl_statfs(res, (long)(path), (long)(buf))
#define __sanitizer_syscall_pre_statfs64(path, sz, buf)                        \
  __sanitizer_syscall_pre_impl_statfs64((long)(path), (long)(sz), (long)(buf))
#define __sanitizer_syscall_post_statfs64(res, path, sz, buf)                  \
  __sanitizer_syscall_post_impl_statfs64(res, (long)(path), (long)(sz),        \
                                         (long)(buf))
#define __sanitizer_syscall_pre_fstatfs(fd, buf)                               \
  __sanitizer_syscall_pre_impl_fstatfs((long)(fd), (long)(buf))
#define __sanitizer_syscall_post_fstatfs(res, fd, buf)                         \
  __sanitizer_syscall_post_impl_fstatfs(res, (long)(fd), (long)(buf))
#define __sanitizer_syscall_pre_fstatfs64(fd, sz, buf)                         \
  __sanitizer_syscall_pre_impl_fstatfs64((long)(fd), (long)(sz), (long)(buf))
#define __sanitizer_syscall_post_fstatfs64(res, fd, sz, buf)                   \
  __sanitizer_syscall_post_impl_fstatfs64(res, (long)(fd), (long)(sz),         \
                                          (long)(buf))
#define __sanitizer_syscall_pre_lstat(filename, statbuf)                       \
  __sanitizer_syscall_pre_impl_lstat((long)(filename), (long)(statbuf))
#define __sanitizer_syscall_post_lstat(res, filename, statbuf)                 \
  __sanitizer_syscall_post_impl_lstat(res, (long)(filename), (long)(statbuf))
#define __sanitizer_syscall_pre_fstat(fd, statbuf)                             \
  __sanitizer_syscall_pre_impl_fstat((long)(fd), (long)(statbuf))
#define __sanitizer_syscall_post_fstat(res, fd, statbuf)                       \
  __sanitizer_syscall_post_impl_fstat(res, (long)(fd), (long)(statbuf))
#define __sanitizer_syscall_pre_newstat(filename, statbuf)                     \
  __sanitizer_syscall_pre_impl_newstat((long)(filename), (long)(statbuf))
#define __sanitizer_syscall_post_newstat(res, filename, statbuf)               \
  __sanitizer_syscall_post_impl_newstat(res, (long)(filename), (long)(statbuf))
#define __sanitizer_syscall_pre_newlstat(filename, statbuf)                    \
  __sanitizer_syscall_pre_impl_newlstat((long)(filename), (long)(statbuf))
#define __sanitizer_syscall_post_newlstat(res, filename, statbuf)              \
  __sanitizer_syscall_post_impl_newlstat(res, (long)(filename), (long)(statbuf))
#define __sanitizer_syscall_pre_newfstat(fd, statbuf)                          \
  __sanitizer_syscall_pre_impl_newfstat((long)(fd), (long)(statbuf))
#define __sanitizer_syscall_post_newfstat(res, fd, statbuf)                    \
  __sanitizer_syscall_post_impl_newfstat(res, (long)(fd), (long)(statbuf))
#define __sanitizer_syscall_pre_ustat(dev, ubuf)                               \
  __sanitizer_syscall_pre_impl_ustat((long)(dev), (long)(ubuf))
#define __sanitizer_syscall_post_ustat(res, dev, ubuf)                         \
  __sanitizer_syscall_post_impl_ustat(res, (long)(dev), (long)(ubuf))
#define __sanitizer_syscall_pre_stat64(filename, statbuf)                      \
  __sanitizer_syscall_pre_impl_stat64((long)(filename), (long)(statbuf))
#define __sanitizer_syscall_post_stat64(res, filename, statbuf)                \
  __sanitizer_syscall_post_impl_stat64(res, (long)(filename), (long)(statbuf))
#define __sanitizer_syscall_pre_fstat64(fd, statbuf)                           \
  __sanitizer_syscall_pre_impl_fstat64((long)(fd), (long)(statbuf))
#define __sanitizer_syscall_post_fstat64(res, fd, statbuf)                     \
  __sanitizer_syscall_post_impl_fstat64(res, (long)(fd), (long)(statbuf))
#define __sanitizer_syscall_pre_lstat64(filename, statbuf)                     \
  __sanitizer_syscall_pre_impl_lstat64((long)(filename), (long)(statbuf))
#define __sanitizer_syscall_post_lstat64(res, filename, statbuf)               \
  __sanitizer_syscall_post_impl_lstat64(res, (long)(filename), (long)(statbuf))
#define __sanitizer_syscall_pre_setxattr(path, name, value, size, flags)       \
  __sanitizer_syscall_pre_impl_setxattr(                                       \
      (long)(path), (long)(name), (long)(value), (long)(size), (long)(flags))
#define __sanitizer_syscall_post_setxattr(res, path, name, value, size, flags) \
  __sanitizer_syscall_post_impl_setxattr(res, (long)(path), (long)(name),      \
                                         (long)(value), (long)(size),          \
                                         (long)(flags))
#define __sanitizer_syscall_pre_lsetxattr(path, name, value, size, flags)      \
  __sanitizer_syscall_pre_impl_lsetxattr(                                      \
      (long)(path), (long)(name), (long)(value), (long)(size), (long)(flags))
#define __sanitizer_syscall_post_lsetxattr(res, path, name, value, size,       \
                                           flags)                              \
  __sanitizer_syscall_post_impl_lsetxattr(res, (long)(path), (long)(name),     \
                                          (long)(value), (long)(size),         \
                                          (long)(flags))
#define __sanitizer_syscall_pre_fsetxattr(fd, name, value, size, flags)        \
  __sanitizer_syscall_pre_impl_fsetxattr(                                      \
      (long)(fd), (long)(name), (long)(value), (long)(size), (long)(flags))
#define __sanitizer_syscall_post_fsetxattr(res, fd, name, value, size, flags)  \
  __sanitizer_syscall_post_impl_fsetxattr(res, (long)(fd), (long)(name),       \
                                          (long)(value), (long)(size),         \
                                          (long)(flags))
#define __sanitizer_syscall_pre_getxattr(path, name, value, size)              \
  __sanitizer_syscall_pre_impl_getxattr((long)(path), (long)(name),            \
                                        (long)(value), (long)(size))
#define __sanitizer_syscall_post_getxattr(res, path, name, value, size)        \
  __sanitizer_syscall_post_impl_getxattr(res, (long)(path), (long)(name),      \
                                         (long)(value), (long)(size))
#define __sanitizer_syscall_pre_lgetxattr(path, name, value, size)             \
  __sanitizer_syscall_pre_impl_lgetxattr((long)(path), (long)(name),           \
                                         (long)(value), (long)(size))
#define __sanitizer_syscall_post_lgetxattr(res, path, name, value, size)       \
  __sanitizer_syscall_post_impl_lgetxattr(res, (long)(path), (long)(name),     \
                                          (long)(value), (long)(size))
#define __sanitizer_syscall_pre_fgetxattr(fd, name, value, size)               \
  __sanitizer_syscall_pre_impl_fgetxattr((long)(fd), (long)(name),             \
                                         (long)(value), (long)(size))
#define __sanitizer_syscall_post_fgetxattr(res, fd, name, value, size)         \
  __sanitizer_syscall_post_impl_fgetxattr(res, (long)(fd), (long)(name),       \
                                          (long)(value), (long)(size))
#define __sanitizer_syscall_pre_listxattr(path, list, size)                    \
  __sanitizer_syscall_pre_impl_listxattr((long)(path), (long)(list),           \
                                         (long)(size))
#define __sanitizer_syscall_post_listxattr(res, path, list, size)              \
  __sanitizer_syscall_post_impl_listxattr(res, (long)(path), (long)(list),     \
                                          (long)(size))
#define __sanitizer_syscall_pre_llistxattr(path, list, size)                   \
  __sanitizer_syscall_pre_impl_llistxattr((long)(path), (long)(list),          \
                                          (long)(size))
#define __sanitizer_syscall_post_llistxattr(res, path, list, size)             \
  __sanitizer_syscall_post_impl_llistxattr(res, (long)(path), (long)(list),    \
                                           (long)(size))
#define __sanitizer_syscall_pre_flistxattr(fd, list, size)                     \
  __sanitizer_syscall_pre_impl_flistxattr((long)(fd), (long)(list),            \
                                          (long)(size))
#define __sanitizer_syscall_post_flistxattr(res, fd, list, size)               \
  __sanitizer_syscall_post_impl_flistxattr(res, (long)(fd), (long)(list),      \
                                           (long)(size))
#define __sanitizer_syscall_pre_removexattr(path, name)                        \
  __sanitizer_syscall_pre_impl_removexattr((long)(path), (long)(name))
#define __sanitizer_syscall_post_removexattr(res, path, name)                  \
  __sanitizer_syscall_post_impl_removexattr(res, (long)(path), (long)(name))
#define __sanitizer_syscall_pre_lremovexattr(path, name)                       \
  __sanitizer_syscall_pre_impl_lremovexattr((long)(path), (long)(name))
#define __sanitizer_syscall_post_lremovexattr(res, path, name)                 \
  __sanitizer_syscall_post_impl_lremovexattr(res, (long)(path), (long)(name))
#define __sanitizer_syscall_pre_fremovexattr(fd, name)                         \
  __sanitizer_syscall_pre_impl_fremovexattr((long)(fd), (long)(name))
#define __sanitizer_syscall_post_fremovexattr(res, fd, name)                   \
  __sanitizer_syscall_post_impl_fremovexattr(res, (long)(fd), (long)(name))
#define __sanitizer_syscall_pre_brk(brk)                                       \
  __sanitizer_syscall_pre_impl_brk((long)(brk))
#define __sanitizer_syscall_post_brk(res, brk)                                 \
  __sanitizer_syscall_post_impl_brk(res, (long)(brk))
#define __sanitizer_syscall_pre_mprotect(start, len, prot)                     \
  __sanitizer_syscall_pre_impl_mprotect((long)(start), (long)(len),            \
                                        (long)(prot))
#define __sanitizer_syscall_post_mprotect(res, start, len, prot)               \
  __sanitizer_syscall_post_impl_mprotect(res, (long)(start), (long)(len),      \
                                         (long)(prot))
#define __sanitizer_syscall_pre_mremap(addr, old_len, new_len, flags,          \
                                       new_addr)                               \
  __sanitizer_syscall_pre_impl_mremap((long)(addr), (long)(old_len),           \
                                      (long)(new_len), (long)(flags),          \
                                      (long)(new_addr))
#define __sanitizer_syscall_post_mremap(res, addr, old_len, new_len, flags,    \
                                        new_addr)                              \
  __sanitizer_syscall_post_impl_mremap(res, (long)(addr), (long)(old_len),     \
                                       (long)(new_len), (long)(flags),         \
                                       (long)(new_addr))
#define __sanitizer_syscall_pre_remap_file_pages(start, size, prot, pgoff,     \
                                                 flags)                        \
  __sanitizer_syscall_pre_impl_remap_file_pages(                               \
      (long)(start), (long)(size), (long)(prot), (long)(pgoff), (long)(flags))
#define __sanitizer_syscall_post_remap_file_pages(res, start, size, prot,      \
                                                  pgoff, flags)                \
  __sanitizer_syscall_post_impl_remap_file_pages(res, (long)(start),           \
                                                 (long)(size), (long)(prot),   \
                                                 (long)(pgoff), (long)(flags))
#define __sanitizer_syscall_pre_msync(start, len, flags)                       \
  __sanitizer_syscall_pre_impl_msync((long)(start), (long)(len), (long)(flags))
#define __sanitizer_syscall_post_msync(res, start, len, flags)                 \
  __sanitizer_syscall_post_impl_msync(res, (long)(start), (long)(len),         \
                                      (long)(flags))
#define __sanitizer_syscall_pre_munmap(addr, len)                              \
  __sanitizer_syscall_pre_impl_munmap((long)(addr), (long)(len))
#define __sanitizer_syscall_post_munmap(res, addr, len)                        \
  __sanitizer_syscall_post_impl_munmap(res, (long)(addr), (long)(len))
#define __sanitizer_syscall_pre_mlock(start, len)                              \
  __sanitizer_syscall_pre_impl_mlock((long)(start), (long)(len))
#define __sanitizer_syscall_post_mlock(res, start, len)                        \
  __sanitizer_syscall_post_impl_mlock(res, (long)(start), (long)(len))
#define __sanitizer_syscall_pre_munlock(start, len)                            \
  __sanitizer_syscall_pre_impl_munlock((long)(start), (long)(len))
#define __sanitizer_syscall_post_munlock(res, start, len)                      \
  __sanitizer_syscall_post_impl_munlock(res, (long)(start), (long)(len))
#define __sanitizer_syscall_pre_mlockall(flags)                                \
  __sanitizer_syscall_pre_impl_mlockall((long)(flags))
#define __sanitizer_syscall_post_mlockall(res, flags)                          \
  __sanitizer_syscall_post_impl_mlockall(res, (long)(flags))
#define __sanitizer_syscall_pre_munlockall()                                   \
  __sanitizer_syscall_pre_impl_munlockall()
#define __sanitizer_syscall_post_munlockall(res)                               \
  __sanitizer_syscall_post_impl_munlockall(res)
#define __sanitizer_syscall_pre_madvise(start, len, behavior)                  \
  __sanitizer_syscall_pre_impl_madvise((long)(start), (long)(len),             \
                                       (long)(behavior))
#define __sanitizer_syscall_post_madvise(res, start, len, behavior)            \
  __sanitizer_syscall_post_impl_madvise(res, (long)(start), (long)(len),       \
                                        (long)(behavior))
#define __sanitizer_syscall_pre_mincore(start, len, vec)                       \
  __sanitizer_syscall_pre_impl_mincore((long)(start), (long)(len), (long)(vec))
#define __sanitizer_syscall_post_mincore(res, start, len, vec)                 \
  __sanitizer_syscall_post_impl_mincore(res, (long)(start), (long)(len),       \
                                        (long)(vec))
#define __sanitizer_syscall_pre_pivot_root(new_root, put_old)                  \
  __sanitizer_syscall_pre_impl_pivot_root((long)(new_root), (long)(put_old))
#define __sanitizer_syscall_post_pivot_root(res, new_root, put_old)            \
  __sanitizer_syscall_post_impl_pivot_root(res, (long)(new_root),              \
                                           (long)(put_old))
#define __sanitizer_syscall_pre_chroot(filename)                               \
  __sanitizer_syscall_pre_impl_chroot((long)(filename))
#define __sanitizer_syscall_post_chroot(res, filename)                         \
  __sanitizer_syscall_post_impl_chroot(res, (long)(filename))
#define __sanitizer_syscall_pre_mknod(filename, mode, dev)                     \
  __sanitizer_syscall_pre_impl_mknod((long)(filename), (long)(mode),           \
                                     (long)(dev))
#define __sanitizer_syscall_post_mknod(res, filename, mode, dev)               \
  __sanitizer_syscall_post_impl_mknod(res, (long)(filename), (long)(mode),     \
                                      (long)(dev))
#define __sanitizer_syscall_pre_link(oldname, newname)                         \
  __sanitizer_syscall_pre_impl_link((long)(oldname), (long)(newname))
#define __sanitizer_syscall_post_link(res, oldname, newname)                   \
  __sanitizer_syscall_post_impl_link(res, (long)(oldname), (long)(newname))
#define __sanitizer_syscall_pre_symlink(old, new_)                             \
  __sanitizer_syscall_pre_impl_symlink((long)(old), (long)(new_))
#define __sanitizer_syscall_post_symlink(res, old, new_)                       \
  __sanitizer_syscall_post_impl_symlink(res, (long)(old), (long)(new_))
#define __sanitizer_syscall_pre_unlink(pathname)                               \
  __sanitizer_syscall_pre_impl_unlink((long)(pathname))
#define __sanitizer_syscall_post_unlink(res, pathname)                         \
  __sanitizer_syscall_post_impl_unlink(res, (long)(pathname))
#define __sanitizer_syscall_pre_rename(oldname, newname)                       \
  __sanitizer_syscall_pre_impl_rename((long)(oldname), (long)(newname))
#define __sanitizer_syscall_post_rename(res, oldname, newname)                 \
  __sanitizer_syscall_post_impl_rename(res, (long)(oldname), (long)(newname))
#define __sanitizer_syscall_pre_chmod(filename, mode)                          \
  __sanitizer_syscall_pre_impl_chmod((long)(filename), (long)(mode))
#define __sanitizer_syscall_post_chmod(res, filename, mode)                    \
  __sanitizer_syscall_post_impl_chmod(res, (long)(filename), (long)(mode))
#define __sanitizer_syscall_pre_fchmod(fd, mode)                               \
  __sanitizer_syscall_pre_impl_fchmod((long)(fd), (long)(mode))
#define __sanitizer_syscall_post_fchmod(res, fd, mode)                         \
  __sanitizer_syscall_post_impl_fchmod(res, (long)(fd), (long)(mode))
#define __sanitizer_syscall_pre_fcntl(fd, cmd, arg)                            \
  __sanitizer_syscall_pre_impl_fcntl((long)(fd), (long)(cmd), (long)(arg))
#define __sanitizer_syscall_post_fcntl(res, fd, cmd, arg)                      \
  __sanitizer_syscall_post_impl_fcntl(res, (long)(fd), (long)(cmd), (long)(arg))
#define __sanitizer_syscall_pre_fcntl64(fd, cmd, arg)                          \
  __sanitizer_syscall_pre_impl_fcntl64((long)(fd), (long)(cmd), (long)(arg))
#define __sanitizer_syscall_post_fcntl64(res, fd, cmd, arg)                    \
  __sanitizer_syscall_post_impl_fcntl64(res, (long)(fd), (long)(cmd),          \
                                        (long)(arg))
#define __sanitizer_syscall_pre_pipe(fildes)                                   \
  __sanitizer_syscall_pre_impl_pipe((long)(fildes))
#define __sanitizer_syscall_post_pipe(res, fildes)                             \
  __sanitizer_syscall_post_impl_pipe(res, (long)(fildes))
#define __sanitizer_syscall_pre_pipe2(fildes, flags)                           \
  __sanitizer_syscall_pre_impl_pipe2((long)(fildes), (long)(flags))
#define __sanitizer_syscall_post_pipe2(res, fildes, flags)                     \
  __sanitizer_syscall_post_impl_pipe2(res, (long)(fildes), (long)(flags))
#define __sanitizer_syscall_pre_dup(fildes)                                    \
  __sanitizer_syscall_pre_impl_dup((long)(fildes))
#define __sanitizer_syscall_post_dup(res, fildes)                              \
  __sanitizer_syscall_post_impl_dup(res, (long)(fildes))
#define __sanitizer_syscall_pre_dup2(oldfd, newfd)                             \
  __sanitizer_syscall_pre_impl_dup2((long)(oldfd), (long)(newfd))
#define __sanitizer_syscall_post_dup2(res, oldfd, newfd)                       \
  __sanitizer_syscall_post_impl_dup2(res, (long)(oldfd), (long)(newfd))
#define __sanitizer_syscall_pre_dup3(oldfd, newfd, flags)                      \
  __sanitizer_syscall_pre_impl_dup3((long)(oldfd), (long)(newfd), (long)(flags))
#define __sanitizer_syscall_post_dup3(res, oldfd, newfd, flags)                \
  __sanitizer_syscall_post_impl_dup3(res, (long)(oldfd), (long)(newfd),        \
                                     (long)(flags))
#define __sanitizer_syscall_pre_ioperm(from, num, on)                          \
  __sanitizer_syscall_pre_impl_ioperm((long)(from), (long)(num), (long)(on))
#define __sanitizer_syscall_post_ioperm(res, from, num, on)                    \
  __sanitizer_syscall_post_impl_ioperm(res, (long)(from), (long)(num),         \
                                       (long)(on))
#define __sanitizer_syscall_pre_ioctl(fd, cmd, arg)                            \
  __sanitizer_syscall_pre_impl_ioctl((long)(fd), (long)(cmd), (long)(arg))
#define __sanitizer_syscall_post_ioctl(res, fd, cmd, arg)                      \
  __sanitizer_syscall_post_impl_ioctl(res, (long)(fd), (long)(cmd), (long)(arg))
#define __sanitizer_syscall_pre_flock(fd, cmd)                                 \
  __sanitizer_syscall_pre_impl_flock((long)(fd), (long)(cmd))
#define __sanitizer_syscall_post_flock(res, fd, cmd)                           \
  __sanitizer_syscall_post_impl_flock(res, (long)(fd), (long)(cmd))
#define __sanitizer_syscall_pre_io_setup(nr_reqs, ctx)                         \
  __sanitizer_syscall_pre_impl_io_setup((long)(nr_reqs), (long)(ctx))
#define __sanitizer_syscall_post_io_setup(res, nr_reqs, ctx)                   \
  __sanitizer_syscall_post_impl_io_setup(res, (long)(nr_reqs), (long)(ctx))
#define __sanitizer_syscall_pre_io_destroy(ctx)                                \
  __sanitizer_syscall_pre_impl_io_destroy((long)(ctx))
#define __sanitizer_syscall_post_io_destroy(res, ctx)                          \
  __sanitizer_syscall_post_impl_io_destroy(res, (long)(ctx))
#define __sanitizer_syscall_pre_io_getevents(ctx_id, min_nr, nr, events,       \
                                             timeout)                          \
  __sanitizer_syscall_pre_impl_io_getevents((long)(ctx_id), (long)(min_nr),    \
                                            (long)(nr), (long)(events),        \
                                            (long)(timeout))
#define __sanitizer_syscall_post_io_getevents(res, ctx_id, min_nr, nr, events, \
                                              timeout)                         \
  __sanitizer_syscall_post_impl_io_getevents(res, (long)(ctx_id),              \
                                             (long)(min_nr), (long)(nr),       \
                                             (long)(events), (long)(timeout))
#define __sanitizer_syscall_pre_io_submit(ctx_id, arg1, arg2)                  \
  __sanitizer_syscall_pre_impl_io_submit((long)(ctx_id), (long)(arg1),         \
                                         (long)(arg2))
#define __sanitizer_syscall_post_io_submit(res, ctx_id, arg1, arg2)            \
  __sanitizer_syscall_post_impl_io_submit(res, (long)(ctx_id), (long)(arg1),   \
                                          (long)(arg2))
#define __sanitizer_syscall_pre_io_cancel(ctx_id, iocb, result)                \
  __sanitizer_syscall_pre_impl_io_cancel((long)(ctx_id), (long)(iocb),         \
                                         (long)(result))
#define __sanitizer_syscall_post_io_cancel(res, ctx_id, iocb, result)          \
  __sanitizer_syscall_post_impl_io_cancel(res, (long)(ctx_id), (long)(iocb),   \
                                          (long)(result))
#define __sanitizer_syscall_pre_sendfile(out_fd, in_fd, offset, count)         \
  __sanitizer_syscall_pre_impl_sendfile((long)(out_fd), (long)(in_fd),         \
                                        (long)(offset), (long)(count))
#define __sanitizer_syscall_post_sendfile(res, out_fd, in_fd, offset, count)   \
  __sanitizer_syscall_post_impl_sendfile(res, (long)(out_fd), (long)(in_fd),   \
                                         (long)(offset), (long)(count))
#define __sanitizer_syscall_pre_sendfile64(out_fd, in_fd, offset, count)       \
  __sanitizer_syscall_pre_impl_sendfile64((long)(out_fd), (long)(in_fd),       \
                                          (long)(offset), (long)(count))
#define __sanitizer_syscall_post_sendfile64(res, out_fd, in_fd, offset, count) \
  __sanitizer_syscall_post_impl_sendfile64(res, (long)(out_fd), (long)(in_fd), \
                                           (long)(offset), (long)(count))
#define __sanitizer_syscall_pre_readlink(path, buf, bufsiz)                    \
  __sanitizer_syscall_pre_impl_readlink((long)(path), (long)(buf),             \
                                        (long)(bufsiz))
#define __sanitizer_syscall_post_readlink(res, path, buf, bufsiz)              \
  __sanitizer_syscall_post_impl_readlink(res, (long)(path), (long)(buf),       \
                                         (long)(bufsiz))
#define __sanitizer_syscall_pre_creat(pathname, mode)                          \
  __sanitizer_syscall_pre_impl_creat((long)(pathname), (long)(mode))
#define __sanitizer_syscall_post_creat(res, pathname, mode)                    \
  __sanitizer_syscall_post_impl_creat(res, (long)(pathname), (long)(mode))
#define __sanitizer_syscall_pre_open(filename, flags, mode)                    \
  __sanitizer_syscall_pre_impl_open((long)(filename), (long)(flags),           \
                                    (long)(mode))
#define __sanitizer_syscall_post_open(res, filename, flags, mode)              \
  __sanitizer_syscall_post_impl_open(res, (long)(filename), (long)(flags),     \
                                     (long)(mode))
#define __sanitizer_syscall_pre_close(fd)                                      \
  __sanitizer_syscall_pre_impl_close((long)(fd))
#define __sanitizer_syscall_post_close(res, fd)                                \
  __sanitizer_syscall_post_impl_close(res, (long)(fd))
#define __sanitizer_syscall_pre_access(filename, mode)                         \
  __sanitizer_syscall_pre_impl_access((long)(filename), (long)(mode))
#define __sanitizer_syscall_post_access(res, filename, mode)                   \
  __sanitizer_syscall_post_impl_access(res, (long)(filename), (long)(mode))
#define __sanitizer_syscall_pre_vhangup() __sanitizer_syscall_pre_impl_vhangup()
#define __sanitizer_syscall_post_vhangup(res)                                  \
  __sanitizer_syscall_post_impl_vhangup(res)
#define __sanitizer_syscall_pre_chown(filename, user, group)                   \
  __sanitizer_syscall_pre_impl_chown((long)(filename), (long)(user),           \
                                     (long)(group))
#define __sanitizer_syscall_post_chown(res, filename, user, group)             \
  __sanitizer_syscall_post_impl_chown(res, (long)(filename), (long)(user),     \
                                      (long)(group))
#define __sanitizer_syscall_pre_lchown(filename, user, group)                  \
  __sanitizer_syscall_pre_impl_lchown((long)(filename), (long)(user),          \
                                      (long)(group))
#define __sanitizer_syscall_post_lchown(res, filename, user, group)            \
  __sanitizer_syscall_post_impl_lchown(res, (long)(filename), (long)(user),    \
                                       (long)(group))
#define __sanitizer_syscall_pre_fchown(fd, user, group)                        \
  __sanitizer_syscall_pre_impl_fchown((long)(fd), (long)(user), (long)(group))
#define __sanitizer_syscall_post_fchown(res, fd, user, group)                  \
  __sanitizer_syscall_post_impl_fchown(res, (long)(fd), (long)(user),          \
                                       (long)(group))
#define __sanitizer_syscall_pre_chown16(filename, user, group)                 \
  __sanitizer_syscall_pre_impl_chown16((long)(filename), (long)user,           \
                                       (long)group)
#define __sanitizer_syscall_post_chown16(res, filename, user, group)           \
  __sanitizer_syscall_post_impl_chown16(res, (long)(filename), (long)user,     \
                                        (long)group)
#define __sanitizer_syscall_pre_lchown16(filename, user, group)                \
  __sanitizer_syscall_pre_impl_lchown16((long)(filename), (long)user,          \
                                        (long)group)
#define __sanitizer_syscall_post_lchown16(res, filename, user, group)          \
  __sanitizer_syscall_post_impl_lchown16(res, (long)(filename), (long)user,    \
                                         (long)group)
#define __sanitizer_syscall_pre_fchown16(fd, user, group)                      \
  __sanitizer_syscall_pre_impl_fchown16((long)(fd), (long)user, (long)group)
#define __sanitizer_syscall_post_fchown16(res, fd, user, group)                \
  __sanitizer_syscall_post_impl_fchown16(res, (long)(fd), (long)user,          \
                                         (long)group)
#define __sanitizer_syscall_pre_setregid16(rgid, egid)                         \
  __sanitizer_syscall_pre_impl_setregid16((long)rgid, (long)egid)
#define __sanitizer_syscall_post_setregid16(res, rgid, egid)                   \
  __sanitizer_syscall_post_impl_setregid16(res, (long)rgid, (long)egid)
#define __sanitizer_syscall_pre_setgid16(gid)                                  \
  __sanitizer_syscall_pre_impl_setgid16((long)gid)
#define __sanitizer_syscall_post_setgid16(res, gid)                            \
  __sanitizer_syscall_post_impl_setgid16(res, (long)gid)
#define __sanitizer_syscall_pre_setreuid16(ruid, euid)                         \
  __sanitizer_syscall_pre_impl_setreuid16((long)ruid, (long)euid)
#define __sanitizer_syscall_post_setreuid16(res, ruid, euid)                   \
  __sanitizer_syscall_post_impl_setreuid16(res, (long)ruid, (long)euid)
#define __sanitizer_syscall_pre_setuid16(uid)                                  \
  __sanitizer_syscall_pre_impl_setuid16((long)uid)
#define __sanitizer_syscall_post_setuid16(res, uid)                            \
  __sanitizer_syscall_post_impl_setuid16(res, (long)uid)
#define __sanitizer_syscall_pre_setresuid16(ruid, euid, suid)                  \
  __sanitizer_syscall_pre_impl_setresuid16((long)ruid, (long)euid, (long)suid)
#define __sanitizer_syscall_post_setresuid16(res, ruid, euid, suid)            \
  __sanitizer_syscall_post_impl_setresuid16(res, (long)ruid, (long)euid,       \
                                            (long)suid)
#define __sanitizer_syscall_pre_getresuid16(ruid, euid, suid)                  \
  __sanitizer_syscall_pre_impl_getresuid16((long)(ruid), (long)(euid),         \
                                           (long)(suid))
#define __sanitizer_syscall_post_getresuid16(res, ruid, euid, suid)            \
  __sanitizer_syscall_post_impl_getresuid16(res, (long)(ruid), (long)(euid),   \
                                            (long)(suid))
#define __sanitizer_syscall_pre_setresgid16(rgid, egid, sgid)                  \
  __sanitizer_syscall_pre_impl_setresgid16((long)rgid, (long)egid, (long)sgid)
#define __sanitizer_syscall_post_setresgid16(res, rgid, egid, sgid)            \
  __sanitizer_syscall_post_impl_setresgid16(res, (long)rgid, (long)egid,       \
                                            (long)sgid)
#define __sanitizer_syscall_pre_getresgid16(rgid, egid, sgid)                  \
  __sanitizer_syscall_pre_impl_getresgid16((long)(rgid), (long)(egid),         \
                                           (long)(sgid))
#define __sanitizer_syscall_post_getresgid16(res, rgid, egid, sgid)            \
  __sanitizer_syscall_post_impl_getresgid16(res, (long)(rgid), (long)(egid),   \
                                            (long)(sgid))
#define __sanitizer_syscall_pre_setfsuid16(uid)                                \
  __sanitizer_syscall_pre_impl_setfsuid16((long)uid)
#define __sanitizer_syscall_post_setfsuid16(res, uid)                          \
  __sanitizer_syscall_post_impl_setfsuid16(res, (long)uid)
#define __sanitizer_syscall_pre_setfsgid16(gid)                                \
  __sanitizer_syscall_pre_impl_setfsgid16((long)gid)
#define __sanitizer_syscall_post_setfsgid16(res, gid)                          \
  __sanitizer_syscall_post_impl_setfsgid16(res, (long)gid)
#define __sanitizer_syscall_pre_getgroups16(gidsetsize, grouplist)             \
  __sanitizer_syscall_pre_impl_getgroups16((long)(gidsetsize),                 \
                                           (long)(grouplist))
#define __sanitizer_syscall_post_getgroups16(res, gidsetsize, grouplist)       \
  __sanitizer_syscall_post_impl_getgroups16(res, (long)(gidsetsize),           \
                                            (long)(grouplist))
#define __sanitizer_syscall_pre_setgroups16(gidsetsize, grouplist)             \
  __sanitizer_syscall_pre_impl_setgroups16((long)(gidsetsize),                 \
                                           (long)(grouplist))
#define __sanitizer_syscall_post_setgroups16(res, gidsetsize, grouplist)       \
  __sanitizer_syscall_post_impl_setgroups16(res, (long)(gidsetsize),           \
                                            (long)(grouplist))
#define __sanitizer_syscall_pre_getuid16()                                     \
  __sanitizer_syscall_pre_impl_getuid16()
#define __sanitizer_syscall_post_getuid16(res)                                 \
  __sanitizer_syscall_post_impl_getuid16(res)
#define __sanitizer_syscall_pre_geteuid16()                                    \
  __sanitizer_syscall_pre_impl_geteuid16()
#define __sanitizer_syscall_post_geteuid16(res)                                \
  __sanitizer_syscall_post_impl_geteuid16(res)
#define __sanitizer_syscall_pre_getgid16()                                     \
  __sanitizer_syscall_pre_impl_getgid16()
#define __sanitizer_syscall_post_getgid16(res)                                 \
  __sanitizer_syscall_post_impl_getgid16(res)
#define __sanitizer_syscall_pre_getegid16()                                    \
  __sanitizer_syscall_pre_impl_getegid16()
#define __sanitizer_syscall_post_getegid16(res)                                \
  __sanitizer_syscall_post_impl_getegid16(res)
#define __sanitizer_syscall_pre_utime(filename, times)                         \
  __sanitizer_syscall_pre_impl_utime((long)(filename), (long)(times))
#define __sanitizer_syscall_post_utime(res, filename, times)                   \
  __sanitizer_syscall_post_impl_utime(res, (long)(filename), (long)(times))
#define __sanitizer_syscall_pre_utimes(filename, utimes)                       \
  __sanitizer_syscall_pre_impl_utimes((long)(filename), (long)(utimes))
#define __sanitizer_syscall_post_utimes(res, filename, utimes)                 \
  __sanitizer_syscall_post_impl_utimes(res, (long)(filename), (long)(utimes))
#define __sanitizer_syscall_pre_lseek(fd, offset, origin)                      \
  __sanitizer_syscall_pre_impl_lseek((long)(fd), (long)(offset), (long)(origin))
#define __sanitizer_syscall_post_lseek(res, fd, offset, origin)                \
  __sanitizer_syscall_post_impl_lseek(res, (long)(fd), (long)(offset),         \
                                      (long)(origin))
#define __sanitizer_syscall_pre_llseek(fd, offset_high, offset_low, result,    \
                                       origin)                                 \
  __sanitizer_syscall_pre_impl_llseek((long)(fd), (long)(offset_high),         \
                                      (long)(offset_low), (long)(result),      \
                                      (long)(origin))
#define __sanitizer_syscall_post_llseek(res, fd, offset_high, offset_low,      \
                                        result, origin)                        \
  __sanitizer_syscall_post_impl_llseek(res, (long)(fd), (long)(offset_high),   \
                                       (long)(offset_low), (long)(result),     \
                                       (long)(origin))
#define __sanitizer_syscall_pre_read(fd, buf, count)                           \
  __sanitizer_syscall_pre_impl_read((long)(fd), (long)(buf), (long)(count))
#define __sanitizer_syscall_post_read(res, fd, buf, count)                     \
  __sanitizer_syscall_post_impl_read(res, (long)(fd), (long)(buf),             \
                                     (long)(count))
#define __sanitizer_syscall_pre_readv(fd, vec, vlen)                           \
  __sanitizer_syscall_pre_impl_readv((long)(fd), (long)(vec), (long)(vlen))
#define __sanitizer_syscall_post_readv(res, fd, vec, vlen)                     \
  __sanitizer_syscall_post_impl_readv(res, (long)(fd), (long)(vec),            \
                                      (long)(vlen))
#define __sanitizer_syscall_pre_write(fd, buf, count)                          \
  __sanitizer_syscall_pre_impl_write((long)(fd), (long)(buf), (long)(count))
#define __sanitizer_syscall_post_write(res, fd, buf, count)                    \
  __sanitizer_syscall_post_impl_write(res, (long)(fd), (long)(buf),            \
                                      (long)(count))
#define __sanitizer_syscall_pre_writev(fd, vec, vlen)                          \
  __sanitizer_syscall_pre_impl_writev((long)(fd), (long)(vec), (long)(vlen))
#define __sanitizer_syscall_post_writev(res, fd, vec, vlen)                    \
  __sanitizer_syscall_post_impl_writev(res, (long)(fd), (long)(vec),           \
                                       (long)(vlen))

#ifdef _LP64
#define __sanitizer_syscall_pre_pread64(fd, buf, count, pos)                   \
  __sanitizer_syscall_pre_impl_pread64((long)(fd), (long)(buf), (long)(count), \
                                       (long)(pos))
#define __sanitizer_syscall_post_pread64(res, fd, buf, count, pos)             \
  __sanitizer_syscall_post_impl_pread64(res, (long)(fd), (long)(buf),          \
                                        (long)(count), (long)(pos))
#define __sanitizer_syscall_pre_pwrite64(fd, buf, count, pos)                  \
  __sanitizer_syscall_pre_impl_pwrite64((long)(fd), (long)(buf),               \
                                        (long)(count), (long)(pos))
#define __sanitizer_syscall_post_pwrite64(res, fd, buf, count, pos)            \
  __sanitizer_syscall_post_impl_pwrite64(res, (long)(fd), (long)(buf),         \
                                         (long)(count), (long)(pos))
#else
#define __sanitizer_syscall_pre_pread64(fd, buf, count, pos0, pos1)            \
  __sanitizer_syscall_pre_impl_pread64((long)(fd), (long)(buf), (long)(count), \
                                       (long)(pos0), (long)(pos1))
#define __sanitizer_syscall_post_pread64(res, fd, buf, count, pos0, pos1)      \
  __sanitizer_syscall_post_impl_pread64(                                       \
      res, (long)(fd), (long)(buf), (long)(count), (long)(pos0), (long)(pos1))
#define __sanitizer_syscall_pre_pwrite64(fd, buf, count, pos0, pos1)           \
  __sanitizer_syscall_pre_impl_pwrite64(                                       \
      (long)(fd), (long)(buf), (long)(count), (long)(pos0), (long)(pos1))
#define __sanitizer_syscall_post_pwrite64(res, fd, buf, count, pos0, pos1)     \
  __sanitizer_syscall_post_impl_pwrite64(                                      \
      res, (long)(fd), (long)(buf), (long)(count), (long)(pos0), (long)(pos1))
#endif

#define __sanitizer_syscall_pre_preadv(fd, vec, vlen, pos_l, pos_h)            \
  __sanitizer_syscall_pre_impl_preadv((long)(fd), (long)(vec), (long)(vlen),   \
                                      (long)(pos_l), (long)(pos_h))
#define __sanitizer_syscall_post_preadv(res, fd, vec, vlen, pos_l, pos_h)      \
  __sanitizer_syscall_post_impl_preadv(res, (long)(fd), (long)(vec),           \
                                       (long)(vlen), (long)(pos_l),            \
                                       (long)(pos_h))
#define __sanitizer_syscall_pre_pwritev(fd, vec, vlen, pos_l, pos_h)           \
  __sanitizer_syscall_pre_impl_pwritev((long)(fd), (long)(vec), (long)(vlen),  \
                                       (long)(pos_l), (long)(pos_h))
#define __sanitizer_syscall_post_pwritev(res, fd, vec, vlen, pos_l, pos_h)     \
  __sanitizer_syscall_post_impl_pwritev(res, (long)(fd), (long)(vec),          \
                                        (long)(vlen), (long)(pos_l),           \
                                        (long)(pos_h))
#define __sanitizer_syscall_pre_getcwd(buf, size)                              \
  __sanitizer_syscall_pre_impl_getcwd((long)(buf), (long)(size))
#define __sanitizer_syscall_post_getcwd(res, buf, size)                        \
  __sanitizer_syscall_post_impl_getcwd(res, (long)(buf), (long)(size))
#define __sanitizer_syscall_pre_mkdir(pathname, mode)                          \
  __sanitizer_syscall_pre_impl_mkdir((long)(pathname), (long)(mode))
#define __sanitizer_syscall_post_mkdir(res, pathname, mode)                    \
  __sanitizer_syscall_post_impl_mkdir(res, (long)(pathname), (long)(mode))
#define __sanitizer_syscall_pre_chdir(filename)                                \
  __sanitizer_syscall_pre_impl_chdir((long)(filename))
#define __sanitizer_syscall_post_chdir(res, filename)                          \
  __sanitizer_syscall_post_impl_chdir(res, (long)(filename))
#define __sanitizer_syscall_pre_fchdir(fd)                                     \
  __sanitizer_syscall_pre_impl_fchdir((long)(fd))
#define __sanitizer_syscall_post_fchdir(res, fd)                               \
  __sanitizer_syscall_post_impl_fchdir(res, (long)(fd))
#define __sanitizer_syscall_pre_rmdir(pathname)                                \
  __sanitizer_syscall_pre_impl_rmdir((long)(pathname))
#define __sanitizer_syscall_post_rmdir(res, pathname)                          \
  __sanitizer_syscall_post_impl_rmdir(res, (long)(pathname))
#define __sanitizer_syscall_pre_lookup_dcookie(cookie64, buf, len)             \
  __sanitizer_syscall_pre_impl_lookup_dcookie((long)(cookie64), (long)(buf),   \
                                              (long)(len))
#define __sanitizer_syscall_post_lookup_dcookie(res, cookie64, buf, len)       \
  __sanitizer_syscall_post_impl_lookup_dcookie(res, (long)(cookie64),          \
                                               (long)(buf), (long)(len))
#define __sanitizer_syscall_pre_quotactl(cmd, special, id, addr)               \
  __sanitizer_syscall_pre_impl_quotactl((long)(cmd), (long)(special),          \
                                        (long)(id), (long)(addr))
#define __sanitizer_syscall_post_quotactl(res, cmd, special, id, addr)         \
  __sanitizer_syscall_post_impl_quotactl(res, (long)(cmd), (long)(special),    \
                                         (long)(id), (long)(addr))
#define __sanitizer_syscall_pre_getdents(fd, dirent, count)                    \
  __sanitizer_syscall_pre_impl_getdents((long)(fd), (long)(dirent),            \
                                        (long)(count))
#define __sanitizer_syscall_post_getdents(res, fd, dirent, count)              \
  __sanitizer_syscall_post_impl_getdents(res, (long)(fd), (long)(dirent),      \
                                         (long)(count))
#define __sanitizer_syscall_pre_getdents64(fd, dirent, count)                  \
  __sanitizer_syscall_pre_impl_getdents64((long)(fd), (long)(dirent),          \
                                          (long)(count))
#define __sanitizer_syscall_post_getdents64(res, fd, dirent, count)            \
  __sanitizer_syscall_post_impl_getdents64(res, (long)(fd), (long)(dirent),    \
                                           (long)(count))
#define __sanitizer_syscall_pre_setsockopt(fd, level, optname, optval, optlen) \
  __sanitizer_syscall_pre_impl_setsockopt((long)(fd), (long)(level),           \
                                          (long)(optname), (long)(optval),     \
                                          (long)(optlen))
#define __sanitizer_syscall_post_setsockopt(res, fd, level, optname, optval,   \
                                            optlen)                            \
  __sanitizer_syscall_post_impl_setsockopt(res, (long)(fd), (long)(level),     \
                                           (long)(optname), (long)(optval),    \
                                           (long)(optlen))
#define __sanitizer_syscall_pre_getsockopt(fd, level, optname, optval, optlen) \
  __sanitizer_syscall_pre_impl_getsockopt((long)(fd), (long)(level),           \
                                          (long)(optname), (long)(optval),     \
                                          (long)(optlen))
#define __sanitizer_syscall_post_getsockopt(res, fd, level, optname, optval,   \
                                            optlen)                            \
  __sanitizer_syscall_post_impl_getsockopt(res, (long)(fd), (long)(level),     \
                                           (long)(optname), (long)(optval),    \
                                           (long)(optlen))
#define __sanitizer_syscall_pre_bind(arg0, arg1, arg2)                         \
  __sanitizer_syscall_pre_impl_bind((long)(arg0), (long)(arg1), (long)(arg2))
#define __sanitizer_syscall_post_bind(res, arg0, arg1, arg2)                   \
  __sanitizer_syscall_post_impl_bind(res, (long)(arg0), (long)(arg1),          \
                                     (long)(arg2))
#define __sanitizer_syscall_pre_connect(arg0, arg1, arg2)                      \
  __sanitizer_syscall_pre_impl_connect((long)(arg0), (long)(arg1), (long)(arg2))
#define __sanitizer_syscall_post_connect(res, arg0, arg1, arg2)                \
  __sanitizer_syscall_post_impl_connect(res, (long)(arg0), (long)(arg1),       \
                                        (long)(arg2))
#define __sanitizer_syscall_pre_accept(arg0, arg1, arg2)                       \
  __sanitizer_syscall_pre_impl_accept((long)(arg0), (long)(arg1), (long)(arg2))
#define __sanitizer_syscall_post_accept(res, arg0, arg1, arg2)                 \
  __sanitizer_syscall_post_impl_accept(res, (long)(arg0), (long)(arg1),        \
                                       (long)(arg2))
#define __sanitizer_syscall_pre_accept4(arg0, arg1, arg2, arg3)                \
  __sanitizer_syscall_pre_impl_accept4((long)(arg0), (long)(arg1),             \
                                       (long)(arg2), (long)(arg3))
#define __sanitizer_syscall_post_accept4(res, arg0, arg1, arg2, arg3)          \
  __sanitizer_syscall_post_impl_accept4(res, (long)(arg0), (long)(arg1),       \
                                        (long)(arg2), (long)(arg3))
#define __sanitizer_syscall_pre_getsockname(arg0, arg1, arg2)                  \
  __sanitizer_syscall_pre_impl_getsockname((long)(arg0), (long)(arg1),         \
                                           (long)(arg2))
#define __sanitizer_syscall_post_getsockname(res, arg0, arg1, arg2)            \
  __sanitizer_syscall_post_impl_getsockname(res, (long)(arg0), (long)(arg1),   \
                                            (long)(arg2))
#define __sanitizer_syscall_pre_getpeername(arg0, arg1, arg2)                  \
  __sanitizer_syscall_pre_impl_getpeername((long)(arg0), (long)(arg1),         \
                                           (long)(arg2))
#define __sanitizer_syscall_post_getpeername(res, arg0, arg1, arg2)            \
  __sanitizer_syscall_post_impl_getpeername(res, (long)(arg0), (long)(arg1),   \
                                            (long)(arg2))
#define __sanitizer_syscall_pre_send(arg0, arg1, arg2, arg3)                   \
  __sanitizer_syscall_pre_impl_send((long)(arg0), (long)(arg1), (long)(arg2),  \
                                    (long)(arg3))
#define __sanitizer_syscall_post_send(res, arg0, arg1, arg2, arg3)             \
  __sanitizer_syscall_post_impl_send(res, (long)(arg0), (long)(arg1),          \
                                     (long)(arg2), (long)(arg3))
#define __sanitizer_syscall_pre_sendto(arg0, arg1, arg2, arg3, arg4, arg5)     \
  __sanitizer_syscall_pre_impl_sendto((long)(arg0), (long)(arg1),              \
                                      (long)(arg2), (long)(arg3),              \
                                      (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_post_sendto(res, arg0, arg1, arg2, arg3, arg4,     \
                                        arg5)                                  \
  __sanitizer_syscall_post_impl_sendto(res, (long)(arg0), (long)(arg1),        \
                                       (long)(arg2), (long)(arg3),             \
                                       (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_pre_sendmsg(fd, msg, flags)                        \
  __sanitizer_syscall_pre_impl_sendmsg((long)(fd), (long)(msg), (long)(flags))
#define __sanitizer_syscall_post_sendmsg(res, fd, msg, flags)                  \
  __sanitizer_syscall_post_impl_sendmsg(res, (long)(fd), (long)(msg),          \
                                        (long)(flags))
#define __sanitizer_syscall_pre_sendmmsg(fd, msg, vlen, flags)                 \
  __sanitizer_syscall_pre_impl_sendmmsg((long)(fd), (long)(msg), (long)(vlen), \
                                        (long)(flags))
#define __sanitizer_syscall_post_sendmmsg(res, fd, msg, vlen, flags)           \
  __sanitizer_syscall_post_impl_sendmmsg(res, (long)(fd), (long)(msg),         \
                                         (long)(vlen), (long)(flags))
#define __sanitizer_syscall_pre_recv(arg0, arg1, arg2, arg3)                   \
  __sanitizer_syscall_pre_impl_recv((long)(arg0), (long)(arg1), (long)(arg2),  \
                                    (long)(arg3))
#define __sanitizer_syscall_post_recv(res, arg0, arg1, arg2, arg3)             \
  __sanitizer_syscall_post_impl_recv(res, (long)(arg0), (long)(arg1),          \
                                     (long)(arg2), (long)(arg3))
#define __sanitizer_syscall_pre_recvfrom(arg0, arg1, arg2, arg3, arg4, arg5)   \
  __sanitizer_syscall_pre_impl_recvfrom((long)(arg0), (long)(arg1),            \
                                        (long)(arg2), (long)(arg3),            \
                                        (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_post_recvfrom(res, arg0, arg1, arg2, arg3, arg4,   \
                                          arg5)                                \
  __sanitizer_syscall_post_impl_recvfrom(res, (long)(arg0), (long)(arg1),      \
                                         (long)(arg2), (long)(arg3),           \
                                         (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_pre_recvmsg(fd, msg, flags)                        \
  __sanitizer_syscall_pre_impl_recvmsg((long)(fd), (long)(msg), (long)(flags))
#define __sanitizer_syscall_post_recvmsg(res, fd, msg, flags)                  \
  __sanitizer_syscall_post_impl_recvmsg(res, (long)(fd), (long)(msg),          \
                                        (long)(flags))
#define __sanitizer_syscall_pre_recvmmsg(fd, msg, vlen, flags, timeout)        \
  __sanitizer_syscall_pre_impl_recvmmsg((long)(fd), (long)(msg), (long)(vlen), \
                                        (long)(flags), (long)(timeout))
#define __sanitizer_syscall_post_recvmmsg(res, fd, msg, vlen, flags, timeout)  \
  __sanitizer_syscall_post_impl_recvmmsg(res, (long)(fd), (long)(msg),         \
                                         (long)(vlen), (long)(flags),          \
                                         (long)(timeout))
#define __sanitizer_syscall_pre_socket(arg0, arg1, arg2)                       \
  __sanitizer_syscall_pre_impl_socket((long)(arg0), (long)(arg1), (long)(arg2))
#define __sanitizer_syscall_post_socket(res, arg0, arg1, arg2)                 \
  __sanitizer_syscall_post_impl_socket(res, (long)(arg0), (long)(arg1),        \
                                       (long)(arg2))
#define __sanitizer_syscall_pre_socketpair(arg0, arg1, arg2, arg3)             \
  __sanitizer_syscall_pre_impl_socketpair((long)(arg0), (long)(arg1),          \
                                          (long)(arg2), (long)(arg3))
#define __sanitizer_syscall_post_socketpair(res, arg0, arg1, arg2, arg3)       \
  __sanitizer_syscall_post_impl_socketpair(res, (long)(arg0), (long)(arg1),    \
                                           (long)(arg2), (long)(arg3))
#define __sanitizer_syscall_pre_socketcall(call, args)                         \
  __sanitizer_syscall_pre_impl_socketcall((long)(call), (long)(args))
#define __sanitizer_syscall_post_socketcall(res, call, args)                   \
  __sanitizer_syscall_post_impl_socketcall(res, (long)(call), (long)(args))
#define __sanitizer_syscall_pre_listen(arg0, arg1)                             \
  __sanitizer_syscall_pre_impl_listen((long)(arg0), (long)(arg1))
#define __sanitizer_syscall_post_listen(res, arg0, arg1)                       \
  __sanitizer_syscall_post_impl_listen(res, (long)(arg0), (long)(arg1))
#define __sanitizer_syscall_pre_poll(ufds, nfds, timeout)                      \
  __sanitizer_syscall_pre_impl_poll((long)(ufds), (long)(nfds), (long)(timeout))
#define __sanitizer_syscall_post_poll(res, ufds, nfds, timeout)                \
  __sanitizer_syscall_post_impl_poll(res, (long)(ufds), (long)(nfds),          \
                                     (long)(timeout))
#define __sanitizer_syscall_pre_select(n, inp, outp, exp, tvp)                 \
  __sanitizer_syscall_pre_impl_select((long)(n), (long)(inp), (long)(outp),    \
                                      (long)(exp), (long)(tvp))
#define __sanitizer_syscall_post_select(res, n, inp, outp, exp, tvp)           \
  __sanitizer_syscall_post_impl_select(res, (long)(n), (long)(inp),            \
                                       (long)(outp), (long)(exp), (long)(tvp))
#define __sanitizer_syscall_pre_old_select(arg)                                \
  __sanitizer_syscall_pre_impl_old_select((long)(arg))
#define __sanitizer_syscall_post_old_select(res, arg)                          \
  __sanitizer_syscall_post_impl_old_select(res, (long)(arg))
#define __sanitizer_syscall_pre_epoll_create(size)                             \
  __sanitizer_syscall_pre_impl_epoll_create((long)(size))
#define __sanitizer_syscall_post_epoll_create(res, size)                       \
  __sanitizer_syscall_post_impl_epoll_create(res, (long)(size))
#define __sanitizer_syscall_pre_epoll_create1(flags)                           \
  __sanitizer_syscall_pre_impl_epoll_create1((long)(flags))
#define __sanitizer_syscall_post_epoll_create1(res, flags)                     \
  __sanitizer_syscall_post_impl_epoll_create1(res, (long)(flags))
#define __sanitizer_syscall_pre_epoll_ctl(epfd, op, fd, event)                 \
  __sanitizer_syscall_pre_impl_epoll_ctl((long)(epfd), (long)(op), (long)(fd), \
                                         (long)(event))
#define __sanitizer_syscall_post_epoll_ctl(res, epfd, op, fd, event)           \
  __sanitizer_syscall_post_impl_epoll_ctl(res, (long)(epfd), (long)(op),       \
                                          (long)(fd), (long)(event))
#define __sanitizer_syscall_pre_epoll_wait(epfd, events, maxevents, timeout)   \
  __sanitizer_syscall_pre_impl_epoll_wait((long)(epfd), (long)(events),        \
                                          (long)(maxevents), (long)(timeout))
#define __sanitizer_syscall_post_epoll_wait(res, epfd, events, maxevents,      \
                                            timeout)                           \
  __sanitizer_syscall_post_impl_epoll_wait(res, (long)(epfd), (long)(events),  \
                                           (long)(maxevents), (long)(timeout))
#define __sanitizer_syscall_pre_epoll_pwait(epfd, events, maxevents, timeout,  \
                                            sigmask, sigsetsize)               \
  __sanitizer_syscall_pre_impl_epoll_pwait(                                    \
      (long)(epfd), (long)(events), (long)(maxevents), (long)(timeout),        \
      (long)(sigmask), (long)(sigsetsize))
#define __sanitizer_syscall_post_epoll_pwait(res, epfd, events, maxevents,     \
                                             timeout, sigmask, sigsetsize)     \
  __sanitizer_syscall_post_impl_epoll_pwait(                                   \
      res, (long)(epfd), (long)(events), (long)(maxevents), (long)(timeout),   \
      (long)(sigmask), (long)(sigsetsize))
#define __sanitizer_syscall_pre_epoll_pwait2(epfd, events, maxevents, timeout, \
                                             sigmask, sigsetsize)              \
  __sanitizer_syscall_pre_impl_epoll_pwait2(                                   \
      (long)(epfd), (long)(events), (long)(maxevents), (long)(timeout),        \
      (long)(sigmask), (long)(sigsetsize))
#define __sanitizer_syscall_post_epoll_pwait2(res, epfd, events, maxevents,    \
                                              timeout, sigmask, sigsetsize)    \
  __sanitizer_syscall_post_impl_epoll_pwait2(                                  \
      res, (long)(epfd), (long)(events), (long)(maxevents), (long)(timeout),   \
      (long)(sigmask), (long)(sigsetsize))
#define __sanitizer_syscall_pre_gethostname(name, len)                         \
  __sanitizer_syscall_pre_impl_gethostname((long)(name), (long)(len))
#define __sanitizer_syscall_post_gethostname(res, name, len)                   \
  __sanitizer_syscall_post_impl_gethostname(res, (long)(name), (long)(len))
#define __sanitizer_syscall_pre_sethostname(name, len)                         \
  __sanitizer_syscall_pre_impl_sethostname((long)(name), (long)(len))
#define __sanitizer_syscall_post_sethostname(res, name, len)                   \
  __sanitizer_syscall_post_impl_sethostname(res, (long)(name), (long)(len))
#define __sanitizer_syscall_pre_setdomainname(name, len)                       \
  __sanitizer_syscall_pre_impl_setdomainname((long)(name), (long)(len))
#define __sanitizer_syscall_post_setdomainname(res, name, len)                 \
  __sanitizer_syscall_post_impl_setdomainname(res, (long)(name), (long)(len))
#define __sanitizer_syscall_pre_newuname(name)                                 \
  __sanitizer_syscall_pre_impl_newuname((long)(name))
#define __sanitizer_syscall_post_newuname(res, name)                           \
  __sanitizer_syscall_post_impl_newuname(res, (long)(name))
#define __sanitizer_syscall_pre_uname(arg0)                                    \
  __sanitizer_syscall_pre_impl_uname((long)(arg0))
#define __sanitizer_syscall_post_uname(res, arg0)                              \
  __sanitizer_syscall_post_impl_uname(res, (long)(arg0))
#define __sanitizer_syscall_pre_olduname(arg0)                                 \
  __sanitizer_syscall_pre_impl_olduname((long)(arg0))
#define __sanitizer_syscall_post_olduname(res, arg0)                           \
  __sanitizer_syscall_post_impl_olduname(res, (long)(arg0))
#define __sanitizer_syscall_pre_getrlimit(resource, rlim)                      \
  __sanitizer_syscall_pre_impl_getrlimit((long)(resource), (long)(rlim))
#define __sanitizer_syscall_post_getrlimit(res, resource, rlim)                \
  __sanitizer_syscall_post_impl_getrlimit(res, (long)(resource), (long)(rlim))
#define __sanitizer_syscall_pre_old_getrlimit(resource, rlim)                  \
  __sanitizer_syscall_pre_impl_old_getrlimit((long)(resource), (long)(rlim))
#define __sanitizer_syscall_post_old_getrlimit(res, resource, rlim)            \
  __sanitizer_syscall_post_impl_old_getrlimit(res, (long)(resource),           \
                                              (long)(rlim))
#define __sanitizer_syscall_pre_setrlimit(resource, rlim)                      \
  __sanitizer_syscall_pre_impl_setrlimit((long)(resource), (long)(rlim))
#define __sanitizer_syscall_post_setrlimit(res, resource, rlim)                \
  __sanitizer_syscall_post_impl_setrlimit(res, (long)(resource), (long)(rlim))
#define __sanitizer_syscall_pre_prlimit64(pid, resource, new_rlim, old_rlim)   \
  __sanitizer_syscall_pre_impl_prlimit64((long)(pid), (long)(resource),        \
                                         (long)(new_rlim), (long)(old_rlim))
#define __sanitizer_syscall_post_prlimit64(res, pid, resource, new_rlim,       \
                                           old_rlim)                           \
  __sanitizer_syscall_post_impl_prlimit64(res, (long)(pid), (long)(resource),  \
                                          (long)(new_rlim), (long)(old_rlim))
#define __sanitizer_syscall_pre_getrusage(who, ru)                             \
  __sanitizer_syscall_pre_impl_getrusage((long)(who), (long)(ru))
#define __sanitizer_syscall_post_getrusage(res, who, ru)                       \
  __sanitizer_syscall_post_impl_getrusage(res, (long)(who), (long)(ru))
#define __sanitizer_syscall_pre_umask(mask)                                    \
  __sanitizer_syscall_pre_impl_umask((long)(mask))
#define __sanitizer_syscall_post_umask(res, mask)                              \
  __sanitizer_syscall_post_impl_umask(res, (long)(mask))
#define __sanitizer_syscall_pre_msgget(key, msgflg)                            \
  __sanitizer_syscall_pre_impl_msgget((long)(key), (long)(msgflg))
#define __sanitizer_syscall_post_msgget(res, key, msgflg)                      \
  __sanitizer_syscall_post_impl_msgget(res, (long)(key), (long)(msgflg))
#define __sanitizer_syscall_pre_msgsnd(msqid, msgp, msgsz, msgflg)             \
  __sanitizer_syscall_pre_impl_msgsnd((long)(msqid), (long)(msgp),             \
                                      (long)(msgsz), (long)(msgflg))
#define __sanitizer_syscall_post_msgsnd(res, msqid, msgp, msgsz, msgflg)       \
  __sanitizer_syscall_post_impl_msgsnd(res, (long)(msqid), (long)(msgp),       \
                                       (long)(msgsz), (long)(msgflg))
#define __sanitizer_syscall_pre_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg)     \
  __sanitizer_syscall_pre_impl_msgrcv((long)(msqid), (long)(msgp),             \
                                      (long)(msgsz), (long)(msgtyp),           \
                                      (long)(msgflg))
#define __sanitizer_syscall_post_msgrcv(res, msqid, msgp, msgsz, msgtyp,       \
                                        msgflg)                                \
  __sanitizer_syscall_post_impl_msgrcv(res, (long)(msqid), (long)(msgp),       \
                                       (long)(msgsz), (long)(msgtyp),          \
                                       (long)(msgflg))
#define __sanitizer_syscall_pre_msgctl(msqid, cmd, buf)                        \
  __sanitizer_syscall_pre_impl_msgctl((long)(msqid), (long)(cmd), (long)(buf))
#define __sanitizer_syscall_post_msgctl(res, msqid, cmd, buf)                  \
  __sanitizer_syscall_post_impl_msgctl(res, (long)(msqid), (long)(cmd),        \
                                       (long)(buf))
#define __sanitizer_syscall_pre_semget(key, nsems, semflg)                     \
  __sanitizer_syscall_pre_impl_semget((long)(key), (long)(nsems),              \
                                      (long)(semflg))
#define __sanitizer_syscall_post_semget(res, key, nsems, semflg)               \
  __sanitizer_syscall_post_impl_semget(res, (long)(key), (long)(nsems),        \
                                       (long)(semflg))
#define __sanitizer_syscall_pre_semop(semid, sops, nsops)                      \
  __sanitizer_syscall_pre_impl_semop((long)(semid), (long)(sops), (long)(nsops))
#define __sanitizer_syscall_post_semop(res, semid, sops, nsops)                \
  __sanitizer_syscall_post_impl_semop(res, (long)(semid), (long)(sops),        \
                                      (long)(nsops))
#define __sanitizer_syscall_pre_semctl(semid, semnum, cmd, arg)                \
  __sanitizer_syscall_pre_impl_semctl((long)(semid), (long)(semnum),           \
                                      (long)(cmd), (long)(arg))
#define __sanitizer_syscall_post_semctl(res, semid, semnum, cmd, arg)          \
  __sanitizer_syscall_post_impl_semctl(res, (long)(semid), (long)(semnum),     \
                                       (long)(cmd), (long)(arg))
#define __sanitizer_syscall_pre_semtimedop(semid, sops, nsops, timeout)        \
  __sanitizer_syscall_pre_impl_semtimedop((long)(semid), (long)(sops),         \
                                          (long)(nsops), (long)(timeout))
#define __sanitizer_syscall_post_semtimedop(res, semid, sops, nsops, timeout)  \
  __sanitizer_syscall_post_impl_semtimedop(res, (long)(semid), (long)(sops),   \
                                           (long)(nsops), (long)(timeout))
#define __sanitizer_syscall_pre_shmat(shmid, shmaddr, shmflg)                  \
  __sanitizer_syscall_pre_impl_shmat((long)(shmid), (long)(shmaddr),           \
                                     (long)(shmflg))
#define __sanitizer_syscall_post_shmat(res, shmid, shmaddr, shmflg)            \
  __sanitizer_syscall_post_impl_shmat(res, (long)(shmid), (long)(shmaddr),     \
                                      (long)(shmflg))
#define __sanitizer_syscall_pre_shmget(key, size, flag)                        \
  __sanitizer_syscall_pre_impl_shmget((long)(key), (long)(size), (long)(flag))
#define __sanitizer_syscall_post_shmget(res, key, size, flag)                  \
  __sanitizer_syscall_post_impl_shmget(res, (long)(key), (long)(size),         \
                                       (long)(flag))
#define __sanitizer_syscall_pre_shmdt(shmaddr)                                 \
  __sanitizer_syscall_pre_impl_shmdt((long)(shmaddr))
#define __sanitizer_syscall_post_shmdt(res, shmaddr)                           \
  __sanitizer_syscall_post_impl_shmdt(res, (long)(shmaddr))
#define __sanitizer_syscall_pre_shmctl(shmid, cmd, buf)                        \
  __sanitizer_syscall_pre_impl_shmctl((long)(shmid), (long)(cmd), (long)(buf))
#define __sanitizer_syscall_post_shmctl(res, shmid, cmd, buf)                  \
  __sanitizer_syscall_post_impl_shmctl(res, (long)(shmid), (long)(cmd),        \
                                       (long)(buf))
#define __sanitizer_syscall_pre_ipc(call, first, second, third, ptr, fifth)    \
  __sanitizer_syscall_pre_impl_ipc((long)(call), (long)(first),                \
                                   (long)(second), (long)(third), (long)(ptr), \
                                   (long)(fifth))
#define __sanitizer_syscall_post_ipc(res, call, first, second, third, ptr,     \
                                     fifth)                                    \
  __sanitizer_syscall_post_impl_ipc(res, (long)(call), (long)(first),          \
                                    (long)(second), (long)(third),             \
                                    (long)(ptr), (long)(fifth))
#define __sanitizer_syscall_pre_mq_open(name, oflag, mode, attr)               \
  __sanitizer_syscall_pre_impl_mq_open((long)(name), (long)(oflag),            \
                                       (long)(mode), (long)(attr))
#define __sanitizer_syscall_post_mq_open(res, name, oflag, mode, attr)         \
  __sanitizer_syscall_post_impl_mq_open(res, (long)(name), (long)(oflag),      \
                                        (long)(mode), (long)(attr))
#define __sanitizer_syscall_pre_mq_unlink(name)                                \
  __sanitizer_syscall_pre_impl_mq_unlink((long)(name))
#define __sanitizer_syscall_post_mq_unlink(res, name)                          \
  __sanitizer_syscall_post_impl_mq_unlink(res, (long)(name))
#define __sanitizer_syscall_pre_mq_timedsend(mqdes, msg_ptr, msg_len,          \
                                             msg_prio, abs_timeout)            \
  __sanitizer_syscall_pre_impl_mq_timedsend((long)(mqdes), (long)(msg_ptr),    \
                                            (long)(msg_len), (long)(msg_prio), \
                                            (long)(abs_timeout))
#define __sanitizer_syscall_post_mq_timedsend(res, mqdes, msg_ptr, msg_len,    \
                                              msg_prio, abs_timeout)           \
  __sanitizer_syscall_post_impl_mq_timedsend(                                  \
      res, (long)(mqdes), (long)(msg_ptr), (long)(msg_len), (long)(msg_prio),  \
      (long)(abs_timeout))
#define __sanitizer_syscall_pre_mq_timedreceive(mqdes, msg_ptr, msg_len,       \
                                                msg_prio, abs_timeout)         \
  __sanitizer_syscall_pre_impl_mq_timedreceive(                                \
      (long)(mqdes), (long)(msg_ptr), (long)(msg_len), (long)(msg_prio),       \
      (long)(abs_timeout))
#define __sanitizer_syscall_post_mq_timedreceive(res, mqdes, msg_ptr, msg_len, \
                                                 msg_prio, abs_timeout)        \
  __sanitizer_syscall_post_impl_mq_timedreceive(                               \
      res, (long)(mqdes), (long)(msg_ptr), (long)(msg_len), (long)(msg_prio),  \
      (long)(abs_timeout))
#define __sanitizer_syscall_pre_mq_notify(mqdes, notification)                 \
  __sanitizer_syscall_pre_impl_mq_notify((long)(mqdes), (long)(notification))
#define __sanitizer_syscall_post_mq_notify(res, mqdes, notification)           \
  __sanitizer_syscall_post_impl_mq_notify(res, (long)(mqdes),                  \
                                          (long)(notification))
#define __sanitizer_syscall_pre_mq_getsetattr(mqdes, mqstat, omqstat)          \
  __sanitizer_syscall_pre_impl_mq_getsetattr((long)(mqdes), (long)(mqstat),    \
                                             (long)(omqstat))
#define __sanitizer_syscall_post_mq_getsetattr(res, mqdes, mqstat, omqstat)    \
  __sanitizer_syscall_post_impl_mq_getsetattr(res, (long)(mqdes),              \
                                              (long)(mqstat), (long)(omqstat))
#define __sanitizer_syscall_pre_pciconfig_iobase(which, bus, devfn)            \
  __sanitizer_syscall_pre_impl_pciconfig_iobase((long)(which), (long)(bus),    \
                                                (long)(devfn))
#define __sanitizer_syscall_post_pciconfig_iobase(res, which, bus, devfn)      \
  __sanitizer_syscall_post_impl_pciconfig_iobase(res, (long)(which),           \
                                                 (long)(bus), (long)(devfn))
#define __sanitizer_syscall_pre_pciconfig_read(bus, dfn, off, len, buf)        \
  __sanitizer_syscall_pre_impl_pciconfig_read(                                 \
      (long)(bus), (long)(dfn), (long)(off), (long)(len), (long)(buf))
#define __sanitizer_syscall_post_pciconfig_read(res, bus, dfn, off, len, buf)  \
  __sanitizer_syscall_post_impl_pciconfig_read(                                \
      res, (long)(bus), (long)(dfn), (long)(off), (long)(len), (long)(buf))
#define __sanitizer_syscall_pre_pciconfig_write(bus, dfn, off, len, buf)       \
  __sanitizer_syscall_pre_impl_pciconfig_write(                                \
      (long)(bus), (long)(dfn), (long)(off), (long)(len), (long)(buf))
#define __sanitizer_syscall_post_pciconfig_write(res, bus, dfn, off, len, buf) \
  __sanitizer_syscall_post_impl_pciconfig_write(                               \
      res, (long)(bus), (long)(dfn), (long)(off), (long)(len), (long)(buf))
#define __sanitizer_syscall_pre_swapon(specialfile, swap_flags)                \
  __sanitizer_syscall_pre_impl_swapon((long)(specialfile), (long)(swap_flags))
#define __sanitizer_syscall_post_swapon(res, specialfile, swap_flags)          \
  __sanitizer_syscall_post_impl_swapon(res, (long)(specialfile),               \
                                       (long)(swap_flags))
#define __sanitizer_syscall_pre_swapoff(specialfile)                           \
  __sanitizer_syscall_pre_impl_swapoff((long)(specialfile))
#define __sanitizer_syscall_post_swapoff(res, specialfile)                     \
  __sanitizer_syscall_post_impl_swapoff(res, (long)(specialfile))
#define __sanitizer_syscall_pre_sysctl(args)                                   \
  __sanitizer_syscall_pre_impl_sysctl((long)(args))
#define __sanitizer_syscall_post_sysctl(res, args)                             \
  __sanitizer_syscall_post_impl_sysctl(res, (long)(args))
#define __sanitizer_syscall_pre_sysinfo(info)                                  \
  __sanitizer_syscall_pre_impl_sysinfo((long)(info))
#define __sanitizer_syscall_post_sysinfo(res, info)                            \
  __sanitizer_syscall_post_impl_sysinfo(res, (long)(info))
#define __sanitizer_syscall_pre_sysfs(option, arg1, arg2)                      \
  __sanitizer_syscall_pre_impl_sysfs((long)(option), (long)(arg1), (long)(arg2))
#define __sanitizer_syscall_post_sysfs(res, option, arg1, arg2)                \
  __sanitizer_syscall_post_impl_sysfs(res, (long)(option), (long)(arg1),       \
                                      (long)(arg2))
#define __sanitizer_syscall_pre_syslog(type, buf, len)                         \
  __sanitizer_syscall_pre_impl_syslog((long)(type), (long)(buf), (long)(len))
#define __sanitizer_syscall_post_syslog(res, type, buf, len)                   \
  __sanitizer_syscall_post_impl_syslog(res, (long)(type), (long)(buf),         \
                                       (long)(len))
#define __sanitizer_syscall_pre_uselib(library)                                \
  __sanitizer_syscall_pre_impl_uselib((long)(library))
#define __sanitizer_syscall_post_uselib(res, library)                          \
  __sanitizer_syscall_post_impl_uselib(res, (long)(library))
#define __sanitizer_syscall_pre_ni_syscall()                                   \
  __sanitizer_syscall_pre_impl_ni_syscall()
#define __sanitizer_syscall_post_ni_syscall(res)                               \
  __sanitizer_syscall_post_impl_ni_syscall(res)
#define __sanitizer_syscall_pre_ptrace(request, pid, addr, data)               \
  __sanitizer_syscall_pre_impl_ptrace((long)(request), (long)(pid),            \
                                      (long)(addr), (long)(data))
#define __sanitizer_syscall_post_ptrace(res, request, pid, addr, data)         \
  __sanitizer_syscall_post_impl_ptrace(res, (long)(request), (long)(pid),      \
                                       (long)(addr), (long)(data))
#define __sanitizer_syscall_pre_add_key(_type, _description, _payload, plen,   \
                                        destringid)                            \
  __sanitizer_syscall_pre_impl_add_key((long)(_type), (long)(_description),    \
                                       (long)(_payload), (long)(plen),         \
                                       (long)(destringid))
#define __sanitizer_syscall_post_add_key(res, _type, _description, _payload,   \
                                         plen, destringid)                     \
  __sanitizer_syscall_post_impl_add_key(                                       \
      res, (long)(_type), (long)(_description), (long)(_payload),              \
      (long)(plen), (long)(destringid))
#define __sanitizer_syscall_pre_request_key(_type, _description,               \
                                            _callout_info, destringid)         \
  __sanitizer_syscall_pre_impl_request_key(                                    \
      (long)(_type), (long)(_description), (long)(_callout_info),              \
      (long)(destringid))
#define __sanitizer_syscall_post_request_key(res, _type, _description,         \
                                             _callout_info, destringid)        \
  __sanitizer_syscall_post_impl_request_key(                                   \
      res, (long)(_type), (long)(_description), (long)(_callout_info),         \
      (long)(destringid))
#define __sanitizer_syscall_pre_keyctl(cmd, arg2, arg3, arg4, arg5)            \
  __sanitizer_syscall_pre_impl_keyctl((long)(cmd), (long)(arg2), (long)(arg3), \
                                      (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_post_keyctl(res, cmd, arg2, arg3, arg4, arg5)      \
  __sanitizer_syscall_post_impl_keyctl(res, (long)(cmd), (long)(arg2),         \
                                       (long)(arg3), (long)(arg4),             \
                                       (long)(arg5))
#define __sanitizer_syscall_pre_ioprio_set(which, who, ioprio)                 \
  __sanitizer_syscall_pre_impl_ioprio_set((long)(which), (long)(who),          \
                                          (long)(ioprio))
#define __sanitizer_syscall_post_ioprio_set(res, which, who, ioprio)           \
  __sanitizer_syscall_post_impl_ioprio_set(res, (long)(which), (long)(who),    \
                                           (long)(ioprio))
#define __sanitizer_syscall_pre_ioprio_get(which, who)                         \
  __sanitizer_syscall_pre_impl_ioprio_get((long)(which), (long)(who))
#define __sanitizer_syscall_post_ioprio_get(res, which, who)                   \
  __sanitizer_syscall_post_impl_ioprio_get(res, (long)(which), (long)(who))
#define __sanitizer_syscall_pre_set_mempolicy(mode, nmask, maxnode)            \
  __sanitizer_syscall_pre_impl_set_mempolicy((long)(mode), (long)(nmask),      \
                                             (long)(maxnode))
#define __sanitizer_syscall_post_set_mempolicy(res, mode, nmask, maxnode)      \
  __sanitizer_syscall_post_impl_set_mempolicy(res, (long)(mode),               \
                                              (long)(nmask), (long)(maxnode))
#define __sanitizer_syscall_pre_migrate_pages(pid, maxnode, from, to)          \
  __sanitizer_syscall_pre_impl_migrate_pages((long)(pid), (long)(maxnode),     \
                                             (long)(from), (long)(to))
#define __sanitizer_syscall_post_migrate_pages(res, pid, maxnode, from, to)    \
  __sanitizer_syscall_post_impl_migrate_pages(                                 \
      res, (long)(pid), (long)(maxnode), (long)(from), (long)(to))
#define __sanitizer_syscall_pre_move_pages(pid, nr_pages, pages, nodes,        \
                                           status, flags)                      \
  __sanitizer_syscall_pre_impl_move_pages((long)(pid), (long)(nr_pages),       \
                                          (long)(pages), (long)(nodes),        \
                                          (long)(status), (long)(flags))
#define __sanitizer_syscall_post_move_pages(res, pid, nr_pages, pages, nodes,  \
                                            status, flags)                     \
  __sanitizer_syscall_post_impl_move_pages(res, (long)(pid), (long)(nr_pages), \
                                           (long)(pages), (long)(nodes),       \
                                           (long)(status), (long)(flags))
#define __sanitizer_syscall_pre_mbind(start, len, mode, nmask, maxnode, flags) \
  __sanitizer_syscall_pre_impl_mbind((long)(start), (long)(len), (long)(mode), \
                                     (long)(nmask), (long)(maxnode),           \
                                     (long)(flags))
#define __sanitizer_syscall_post_mbind(res, start, len, mode, nmask, maxnode,  \
                                       flags)                                  \
  __sanitizer_syscall_post_impl_mbind(res, (long)(start), (long)(len),         \
                                      (long)(mode), (long)(nmask),             \
                                      (long)(maxnode), (long)(flags))
#define __sanitizer_syscall_pre_get_mempolicy(policy, nmask, maxnode, addr,    \
                                              flags)                           \
  __sanitizer_syscall_pre_impl_get_mempolicy((long)(policy), (long)(nmask),    \
                                             (long)(maxnode), (long)(addr),    \
                                             (long)(flags))
#define __sanitizer_syscall_post_get_mempolicy(res, policy, nmask, maxnode,    \
                                               addr, flags)                    \
  __sanitizer_syscall_post_impl_get_mempolicy(res, (long)(policy),             \
                                              (long)(nmask), (long)(maxnode),  \
                                              (long)(addr), (long)(flags))
#define __sanitizer_syscall_pre_inotify_init()                                 \
  __sanitizer_syscall_pre_impl_inotify_init()
#define __sanitizer_syscall_post_inotify_init(res)                             \
  __sanitizer_syscall_post_impl_inotify_init(res)
#define __sanitizer_syscall_pre_inotify_init1(flags)                           \
  __sanitizer_syscall_pre_impl_inotify_init1((long)(flags))
#define __sanitizer_syscall_post_inotify_init1(res, flags)                     \
  __sanitizer_syscall_post_impl_inotify_init1(res, (long)(flags))
#define __sanitizer_syscall_pre_inotify_add_watch(fd, path, mask)              \
  __sanitizer_syscall_pre_impl_inotify_add_watch((long)(fd), (long)(path),     \
                                                 (long)(mask))
#define __sanitizer_syscall_post_inotify_add_watch(res, fd, path, mask)        \
  __sanitizer_syscall_post_impl_inotify_add_watch(res, (long)(fd),             \
                                                  (long)(path), (long)(mask))
#define __sanitizer_syscall_pre_inotify_rm_watch(fd, wd)                       \
  __sanitizer_syscall_pre_impl_inotify_rm_watch((long)(fd), (long)(wd))
#define __sanitizer_syscall_post_inotify_rm_watch(res, fd, wd)                 \
  __sanitizer_syscall_post_impl_inotify_rm_watch(res, (long)(fd), (long)(wd))
#define __sanitizer_syscall_pre_spu_run(fd, unpc, ustatus)                     \
  __sanitizer_syscall_pre_impl_spu_run((long)(fd), (long)(unpc),               \
                                       (long)(ustatus))
#define __sanitizer_syscall_post_spu_run(res, fd, unpc, ustatus)               \
  __sanitizer_syscall_post_impl_spu_run(res, (long)(fd), (long)(unpc),         \
                                        (long)(ustatus))
#define __sanitizer_syscall_pre_spu_create(name, flags, mode, fd)              \
  __sanitizer_syscall_pre_impl_spu_create((long)(name), (long)(flags),         \
                                          (long)(mode), (long)(fd))
#define __sanitizer_syscall_post_spu_create(res, name, flags, mode, fd)        \
  __sanitizer_syscall_post_impl_spu_create(res, (long)(name), (long)(flags),   \
                                           (long)(mode), (long)(fd))
#define __sanitizer_syscall_pre_mknodat(dfd, filename, mode, dev)              \
  __sanitizer_syscall_pre_impl_mknodat((long)(dfd), (long)(filename),          \
                                       (long)(mode), (long)(dev))
#define __sanitizer_syscall_post_mknodat(res, dfd, filename, mode, dev)        \
  __sanitizer_syscall_post_impl_mknodat(res, (long)(dfd), (long)(filename),    \
                                        (long)(mode), (long)(dev))
#define __sanitizer_syscall_pre_mkdirat(dfd, pathname, mode)                   \
  __sanitizer_syscall_pre_impl_mkdirat((long)(dfd), (long)(pathname),          \
                                       (long)(mode))
#define __sanitizer_syscall_post_mkdirat(res, dfd, pathname, mode)             \
  __sanitizer_syscall_post_impl_mkdirat(res, (long)(dfd), (long)(pathname),    \
                                        (long)(mode))
#define __sanitizer_syscall_pre_unlinkat(dfd, pathname, flag)                  \
  __sanitizer_syscall_pre_impl_unlinkat((long)(dfd), (long)(pathname),         \
                                        (long)(flag))
#define __sanitizer_syscall_post_unlinkat(res, dfd, pathname, flag)            \
  __sanitizer_syscall_post_impl_unlinkat(res, (long)(dfd), (long)(pathname),   \
                                         (long)(flag))
#define __sanitizer_syscall_pre_symlinkat(oldname, newdfd, newname)            \
  __sanitizer_syscall_pre_impl_symlinkat((long)(oldname), (long)(newdfd),      \
                                         (long)(newname))
#define __sanitizer_syscall_post_symlinkat(res, oldname, newdfd, newname)      \
  __sanitizer_syscall_post_impl_symlinkat(res, (long)(oldname),                \
                                          (long)(newdfd), (long)(newname))
#define __sanitizer_syscall_pre_linkat(olddfd, oldname, newdfd, newname,       \
                                       flags)                                  \
  __sanitizer_syscall_pre_impl_linkat((long)(olddfd), (long)(oldname),         \
                                      (long)(newdfd), (long)(newname),         \
                                      (long)(flags))
#define __sanitizer_syscall_post_linkat(res, olddfd, oldname, newdfd, newname, \
                                        flags)                                 \
  __sanitizer_syscall_post_impl_linkat(res, (long)(olddfd), (long)(oldname),   \
                                       (long)(newdfd), (long)(newname),        \
                                       (long)(flags))
#define __sanitizer_syscall_pre_renameat(olddfd, oldname, newdfd, newname)     \
  __sanitizer_syscall_pre_impl_renameat((long)(olddfd), (long)(oldname),       \
                                        (long)(newdfd), (long)(newname))
#define __sanitizer_syscall_post_renameat(res, olddfd, oldname, newdfd,        \
                                          newname)                             \
  __sanitizer_syscall_post_impl_renameat(res, (long)(olddfd), (long)(oldname), \
                                         (long)(newdfd), (long)(newname))
#define __sanitizer_syscall_pre_futimesat(dfd, filename, utimes)               \
  __sanitizer_syscall_pre_impl_futimesat((long)(dfd), (long)(filename),        \
                                         (long)(utimes))
#define __sanitizer_syscall_post_futimesat(res, dfd, filename, utimes)         \
  __sanitizer_syscall_post_impl_futimesat(res, (long)(dfd), (long)(filename),  \
                                          (long)(utimes))
#define __sanitizer_syscall_pre_faccessat(dfd, filename, mode)                 \
  __sanitizer_syscall_pre_impl_faccessat((long)(dfd), (long)(filename),        \
                                         (long)(mode))
#define __sanitizer_syscall_post_faccessat(res, dfd, filename, mode)           \
  __sanitizer_syscall_post_impl_faccessat(res, (long)(dfd), (long)(filename),  \
                                          (long)(mode))
#define __sanitizer_syscall_pre_fchmodat(dfd, filename, mode)                  \
  __sanitizer_syscall_pre_impl_fchmodat((long)(dfd), (long)(filename),         \
                                        (long)(mode))
#define __sanitizer_syscall_post_fchmodat(res, dfd, filename, mode)            \
  __sanitizer_syscall_post_impl_fchmodat(res, (long)(dfd), (long)(filename),   \
                                         (long)(mode))
#define __sanitizer_syscall_pre_fchownat(dfd, filename, user, group, flag)     \
  __sanitizer_syscall_pre_impl_fchownat((long)(dfd), (long)(filename),         \
                                        (long)(user), (long)(group),           \
                                        (long)(flag))
#define __sanitizer_syscall_post_fchownat(res, dfd, filename, user, group,     \
                                          flag)                                \
  __sanitizer_syscall_post_impl_fchownat(res, (long)(dfd), (long)(filename),   \
                                         (long)(user), (long)(group),          \
                                         (long)(flag))
#define __sanitizer_syscall_pre_openat(dfd, filename, flags, mode)             \
  __sanitizer_syscall_pre_impl_openat((long)(dfd), (long)(filename),           \
                                      (long)(flags), (long)(mode))
#define __sanitizer_syscall_post_openat(res, dfd, filename, flags, mode)       \
  __sanitizer_syscall_post_impl_openat(res, (long)(dfd), (long)(filename),     \
                                       (long)(flags), (long)(mode))
#define __sanitizer_syscall_pre_newfstatat(dfd, filename, statbuf, flag)       \
  __sanitizer_syscall_pre_impl_newfstatat((long)(dfd), (long)(filename),       \
                                          (long)(statbuf), (long)(flag))
#define __sanitizer_syscall_post_newfstatat(res, dfd, filename, statbuf, flag) \
  __sanitizer_syscall_post_impl_newfstatat(res, (long)(dfd), (long)(filename), \
                                           (long)(statbuf), (long)(flag))
#define __sanitizer_syscall_pre_fstatat64(dfd, filename, statbuf, flag)        \
  __sanitizer_syscall_pre_impl_fstatat64((long)(dfd), (long)(filename),        \
                                         (long)(statbuf), (long)(flag))
#define __sanitizer_syscall_post_fstatat64(res, dfd, filename, statbuf, flag)  \
  __sanitizer_syscall_post_impl_fstatat64(res, (long)(dfd), (long)(filename),  \
                                          (long)(statbuf), (long)(flag))
#define __sanitizer_syscall_pre_readlinkat(dfd, path, buf, bufsiz)             \
  __sanitizer_syscall_pre_impl_readlinkat((long)(dfd), (long)(path),           \
                                          (long)(buf), (long)(bufsiz))
#define __sanitizer_syscall_post_readlinkat(res, dfd, path, buf, bufsiz)       \
  __sanitizer_syscall_post_impl_readlinkat(res, (long)(dfd), (long)(path),     \
                                           (long)(buf), (long)(bufsiz))
#define __sanitizer_syscall_pre_utimensat(dfd, filename, utimes, flags)        \
  __sanitizer_syscall_pre_impl_utimensat((long)(dfd), (long)(filename),        \
                                         (long)(utimes), (long)(flags))
#define __sanitizer_syscall_post_utimensat(res, dfd, filename, utimes, flags)  \
  __sanitizer_syscall_post_impl_utimensat(res, (long)(dfd), (long)(filename),  \
                                          (long)(utimes), (long)(flags))
#define __sanitizer_syscall_pre_unshare(unshare_flags)                         \
  __sanitizer_syscall_pre_impl_unshare((long)(unshare_flags))
#define __sanitizer_syscall_post_unshare(res, unshare_flags)                   \
  __sanitizer_syscall_post_impl_unshare(res, (long)(unshare_flags))
#define __sanitizer_syscall_pre_splice(fd_in, off_in, fd_out, off_out, len,    \
                                       flags)                                  \
  __sanitizer_syscall_pre_impl_splice((long)(fd_in), (long)(off_in),           \
                                      (long)(fd_out), (long)(off_out),         \
                                      (long)(len), (long)(flags))
#define __sanitizer_syscall_post_splice(res, fd_in, off_in, fd_out, off_out,   \
                                        len, flags)                            \
  __sanitizer_syscall_post_impl_splice(res, (long)(fd_in), (long)(off_in),     \
                                       (long)(fd_out), (long)(off_out),        \
                                       (long)(len), (long)(flags))
#define __sanitizer_syscall_pre_vmsplice(fd, iov, nr_segs, flags)              \
  __sanitizer_syscall_pre_impl_vmsplice((long)(fd), (long)(iov),               \
                                        (long)(nr_segs), (long)(flags))
#define __sanitizer_syscall_post_vmsplice(res, fd, iov, nr_segs, flags)        \
  __sanitizer_syscall_post_impl_vmsplice(res, (long)(fd), (long)(iov),         \
                                         (long)(nr_segs), (long)(flags))
#define __sanitizer_syscall_pre_tee(fdin, fdout, len, flags)                   \
  __sanitizer_syscall_pre_impl_tee((long)(fdin), (long)(fdout), (long)(len),   \
                                   (long)(flags))
#define __sanitizer_syscall_post_tee(res, fdin, fdout, len, flags)             \
  __sanitizer_syscall_post_impl_tee(res, (long)(fdin), (long)(fdout),          \
                                    (long)(len), (long)(flags))
#define __sanitizer_syscall_pre_get_robust_list(pid, head_ptr, len_ptr)        \
  __sanitizer_syscall_pre_impl_get_robust_list((long)(pid), (long)(head_ptr),  \
                                               (long)(len_ptr))
#define __sanitizer_syscall_post_get_robust_list(res, pid, head_ptr, len_ptr)  \
  __sanitizer_syscall_post_impl_get_robust_list(                               \
      res, (long)(pid), (long)(head_ptr), (long)(len_ptr))
#define __sanitizer_syscall_pre_set_robust_list(head, len)                     \
  __sanitizer_syscall_pre_impl_set_robust_list((long)(head), (long)(len))
#define __sanitizer_syscall_post_set_robust_list(res, head, len)               \
  __sanitizer_syscall_post_impl_set_robust_list(res, (long)(head), (long)(len))
#define __sanitizer_syscall_pre_getcpu(cpu, node, cache)                       \
  __sanitizer_syscall_pre_impl_getcpu((long)(cpu), (long)(node), (long)(cache))
#define __sanitizer_syscall_post_getcpu(res, cpu, node, cache)                 \
  __sanitizer_syscall_post_impl_getcpu(res, (long)(cpu), (long)(node),         \
                                       (long)(cache))
#define __sanitizer_syscall_pre_signalfd(ufd, user_mask, sizemask)             \
  __sanitizer_syscall_pre_impl_signalfd((long)(ufd), (long)(user_mask),        \
                                        (long)(sizemask))
#define __sanitizer_syscall_post_signalfd(res, ufd, user_mask, sizemask)       \
  __sanitizer_syscall_post_impl_signalfd(res, (long)(ufd), (long)(user_mask),  \
                                         (long)(sizemask))
#define __sanitizer_syscall_pre_signalfd4(ufd, user_mask, sizemask, flags)     \
  __sanitizer_syscall_pre_impl_signalfd4((long)(ufd), (long)(user_mask),       \
                                         (long)(sizemask), (long)(flags))
#define __sanitizer_syscall_post_signalfd4(res, ufd, user_mask, sizemask,      \
                                           flags)                              \
  __sanitizer_syscall_post_impl_signalfd4(res, (long)(ufd), (long)(user_mask), \
                                          (long)(sizemask), (long)(flags))
#define __sanitizer_syscall_pre_timerfd_create(clockid, flags)                 \
  __sanitizer_syscall_pre_impl_timerfd_create((long)(clockid), (long)(flags))
#define __sanitizer_syscall_post_timerfd_create(res, clockid, flags)           \
  __sanitizer_syscall_post_impl_timerfd_create(res, (long)(clockid),           \
                                               (long)(flags))
#define __sanitizer_syscall_pre_timerfd_settime(ufd, flags, utmr, otmr)        \
  __sanitizer_syscall_pre_impl_timerfd_settime((long)(ufd), (long)(flags),     \
                                               (long)(utmr), (long)(otmr))
#define __sanitizer_syscall_post_timerfd_settime(res, ufd, flags, utmr, otmr)  \
  __sanitizer_syscall_post_impl_timerfd_settime(                               \
      res, (long)(ufd), (long)(flags), (long)(utmr), (long)(otmr))
#define __sanitizer_syscall_pre_timerfd_gettime(ufd, otmr)                     \
  __sanitizer_syscall_pre_impl_timerfd_gettime((long)(ufd), (long)(otmr))
#define __sanitizer_syscall_post_timerfd_gettime(res, ufd, otmr)               \
  __sanitizer_syscall_post_impl_timerfd_gettime(res, (long)(ufd), (long)(otmr))
#define __sanitizer_syscall_pre_eventfd(count)                                 \
  __sanitizer_syscall_pre_impl_eventfd((long)(count))
#define __sanitizer_syscall_post_eventfd(res, count)                           \
  __sanitizer_syscall_post_impl_eventfd(res, (long)(count))
#define __sanitizer_syscall_pre_eventfd2(count, flags)                         \
  __sanitizer_syscall_pre_impl_eventfd2((long)(count), (long)(flags))
#define __sanitizer_syscall_post_eventfd2(res, count, flags)                   \
  __sanitizer_syscall_post_impl_eventfd2(res, (long)(count), (long)(flags))
#define __sanitizer_syscall_pre_old_readdir(arg0, arg1, arg2)                  \
  __sanitizer_syscall_pre_impl_old_readdir((long)(arg0), (long)(arg1),         \
                                           (long)(arg2))
#define __sanitizer_syscall_post_old_readdir(res, arg0, arg1, arg2)            \
  __sanitizer_syscall_post_impl_old_readdir(res, (long)(arg0), (long)(arg1),   \
                                            (long)(arg2))
#define __sanitizer_syscall_pre_pselect6(arg0, arg1, arg2, arg3, arg4, arg5)   \
  __sanitizer_syscall_pre_impl_pselect6((long)(arg0), (long)(arg1),            \
                                        (long)(arg2), (long)(arg3),            \
                                        (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_post_pselect6(res, arg0, arg1, arg2, arg3, arg4,   \
                                          arg5)                                \
  __sanitizer_syscall_post_impl_pselect6(res, (long)(arg0), (long)(arg1),      \
                                         (long)(arg2), (long)(arg3),           \
                                         (long)(arg4), (long)(arg5))
#define __sanitizer_syscall_pre_ppoll(arg0, arg1, arg2, arg3, arg4)            \
  __sanitizer_syscall_pre_impl_ppoll((long)(arg0), (long)(arg1), (long)(arg2), \
                                     (long)(arg3), (long)(arg4))
#define __sanitizer_syscall_post_ppoll(res, arg0, arg1, arg2, arg3, arg4)      \
  __sanitizer_syscall_post_impl_ppoll(res, (long)(arg0), (long)(arg1),         \
                                      (long)(arg2), (long)(arg3),              \
                                      (long)(arg4))
#define __sanitizer_syscall_pre_syncfs(fd)                                     \
  __sanitizer_syscall_pre_impl_syncfs((long)(fd))
#define __sanitizer_syscall_post_syncfs(res, fd)                               \
  __sanitizer_syscall_post_impl_syncfs(res, (long)(fd))
#define __sanitizer_syscall_pre_perf_event_open(attr_uptr, pid, cpu, group_fd, \
                                                flags)                         \
  __sanitizer_syscall_pre_impl_perf_event_open((long)(attr_uptr), (long)(pid), \
                                               (long)(cpu), (long)(group_fd),  \
                                               (long)(flags))
#define __sanitizer_syscall_post_perf_event_open(res, attr_uptr, pid, cpu,     \
                                                 group_fd, flags)              \
  __sanitizer_syscall_post_impl_perf_event_open(                               \
      res, (long)(attr_uptr), (long)(pid), (long)(cpu), (long)(group_fd),      \
      (long)(flags))
#define __sanitizer_syscall_pre_mmap_pgoff(addr, len, prot, flags, fd, pgoff)  \
  __sanitizer_syscall_pre_impl_mmap_pgoff((long)(addr), (long)(len),           \
                                          (long)(prot), (long)(flags),         \
                                          (long)(fd), (long)(pgoff))
#define __sanitizer_syscall_post_mmap_pgoff(res, addr, len, prot, flags, fd,   \
                                            pgoff)                             \
  __sanitizer_syscall_post_impl_mmap_pgoff(res, (long)(addr), (long)(len),     \
                                           (long)(prot), (long)(flags),        \
                                           (long)(fd), (long)(pgoff))
#define __sanitizer_syscall_pre_old_mmap(arg)                                  \
  __sanitizer_syscall_pre_impl_old_mmap((long)(arg))
#define __sanitizer_syscall_post_old_mmap(res, arg)                            \
  __sanitizer_syscall_post_impl_old_mmap(res, (long)(arg))
#define __sanitizer_syscall_pre_name_to_handle_at(dfd, name, handle, mnt_id,   \
                                                  flag)                        \
  __sanitizer_syscall_pre_impl_name_to_handle_at(                              \
      (long)(dfd), (long)(name), (long)(handle), (long)(mnt_id), (long)(flag))
#define __sanitizer_syscall_post_name_to_handle_at(res, dfd, name, handle,     \
                                                   mnt_id, flag)               \
  __sanitizer_syscall_post_impl_name_to_handle_at(                             \
      res, (long)(dfd), (long)(name), (long)(handle), (long)(mnt_id),          \
      (long)(flag))
#define __sanitizer_syscall_pre_open_by_handle_at(mountdirfd, handle, flags)   \
  __sanitizer_syscall_pre_impl_open_by_handle_at(                              \
      (long)(mountdirfd), (long)(handle), (long)(flags))
#define __sanitizer_syscall_post_open_by_handle_at(res, mountdirfd, handle,    \
                                                   flags)                      \
  __sanitizer_syscall_post_impl_open_by_handle_at(                             \
      res, (long)(mountdirfd), (long)(handle), (long)(flags))
#define __sanitizer_syscall_pre_setns(fd, nstype)                              \
  __sanitizer_syscall_pre_impl_setns((long)(fd), (long)(nstype))
#define __sanitizer_syscall_post_setns(res, fd, nstype)                        \
  __sanitizer_syscall_post_impl_setns(res, (long)(fd), (long)(nstype))
#define __sanitizer_syscall_pre_process_vm_readv(pid, lvec, liovcnt, rvec,     \
                                                 riovcnt, flags)               \
  __sanitizer_syscall_pre_impl_process_vm_readv(                               \
      (long)(pid), (long)(lvec), (long)(liovcnt), (long)(rvec),                \
      (long)(riovcnt), (long)(flags))
#define __sanitizer_syscall_post_process_vm_readv(res, pid, lvec, liovcnt,     \
                                                  rvec, riovcnt, flags)        \
  __sanitizer_syscall_post_impl_process_vm_readv(                              \
      res, (long)(pid), (long)(lvec), (long)(liovcnt), (long)(rvec),           \
      (long)(riovcnt), (long)(flags))
#define __sanitizer_syscall_pre_process_vm_writev(pid, lvec, liovcnt, rvec,    \
                                                  riovcnt, flags)              \
  __sanitizer_syscall_pre_impl_process_vm_writev(                              \
      (long)(pid), (long)(lvec), (long)(liovcnt), (long)(rvec),                \
      (long)(riovcnt), (long)(flags))
#define __sanitizer_syscall_post_process_vm_writev(res, pid, lvec, liovcnt,    \
                                                   rvec, riovcnt, flags)       \
  __sanitizer_syscall_post_impl_process_vm_writev(                             \
      res, (long)(pid), (long)(lvec), (long)(liovcnt), (long)(rvec),           \
      (long)(riovcnt), (long)(flags))
#define __sanitizer_syscall_pre_fork() __sanitizer_syscall_pre_impl_fork()
#define __sanitizer_syscall_post_fork(res)                                     \
  __sanitizer_syscall_post_impl_fork(res)
#define __sanitizer_syscall_pre_vfork() __sanitizer_syscall_pre_impl_vfork()
#define __sanitizer_syscall_post_vfork(res)                                    \
  __sanitizer_syscall_post_impl_vfork(res)
#define __sanitizer_syscall_pre_sigaction(signum, act, oldact)                 \
  __sanitizer_syscall_pre_impl_sigaction((long)signum, (long)act, (long)oldact)
#define __sanitizer_syscall_post_sigaction(res, signum, act, oldact)           \
  __sanitizer_syscall_post_impl_sigaction(res, (long)signum, (long)act,        \
                                          (long)oldact)
#define __sanitizer_syscall_pre_rt_sigaction(signum, act, oldact, sz)          \
  __sanitizer_syscall_pre_impl_rt_sigaction((long)signum, (long)act,           \
                                            (long)oldact, (long)sz)
#define __sanitizer_syscall_post_rt_sigaction(res, signum, act, oldact, sz)    \
  __sanitizer_syscall_post_impl_rt_sigaction(res, (long)signum, (long)act,     \
                                             (long)oldact, (long)sz)
#define __sanitizer_syscall_pre_sigaltstack(ss, oss)                           \
  __sanitizer_syscall_pre_impl_sigaltstack((long)ss, (long)oss)
#define __sanitizer_syscall_post_sigaltstack(res, ss, oss)                     \
  __sanitizer_syscall_post_impl_sigaltstack(res, (long)ss, (long)oss)
#define __sanitizer_syscall_pre_futex(uaddr, futex_op, val, timeout, uaddr2,   \
                                      val3)                                    \
  __sanitizer_syscall_pre_impl_futex((long)uaddr, (long)futex_op, (long)val,   \
                                     (long)timeout, (long)uaddr2, (long)val3)
#define __sanitizer_syscall_post_futex(res, uaddr, futex_op, val, timeout,     \
                                       uaddr2, val3)                           \
  __sanitizer_syscall_post_impl_futex(res, (long)uaddr, (long)futex_op,        \
                                      (long)val, (long)timeout, (long)uaddr2,  \
                                      (long)val3)

// And now a few syscalls we don't handle yet.
#define __sanitizer_syscall_pre_afs_syscall(...)
#define __sanitizer_syscall_pre_arch_prctl(...)
#define __sanitizer_syscall_pre_break(...)
#define __sanitizer_syscall_pre_chown32(...)
#define __sanitizer_syscall_pre_clone(...)
#define __sanitizer_syscall_pre_create_module(...)
#define __sanitizer_syscall_pre_epoll_ctl_old(...)
#define __sanitizer_syscall_pre_epoll_wait_old(...)
#define __sanitizer_syscall_pre_execve(...)
#define __sanitizer_syscall_pre_fadvise64(...)
#define __sanitizer_syscall_pre_fadvise64_64(...)
#define __sanitizer_syscall_pre_fallocate(...)
#define __sanitizer_syscall_pre_fanotify_init(...)
#define __sanitizer_syscall_pre_fanotify_mark(...)
#define __sanitizer_syscall_pre_fchown32(...)
#define __sanitizer_syscall_pre_ftime(...)
#define __sanitizer_syscall_pre_ftruncate64(...)
#define __sanitizer_syscall_pre_getegid32(...)
#define __sanitizer_syscall_pre_geteuid32(...)
#define __sanitizer_syscall_pre_getgid32(...)
#define __sanitizer_syscall_pre_getgroups32(...)
#define __sanitizer_syscall_pre_get_kernel_syms(...)
#define __sanitizer_syscall_pre_getpmsg(...)
#define __sanitizer_syscall_pre_getresgid32(...)
#define __sanitizer_syscall_pre_getresuid32(...)
#define __sanitizer_syscall_pre_get_thread_area(...)
#define __sanitizer_syscall_pre_getuid32(...)
#define __sanitizer_syscall_pre_gtty(...)
#define __sanitizer_syscall_pre_idle(...)
#define __sanitizer_syscall_pre_iopl(...)
#define __sanitizer_syscall_pre_lchown32(...)
#define __sanitizer_syscall_pre__llseek(...)
#define __sanitizer_syscall_pre_lock(...)
#define __sanitizer_syscall_pre_madvise1(...)
#define __sanitizer_syscall_pre_mmap(...)
#define __sanitizer_syscall_pre_mmap2(...)
#define __sanitizer_syscall_pre_modify_ldt(...)
#define __sanitizer_syscall_pre_mpx(...)
#define __sanitizer_syscall_pre__newselect(...)
#define __sanitizer_syscall_pre_nfsservctl(...)
#define __sanitizer_syscall_pre_oldfstat(...)
#define __sanitizer_syscall_pre_oldlstat(...)
#define __sanitizer_syscall_pre_oldolduname(...)
#define __sanitizer_syscall_pre_oldstat(...)
#define __sanitizer_syscall_pre_prctl(...)
#define __sanitizer_syscall_pre_prof(...)
#define __sanitizer_syscall_pre_profil(...)
#define __sanitizer_syscall_pre_putpmsg(...)
#define __sanitizer_syscall_pre_query_module(...)
#define __sanitizer_syscall_pre_readahead(...)
#define __sanitizer_syscall_pre_readdir(...)
#define __sanitizer_syscall_pre_rt_sigreturn(...)
#define __sanitizer_syscall_pre_rt_sigsuspend(...)
#define __sanitizer_syscall_pre_security(...)
#define __sanitizer_syscall_pre_setfsgid32(...)
#define __sanitizer_syscall_pre_setfsuid32(...)
#define __sanitizer_syscall_pre_setgid32(...)
#define __sanitizer_syscall_pre_setgroups32(...)
#define __sanitizer_syscall_pre_setregid32(...)
#define __sanitizer_syscall_pre_setresgid32(...)
#define __sanitizer_syscall_pre_setresuid32(...)
#define __sanitizer_syscall_pre_setreuid32(...)
#define __sanitizer_syscall_pre_set_thread_area(...)
#define __sanitizer_syscall_pre_setuid32(...)
#define __sanitizer_syscall_pre_sigreturn(...)
#define __sanitizer_syscall_pre_sigsuspend(...)
#define __sanitizer_syscall_pre_stty(...)
#define __sanitizer_syscall_pre_sync_file_range(...)
#define __sanitizer_syscall_pre__sysctl(...)
#define __sanitizer_syscall_pre_truncate64(...)
#define __sanitizer_syscall_pre_tuxcall(...)
#define __sanitizer_syscall_pre_ugetrlimit(...)
#define __sanitizer_syscall_pre_ulimit(...)
#define __sanitizer_syscall_pre_umount2(...)
#define __sanitizer_syscall_pre_vm86(...)
#define __sanitizer_syscall_pre_vm86old(...)
#define __sanitizer_syscall_pre_vserver(...)

#define __sanitizer_syscall_post_afs_syscall(res, ...)
#define __sanitizer_syscall_post_arch_prctl(res, ...)
#define __sanitizer_syscall_post_break(res, ...)
#define __sanitizer_syscall_post_chown32(res, ...)
#define __sanitizer_syscall_post_clone(res, ...)
#define __sanitizer_syscall_post_create_module(res, ...)
#define __sanitizer_syscall_post_epoll_ctl_old(res, ...)
#define __sanitizer_syscall_post_epoll_wait_old(res, ...)
#define __sanitizer_syscall_post_execve(res, ...)
#define __sanitizer_syscall_post_fadvise64(res, ...)
#define __sanitizer_syscall_post_fadvise64_64(res, ...)
#define __sanitizer_syscall_post_fallocate(res, ...)
#define __sanitizer_syscall_post_fanotify_init(res, ...)
#define __sanitizer_syscall_post_fanotify_mark(res, ...)
#define __sanitizer_syscall_post_fchown32(res, ...)
#define __sanitizer_syscall_post_ftime(res, ...)
#define __sanitizer_syscall_post_ftruncate64(res, ...)
#define __sanitizer_syscall_post_getegid32(res, ...)
#define __sanitizer_syscall_post_geteuid32(res, ...)
#define __sanitizer_syscall_post_getgid32(res, ...)
#define __sanitizer_syscall_post_getgroups32(res, ...)
#define __sanitizer_syscall_post_get_kernel_syms(res, ...)
#define __sanitizer_syscall_post_getpmsg(res, ...)
#define __sanitizer_syscall_post_getresgid32(res, ...)
#define __sanitizer_syscall_post_getresuid32(res, ...)
#define __sanitizer_syscall_post_get_thread_area(res, ...)
#define __sanitizer_syscall_post_getuid32(res, ...)
#define __sanitizer_syscall_post_gtty(res, ...)
#define __sanitizer_syscall_post_idle(res, ...)
#define __sanitizer_syscall_post_iopl(res, ...)
#define __sanitizer_syscall_post_lchown32(res, ...)
#define __sanitizer_syscall_post__llseek(res, ...)
#define __sanitizer_syscall_post_lock(res, ...)
#define __sanitizer_syscall_post_madvise1(res, ...)
#define __sanitizer_syscall_post_mmap2(res, ...)
#define __sanitizer_syscall_post_mmap(res, ...)
#define __sanitizer_syscall_post_modify_ldt(res, ...)
#define __sanitizer_syscall_post_mpx(res, ...)
#define __sanitizer_syscall_post__newselect(res, ...)
#define __sanitizer_syscall_post_nfsservctl(res, ...)
#define __sanitizer_syscall_post_oldfstat(res, ...)
#define __sanitizer_syscall_post_oldlstat(res, ...)
#define __sanitizer_syscall_post_oldolduname(res, ...)
#define __sanitizer_syscall_post_oldstat(res, ...)
#define __sanitizer_syscall_post_prctl(res, ...)
#define __sanitizer_syscall_post_profil(res, ...)
#define __sanitizer_syscall_post_prof(res, ...)
#define __sanitizer_syscall_post_putpmsg(res, ...)
#define __sanitizer_syscall_post_query_module(res, ...)
#define __sanitizer_syscall_post_readahead(res, ...)
#define __sanitizer_syscall_post_readdir(res, ...)
#define __sanitizer_syscall_post_rt_sigreturn(res, ...)
#define __sanitizer_syscall_post_rt_sigsuspend(res, ...)
#define __sanitizer_syscall_post_security(res, ...)
#define __sanitizer_syscall_post_setfsgid32(res, ...)
#define __sanitizer_syscall_post_setfsuid32(res, ...)
#define __sanitizer_syscall_post_setgid32(res, ...)
#define __sanitizer_syscall_post_setgroups32(res, ...)
#define __sanitizer_syscall_post_setregid32(res, ...)
#define __sanitizer_syscall_post_setresgid32(res, ...)
#define __sanitizer_syscall_post_setresuid32(res, ...)
#define __sanitizer_syscall_post_setreuid32(res, ...)
#define __sanitizer_syscall_post_set_thread_area(res, ...)
#define __sanitizer_syscall_post_setuid32(res, ...)
#define __sanitizer_syscall_post_sigreturn(res, ...)
#define __sanitizer_syscall_post_sigsuspend(res, ...)
#define __sanitizer_syscall_post_stty(res, ...)
#define __sanitizer_syscall_post_sync_file_range(res, ...)
#define __sanitizer_syscall_post__sysctl(res, ...)
#define __sanitizer_syscall_post_truncate64(res, ...)
#define __sanitizer_syscall_post_tuxcall(res, ...)
#define __sanitizer_syscall_post_ugetrlimit(res, ...)
#define __sanitizer_syscall_post_ulimit(res, ...)
#define __sanitizer_syscall_post_umount2(res, ...)
#define __sanitizer_syscall_post_vm86old(res, ...)
#define __sanitizer_syscall_post_vm86(res, ...)
#define __sanitizer_syscall_post_vserver(res, ...)

#ifdef __cplusplus
extern "C" {
#endif

// Private declarations. Do not call directly from user code. Use macros above.
void __sanitizer_syscall_pre_impl_time(long tloc);
void __sanitizer_syscall_post_impl_time(long res, long tloc);
void __sanitizer_syscall_pre_impl_stime(long tptr);
void __sanitizer_syscall_post_impl_stime(long res, long tptr);
void __sanitizer_syscall_pre_impl_gettimeofday(long tv, long tz);
void __sanitizer_syscall_post_impl_gettimeofday(long res, long tv, long tz);
void __sanitizer_syscall_pre_impl_settimeofday(long tv, long tz);
void __sanitizer_syscall_post_impl_settimeofday(long res, long tv, long tz);
void __sanitizer_syscall_pre_impl_adjtimex(long txc_p);
void __sanitizer_syscall_post_impl_adjtimex(long res, long txc_p);
void __sanitizer_syscall_pre_impl_times(long tbuf);
void __sanitizer_syscall_post_impl_times(long res, long tbuf);
void __sanitizer_syscall_pre_impl_gettid();
void __sanitizer_syscall_post_impl_gettid(long res);
void __sanitizer_syscall_pre_impl_nanosleep(long rqtp, long rmtp);
void __sanitizer_syscall_post_impl_nanosleep(long res, long rqtp, long rmtp);
void __sanitizer_syscall_pre_impl_alarm(long seconds);
void __sanitizer_syscall_post_impl_alarm(long res, long seconds);
void __sanitizer_syscall_pre_impl_getpid();
void __sanitizer_syscall_post_impl_getpid(long res);
void __sanitizer_syscall_pre_impl_getppid();
void __sanitizer_syscall_post_impl_getppid(long res);
void __sanitizer_syscall_pre_impl_getuid();
void __sanitizer_syscall_post_impl_getuid(long res);
void __sanitizer_syscall_pre_impl_geteuid();
void __sanitizer_syscall_post_impl_geteuid(long res);
void __sanitizer_syscall_pre_impl_getgid();
void __sanitizer_syscall_post_impl_getgid(long res);
void __sanitizer_syscall_pre_impl_getegid();
void __sanitizer_syscall_post_impl_getegid(long res);
void __sanitizer_syscall_pre_impl_getresuid(long ruid, long euid, long suid);
void __sanitizer_syscall_post_impl_getresuid(long res, long ruid, long euid,
                                             long suid);
void __sanitizer_syscall_pre_impl_getresgid(long rgid, long egid, long sgid);
void __sanitizer_syscall_post_impl_getresgid(long res, long rgid, long egid,
                                             long sgid);
void __sanitizer_syscall_pre_impl_getpgid(long pid);
void __sanitizer_syscall_post_impl_getpgid(long res, long pid);
void __sanitizer_syscall_pre_impl_getpgrp();
void __sanitizer_syscall_post_impl_getpgrp(long res);
void __sanitizer_syscall_pre_impl_getsid(long pid);
void __sanitizer_syscall_post_impl_getsid(long res, long pid);
void __sanitizer_syscall_pre_impl_getgroups(long gidsetsize, long grouplist);
void __sanitizer_syscall_post_impl_getgroups(long res, long gidsetsize,
                                             long grouplist);
void __sanitizer_syscall_pre_impl_setregid(long rgid, long egid);
void __sanitizer_syscall_post_impl_setregid(long res, long rgid, long egid);
void __sanitizer_syscall_pre_impl_setgid(long gid);
void __sanitizer_syscall_post_impl_setgid(long res, long gid);
void __sanitizer_syscall_pre_impl_setreuid(long ruid, long euid);
void __sanitizer_syscall_post_impl_setreuid(long res, long ruid, long euid);
void __sanitizer_syscall_pre_impl_setuid(long uid);
void __sanitizer_syscall_post_impl_setuid(long res, long uid);
void __sanitizer_syscall_pre_impl_setresuid(long ruid, long euid, long suid);
void __sanitizer_syscall_post_impl_setresuid(long res, long ruid, long euid,
                                             long suid);
void __sanitizer_syscall_pre_impl_setresgid(long rgid, long egid, long sgid);
void __sanitizer_syscall_post_impl_setresgid(long res, long rgid, long egid,
                                             long sgid);
void __sanitizer_syscall_pre_impl_setfsuid(long uid);
void __sanitizer_syscall_post_impl_setfsuid(long res, long uid);
void __sanitizer_syscall_pre_impl_setfsgid(long gid);
void __sanitizer_syscall_post_impl_setfsgid(long res, long gid);
void __sanitizer_syscall_pre_impl_setpgid(long pid, long pgid);
void __sanitizer_syscall_post_impl_setpgid(long res, long pid, long pgid);
void __sanitizer_syscall_pre_impl_setsid();
void __sanitizer_syscall_post_impl_setsid(long res);
void __sanitizer_syscall_pre_impl_setgroups(long gidsetsize, long grouplist);
void __sanitizer_syscall_post_impl_setgroups(long res, long gidsetsize,
                                             long grouplist);
void __sanitizer_syscall_pre_impl_acct(long name);
void __sanitizer_syscall_post_impl_acct(long res, long name);
void __sanitizer_syscall_pre_impl_capget(long header, long dataptr);
void __sanitizer_syscall_post_impl_capget(long res, long header, long dataptr);
void __sanitizer_syscall_pre_impl_capset(long header, long data);
void __sanitizer_syscall_post_impl_capset(long res, long header, long data);
void __sanitizer_syscall_pre_impl_personality(long personality);
void __sanitizer_syscall_post_impl_personality(long res, long personality);
void __sanitizer_syscall_pre_impl_sigpending(long set);
void __sanitizer_syscall_post_impl_sigpending(long res, long set);
void __sanitizer_syscall_pre_impl_sigprocmask(long how, long set, long oset);
void __sanitizer_syscall_post_impl_sigprocmask(long res, long how, long set,
                                               long oset);
void __sanitizer_syscall_pre_impl_getitimer(long which, long value);
void __sanitizer_syscall_post_impl_getitimer(long res, long which, long value);
void __sanitizer_syscall_pre_impl_setitimer(long which, long value,
                                            long ovalue);
void __sanitizer_syscall_post_impl_setitimer(long res, long which, long value,
                                             long ovalue);
void __sanitizer_syscall_pre_impl_timer_create(long which_clock,
                                               long timer_event_spec,
                                               long created_timer_id);
void __sanitizer_syscall_post_impl_timer_create(long res, long which_clock,
                                                long timer_event_spec,
                                                long created_timer_id);
void __sanitizer_syscall_pre_impl_timer_gettime(long timer_id, long setting);
void __sanitizer_syscall_post_impl_timer_gettime(long res, long timer_id,
                                                 long setting);
void __sanitizer_syscall_pre_impl_timer_getoverrun(long timer_id);
void __sanitizer_syscall_post_impl_timer_getoverrun(long res, long timer_id);
void __sanitizer_syscall_pre_impl_timer_settime(long timer_id, long flags,
                                                long new_setting,
                                                long old_setting);
void __sanitizer_syscall_post_impl_timer_settime(long res, long timer_id,
                                                 long flags, long new_setting,
                                                 long old_setting);
void __sanitizer_syscall_pre_impl_timer_delete(long timer_id);
void __sanitizer_syscall_post_impl_timer_delete(long res, long timer_id);
void __sanitizer_syscall_pre_impl_clock_settime(long which_clock, long tp);
void __sanitizer_syscall_post_impl_clock_settime(long res, long which_clock,
                                                 long tp);
void __sanitizer_syscall_pre_impl_clock_gettime(long which_clock, long tp);
void __sanitizer_syscall_post_impl_clock_gettime(long res, long which_clock,
                                                 long tp);
void __sanitizer_syscall_pre_impl_clock_adjtime(long which_clock, long tx);
void __sanitizer_syscall_post_impl_clock_adjtime(long res, long which_clock,
                                                 long tx);
void __sanitizer_syscall_pre_impl_clock_getres(long which_clock, long tp);
void __sanitizer_syscall_post_impl_clock_getres(long res, long which_clock,
                                                long tp);
void __sanitizer_syscall_pre_impl_clock_nanosleep(long which_clock, long flags,
                                                  long rqtp, long rmtp);
void __sanitizer_syscall_post_impl_clock_nanosleep(long res, long which_clock,
                                                   long flags, long rqtp,
                                                   long rmtp);
void __sanitizer_syscall_pre_impl_nice(long increment);
void __sanitizer_syscall_post_impl_nice(long res, long increment);
void __sanitizer_syscall_pre_impl_sched_setscheduler(long pid, long policy,
                                                     long param);
void __sanitizer_syscall_post_impl_sched_setscheduler(long res, long pid,
                                                      long policy, long param);
void __sanitizer_syscall_pre_impl_sched_setparam(long pid, long param);
void __sanitizer_syscall_post_impl_sched_setparam(long res, long pid,
                                                  long param);
void __sanitizer_syscall_pre_impl_sched_getscheduler(long pid);
void __sanitizer_syscall_post_impl_sched_getscheduler(long res, long pid);
void __sanitizer_syscall_pre_impl_sched_getparam(long pid, long param);
void __sanitizer_syscall_post_impl_sched_getparam(long res, long pid,
                                                  long param);
void __sanitizer_syscall_pre_impl_sched_setaffinity(long pid, long len,
                                                    long user_mask_ptr);
void __sanitizer_syscall_post_impl_sched_setaffinity(long res, long pid,
                                                     long len,
                                                     long user_mask_ptr);
void __sanitizer_syscall_pre_impl_sched_getaffinity(long pid, long len,
                                                    long user_mask_ptr);
void __sanitizer_syscall_post_impl_sched_getaffinity(long res, long pid,
                                                     long len,
                                                     long user_mask_ptr);
void __sanitizer_syscall_pre_impl_sched_yield();
void __sanitizer_syscall_post_impl_sched_yield(long res);
void __sanitizer_syscall_pre_impl_sched_get_priority_max(long policy);
void __sanitizer_syscall_post_impl_sched_get_priority_max(long res,
                                                          long policy);
void __sanitizer_syscall_pre_impl_sched_get_priority_min(long policy);
void __sanitizer_syscall_post_impl_sched_get_priority_min(long res,
                                                          long policy);
void __sanitizer_syscall_pre_impl_sched_rr_get_interval(long pid,
                                                        long interval);
void __sanitizer_syscall_post_impl_sched_rr_get_interval(long res, long pid,
                                                         long interval);
void __sanitizer_syscall_pre_impl_setpriority(long which, long who,
                                              long niceval);
void __sanitizer_syscall_post_impl_setpriority(long res, long which, long who,
                                               long niceval);
void __sanitizer_syscall_pre_impl_getpriority(long which, long who);
void __sanitizer_syscall_post_impl_getpriority(long res, long which, long who);
void __sanitizer_syscall_pre_impl_shutdown(long arg0, long arg1);
void __sanitizer_syscall_post_impl_shutdown(long res, long arg0, long arg1);
void __sanitizer_syscall_pre_impl_reboot(long magic1, long magic2, long cmd,
                                         long arg);
void __sanitizer_syscall_post_impl_reboot(long res, long magic1, long magic2,
                                          long cmd, long arg);
void __sanitizer_syscall_pre_impl_restart_syscall();
void __sanitizer_syscall_post_impl_restart_syscall(long res);
void __sanitizer_syscall_pre_impl_kexec_load(long entry, long nr_segments,
                                             long segments, long flags);
void __sanitizer_syscall_post_impl_kexec_load(long res, long entry,
                                              long nr_segments, long segments,
                                              long flags);
void __sanitizer_syscall_pre_impl_exit(long error_code);
void __sanitizer_syscall_post_impl_exit(long res, long error_code);
void __sanitizer_syscall_pre_impl_exit_group(long error_code);
void __sanitizer_syscall_post_impl_exit_group(long res, long error_code);
void __sanitizer_syscall_pre_impl_wait4(long pid, long stat_addr, long options,
                                        long ru);
void __sanitizer_syscall_post_impl_wait4(long res, long pid, long stat_addr,
                                         long options, long ru);
void __sanitizer_syscall_pre_impl_waitid(long which, long pid, long infop,
                                         long options, long ru);
void __sanitizer_syscall_post_impl_waitid(long res, long which, long pid,
                                          long infop, long options, long ru);
void __sanitizer_syscall_pre_impl_waitpid(long pid, long stat_addr,
                                          long options);
void __sanitizer_syscall_post_impl_waitpid(long res, long pid, long stat_addr,
                                           long options);
void __sanitizer_syscall_pre_impl_set_tid_address(long tidptr);
void __sanitizer_syscall_post_impl_set_tid_address(long res, long tidptr);
void __sanitizer_syscall_pre_impl_init_module(long umod, long len, long uargs);
void __sanitizer_syscall_post_impl_init_module(long res, long umod, long len,
                                               long uargs);
void __sanitizer_syscall_pre_impl_delete_module(long name_user, long flags);
void __sanitizer_syscall_post_impl_delete_module(long res, long name_user,
                                                 long flags);
void __sanitizer_syscall_pre_impl_rt_sigprocmask(long how, long set, long oset,
                                                 long sigsetsize);
void __sanitizer_syscall_post_impl_rt_sigprocmask(long res, long how, long set,
                                                  long oset, long sigsetsize);
void __sanitizer_syscall_pre_impl_rt_sigpending(long set, long sigsetsize);
void __sanitizer_syscall_post_impl_rt_sigpending(long res, long set,
                                                 long sigsetsize);
void __sanitizer_syscall_pre_impl_rt_sigtimedwait(long uthese, long uinfo,
                                                  long uts, long sigsetsize);
void __sanitizer_syscall_post_impl_rt_sigtimedwait(long res, long uthese,
                                                   long uinfo, long uts,
                                                   long sigsetsize);
void __sanitizer_syscall_pre_impl_rt_tgsigqueueinfo(long tgid, long pid,
                                                    long sig, long uinfo);
void __sanitizer_syscall_post_impl_rt_tgsigqueueinfo(long res, long tgid,
                                                     long pid, long sig,
                                                     long uinfo);
void __sanitizer_syscall_pre_impl_kill(long pid, long sig);
void __sanitizer_syscall_post_impl_kill(long res, long pid, long sig);
void __sanitizer_syscall_pre_impl_tgkill(long tgid, long pid, long sig);
void __sanitizer_syscall_post_impl_tgkill(long res, long tgid, long pid,
                                          long sig);
void __sanitizer_syscall_pre_impl_tkill(long pid, long sig);
void __sanitizer_syscall_post_impl_tkill(long res, long pid, long sig);
void __sanitizer_syscall_pre_impl_rt_sigqueueinfo(long pid, long sig,
                                                  long uinfo);
void __sanitizer_syscall_post_impl_rt_sigqueueinfo(long res, long pid, long sig,
                                                   long uinfo);
void __sanitizer_syscall_pre_impl_sgetmask();
void __sanitizer_syscall_post_impl_sgetmask(long res);
void __sanitizer_syscall_pre_impl_ssetmask(long newmask);
void __sanitizer_syscall_post_impl_ssetmask(long res, long newmask);
void __sanitizer_syscall_pre_impl_signal(long sig, long handler);
void __sanitizer_syscall_post_impl_signal(long res, long sig, long handler);
void __sanitizer_syscall_pre_impl_pause();
void __sanitizer_syscall_post_impl_pause(long res);
void __sanitizer_syscall_pre_impl_sync();
void __sanitizer_syscall_post_impl_sync(long res);
void __sanitizer_syscall_pre_impl_fsync(long fd);
void __sanitizer_syscall_post_impl_fsync(long res, long fd);
void __sanitizer_syscall_pre_impl_fdatasync(long fd);
void __sanitizer_syscall_post_impl_fdatasync(long res, long fd);
void __sanitizer_syscall_pre_impl_bdflush(long func, long data);
void __sanitizer_syscall_post_impl_bdflush(long res, long func, long data);
void __sanitizer_syscall_pre_impl_mount(long dev_name, long dir_name, long type,
                                        long flags, long data);
void __sanitizer_syscall_post_impl_mount(long res, long dev_name, long dir_name,
                                         long type, long flags, long data);
void __sanitizer_syscall_pre_impl_umount(long name, long flags);
void __sanitizer_syscall_post_impl_umount(long res, long name, long flags);
void __sanitizer_syscall_pre_impl_oldumount(long name);
void __sanitizer_syscall_post_impl_oldumount(long res, long name);
void __sanitizer_syscall_pre_impl_truncate(long path, long length);
void __sanitizer_syscall_post_impl_truncate(long res, long path, long length);
void __sanitizer_syscall_pre_impl_ftruncate(long fd, long length);
void __sanitizer_syscall_post_impl_ftruncate(long res, long fd, long length);
void __sanitizer_syscall_pre_impl_stat(long filename, long statbuf);
void __sanitizer_syscall_post_impl_stat(long res, long filename, long statbuf);
void __sanitizer_syscall_pre_impl_statfs(long path, long buf);
void __sanitizer_syscall_post_impl_statfs(long res, long path, long buf);
void __sanitizer_syscall_pre_impl_statfs64(long path, long sz, long buf);
void __sanitizer_syscall_post_impl_statfs64(long res, long path, long sz,
                                            long buf);
void __sanitizer_syscall_pre_impl_fstatfs(long fd, long buf);
void __sanitizer_syscall_post_impl_fstatfs(long res, long fd, long buf);
void __sanitizer_syscall_pre_impl_fstatfs64(long fd, long sz, long buf);
void __sanitizer_syscall_post_impl_fstatfs64(long res, long fd, long sz,
                                             long buf);
void __sanitizer_syscall_pre_impl_lstat(long filename, long statbuf);
void __sanitizer_syscall_post_impl_lstat(long res, long filename, long statbuf);
void __sanitizer_syscall_pre_impl_fstat(long fd, long statbuf);
void __sanitizer_syscall_post_impl_fstat(long res, long fd, long statbuf);
void __sanitizer_syscall_pre_impl_newstat(long filename, long statbuf);
void __sanitizer_syscall_post_impl_newstat(long res, long filename,
                                           long statbuf);
void __sanitizer_syscall_pre_impl_newlstat(long filename, long statbuf);
void __sanitizer_syscall_post_impl_newlstat(long res, long filename,
                                            long statbuf);
void __sanitizer_syscall_pre_impl_newfstat(long fd, long statbuf);
void __sanitizer_syscall_post_impl_newfstat(long res, long fd, long statbuf);
void __sanitizer_syscall_pre_impl_ustat(long dev, long ubuf);
void __sanitizer_syscall_post_impl_ustat(long res, long dev, long ubuf);
void __sanitizer_syscall_pre_impl_stat64(long filename, long statbuf);
void __sanitizer_syscall_post_impl_stat64(long res, long filename,
                                          long statbuf);
void __sanitizer_syscall_pre_impl_fstat64(long fd, long statbuf);
void __sanitizer_syscall_post_impl_fstat64(long res, long fd, long statbuf);
void __sanitizer_syscall_pre_impl_lstat64(long filename, long statbuf);
void __sanitizer_syscall_post_impl_lstat64(long res, long filename,
                                           long statbuf);
void __sanitizer_syscall_pre_impl_setxattr(long path, long name, long value,
                                           long size, long flags);
void __sanitizer_syscall_post_impl_setxattr(long res, long path, long name,
                                            long value, long size, long flags);
void __sanitizer_syscall_pre_impl_lsetxattr(long path, long name, long value,
                                            long size, long flags);
void __sanitizer_syscall_post_impl_lsetxattr(long res, long path, long name,
                                             long value, long size, long flags);
void __sanitizer_syscall_pre_impl_fsetxattr(long fd, long name, long value,
                                            long size, long flags);
void __sanitizer_syscall_post_impl_fsetxattr(long res, long fd, long name,
                                             long value, long size, long flags);
void __sanitizer_syscall_pre_impl_getxattr(long path, long name, long value,
                                           long size);
void __sanitizer_syscall_post_impl_getxattr(long res, long path, long name,
                                            long value, long size);
void __sanitizer_syscall_pre_impl_lgetxattr(long path, long name, long value,
                                            long size);
void __sanitizer_syscall_post_impl_lgetxattr(long res, long path, long name,
                                             long value, long size);
void __sanitizer_syscall_pre_impl_fgetxattr(long fd, long name, long value,
                                            long size);
void __sanitizer_syscall_post_impl_fgetxattr(long res, long fd, long name,
                                             long value, long size);
void __sanitizer_syscall_pre_impl_listxattr(long path, long list, long size);
void __sanitizer_syscall_post_impl_listxattr(long res, long path, long list,
                                             long size);
void __sanitizer_syscall_pre_impl_llistxattr(long path, long list, long size);
void __sanitizer_syscall_post_impl_llistxattr(long res, long path, long list,
                                              long size);
void __sanitizer_syscall_pre_impl_flistxattr(long fd, long list, long size);
void __sanitizer_syscall_post_impl_flistxattr(long res, long fd, long list,
                                              long size);
void __sanitizer_syscall_pre_impl_removexattr(long path, long name);
void __sanitizer_syscall_post_impl_removexattr(long res, long path, long name);
void __sanitizer_syscall_pre_impl_lremovexattr(long path, long name);
void __sanitizer_syscall_post_impl_lremovexattr(long res, long path, long name);
void __sanitizer_syscall_pre_impl_fremovexattr(long fd, long name);
void __sanitizer_syscall_post_impl_fremovexattr(long res, long fd, long name);
void __sanitizer_syscall_pre_impl_brk(long brk);
void __sanitizer_syscall_post_impl_brk(long res, long brk);
void __sanitizer_syscall_pre_impl_mprotect(long start, long len, long prot);
void __sanitizer_syscall_post_impl_mprotect(long res, long start, long len,
                                            long prot);
void __sanitizer_syscall_pre_impl_mremap(long addr, long old_len, long new_len,
                                         long flags, long new_addr);
void __sanitizer_syscall_post_impl_mremap(long res, long addr, long old_len,
                                          long new_len, long flags,
                                          long new_addr);
void __sanitizer_syscall_pre_impl_remap_file_pages(long start, long size,
                                                   long prot, long pgoff,
                                                   long flags);
void __sanitizer_syscall_post_impl_remap_file_pages(long res, long start,
                                                    long size, long prot,
                                                    long pgoff, long flags);
void __sanitizer_syscall_pre_impl_msync(long start, long len, long flags);
void __sanitizer_syscall_post_impl_msync(long res, long start, long len,
                                         long flags);
void __sanitizer_syscall_pre_impl_munmap(long addr, long len);
void __sanitizer_syscall_post_impl_munmap(long res, long addr, long len);
void __sanitizer_syscall_pre_impl_mlock(long start, long len);
void __sanitizer_syscall_post_impl_mlock(long res, long start, long len);
void __sanitizer_syscall_pre_impl_munlock(long start, long len);
void __sanitizer_syscall_post_impl_munlock(long res, long start, long len);
void __sanitizer_syscall_pre_impl_mlockall(long flags);
void __sanitizer_syscall_post_impl_mlockall(long res, long flags);
void __sanitizer_syscall_pre_impl_munlockall();
void __sanitizer_syscall_post_impl_munlockall(long res);
void __sanitizer_syscall_pre_impl_madvise(long start, long len, long behavior);
void __sanitizer_syscall_post_impl_madvise(long res, long start, long len,
                                           long behavior);
void __sanitizer_syscall_pre_impl_mincore(long start, long len, long vec);
void __sanitizer_syscall_post_impl_mincore(long res, long start, long len,
                                           long vec);
void __sanitizer_syscall_pre_impl_pivot_root(long new_root, long put_old);
void __sanitizer_syscall_post_impl_pivot_root(long res, long new_root,
                                              long put_old);
void __sanitizer_syscall_pre_impl_chroot(long filename);
void __sanitizer_syscall_post_impl_chroot(long res, long filename);
void __sanitizer_syscall_pre_impl_mknod(long filename, long mode, long dev);
void __sanitizer_syscall_post_impl_mknod(long res, long filename, long mode,
                                         long dev);
void __sanitizer_syscall_pre_impl_link(long oldname, long newname);
void __sanitizer_syscall_post_impl_link(long res, long oldname, long newname);
void __sanitizer_syscall_pre_impl_symlink(long old, long new_);
void __sanitizer_syscall_post_impl_symlink(long res, long old, long new_);
void __sanitizer_syscall_pre_impl_unlink(long pathname);
void __sanitizer_syscall_post_impl_unlink(long res, long pathname);
void __sanitizer_syscall_pre_impl_rename(long oldname, long newname);
void __sanitizer_syscall_post_impl_rename(long res, long oldname, long newname);
void __sanitizer_syscall_pre_impl_chmod(long filename, long mode);
void __sanitizer_syscall_post_impl_chmod(long res, long filename, long mode);
void __sanitizer_syscall_pre_impl_fchmod(long fd, long mode);
void __sanitizer_syscall_post_impl_fchmod(long res, long fd, long mode);
void __sanitizer_syscall_pre_impl_fcntl(long fd, long cmd, long arg);
void __sanitizer_syscall_post_impl_fcntl(long res, long fd, long cmd, long arg);
void __sanitizer_syscall_pre_impl_fcntl64(long fd, long cmd, long arg);
void __sanitizer_syscall_post_impl_fcntl64(long res, long fd, long cmd,
                                           long arg);
void __sanitizer_syscall_pre_impl_pipe(long fildes);
void __sanitizer_syscall_post_impl_pipe(long res, long fildes);
void __sanitizer_syscall_pre_impl_pipe2(long fildes, long flags);
void __sanitizer_syscall_post_impl_pipe2(long res, long fildes, long flags);
void __sanitizer_syscall_pre_impl_dup(long fildes);
void __sanitizer_syscall_post_impl_dup(long res, long fildes);
void __sanitizer_syscall_pre_impl_dup2(long oldfd, long newfd);
void __sanitizer_syscall_post_impl_dup2(long res, long oldfd, long newfd);
void __sanitizer_syscall_pre_impl_dup3(long oldfd, long newfd, long flags);
void __sanitizer_syscall_post_impl_dup3(long res, long oldfd, long newfd,
                                        long flags);
void __sanitizer_syscall_pre_impl_ioperm(long from, long num, long on);
void __sanitizer_syscall_post_impl_ioperm(long res, long from, long num,
                                          long on);
void __sanitizer_syscall_pre_impl_ioctl(long fd, long cmd, long arg);
void __sanitizer_syscall_post_impl_ioctl(long res, long fd, long cmd, long arg);
void __sanitizer_syscall_pre_impl_flock(long fd, long cmd);
void __sanitizer_syscall_post_impl_flock(long res, long fd, long cmd);
void __sanitizer_syscall_pre_impl_io_setup(long nr_reqs, long ctx);
void __sanitizer_syscall_post_impl_io_setup(long res, long nr_reqs, long ctx);
void __sanitizer_syscall_pre_impl_io_destroy(long ctx);
void __sanitizer_syscall_post_impl_io_destroy(long res, long ctx);
void __sanitizer_syscall_pre_impl_io_getevents(long ctx_id, long min_nr,
                                               long nr, long events,
                                               long timeout);
void __sanitizer_syscall_post_impl_io_getevents(long res, long ctx_id,
                                                long min_nr, long nr,
                                                long events, long timeout);
void __sanitizer_syscall_pre_impl_io_submit(long ctx_id, long arg1, long arg2);
void __sanitizer_syscall_post_impl_io_submit(long res, long ctx_id, long arg1,
                                             long arg2);
void __sanitizer_syscall_pre_impl_io_cancel(long ctx_id, long iocb,
                                            long result);
void __sanitizer_syscall_post_impl_io_cancel(long res, long ctx_id, long iocb,
                                             long result);
void __sanitizer_syscall_pre_impl_sendfile(long out_fd, long in_fd, long offset,
                                           long count);
void __sanitizer_syscall_post_impl_sendfile(long res, long out_fd, long in_fd,
                                            long offset, long count);
void __sanitizer_syscall_pre_impl_sendfile64(long out_fd, long in_fd,
                                             long offset, long count);
void __sanitizer_syscall_post_impl_sendfile64(long res, long out_fd, long in_fd,
                                              long offset, long count);
void __sanitizer_syscall_pre_impl_readlink(long path, long buf, long bufsiz);
void __sanitizer_syscall_post_impl_readlink(long res, long path, long buf,
                                            long bufsiz);
void __sanitizer_syscall_pre_impl_creat(long pathname, long mode);
void __sanitizer_syscall_post_impl_creat(long res, long pathname, long mode);
void __sanitizer_syscall_pre_impl_open(long filename, long flags, long mode);
void __sanitizer_syscall_post_impl_open(long res, long filename, long flags,
                                        long mode);
void __sanitizer_syscall_pre_impl_close(long fd);
void __sanitizer_syscall_post_impl_close(long res, long fd);
void __sanitizer_syscall_pre_impl_access(long filename, long mode);
void __sanitizer_syscall_post_impl_access(long res, long filename, long mode);
void __sanitizer_syscall_pre_impl_vhangup();
void __sanitizer_syscall_post_impl_vhangup(long res);
void __sanitizer_syscall_pre_impl_chown(long filename, long user, long group);
void __sanitizer_syscall_post_impl_chown(long res, long filename, long user,
                                         long group);
void __sanitizer_syscall_pre_impl_lchown(long filename, long user, long group);
void __sanitizer_syscall_post_impl_lchown(long res, long filename, long user,
                                          long group);
void __sanitizer_syscall_pre_impl_fchown(long fd, long user, long group);
void __sanitizer_syscall_post_impl_fchown(long res, long fd, long user,
                                          long group);
void __sanitizer_syscall_pre_impl_chown16(long filename, long user, long group);
void __sanitizer_syscall_post_impl_chown16(long res, long filename, long user,
                                           long group);
void __sanitizer_syscall_pre_impl_lchown16(long filename, long user,
                                           long group);
void __sanitizer_syscall_post_impl_lchown16(long res, long filename, long user,
                                            long group);
void __sanitizer_syscall_pre_impl_fchown16(long fd, long user, long group);
void __sanitizer_syscall_post_impl_fchown16(long res, long fd, long user,
                                            long group);
void __sanitizer_syscall_pre_impl_setregid16(long rgid, long egid);
void __sanitizer_syscall_post_impl_setregid16(long res, long rgid, long egid);
void __sanitizer_syscall_pre_impl_setgid16(long gid);
void __sanitizer_syscall_post_impl_setgid16(long res, long gid);
void __sanitizer_syscall_pre_impl_setreuid16(long ruid, long euid);
void __sanitizer_syscall_post_impl_setreuid16(long res, long ruid, long euid);
void __sanitizer_syscall_pre_impl_setuid16(long uid);
void __sanitizer_syscall_post_impl_setuid16(long res, long uid);
void __sanitizer_syscall_pre_impl_setresuid16(long ruid, long euid, long suid);
void __sanitizer_syscall_post_impl_setresuid16(long res, long ruid, long euid,
                                               long suid);
void __sanitizer_syscall_pre_impl_getresuid16(long ruid, long euid, long suid);
void __sanitizer_syscall_post_impl_getresuid16(long res, long ruid, long euid,
                                               long suid);
void __sanitizer_syscall_pre_impl_setresgid16(long rgid, long egid, long sgid);
void __sanitizer_syscall_post_impl_setresgid16(long res, long rgid, long egid,
                                               long sgid);
void __sanitizer_syscall_pre_impl_getresgid16(long rgid, long egid, long sgid);
void __sanitizer_syscall_post_impl_getresgid16(long res, long rgid, long egid,
                                               long sgid);
void __sanitizer_syscall_pre_impl_setfsuid16(long uid);
void __sanitizer_syscall_post_impl_setfsuid16(long res, long uid);
void __sanitizer_syscall_pre_impl_setfsgid16(long gid);
void __sanitizer_syscall_post_impl_setfsgid16(long res, long gid);
void __sanitizer_syscall_pre_impl_getgroups16(long gidsetsize, long grouplist);
void __sanitizer_syscall_post_impl_getgroups16(long res, long gidsetsize,
                                               long grouplist);
void __sanitizer_syscall_pre_impl_setgroups16(long gidsetsize, long grouplist);
void __sanitizer_syscall_post_impl_setgroups16(long res, long gidsetsize,
                                               long grouplist);
void __sanitizer_syscall_pre_impl_getuid16();
void __sanitizer_syscall_post_impl_getuid16(long res);
void __sanitizer_syscall_pre_impl_geteuid16();
void __sanitizer_syscall_post_impl_geteuid16(long res);
void __sanitizer_syscall_pre_impl_getgid16();
void __sanitizer_syscall_post_impl_getgid16(long res);
void __sanitizer_syscall_pre_impl_getegid16();
void __sanitizer_syscall_post_impl_getegid16(long res);
void __sanitizer_syscall_pre_impl_utime(long filename, long times);
void __sanitizer_syscall_post_impl_utime(long res, long filename, long times);
void __sanitizer_syscall_pre_impl_utimes(long filename, long utimes);
void __sanitizer_syscall_post_impl_utimes(long res, long filename, long utimes);
void __sanitizer_syscall_pre_impl_lseek(long fd, long offset, long origin);
void __sanitizer_syscall_post_impl_lseek(long res, long fd, long offset,
                                         long origin);
void __sanitizer_syscall_pre_impl_llseek(long fd, long offset_high,
                                         long offset_low, long result,
                                         long origin);
void __sanitizer_syscall_post_impl_llseek(long res, long fd, long offset_high,
                                          long offset_low, long result,
                                          long origin);
void __sanitizer_syscall_pre_impl_read(long fd, long buf, long count);
void __sanitizer_syscall_post_impl_read(long res, long fd, long buf,
                                        long count);
void __sanitizer_syscall_pre_impl_readv(long fd, long vec, long vlen);
void __sanitizer_syscall_post_impl_readv(long res, long fd, long vec,
                                         long vlen);
void __sanitizer_syscall_pre_impl_write(long fd, long buf, long count);
void __sanitizer_syscall_post_impl_write(long res, long fd, long buf,
                                         long count);
void __sanitizer_syscall_pre_impl_writev(long fd, long vec, long vlen);
void __sanitizer_syscall_post_impl_writev(long res, long fd, long vec,
                                          long vlen);

#ifdef _LP64
void __sanitizer_syscall_pre_impl_pread64(long fd, long buf, long count,
                                          long pos);
void __sanitizer_syscall_post_impl_pread64(long res, long fd, long buf,
                                           long count, long pos);
void __sanitizer_syscall_pre_impl_pwrite64(long fd, long buf, long count,
                                           long pos);
void __sanitizer_syscall_post_impl_pwrite64(long res, long fd, long buf,
                                            long count, long pos);
#else
void __sanitizer_syscall_pre_impl_pread64(long fd, long buf, long count,
                                          long pos0, long pos1);
void __sanitizer_syscall_post_impl_pread64(long res, long fd, long buf,
                                           long count, long pos0, long pos1);
void __sanitizer_syscall_pre_impl_pwrite64(long fd, long buf, long count,
                                           long pos0, long pos1);
void __sanitizer_syscall_post_impl_pwrite64(long res, long fd, long buf,
                                            long count, long pos0, long pos1);
#endif

void __sanitizer_syscall_pre_impl_preadv(long fd, long vec, long vlen,
                                         long pos_l, long pos_h);
void __sanitizer_syscall_post_impl_preadv(long res, long fd, long vec,
                                          long vlen, long pos_l, long pos_h);
void __sanitizer_syscall_pre_impl_pwritev(long fd, long vec, long vlen,
                                          long pos_l, long pos_h);
void __sanitizer_syscall_post_impl_pwritev(long res, long fd, long vec,
                                           long vlen, long pos_l, long pos_h);
void __sanitizer_syscall_pre_impl_getcwd(long buf, long size);
void __sanitizer_syscall_post_impl_getcwd(long res, long buf, long size);
void __sanitizer_syscall_pre_impl_mkdir(long pathname, long mode);
void __sanitizer_syscall_post_impl_mkdir(long res, long pathname, long mode);
void __sanitizer_syscall_pre_impl_chdir(long filename);
void __sanitizer_syscall_post_impl_chdir(long res, long filename);
void __sanitizer_syscall_pre_impl_fchdir(long fd);
void __sanitizer_syscall_post_impl_fchdir(long res, long fd);
void __sanitizer_syscall_pre_impl_rmdir(long pathname);
void __sanitizer_syscall_post_impl_rmdir(long res, long pathname);
void __sanitizer_syscall_pre_impl_lookup_dcookie(long cookie64, long buf,
                                                 long len);
void __sanitizer_syscall_post_impl_lookup_dcookie(long res, long cookie64,
                                                  long buf, long len);
void __sanitizer_syscall_pre_impl_quotactl(long cmd, long special, long id,
                                           long addr);
void __sanitizer_syscall_post_impl_quotactl(long res, long cmd, long special,
                                            long id, long addr);
void __sanitizer_syscall_pre_impl_getdents(long fd, long dirent, long count);
void __sanitizer_syscall_post_impl_getdents(long res, long fd, long dirent,
                                            long count);
void __sanitizer_syscall_pre_impl_getdents64(long fd, long dirent, long count);
void __sanitizer_syscall_post_impl_getdents64(long res, long fd, long dirent,
                                              long count);
void __sanitizer_syscall_pre_impl_setsockopt(long fd, long level, long optname,
                                             long optval, long optlen);
void __sanitizer_syscall_post_impl_setsockopt(long res, long fd, long level,
                                              long optname, long optval,
                                              long optlen);
void __sanitizer_syscall_pre_impl_getsockopt(long fd, long level, long optname,
                                             long optval, long optlen);
void __sanitizer_syscall_post_impl_getsockopt(long res, long fd, long level,
                                              long optname, long optval,
                                              long optlen);
void __sanitizer_syscall_pre_impl_bind(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_bind(long res, long arg0, long arg1,
                                        long arg2);
void __sanitizer_syscall_pre_impl_connect(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_connect(long res, long arg0, long arg1,
                                           long arg2);
void __sanitizer_syscall_pre_impl_accept(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_accept(long res, long arg0, long arg1,
                                          long arg2);
void __sanitizer_syscall_pre_impl_accept4(long arg0, long arg1, long arg2,
                                          long arg3);
void __sanitizer_syscall_post_impl_accept4(long res, long arg0, long arg1,
                                           long arg2, long arg3);
void __sanitizer_syscall_pre_impl_getsockname(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_getsockname(long res, long arg0, long arg1,
                                               long arg2);
void __sanitizer_syscall_pre_impl_getpeername(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_getpeername(long res, long arg0, long arg1,
                                               long arg2);
void __sanitizer_syscall_pre_impl_send(long arg0, long arg1, long arg2,
                                       long arg3);
void __sanitizer_syscall_post_impl_send(long res, long arg0, long arg1,
                                        long arg2, long arg3);
void __sanitizer_syscall_pre_impl_sendto(long arg0, long arg1, long arg2,
                                         long arg3, long arg4, long arg5);
void __sanitizer_syscall_post_impl_sendto(long res, long arg0, long arg1,
                                          long arg2, long arg3, long arg4,
                                          long arg5);
void __sanitizer_syscall_pre_impl_sendmsg(long fd, long msg, long flags);
void __sanitizer_syscall_post_impl_sendmsg(long res, long fd, long msg,
                                           long flags);
void __sanitizer_syscall_pre_impl_sendmmsg(long fd, long msg, long vlen,
                                           long flags);
void __sanitizer_syscall_post_impl_sendmmsg(long res, long fd, long msg,
                                            long vlen, long flags);
void __sanitizer_syscall_pre_impl_recv(long arg0, long arg1, long arg2,
                                       long arg3);
void __sanitizer_syscall_post_impl_recv(long res, long arg0, long arg1,
                                        long arg2, long arg3);
void __sanitizer_syscall_pre_impl_recvfrom(long arg0, long arg1, long arg2,
                                           long arg3, long arg4, long arg5);
void __sanitizer_syscall_post_impl_recvfrom(long res, long arg0, long arg1,
                                            long arg2, long arg3, long arg4,
                                            long arg5);
void __sanitizer_syscall_pre_impl_recvmsg(long fd, long msg, long flags);
void __sanitizer_syscall_post_impl_recvmsg(long res, long fd, long msg,
                                           long flags);
void __sanitizer_syscall_pre_impl_recvmmsg(long fd, long msg, long vlen,
                                           long flags, long timeout);
void __sanitizer_syscall_post_impl_recvmmsg(long res, long fd, long msg,
                                            long vlen, long flags,
                                            long timeout);
void __sanitizer_syscall_pre_impl_socket(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_socket(long res, long arg0, long arg1,
                                          long arg2);
void __sanitizer_syscall_pre_impl_socketpair(long arg0, long arg1, long arg2,
                                             long arg3);
void __sanitizer_syscall_post_impl_socketpair(long res, long arg0, long arg1,
                                              long arg2, long arg3);
void __sanitizer_syscall_pre_impl_socketcall(long call, long args);
void __sanitizer_syscall_post_impl_socketcall(long res, long call, long args);
void __sanitizer_syscall_pre_impl_listen(long arg0, long arg1);
void __sanitizer_syscall_post_impl_listen(long res, long arg0, long arg1);
void __sanitizer_syscall_pre_impl_poll(long ufds, long nfds, long timeout);
void __sanitizer_syscall_post_impl_poll(long res, long ufds, long nfds,
                                        long timeout);
void __sanitizer_syscall_pre_impl_select(long n, long inp, long outp, long exp,
                                         long tvp);
void __sanitizer_syscall_post_impl_select(long res, long n, long inp, long outp,
                                          long exp, long tvp);
void __sanitizer_syscall_pre_impl_old_select(long arg);
void __sanitizer_syscall_post_impl_old_select(long res, long arg);
void __sanitizer_syscall_pre_impl_epoll_create(long size);
void __sanitizer_syscall_post_impl_epoll_create(long res, long size);
void __sanitizer_syscall_pre_impl_epoll_create1(long flags);
void __sanitizer_syscall_post_impl_epoll_create1(long res, long flags);
void __sanitizer_syscall_pre_impl_epoll_ctl(long epfd, long op, long fd,
                                            long event);
void __sanitizer_syscall_post_impl_epoll_ctl(long res, long epfd, long op,
                                             long fd, long event);
void __sanitizer_syscall_pre_impl_epoll_wait(long epfd, long events,
                                             long maxevents, long timeout);
void __sanitizer_syscall_post_impl_epoll_wait(long res, long epfd, long events,
                                              long maxevents, long timeout);
void __sanitizer_syscall_pre_impl_epoll_pwait(long epfd, long events,
                                              long maxevents, long timeout,
                                              long sigmask, long sigsetsize);
void __sanitizer_syscall_post_impl_epoll_pwait(long res, long epfd, long events,
                                               long maxevents, long timeout,
                                               long sigmask, long sigsetsize);
void __sanitizer_syscall_pre_impl_epoll_pwait2(long epfd, long events,
                                               long maxevents, long timeout,
                                               long sigmask, long sigsetsize);
void __sanitizer_syscall_post_impl_epoll_pwait2(long res, long epfd,
                                                long events, long maxevents,
                                                long timeout, long sigmask,
                                                long sigsetsize);
void __sanitizer_syscall_pre_impl_gethostname(long name, long len);
void __sanitizer_syscall_post_impl_gethostname(long res, long name, long len);
void __sanitizer_syscall_pre_impl_sethostname(long name, long len);
void __sanitizer_syscall_post_impl_sethostname(long res, long name, long len);
void __sanitizer_syscall_pre_impl_setdomainname(long name, long len);
void __sanitizer_syscall_post_impl_setdomainname(long res, long name, long len);
void __sanitizer_syscall_pre_impl_newuname(long name);
void __sanitizer_syscall_post_impl_newuname(long res, long name);
void __sanitizer_syscall_pre_impl_uname(long arg0);
void __sanitizer_syscall_post_impl_uname(long res, long arg0);
void __sanitizer_syscall_pre_impl_olduname(long arg0);
void __sanitizer_syscall_post_impl_olduname(long res, long arg0);
void __sanitizer_syscall_pre_impl_getrlimit(long resource, long rlim);
void __sanitizer_syscall_post_impl_getrlimit(long res, long resource,
                                             long rlim);
void __sanitizer_syscall_pre_impl_old_getrlimit(long resource, long rlim);
void __sanitizer_syscall_post_impl_old_getrlimit(long res, long resource,
                                                 long rlim);
void __sanitizer_syscall_pre_impl_setrlimit(long resource, long rlim);
void __sanitizer_syscall_post_impl_setrlimit(long res, long resource,
                                             long rlim);
void __sanitizer_syscall_pre_impl_prlimit64(long pid, long resource,
                                            long new_rlim, long old_rlim);
void __sanitizer_syscall_post_impl_prlimit64(long res, long pid, long resource,
                                             long new_rlim, long old_rlim);
void __sanitizer_syscall_pre_impl_getrusage(long who, long ru);
void __sanitizer_syscall_post_impl_getrusage(long res, long who, long ru);
void __sanitizer_syscall_pre_impl_umask(long mask);
void __sanitizer_syscall_post_impl_umask(long res, long mask);
void __sanitizer_syscall_pre_impl_msgget(long key, long msgflg);
void __sanitizer_syscall_post_impl_msgget(long res, long key, long msgflg);
void __sanitizer_syscall_pre_impl_msgsnd(long msqid, long msgp, long msgsz,
                                         long msgflg);
void __sanitizer_syscall_post_impl_msgsnd(long res, long msqid, long msgp,
                                          long msgsz, long msgflg);
void __sanitizer_syscall_pre_impl_msgrcv(long msqid, long msgp, long msgsz,
                                         long msgtyp, long msgflg);
void __sanitizer_syscall_post_impl_msgrcv(long res, long msqid, long msgp,
                                          long msgsz, long msgtyp, long msgflg);
void __sanitizer_syscall_pre_impl_msgctl(long msqid, long cmd, long buf);
void __sanitizer_syscall_post_impl_msgctl(long res, long msqid, long cmd,
                                          long buf);
void __sanitizer_syscall_pre_impl_semget(long key, long nsems, long semflg);
void __sanitizer_syscall_post_impl_semget(long res, long key, long nsems,
                                          long semflg);
void __sanitizer_syscall_pre_impl_semop(long semid, long sops, long nsops);
void __sanitizer_syscall_post_impl_semop(long res, long semid, long sops,
                                         long nsops);
void __sanitizer_syscall_pre_impl_semctl(long semid, long semnum, long cmd,
                                         long arg);
void __sanitizer_syscall_post_impl_semctl(long res, long semid, long semnum,
                                          long cmd, long arg);
void __sanitizer_syscall_pre_impl_semtimedop(long semid, long sops, long nsops,
                                             long timeout);
void __sanitizer_syscall_post_impl_semtimedop(long res, long semid, long sops,
                                              long nsops, long timeout);
void __sanitizer_syscall_pre_impl_shmat(long shmid, long shmaddr, long shmflg);
void __sanitizer_syscall_post_impl_shmat(long res, long shmid, long shmaddr,
                                         long shmflg);
void __sanitizer_syscall_pre_impl_shmget(long key, long size, long flag);
void __sanitizer_syscall_post_impl_shmget(long res, long key, long size,
                                          long flag);
void __sanitizer_syscall_pre_impl_shmdt(long shmaddr);
void __sanitizer_syscall_post_impl_shmdt(long res, long shmaddr);
void __sanitizer_syscall_pre_impl_shmctl(long shmid, long cmd, long buf);
void __sanitizer_syscall_post_impl_shmctl(long res, long shmid, long cmd,
                                          long buf);
void __sanitizer_syscall_pre_impl_ipc(long call, long first, long second,
                                      long third, long ptr, long fifth);
void __sanitizer_syscall_post_impl_ipc(long res, long call, long first,
                                       long second, long third, long ptr,
                                       long fifth);
void __sanitizer_syscall_pre_impl_mq_open(long name, long oflag, long mode,
                                          long attr);
void __sanitizer_syscall_post_impl_mq_open(long res, long name, long oflag,
                                           long mode, long attr);
void __sanitizer_syscall_pre_impl_mq_unlink(long name);
void __sanitizer_syscall_post_impl_mq_unlink(long res, long name);
void __sanitizer_syscall_pre_impl_mq_timedsend(long mqdes, long msg_ptr,
                                               long msg_len, long msg_prio,
                                               long abs_timeout);
void __sanitizer_syscall_post_impl_mq_timedsend(long res, long mqdes,
                                                long msg_ptr, long msg_len,
                                                long msg_prio,
                                                long abs_timeout);
void __sanitizer_syscall_pre_impl_mq_timedreceive(long mqdes, long msg_ptr,
                                                  long msg_len, long msg_prio,
                                                  long abs_timeout);
void __sanitizer_syscall_post_impl_mq_timedreceive(long res, long mqdes,
                                                   long msg_ptr, long msg_len,
                                                   long msg_prio,
                                                   long abs_timeout);
void __sanitizer_syscall_pre_impl_mq_notify(long mqdes, long notification);
void __sanitizer_syscall_post_impl_mq_notify(long res, long mqdes,
                                             long notification);
void __sanitizer_syscall_pre_impl_mq_getsetattr(long mqdes, long mqstat,
                                                long omqstat);
void __sanitizer_syscall_post_impl_mq_getsetattr(long res, long mqdes,
                                                 long mqstat, long omqstat);
void __sanitizer_syscall_pre_impl_pciconfig_iobase(long which, long bus,
                                                   long devfn);
void __sanitizer_syscall_post_impl_pciconfig_iobase(long res, long which,
                                                    long bus, long devfn);
void __sanitizer_syscall_pre_impl_pciconfig_read(long bus, long dfn, long off,
                                                 long len, long buf);
void __sanitizer_syscall_post_impl_pciconfig_read(long res, long bus, long dfn,
                                                  long off, long len, long buf);
void __sanitizer_syscall_pre_impl_pciconfig_write(long bus, long dfn, long off,
                                                  long len, long buf);
void __sanitizer_syscall_post_impl_pciconfig_write(long res, long bus, long dfn,
                                                   long off, long len,
                                                   long buf);
void __sanitizer_syscall_pre_impl_swapon(long specialfile, long swap_flags);
void __sanitizer_syscall_post_impl_swapon(long res, long specialfile,
                                          long swap_flags);
void __sanitizer_syscall_pre_impl_swapoff(long specialfile);
void __sanitizer_syscall_post_impl_swapoff(long res, long specialfile);
void __sanitizer_syscall_pre_impl_sysctl(long args);
void __sanitizer_syscall_post_impl_sysctl(long res, long args);
void __sanitizer_syscall_pre_impl_sysinfo(long info);
void __sanitizer_syscall_post_impl_sysinfo(long res, long info);
void __sanitizer_syscall_pre_impl_sysfs(long option, long arg1, long arg2);
void __sanitizer_syscall_post_impl_sysfs(long res, long option, long arg1,
                                         long arg2);
void __sanitizer_syscall_pre_impl_syslog(long type, long buf, long len);
void __sanitizer_syscall_post_impl_syslog(long res, long type, long buf,
                                          long len);
void __sanitizer_syscall_pre_impl_uselib(long library);
void __sanitizer_syscall_post_impl_uselib(long res, long library);
void __sanitizer_syscall_pre_impl_ni_syscall();
void __sanitizer_syscall_post_impl_ni_syscall(long res);
void __sanitizer_syscall_pre_impl_ptrace(long request, long pid, long addr,
                                         long data);
void __sanitizer_syscall_post_impl_ptrace(long res, long request, long pid,
                                          long addr, long data);
void __sanitizer_syscall_pre_impl_add_key(long _type, long _description,
                                          long _payload, long plen,
                                          long destringid);
void __sanitizer_syscall_post_impl_add_key(long res, long _type,
                                           long _description, long _payload,
                                           long plen, long destringid);
void __sanitizer_syscall_pre_impl_request_key(long _type, long _description,
                                              long _callout_info,
                                              long destringid);
void __sanitizer_syscall_post_impl_request_key(long res, long _type,
                                               long _description,
                                               long _callout_info,
                                               long destringid);
void __sanitizer_syscall_pre_impl_keyctl(long cmd, long arg2, long arg3,
                                         long arg4, long arg5);
void __sanitizer_syscall_post_impl_keyctl(long res, long cmd, long arg2,
                                          long arg3, long arg4, long arg5);
void __sanitizer_syscall_pre_impl_ioprio_set(long which, long who, long ioprio);
void __sanitizer_syscall_post_impl_ioprio_set(long res, long which, long who,
                                              long ioprio);
void __sanitizer_syscall_pre_impl_ioprio_get(long which, long who);
void __sanitizer_syscall_post_impl_ioprio_get(long res, long which, long who);
void __sanitizer_syscall_pre_impl_set_mempolicy(long mode, long nmask,
                                                long maxnode);
void __sanitizer_syscall_post_impl_set_mempolicy(long res, long mode,
                                                 long nmask, long maxnode);
void __sanitizer_syscall_pre_impl_migrate_pages(long pid, long maxnode,
                                                long from, long to);
void __sanitizer_syscall_post_impl_migrate_pages(long res, long pid,
                                                 long maxnode, long from,
                                                 long to);
void __sanitizer_syscall_pre_impl_move_pages(long pid, long nr_pages,
                                             long pages, long nodes,
                                             long status, long flags);
void __sanitizer_syscall_post_impl_move_pages(long res, long pid, long nr_pages,
                                              long pages, long nodes,
                                              long status, long flags);
void __sanitizer_syscall_pre_impl_mbind(long start, long len, long mode,
                                        long nmask, long maxnode, long flags);
void __sanitizer_syscall_post_impl_mbind(long res, long start, long len,
                                         long mode, long nmask, long maxnode,
                                         long flags);
void __sanitizer_syscall_pre_impl_get_mempolicy(long policy, long nmask,
                                                long maxnode, long addr,
                                                long flags);
void __sanitizer_syscall_post_impl_get_mempolicy(long res, long policy,
                                                 long nmask, long maxnode,
                                                 long addr, long flags);
void __sanitizer_syscall_pre_impl_inotify_init();
void __sanitizer_syscall_post_impl_inotify_init(long res);
void __sanitizer_syscall_pre_impl_inotify_init1(long flags);
void __sanitizer_syscall_post_impl_inotify_init1(long res, long flags);
void __sanitizer_syscall_pre_impl_inotify_add_watch(long fd, long path,
                                                    long mask);
void __sanitizer_syscall_post_impl_inotify_add_watch(long res, long fd,
                                                     long path, long mask);
void __sanitizer_syscall_pre_impl_inotify_rm_watch(long fd, long wd);
void __sanitizer_syscall_post_impl_inotify_rm_watch(long res, long fd, long wd);
void __sanitizer_syscall_pre_impl_spu_run(long fd, long unpc, long ustatus);
void __sanitizer_syscall_post_impl_spu_run(long res, long fd, long unpc,
                                           long ustatus);
void __sanitizer_syscall_pre_impl_spu_create(long name, long flags, long mode,
                                             long fd);
void __sanitizer_syscall_post_impl_spu_create(long res, long name, long flags,
                                              long mode, long fd);
void __sanitizer_syscall_pre_impl_mknodat(long dfd, long filename, long mode,
                                          long dev);
void __sanitizer_syscall_post_impl_mknodat(long res, long dfd, long filename,
                                           long mode, long dev);
void __sanitizer_syscall_pre_impl_mkdirat(long dfd, long pathname, long mode);
void __sanitizer_syscall_post_impl_mkdirat(long res, long dfd, long pathname,
                                           long mode);
void __sanitizer_syscall_pre_impl_unlinkat(long dfd, long pathname, long flag);
void __sanitizer_syscall_post_impl_unlinkat(long res, long dfd, long pathname,
                                            long flag);
void __sanitizer_syscall_pre_impl_symlinkat(long oldname, long newdfd,
                                            long newname);
void __sanitizer_syscall_post_impl_symlinkat(long res, long oldname,
                                             long newdfd, long newname);
void __sanitizer_syscall_pre_impl_linkat(long olddfd, long oldname, long newdfd,
                                         long newname, long flags);
void __sanitizer_syscall_post_impl_linkat(long res, long olddfd, long oldname,
                                          long newdfd, long newname,
                                          long flags);
void __sanitizer_syscall_pre_impl_renameat(long olddfd, long oldname,
                                           long newdfd, long newname);
void __sanitizer_syscall_post_impl_renameat(long res, long olddfd, long oldname,
                                            long newdfd, long newname);
void __sanitizer_syscall_pre_impl_futimesat(long dfd, long filename,
                                            long utimes);
void __sanitizer_syscall_post_impl_futimesat(long res, long dfd, long filename,
                                             long utimes);
void __sanitizer_syscall_pre_impl_faccessat(long dfd, long filename, long mode);
void __sanitizer_syscall_post_impl_faccessat(long res, long dfd, long filename,
                                             long mode);
void __sanitizer_syscall_pre_impl_fchmodat(long dfd, long filename, long mode);
void __sanitizer_syscall_post_impl_fchmodat(long res, long dfd, long filename,
                                            long mode);
void __sanitizer_syscall_pre_impl_fchownat(long dfd, long filename, long user,
                                           long group, long flag);
void __sanitizer_syscall_post_impl_fchownat(long res, long dfd, long filename,
                                            long user, long group, long flag);
void __sanitizer_syscall_pre_impl_openat(long dfd, long filename, long flags,
                                         long mode);
void __sanitizer_syscall_post_impl_openat(long res, long dfd, long filename,
                                          long flags, long mode);
void __sanitizer_syscall_pre_impl_newfstatat(long dfd, long filename,
                                             long statbuf, long flag);
void __sanitizer_syscall_post_impl_newfstatat(long res, long dfd, long filename,
                                              long statbuf, long flag);
void __sanitizer_syscall_pre_impl_fstatat64(long dfd, long filename,
                                            long statbuf, long flag);
void __sanitizer_syscall_post_impl_fstatat64(long res, long dfd, long filename,
                                             long statbuf, long flag);
void __sanitizer_syscall_pre_impl_readlinkat(long dfd, long path, long buf,
                                             long bufsiz);
void __sanitizer_syscall_post_impl_readlinkat(long res, long dfd, long path,
                                              long buf, long bufsiz);
void __sanitizer_syscall_pre_impl_utimensat(long dfd, long filename,
                                            long utimes, long flags);
void __sanitizer_syscall_post_impl_utimensat(long res, long dfd, long filename,
                                             long utimes, long flags);
void __sanitizer_syscall_pre_impl_unshare(long unshare_flags);
void __sanitizer_syscall_post_impl_unshare(long res, long unshare_flags);
void __sanitizer_syscall_pre_impl_splice(long fd_in, long off_in, long fd_out,
                                         long off_out, long len, long flags);
void __sanitizer_syscall_post_impl_splice(long res, long fd_in, long off_in,
                                          long fd_out, long off_out, long len,
                                          long flags);
void __sanitizer_syscall_pre_impl_vmsplice(long fd, long iov, long nr_segs,
                                           long flags);
void __sanitizer_syscall_post_impl_vmsplice(long res, long fd, long iov,
                                            long nr_segs, long flags);
void __sanitizer_syscall_pre_impl_tee(long fdin, long fdout, long len,
                                      long flags);
void __sanitizer_syscall_post_impl_tee(long res, long fdin, long fdout,
                                       long len, long flags);
void __sanitizer_syscall_pre_impl_get_robust_list(long pid, long head_ptr,
                                                  long len_ptr);
void __sanitizer_syscall_post_impl_get_robust_list(long res, long pid,
                                                   long head_ptr, long len_ptr);
void __sanitizer_syscall_pre_impl_set_robust_list(long head, long len);
void __sanitizer_syscall_post_impl_set_robust_list(long res, long head,
                                                   long len);
void __sanitizer_syscall_pre_impl_getcpu(long cpu, long node, long cache);
void __sanitizer_syscall_post_impl_getcpu(long res, long cpu, long node,
                                          long cache);
void __sanitizer_syscall_pre_impl_signalfd(long ufd, long user_mask,
                                           long sizemask);
void __sanitizer_syscall_post_impl_signalfd(long res, long ufd, long user_mask,
                                            long sizemask);
void __sanitizer_syscall_pre_impl_signalfd4(long ufd, long user_mask,
                                            long sizemask, long flags);
void __sanitizer_syscall_post_impl_signalfd4(long res, long ufd, long user_mask,
                                             long sizemask, long flags);
void __sanitizer_syscall_pre_impl_timerfd_create(long clockid, long flags);
void __sanitizer_syscall_post_impl_timerfd_create(long res, long clockid,
                                                  long flags);
void __sanitizer_syscall_pre_impl_timerfd_settime(long ufd, long flags,
                                                  long utmr, long otmr);
void __sanitizer_syscall_post_impl_timerfd_settime(long res, long ufd,
                                                   long flags, long utmr,
                                                   long otmr);
void __sanitizer_syscall_pre_impl_timerfd_gettime(long ufd, long otmr);
void __sanitizer_syscall_post_impl_timerfd_gettime(long res, long ufd,
                                                   long otmr);
void __sanitizer_syscall_pre_impl_eventfd(long count);
void __sanitizer_syscall_post_impl_eventfd(long res, long count);
void __sanitizer_syscall_pre_impl_eventfd2(long count, long flags);
void __sanitizer_syscall_post_impl_eventfd2(long res, long count, long flags);
void __sanitizer_syscall_pre_impl_old_readdir(long arg0, long arg1, long arg2);
void __sanitizer_syscall_post_impl_old_readdir(long res, long arg0, long arg1,
                                               long arg2);
void __sanitizer_syscall_pre_impl_pselect6(long arg0, long arg1, long arg2,
                                           long arg3, long arg4, long arg5);
void __sanitizer_syscall_post_impl_pselect6(long res, long arg0, long arg1,
                                            long arg2, long arg3, long arg4,
                                            long arg5);
void __sanitizer_syscall_pre_impl_ppoll(long arg0, long arg1, long arg2,
                                        long arg3, long arg4);
void __sanitizer_syscall_post_impl_ppoll(long res, long arg0, long arg1,
                                         long arg2, long arg3, long arg4);
void __sanitizer_syscall_pre_impl_fanotify_init(long flags, long event_f_flags);
void __sanitizer_syscall_post_impl_fanotify_init(long res, long flags,
                                                 long event_f_flags);
void __sanitizer_syscall_pre_impl_fanotify_mark(long fanotify_fd, long flags,
                                                long mask, long fd,
                                                long pathname);
void __sanitizer_syscall_post_impl_fanotify_mark(long res, long fanotify_fd,
                                                 long flags, long mask, long fd,
                                                 long pathname);
void __sanitizer_syscall_pre_impl_syncfs(long fd);
void __sanitizer_syscall_post_impl_syncfs(long res, long fd);
void __sanitizer_syscall_pre_impl_perf_event_open(long attr_uptr, long pid,
                                                  long cpu, long group_fd,
                                                  long flags);
void __sanitizer_syscall_post_impl_perf_event_open(long res, long attr_uptr,
                                                   long pid, long cpu,
                                                   long group_fd, long flags);
void __sanitizer_syscall_pre_impl_mmap_pgoff(long addr, long len, long prot,
                                             long flags, long fd, long pgoff);
void __sanitizer_syscall_post_impl_mmap_pgoff(long res, long addr, long len,
                                              long prot, long flags, long fd,
                                              long pgoff);
void __sanitizer_syscall_pre_impl_old_mmap(long arg);
void __sanitizer_syscall_post_impl_old_mmap(long res, long arg);
void __sanitizer_syscall_pre_impl_name_to_handle_at(long dfd, long name,
                                                    long handle, long mnt_id,
                                                    long flag);
void __sanitizer_syscall_post_impl_name_to_handle_at(long res, long dfd,
                                                     long name, long handle,
                                                     long mnt_id, long flag);
void __sanitizer_syscall_pre_impl_open_by_handle_at(long mountdirfd,
                                                    long handle, long flags);
void __sanitizer_syscall_post_impl_open_by_handle_at(long res, long mountdirfd,
                                                     long handle, long flags);
void __sanitizer_syscall_pre_impl_setns(long fd, long nstype);
void __sanitizer_syscall_post_impl_setns(long res, long fd, long nstype);
void __sanitizer_syscall_pre_impl_process_vm_readv(long pid, long lvec,
                                                   long liovcnt, long rvec,
                                                   long riovcnt, long flags);
void __sanitizer_syscall_post_impl_process_vm_readv(long res, long pid,
                                                    long lvec, long liovcnt,
                                                    long rvec, long riovcnt,
                                                    long flags);
void __sanitizer_syscall_pre_impl_process_vm_writev(long pid, long lvec,
                                                    long liovcnt, long rvec,
                                                    long riovcnt, long flags);
void __sanitizer_syscall_post_impl_process_vm_writev(long res, long pid,
                                                     long lvec, long liovcnt,
                                                     long rvec, long riovcnt,
                                                     long flags);
void __sanitizer_syscall_pre_impl_fork();
void __sanitizer_syscall_post_impl_fork(long res);
void __sanitizer_syscall_pre_impl_vfork();
void __sanitizer_syscall_post_impl_vfork(long res);
void __sanitizer_syscall_pre_impl_sigaction(long signum, long act, long oldact);
void __sanitizer_syscall_post_impl_sigaction(long res, long signum, long act,
                                             long oldact);
void __sanitizer_syscall_pre_impl_rt_sigaction(long signum, long act,
                                               long oldact, long sz);
void __sanitizer_syscall_post_impl_rt_sigaction(long res, long signum, long act,
                                                long oldact, long sz);
void __sanitizer_syscall_pre_impl_sigaltstack(long ss, long oss);
void __sanitizer_syscall_post_impl_sigaltstack(long res, long ss, long oss);
void __sanitizer_syscall_pre_impl_futex(long uaddr, long futex_op, long val,
                                        long timeout, long uaddr2, long val3);
void __sanitizer_syscall_post_impl_futex(long res, long uaddr, long futex_op,
                                         long val, long timeout, long uaddr2,
                                         long val3);
#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_LINUX_SYSCALL_HOOKS_H
