//===-- sanitizer_common_syscalls.inc ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common syscalls handlers for tools like AddressSanitizer,
// ThreadSanitizer, MemorySanitizer, etc.
//
// This file should be included into the tool's interceptor file,
// which has to define it's own macros:
//   COMMON_SYSCALL_PRE_READ_RANGE
//          Called in prehook for regions that will be read by the kernel and
//          must be initialized.
//   COMMON_SYSCALL_PRE_WRITE_RANGE
//          Called in prehook for regions that will be written to by the kernel
//          and must be addressable. The actual write range may be smaller than
//          reported in the prehook. See POST_WRITE_RANGE.
//   COMMON_SYSCALL_POST_READ_RANGE
//          Called in posthook for regions that were read by the kernel. Does
//          not make much sense.
//   COMMON_SYSCALL_POST_WRITE_RANGE
//          Called in posthook for regions that were written to by the kernel
//          and are now initialized.
//   COMMON_SYSCALL_ACQUIRE(addr)
//          Acquire memory visibility from addr.
//   COMMON_SYSCALL_RELEASE(addr)
//          Release memory visibility to addr.
//   COMMON_SYSCALL_FD_CLOSE(fd)
//          Called before closing file descriptor fd.
//   COMMON_SYSCALL_FD_ACQUIRE(fd)
//          Acquire memory visibility from fd.
//   COMMON_SYSCALL_FD_RELEASE(fd)
//          Release memory visibility to fd.
//   COMMON_SYSCALL_PRE_FORK()
//          Called before fork syscall.
//   COMMON_SYSCALL_POST_FORK(long res)
//          Called after fork syscall.
//   COMMON_SYSCALL_BLOCKING_START()
//         Called before blocking syscall.
//   COMMON_SYSCALL_BLOCKING_END()
//         Called after blocking syscall.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_LINUX

#  include "sanitizer_libc.h"

#  define PRE_SYSCALL(name) \
    SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_syscall_pre_impl_##name
#  define PRE_READ(p, s) COMMON_SYSCALL_PRE_READ_RANGE(p, s)
#  define PRE_WRITE(p, s) COMMON_SYSCALL_PRE_WRITE_RANGE(p, s)

#  define POST_SYSCALL(name) \
    SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_syscall_post_impl_##name
#  define POST_READ(p, s) COMMON_SYSCALL_POST_READ_RANGE(p, s)
#  define POST_WRITE(p, s) COMMON_SYSCALL_POST_WRITE_RANGE(p, s)

#  ifndef COMMON_SYSCALL_ACQUIRE
#    define COMMON_SYSCALL_ACQUIRE(addr) ((void)(addr))
#  endif

#  ifndef COMMON_SYSCALL_RELEASE
#    define COMMON_SYSCALL_RELEASE(addr) ((void)(addr))
#  endif

#  ifndef COMMON_SYSCALL_FD_CLOSE
#    define COMMON_SYSCALL_FD_CLOSE(fd) ((void)(fd))
#  endif

#  ifndef COMMON_SYSCALL_FD_ACQUIRE
#    define COMMON_SYSCALL_FD_ACQUIRE(fd) ((void)(fd))
#  endif

#  ifndef COMMON_SYSCALL_FD_RELEASE
#    define COMMON_SYSCALL_FD_RELEASE(fd) ((void)(fd))
#  endif

#  ifndef COMMON_SYSCALL_PRE_FORK
#    define COMMON_SYSCALL_PRE_FORK() \
      {}
#  endif

#  ifndef COMMON_SYSCALL_POST_FORK
#    define COMMON_SYSCALL_POST_FORK(res) \
      {}
#  endif

#  ifndef COMMON_SYSCALL_BLOCKING_START
#    define COMMON_SYSCALL_BLOCKING_START() \
      {}
#  endif

#  ifndef COMMON_SYSCALL_BLOCKING_END
#    define COMMON_SYSCALL_BLOCKING_END() \
      {}
#  endif

// FIXME: do some kind of PRE_READ for all syscall arguments (int(s) and such).

extern "C" {
struct sanitizer_kernel_iovec {
  void *iov_base;
  unsigned long iov_len;
};

struct sanitizer_kernel_msghdr {
  void *msg_name;
  int msg_namelen;
  struct sanitizer_kernel_iovec *msg_iov;
  unsigned long msg_iovlen;
  void *msg_control;
  unsigned long msg_controllen;
  unsigned msg_flags;
};

struct sanitizer_kernel_mmsghdr {
  struct sanitizer_kernel_msghdr msg_hdr;
  unsigned msg_len;
};

struct sanitizer_kernel_timespec {
  long tv_sec;
  long tv_nsec;
};

struct sanitizer_kernel_timeval {
  long tv_sec;
  long tv_usec;
};

struct sanitizer_kernel_rusage {
  struct sanitizer_kernel_timeval ru_timeval[2];
  long ru_long[14];
};

struct sanitizer_kernel_sockaddr {
  unsigned short sa_family;
  char sa_data[14];
};

// Real sigset size is always passed as a syscall argument.
// Declare it "void" to catch sizeof(kernel_sigset_t).
typedef void kernel_sigset_t;

static void kernel_write_iovec(const __sanitizer_iovec *iovec, SIZE_T iovlen,
                               SIZE_T maxlen) {
  for (SIZE_T i = 0; i < iovlen && maxlen; ++i) {
    SSIZE_T sz = Min(iovec[i].iov_len, maxlen);
    POST_WRITE(iovec[i].iov_base, sz);
    maxlen -= sz;
  }
}

// This functions uses POST_READ, because it needs to run after syscall to know
// the real read range.
static void kernel_read_iovec(const __sanitizer_iovec *iovec, SIZE_T iovlen,
                              SIZE_T maxlen) {
  POST_READ(iovec, sizeof(*iovec) * iovlen);
  for (SIZE_T i = 0; i < iovlen && maxlen; ++i) {
    SSIZE_T sz = Min(iovec[i].iov_len, maxlen);
    POST_READ(iovec[i].iov_base, sz);
    maxlen -= sz;
  }
}

PRE_SYSCALL(recvmsg)(long sockfd, sanitizer_kernel_msghdr *msg, long flags) {
  PRE_READ(msg, sizeof(*msg));
}

POST_SYSCALL(recvmsg)
(long res, long sockfd, sanitizer_kernel_msghdr *msg, long flags) {
  if (res >= 0) {
    if (msg) {
      for (unsigned long i = 0; i < msg->msg_iovlen; ++i) {
        POST_WRITE(msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
      }
      POST_WRITE(msg->msg_control, msg->msg_controllen);
    }
  }
}

PRE_SYSCALL(recvmmsg)
(long fd, sanitizer_kernel_mmsghdr *msg, long vlen, long flags, void *timeout) {
  PRE_READ(msg, vlen * sizeof(*msg));
}

POST_SYSCALL(recvmmsg)
(long res, long fd, sanitizer_kernel_mmsghdr *msg, long vlen, long flags,
 void *timeout) {
  if (res >= 0) {
    if (msg) {
      for (unsigned long i = 0; i < msg->msg_hdr.msg_iovlen; ++i) {
        POST_WRITE(msg->msg_hdr.msg_iov[i].iov_base,
                   msg->msg_hdr.msg_iov[i].iov_len);
      }
      POST_WRITE(msg->msg_hdr.msg_control, msg->msg_hdr.msg_controllen);
      POST_WRITE(&msg->msg_len, sizeof(msg->msg_len));
    }
    if (timeout)
      POST_WRITE(timeout, struct_timespec_sz);
  }
}

PRE_SYSCALL(read)(long fd, void *buf, uptr count) {
  if (buf) {
    PRE_WRITE(buf, count);
  }
}

POST_SYSCALL(read)(long res, long fd, void *buf, uptr count) {
  if (res > 0 && buf) {
    POST_WRITE(buf, res);
  }
}

PRE_SYSCALL(time)(void *tloc) {}

POST_SYSCALL(time)(long res, void *tloc) {
  if (res >= 0) {
    if (tloc)
      POST_WRITE(tloc, sizeof(long));
  }
}

PRE_SYSCALL(stime)(void *tptr) {}

POST_SYSCALL(stime)(long res, void *tptr) {
  if (res >= 0) {
    if (tptr)
      POST_WRITE(tptr, sizeof(long));
  }
}

PRE_SYSCALL(gettimeofday)(void *tv, void *tz) {}

POST_SYSCALL(gettimeofday)(long res, void *tv, void *tz) {
  if (res >= 0) {
    if (tv)
      POST_WRITE(tv, timeval_sz);
    if (tz)
      POST_WRITE(tz, struct_timezone_sz);
  }
}

PRE_SYSCALL(settimeofday)(void *tv, void *tz) {}

POST_SYSCALL(settimeofday)(long res, void *tv, void *tz) {
  if (res >= 0) {
    if (tv)
      POST_WRITE(tv, timeval_sz);
    if (tz)
      POST_WRITE(tz, struct_timezone_sz);
  }
}

#  if !SANITIZER_ANDROID
PRE_SYSCALL(adjtimex)(void *txc_p) {}

POST_SYSCALL(adjtimex)(long res, void *txc_p) {
  if (res >= 0) {
    if (txc_p)
      POST_WRITE(txc_p, struct_timex_sz);
  }
}
#  endif

PRE_SYSCALL(times)(void *tbuf) {}

POST_SYSCALL(times)(long res, void *tbuf) {
  if (res >= 0) {
    if (tbuf)
      POST_WRITE(tbuf, struct_tms_sz);
  }
}

PRE_SYSCALL(gettid)() {}

POST_SYSCALL(gettid)(long res) {}

PRE_SYSCALL(nanosleep)(void *rqtp, void *rmtp) {}

POST_SYSCALL(nanosleep)(long res, void *rqtp, void *rmtp) {
  if (res >= 0) {
    if (rqtp)
      POST_WRITE(rqtp, struct_timespec_sz);
    if (rmtp)
      POST_WRITE(rmtp, struct_timespec_sz);
  }
}

PRE_SYSCALL(alarm)(long seconds) {}

POST_SYSCALL(alarm)(long res, long seconds) {}

PRE_SYSCALL(getpid)() {}

POST_SYSCALL(getpid)(long res) {}

PRE_SYSCALL(getppid)() {}

POST_SYSCALL(getppid)(long res) {}

PRE_SYSCALL(getuid)() {}

POST_SYSCALL(getuid)(long res) {}

PRE_SYSCALL(geteuid)() {}

POST_SYSCALL(geteuid)(long res) {}

PRE_SYSCALL(getgid)() {}

POST_SYSCALL(getgid)(long res) {}

PRE_SYSCALL(getegid)() {}

POST_SYSCALL(getegid)(long res) {}

PRE_SYSCALL(getresuid)(void *ruid, void *euid, void *suid) {}

POST_SYSCALL(getresuid)(long res, void *ruid, void *euid, void *suid) {
  if (res >= 0) {
    if (ruid)
      POST_WRITE(ruid, sizeof(unsigned));
    if (euid)
      POST_WRITE(euid, sizeof(unsigned));
    if (suid)
      POST_WRITE(suid, sizeof(unsigned));
  }
}

PRE_SYSCALL(getresgid)(void *rgid, void *egid, void *sgid) {}

POST_SYSCALL(getresgid)(long res, void *rgid, void *egid, void *sgid) {
  if (res >= 0) {
    if (rgid)
      POST_WRITE(rgid, sizeof(unsigned));
    if (egid)
      POST_WRITE(egid, sizeof(unsigned));
    if (sgid)
      POST_WRITE(sgid, sizeof(unsigned));
  }
}

PRE_SYSCALL(getpgid)(long pid) {}

POST_SYSCALL(getpgid)(long res, long pid) {}

PRE_SYSCALL(getpgrp)() {}

POST_SYSCALL(getpgrp)(long res) {}

PRE_SYSCALL(getsid)(long pid) {}

POST_SYSCALL(getsid)(long res, long pid) {}

PRE_SYSCALL(getgroups)(long gidsetsize, void *grouplist) {}

POST_SYSCALL(getgroups)
(long res, long gidsetsize, __sanitizer___kernel_gid_t *grouplist) {
  if (res >= 0) {
    if (grouplist)
      POST_WRITE(grouplist, res * sizeof(*grouplist));
  }
}

PRE_SYSCALL(setregid)(long rgid, long egid) {}

POST_SYSCALL(setregid)(long res, long rgid, long egid) {}

PRE_SYSCALL(setgid)(long gid) {}

POST_SYSCALL(setgid)(long res, long gid) {}

PRE_SYSCALL(setreuid)(long ruid, long euid) {}

POST_SYSCALL(setreuid)(long res, long ruid, long euid) {}

PRE_SYSCALL(setuid)(long uid) {}

POST_SYSCALL(setuid)(long res, long uid) {}

PRE_SYSCALL(setresuid)(long ruid, long euid, long suid) {}

POST_SYSCALL(setresuid)(long res, long ruid, long euid, long suid) {}

PRE_SYSCALL(setresgid)(long rgid, long egid, long sgid) {}

POST_SYSCALL(setresgid)(long res, long rgid, long egid, long sgid) {}

PRE_SYSCALL(setfsuid)(long uid) {}

POST_SYSCALL(setfsuid)(long res, long uid) {}

PRE_SYSCALL(setfsgid)(long gid) {}

POST_SYSCALL(setfsgid)(long res, long gid) {}

PRE_SYSCALL(setpgid)(long pid, long pgid) {}

POST_SYSCALL(setpgid)(long res, long pid, long pgid) {}

PRE_SYSCALL(setsid)() {}

POST_SYSCALL(setsid)(long res) {}

PRE_SYSCALL(setgroups)(long gidsetsize, __sanitizer___kernel_gid_t *grouplist) {
  if (grouplist)
    POST_WRITE(grouplist, gidsetsize * sizeof(*grouplist));
}

POST_SYSCALL(setgroups)
(long res, long gidsetsize, __sanitizer___kernel_gid_t *grouplist) {}

PRE_SYSCALL(acct)(const void *name) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(acct)(long res, const void *name) {}

PRE_SYSCALL(capget)(void *header, void *dataptr) {
  if (header)
    PRE_READ(header, __user_cap_header_struct_sz);
}

POST_SYSCALL(capget)(long res, void *header, void *dataptr) {
  if (res >= 0)
    if (dataptr)
      POST_WRITE(dataptr, __user_cap_data_struct_sz(header));
}

PRE_SYSCALL(capset)(void *header, const void *data) {
  if (header)
    PRE_READ(header, __user_cap_header_struct_sz);
  if (data)
    PRE_READ(data, __user_cap_data_struct_sz(header));
}

POST_SYSCALL(capset)(long res, void *header, const void *data) {}

PRE_SYSCALL(personality)(long personality) {}

POST_SYSCALL(personality)(long res, long personality) {}

PRE_SYSCALL(sigpending)(void *set) {}

POST_SYSCALL(sigpending)(long res, void *set) {
  if (res >= 0) {
    if (set)
      POST_WRITE(set, old_sigset_t_sz);
  }
}

PRE_SYSCALL(sigprocmask)(long how, void *set, void *oset) {}

POST_SYSCALL(sigprocmask)(long res, long how, void *set, void *oset) {
  if (res >= 0) {
    if (set)
      POST_WRITE(set, old_sigset_t_sz);
    if (oset)
      POST_WRITE(oset, old_sigset_t_sz);
  }
}

PRE_SYSCALL(getitimer)(long which, void *value) {}

POST_SYSCALL(getitimer)(long res, long which, void *value) {
  if (res >= 0) {
    if (value)
      POST_WRITE(value, struct_itimerval_sz);
  }
}

PRE_SYSCALL(setitimer)(long which, void *value, void *ovalue) {}

POST_SYSCALL(setitimer)(long res, long which, void *value, void *ovalue) {
  if (res >= 0) {
    if (value)
      POST_WRITE(value, struct_itimerval_sz);
    if (ovalue)
      POST_WRITE(ovalue, struct_itimerval_sz);
  }
}

PRE_SYSCALL(timer_create)
(long which_clock, void *timer_event_spec, void *created_timer_id) {}

POST_SYSCALL(timer_create)
(long res, long which_clock, void *timer_event_spec, void *created_timer_id) {
  if (res >= 0) {
    if (timer_event_spec)
      POST_WRITE(timer_event_spec, struct_sigevent_sz);
    if (created_timer_id)
      POST_WRITE(created_timer_id, sizeof(long));
  }
}

PRE_SYSCALL(timer_gettime)(long timer_id, void *setting) {}

POST_SYSCALL(timer_gettime)(long res, long timer_id, void *setting) {
  if (res >= 0) {
    if (setting)
      POST_WRITE(setting, struct_itimerspec_sz);
  }
}

PRE_SYSCALL(timer_getoverrun)(long timer_id) {}

POST_SYSCALL(timer_getoverrun)(long res, long timer_id) {}

PRE_SYSCALL(timer_settime)
(long timer_id, long flags, const void *new_setting, void *old_setting) {
  if (new_setting)
    PRE_READ(new_setting, struct_itimerspec_sz);
}

POST_SYSCALL(timer_settime)
(long res, long timer_id, long flags, const void *new_setting,
 void *old_setting) {
  if (res >= 0) {
    if (old_setting)
      POST_WRITE(old_setting, struct_itimerspec_sz);
  }
}

PRE_SYSCALL(timer_delete)(long timer_id) {}

POST_SYSCALL(timer_delete)(long res, long timer_id) {}

PRE_SYSCALL(clock_settime)(long which_clock, const void *tp) {
  if (tp)
    PRE_READ(tp, struct_timespec_sz);
}

POST_SYSCALL(clock_settime)(long res, long which_clock, const void *tp) {}

PRE_SYSCALL(clock_gettime)(long which_clock, void *tp) {}

POST_SYSCALL(clock_gettime)(long res, long which_clock, void *tp) {
  if (res >= 0) {
    if (tp)
      POST_WRITE(tp, struct_timespec_sz);
  }
}

#  if !SANITIZER_ANDROID
PRE_SYSCALL(clock_adjtime)(long which_clock, void *tx) {}

POST_SYSCALL(clock_adjtime)(long res, long which_clock, void *tx) {
  if (res >= 0) {
    if (tx)
      POST_WRITE(tx, struct_timex_sz);
  }
}
#  endif

PRE_SYSCALL(clock_getres)(long which_clock, void *tp) {}

POST_SYSCALL(clock_getres)(long res, long which_clock, void *tp) {
  if (res >= 0) {
    if (tp)
      POST_WRITE(tp, struct_timespec_sz);
  }
}

PRE_SYSCALL(clock_nanosleep)
(long which_clock, long flags, const void *rqtp, void *rmtp) {
  if (rqtp)
    PRE_READ(rqtp, struct_timespec_sz);
}

POST_SYSCALL(clock_nanosleep)
(long res, long which_clock, long flags, const void *rqtp, void *rmtp) {
  if (res >= 0) {
    if (rmtp)
      POST_WRITE(rmtp, struct_timespec_sz);
  }
}

PRE_SYSCALL(nice)(long increment) {}

POST_SYSCALL(nice)(long res, long increment) {}

PRE_SYSCALL(sched_setscheduler)(long pid, long policy, void *param) {}

POST_SYSCALL(sched_setscheduler)(long res, long pid, long policy, void *param) {
  if (res >= 0) {
    if (param)
      POST_WRITE(param, struct_sched_param_sz);
  }
}

PRE_SYSCALL(sched_setparam)(long pid, void *param) {
  if (param)
    PRE_READ(param, struct_sched_param_sz);
}

POST_SYSCALL(sched_setparam)(long res, long pid, void *param) {}

PRE_SYSCALL(sched_getscheduler)(long pid) {}

POST_SYSCALL(sched_getscheduler)(long res, long pid) {}

PRE_SYSCALL(sched_getparam)(long pid, void *param) {}

POST_SYSCALL(sched_getparam)(long res, long pid, void *param) {
  if (res >= 0) {
    if (param)
      POST_WRITE(param, struct_sched_param_sz);
  }
}

PRE_SYSCALL(sched_setaffinity)(long pid, long len, void *user_mask_ptr) {
  if (user_mask_ptr)
    PRE_READ(user_mask_ptr, len);
}

POST_SYSCALL(sched_setaffinity)
(long res, long pid, long len, void *user_mask_ptr) {}

PRE_SYSCALL(sched_getaffinity)(long pid, long len, void *user_mask_ptr) {}

POST_SYSCALL(sched_getaffinity)
(long res, long pid, long len, void *user_mask_ptr) {
  if (res >= 0) {
    if (user_mask_ptr)
      POST_WRITE(user_mask_ptr, len);
  }
}

PRE_SYSCALL(sched_yield)() {}

POST_SYSCALL(sched_yield)(long res) {}

PRE_SYSCALL(sched_get_priority_max)(long policy) {}

POST_SYSCALL(sched_get_priority_max)(long res, long policy) {}

PRE_SYSCALL(sched_get_priority_min)(long policy) {}

POST_SYSCALL(sched_get_priority_min)(long res, long policy) {}

PRE_SYSCALL(sched_rr_get_interval)(long pid, void *interval) {}

POST_SYSCALL(sched_rr_get_interval)(long res, long pid, void *interval) {
  if (res >= 0) {
    if (interval)
      POST_WRITE(interval, struct_timespec_sz);
  }
}

PRE_SYSCALL(setpriority)(long which, long who, long niceval) {}

POST_SYSCALL(setpriority)(long res, long which, long who, long niceval) {}

PRE_SYSCALL(getpriority)(long which, long who) {}

POST_SYSCALL(getpriority)(long res, long which, long who) {}

PRE_SYSCALL(shutdown)(long arg0, long arg1) {}

POST_SYSCALL(shutdown)(long res, long arg0, long arg1) {}

PRE_SYSCALL(reboot)(long magic1, long magic2, long cmd, void *arg) {}

POST_SYSCALL(reboot)(long res, long magic1, long magic2, long cmd, void *arg) {}

PRE_SYSCALL(restart_syscall)() {}

POST_SYSCALL(restart_syscall)(long res) {}

PRE_SYSCALL(kexec_load)
(long entry, long nr_segments, void *segments, long flags) {}

POST_SYSCALL(kexec_load)
(long res, long entry, long nr_segments, void *segments, long flags) {
  if (res >= 0) {
    if (segments)
      POST_WRITE(segments, struct_kexec_segment_sz);
  }
}

PRE_SYSCALL(exit)(long error_code) {}

POST_SYSCALL(exit)(long res, long error_code) {}

PRE_SYSCALL(exit_group)(long error_code) {}

POST_SYSCALL(exit_group)(long res, long error_code) {}

PRE_SYSCALL(wait4)(long pid, void *stat_addr, long options, void *ru) {}

POST_SYSCALL(wait4)
(long res, long pid, void *stat_addr, long options, void *ru) {
  if (res >= 0) {
    if (stat_addr)
      POST_WRITE(stat_addr, sizeof(int));
    if (ru)
      POST_WRITE(ru, struct_rusage_sz);
  }
}

PRE_SYSCALL(waitid)
(long which, long pid, void *infop, long options, void *ru) {}

POST_SYSCALL(waitid)
(long res, long which, long pid, void *infop, long options, void *ru) {
  if (res >= 0) {
    if (infop)
      POST_WRITE(infop, siginfo_t_sz);
    if (ru)
      POST_WRITE(ru, struct_rusage_sz);
  }
}

PRE_SYSCALL(waitpid)(long pid, void *stat_addr, long options) {}

POST_SYSCALL(waitpid)(long res, long pid, void *stat_addr, long options) {
  if (res >= 0) {
    if (stat_addr)
      POST_WRITE(stat_addr, sizeof(int));
  }
}

PRE_SYSCALL(set_tid_address)(void *tidptr) {}

POST_SYSCALL(set_tid_address)(long res, void *tidptr) {
  if (res >= 0) {
    if (tidptr)
      POST_WRITE(tidptr, sizeof(int));
  }
}

PRE_SYSCALL(init_module)(void *umod, long len, const void *uargs) {
  if (uargs)
    PRE_READ(uargs, __sanitizer::internal_strlen((const char *)uargs) + 1);
}

POST_SYSCALL(init_module)(long res, void *umod, long len, const void *uargs) {}

PRE_SYSCALL(delete_module)(const void *name_user, long flags) {
  if (name_user)
    PRE_READ(name_user,
             __sanitizer::internal_strlen((const char *)name_user) + 1);
}

POST_SYSCALL(delete_module)(long res, const void *name_user, long flags) {}

PRE_SYSCALL(rt_sigprocmask)(long how, void *set, void *oset, long sigsetsize) {}

POST_SYSCALL(rt_sigprocmask)
(long res, long how, kernel_sigset_t *set, kernel_sigset_t *oset,
 long sigsetsize) {
  if (res >= 0) {
    if (set)
      POST_WRITE(set, sigsetsize);
    if (oset)
      POST_WRITE(oset, sigsetsize);
  }
}

PRE_SYSCALL(rt_sigpending)(void *set, long sigsetsize) {}

POST_SYSCALL(rt_sigpending)(long res, kernel_sigset_t *set, long sigsetsize) {
  if (res >= 0) {
    if (set)
      POST_WRITE(set, sigsetsize);
  }
}

PRE_SYSCALL(rt_sigtimedwait)
(const kernel_sigset_t *uthese, void *uinfo, const void *uts, long sigsetsize) {
  if (uthese)
    PRE_READ(uthese, sigsetsize);
  if (uts)
    PRE_READ(uts, struct_timespec_sz);
}

POST_SYSCALL(rt_sigtimedwait)
(long res, const void *uthese, void *uinfo, const void *uts, long sigsetsize) {
  if (res >= 0) {
    if (uinfo)
      POST_WRITE(uinfo, siginfo_t_sz);
  }
}

PRE_SYSCALL(rt_tgsigqueueinfo)(long tgid, long pid, long sig, void *uinfo) {}

POST_SYSCALL(rt_tgsigqueueinfo)
(long res, long tgid, long pid, long sig, void *uinfo) {
  if (res >= 0) {
    if (uinfo)
      POST_WRITE(uinfo, siginfo_t_sz);
  }
}

PRE_SYSCALL(kill)(long pid, long sig) {}

POST_SYSCALL(kill)(long res, long pid, long sig) {}

PRE_SYSCALL(tgkill)(long tgid, long pid, long sig) {}

POST_SYSCALL(tgkill)(long res, long tgid, long pid, long sig) {}

PRE_SYSCALL(tkill)(long pid, long sig) {}

POST_SYSCALL(tkill)(long res, long pid, long sig) {}

PRE_SYSCALL(rt_sigqueueinfo)(long pid, long sig, void *uinfo) {}

POST_SYSCALL(rt_sigqueueinfo)(long res, long pid, long sig, void *uinfo) {
  if (res >= 0) {
    if (uinfo)
      POST_WRITE(uinfo, siginfo_t_sz);
  }
}

PRE_SYSCALL(sgetmask)() {}

POST_SYSCALL(sgetmask)(long res) {}

PRE_SYSCALL(ssetmask)(long newmask) {}

POST_SYSCALL(ssetmask)(long res, long newmask) {}

PRE_SYSCALL(signal)(long sig, long handler) {}

POST_SYSCALL(signal)(long res, long sig, long handler) {}

PRE_SYSCALL(pause)() {}

POST_SYSCALL(pause)(long res) {}

PRE_SYSCALL(sync)() {}

POST_SYSCALL(sync)(long res) {}

PRE_SYSCALL(fsync)(long fd) {}

POST_SYSCALL(fsync)(long res, long fd) {}

PRE_SYSCALL(fdatasync)(long fd) {}

POST_SYSCALL(fdatasync)(long res, long fd) {}

PRE_SYSCALL(bdflush)(long func, long data) {}

POST_SYSCALL(bdflush)(long res, long func, long data) {}

PRE_SYSCALL(mount)
(void *dev_name, void *dir_name, void *type, long flags, void *data) {}

POST_SYSCALL(mount)
(long res, void *dev_name, void *dir_name, void *type, long flags, void *data) {
  if (res >= 0) {
    if (dev_name)
      POST_WRITE(dev_name,
                 __sanitizer::internal_strlen((const char *)dev_name) + 1);
    if (dir_name)
      POST_WRITE(dir_name,
                 __sanitizer::internal_strlen((const char *)dir_name) + 1);
    if (type)
      POST_WRITE(type, __sanitizer::internal_strlen((const char *)type) + 1);
  }
}

PRE_SYSCALL(umount)(void *name, long flags) {}

POST_SYSCALL(umount)(long res, void *name, long flags) {
  if (res >= 0) {
    if (name)
      POST_WRITE(name, __sanitizer::internal_strlen((const char *)name) + 1);
  }
}

PRE_SYSCALL(oldumount)(void *name) {}

POST_SYSCALL(oldumount)(long res, void *name) {
  if (res >= 0) {
    if (name)
      POST_WRITE(name, __sanitizer::internal_strlen((const char *)name) + 1);
  }
}

PRE_SYSCALL(truncate)(const void *path, long length) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(truncate)(long res, const void *path, long length) {}

PRE_SYSCALL(ftruncate)(long fd, long length) {}

POST_SYSCALL(ftruncate)(long res, long fd, long length) {}

PRE_SYSCALL(stat)(const void *filename, void *statbuf) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(stat)(long res, const void *filename, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct___old_kernel_stat_sz);
  }
}

#  if !SANITIZER_ANDROID
PRE_SYSCALL(statfs)(const void *path, void *buf) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(statfs)(long res, const void *path, void *buf) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, struct_statfs_sz);
  }
}

PRE_SYSCALL(fstatfs)(long fd, void *buf) {}

POST_SYSCALL(fstatfs)(long res, long fd, void *buf) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, struct_statfs_sz);
  }
}
#  endif  // !SANITIZER_ANDROID

#  if SANITIZER_GLIBC
PRE_SYSCALL(statfs64)(const void *path, long sz, void *buf) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(statfs64)(long res, const void *path, long sz, void *buf) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, struct_statfs64_sz);
  }
}

PRE_SYSCALL(fstatfs64)(long fd, long sz, void *buf) {}

POST_SYSCALL(fstatfs64)(long res, long fd, long sz, void *buf) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, struct_statfs64_sz);
  }
}
#  endif  // SANITIZER_GLIBC

PRE_SYSCALL(lstat)(const void *filename, void *statbuf) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(lstat)(long res, const void *filename, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct___old_kernel_stat_sz);
  }
}

PRE_SYSCALL(fstat)(long fd, void *statbuf) {}

POST_SYSCALL(fstat)(long res, long fd, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct___old_kernel_stat_sz);
  }
}

PRE_SYSCALL(newstat)(const void *filename, void *statbuf) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(newstat)(long res, const void *filename, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat_sz);
  }
}

PRE_SYSCALL(newlstat)(const void *filename, void *statbuf) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(newlstat)(long res, const void *filename, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat_sz);
  }
}

PRE_SYSCALL(newfstat)(long fd, void *statbuf) {}

POST_SYSCALL(newfstat)(long res, long fd, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat_sz);
  }
}

#  if SANITIZER_GLIBC
PRE_SYSCALL(ustat)(long dev, void *ubuf) {}

POST_SYSCALL(ustat)(long res, long dev, void *ubuf) {
  if (res >= 0) {
    if (ubuf)
      POST_WRITE(ubuf, struct_ustat_sz);
  }
}
#  endif  // SANITIZER_GLIBC

PRE_SYSCALL(stat64)(const void *filename, void *statbuf) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(stat64)(long res, const void *filename, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat64_sz);
  }
}

PRE_SYSCALL(fstat64)(long fd, void *statbuf) {}

POST_SYSCALL(fstat64)(long res, long fd, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat64_sz);
  }
}

PRE_SYSCALL(lstat64)(const void *filename, void *statbuf) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(lstat64)(long res, const void *filename, void *statbuf) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat64_sz);
  }
}

PRE_SYSCALL(setxattr)
(const void *path, const void *name, const void *value, long size, long flags) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
  if (value)
    PRE_READ(value, size);
}

POST_SYSCALL(setxattr)
(long res, const void *path, const void *name, const void *value, long size,
 long flags) {}

PRE_SYSCALL(lsetxattr)
(const void *path, const void *name, const void *value, long size, long flags) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
  if (value)
    PRE_READ(value, size);
}

POST_SYSCALL(lsetxattr)
(long res, const void *path, const void *name, const void *value, long size,
 long flags) {}

PRE_SYSCALL(fsetxattr)
(long fd, const void *name, const void *value, long size, long flags) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
  if (value)
    PRE_READ(value, size);
}

POST_SYSCALL(fsetxattr)
(long res, long fd, const void *name, const void *value, long size,
 long flags) {}

PRE_SYSCALL(getxattr)
(const void *path, const void *name, void *value, long size) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(getxattr)
(long res, const void *path, const void *name, void *value, long size) {
  if (size && res > 0) {
    if (value)
      POST_WRITE(value, res);
  }
}

PRE_SYSCALL(lgetxattr)
(const void *path, const void *name, void *value, long size) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(lgetxattr)
(long res, const void *path, const void *name, void *value, long size) {
  if (size && res > 0) {
    if (value)
      POST_WRITE(value, res);
  }
}

PRE_SYSCALL(fgetxattr)(long fd, const void *name, void *value, long size) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(fgetxattr)
(long res, long fd, const void *name, void *value, long size) {
  if (size && res > 0) {
    if (value)
      POST_WRITE(value, res);
  }
}

PRE_SYSCALL(listxattr)(const void *path, void *list, long size) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(listxattr)(long res, const void *path, void *list, long size) {
  if (size && res > 0) {
    if (list)
      POST_WRITE(list, res);
  }
}

PRE_SYSCALL(llistxattr)(const void *path, void *list, long size) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(llistxattr)(long res, const void *path, void *list, long size) {
  if (size && res > 0) {
    if (list)
      POST_WRITE(list, res);
  }
}

PRE_SYSCALL(flistxattr)(long fd, void *list, long size) {}

POST_SYSCALL(flistxattr)(long res, long fd, void *list, long size) {
  if (size && res > 0) {
    if (list)
      POST_WRITE(list, res);
  }
}

PRE_SYSCALL(removexattr)(const void *path, const void *name) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(removexattr)(long res, const void *path, const void *name) {}

PRE_SYSCALL(lremovexattr)(const void *path, const void *name) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(lremovexattr)(long res, const void *path, const void *name) {}

PRE_SYSCALL(fremovexattr)(long fd, const void *name) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(fremovexattr)(long res, long fd, const void *name) {}

PRE_SYSCALL(brk)(long brk) {}

POST_SYSCALL(brk)(long res, long brk) {}

PRE_SYSCALL(mprotect)(long start, long len, long prot) {}

POST_SYSCALL(mprotect)(long res, long start, long len, long prot) {}

PRE_SYSCALL(mremap)
(long addr, long old_len, long new_len, long flags, long new_addr) {}

POST_SYSCALL(mremap)
(long res, long addr, long old_len, long new_len, long flags, long new_addr) {}

PRE_SYSCALL(remap_file_pages)
(long start, long size, long prot, long pgoff, long flags) {}

POST_SYSCALL(remap_file_pages)
(long res, long start, long size, long prot, long pgoff, long flags) {}

PRE_SYSCALL(msync)(long start, long len, long flags) {}

POST_SYSCALL(msync)(long res, long start, long len, long flags) {}

PRE_SYSCALL(munmap)(long addr, long len) {}

POST_SYSCALL(munmap)(long res, long addr, long len) {}

PRE_SYSCALL(mlock)(long start, long len) {}

POST_SYSCALL(mlock)(long res, long start, long len) {}

PRE_SYSCALL(munlock)(long start, long len) {}

POST_SYSCALL(munlock)(long res, long start, long len) {}

PRE_SYSCALL(mlockall)(long flags) {}

POST_SYSCALL(mlockall)(long res, long flags) {}

PRE_SYSCALL(munlockall)() {}

POST_SYSCALL(munlockall)(long res) {}

PRE_SYSCALL(madvise)(long start, long len, long behavior) {}

POST_SYSCALL(madvise)(long res, long start, long len, long behavior) {}

PRE_SYSCALL(mincore)(long start, long len, void *vec) {}

POST_SYSCALL(mincore)(long res, long start, long len, void *vec) {
  if (res >= 0) {
    if (vec) {
      POST_WRITE(vec, (len + GetPageSizeCached() - 1) / GetPageSizeCached());
    }
  }
}

PRE_SYSCALL(pivot_root)(const void *new_root, const void *put_old) {
  if (new_root)
    PRE_READ(new_root,
             __sanitizer::internal_strlen((const char *)new_root) + 1);
  if (put_old)
    PRE_READ(put_old, __sanitizer::internal_strlen((const char *)put_old) + 1);
}

POST_SYSCALL(pivot_root)(long res, const void *new_root, const void *put_old) {}

PRE_SYSCALL(chroot)(const void *filename) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(chroot)(long res, const void *filename) {}

PRE_SYSCALL(mknod)(const void *filename, long mode, long dev) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(mknod)(long res, const void *filename, long mode, long dev) {}

PRE_SYSCALL(link)(const void *oldname, const void *newname) {
  if (oldname)
    PRE_READ(oldname, __sanitizer::internal_strlen((const char *)oldname) + 1);
  if (newname)
    PRE_READ(newname, __sanitizer::internal_strlen((const char *)newname) + 1);
}

POST_SYSCALL(link)(long res, const void *oldname, const void *newname) {}

PRE_SYSCALL(symlink)(const void *old, const void *new_) {
  if (old)
    PRE_READ(old, __sanitizer::internal_strlen((const char *)old) + 1);
  if (new_)
    PRE_READ(new_, __sanitizer::internal_strlen((const char *)new_) + 1);
}

POST_SYSCALL(symlink)(long res, const void *old, const void *new_) {}

PRE_SYSCALL(unlink)(const void *pathname) {
  if (pathname)
    PRE_READ(pathname,
             __sanitizer::internal_strlen((const char *)pathname) + 1);
}

POST_SYSCALL(unlink)(long res, const void *pathname) {}

PRE_SYSCALL(rename)(const void *oldname, const void *newname) {
  if (oldname)
    PRE_READ(oldname, __sanitizer::internal_strlen((const char *)oldname) + 1);
  if (newname)
    PRE_READ(newname, __sanitizer::internal_strlen((const char *)newname) + 1);
}

POST_SYSCALL(rename)(long res, const void *oldname, const void *newname) {}

PRE_SYSCALL(chmod)(const void *filename, long mode) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(chmod)(long res, const void *filename, long mode) {}

PRE_SYSCALL(fchmod)(long fd, long mode) {}

POST_SYSCALL(fchmod)(long res, long fd, long mode) {}

PRE_SYSCALL(fcntl)(long fd, long cmd, long arg) {}

POST_SYSCALL(fcntl)(long res, long fd, long cmd, long arg) {}

PRE_SYSCALL(fcntl64)(long fd, long cmd, long arg) {}

POST_SYSCALL(fcntl64)(long res, long fd, long cmd, long arg) {}

PRE_SYSCALL(pipe)(void *fildes) {}

POST_SYSCALL(pipe)(long res, void *fildes) {
  if (res >= 0)
    if (fildes)
      POST_WRITE(fildes, sizeof(int) * 2);
}

PRE_SYSCALL(pipe2)(void *fildes, long flags) {}

POST_SYSCALL(pipe2)(long res, void *fildes, long flags) {
  if (res >= 0)
    if (fildes)
      POST_WRITE(fildes, sizeof(int) * 2);
}

PRE_SYSCALL(dup)(long fildes) {}

POST_SYSCALL(dup)(long res, long fildes) {}

PRE_SYSCALL(dup2)(long oldfd, long newfd) {}

POST_SYSCALL(dup2)(long res, long oldfd, long newfd) {}

PRE_SYSCALL(dup3)(long oldfd, long newfd, long flags) {}

POST_SYSCALL(dup3)(long res, long oldfd, long newfd, long flags) {}

PRE_SYSCALL(ioperm)(long from, long num, long on) {}

POST_SYSCALL(ioperm)(long res, long from, long num, long on) {}

PRE_SYSCALL(ioctl)(long fd, long cmd, long arg) {}

POST_SYSCALL(ioctl)(long res, long fd, long cmd, long arg) {}

PRE_SYSCALL(flock)(long fd, long cmd) {}

POST_SYSCALL(flock)(long res, long fd, long cmd) {}

PRE_SYSCALL(io_setup)(long nr_reqs, void **ctx) {
  if (ctx)
    PRE_WRITE(ctx, sizeof(*ctx));
}

POST_SYSCALL(io_setup)(long res, long nr_reqs, void **ctx) {
  if (res >= 0 && ctx) {
    POST_WRITE(ctx, sizeof(*ctx));
    // (*ctx) is actually a pointer to a kernel mapped page, and there are
    // people out there who are crazy enough to peek into that page's 32-byte
    // header.
    if (*ctx)
      POST_WRITE(*ctx, 32);
  }
}

PRE_SYSCALL(io_destroy)(long ctx) {}

POST_SYSCALL(io_destroy)(long res, long ctx) {}

PRE_SYSCALL(io_getevents)
(long ctx_id, long min_nr, long nr, __sanitizer_io_event *ioevpp,
 void *timeout) {
  if (timeout)
    PRE_READ(timeout, struct_timespec_sz);
}

POST_SYSCALL(io_getevents)
(long res, long ctx_id, long min_nr, long nr, __sanitizer_io_event *ioevpp,
 void *timeout) {
  if (res >= 0) {
    if (ioevpp)
      POST_WRITE(ioevpp, res * sizeof(*ioevpp));
    if (timeout)
      POST_WRITE(timeout, struct_timespec_sz);
  }
  for (long i = 0; i < res; i++) {
    // We synchronize io_submit -> io_getevents/io_cancel using the
    // user-provided data context. Data is not necessary a pointer, it can be
    // an int, 0 or whatever; acquire/release will correctly handle this.
    // This scheme can lead to false negatives, e.g. when all operations
    // synchronize on 0. But there does not seem to be a better solution
    // (except wrapping all operations in own context, which is unreliable).
    // We can not reliably extract fildes in io_getevents.
    COMMON_SYSCALL_ACQUIRE((void *)ioevpp[i].data);
  }
}

PRE_SYSCALL(io_submit)(long ctx_id, long nr, __sanitizer_iocb **iocbpp) {
  for (long i = 0; i < nr; ++i) {
    uptr op = iocbpp[i]->aio_lio_opcode;
    void *data = (void *)iocbpp[i]->aio_data;
    void *buf = (void *)iocbpp[i]->aio_buf;
    uptr len = (uptr)iocbpp[i]->aio_nbytes;
    if (op == iocb_cmd_pwrite && buf && len) {
      PRE_READ(buf, len);
    } else if (op == iocb_cmd_pread && buf && len) {
      POST_WRITE(buf, len);
    } else if (op == iocb_cmd_pwritev) {
      __sanitizer_iovec *iovec = (__sanitizer_iovec *)buf;
      for (uptr v = 0; v < len; v++)
        PRE_READ(iovec[v].iov_base, iovec[v].iov_len);
    } else if (op == iocb_cmd_preadv) {
      __sanitizer_iovec *iovec = (__sanitizer_iovec *)buf;
      for (uptr v = 0; v < len; v++)
        POST_WRITE(iovec[v].iov_base, iovec[v].iov_len);
    }
    // See comment in io_getevents.
    COMMON_SYSCALL_RELEASE(data);
  }
}

POST_SYSCALL(io_submit)
(long res, long ctx_id, long nr, __sanitizer_iocb **iocbpp) {}

PRE_SYSCALL(io_cancel)
(long ctx_id, __sanitizer_iocb *iocb, __sanitizer_io_event *result) {}

POST_SYSCALL(io_cancel)
(long res, long ctx_id, __sanitizer_iocb *iocb, __sanitizer_io_event *result) {
  if (res == 0) {
    if (result) {
      // See comment in io_getevents.
      COMMON_SYSCALL_ACQUIRE((void *)result->data);
      POST_WRITE(result, sizeof(*result));
    }
    if (iocb)
      POST_WRITE(iocb, sizeof(*iocb));
  }
}

PRE_SYSCALL(sendfile)(long out_fd, long in_fd, void *offset, long count) {}

POST_SYSCALL(sendfile)
(long res, long out_fd, long in_fd, __sanitizer___kernel_off_t *offset,
 long count) {
  if (res >= 0) {
    if (offset)
      POST_WRITE(offset, sizeof(*offset));
  }
}

PRE_SYSCALL(sendfile64)(long out_fd, long in_fd, void *offset, long count) {}

POST_SYSCALL(sendfile64)
(long res, long out_fd, long in_fd, __sanitizer___kernel_loff_t *offset,
 long count) {
  if (res >= 0) {
    if (offset)
      POST_WRITE(offset, sizeof(*offset));
  }
}

PRE_SYSCALL(readlink)(const void *path, void *buf, long bufsiz) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(readlink)(long res, const void *path, void *buf, long bufsiz) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, __sanitizer::internal_strlen((const char *)buf) + 1);
  }
}

PRE_SYSCALL(creat)(const void *pathname, long mode) {
  if (pathname)
    PRE_READ(pathname,
             __sanitizer::internal_strlen((const char *)pathname) + 1);
}

POST_SYSCALL(creat)(long res, const void *pathname, long mode) {}

PRE_SYSCALL(open)(const void *filename, long flags, long mode) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(open)(long res, const void *filename, long flags, long mode) {}

PRE_SYSCALL(close)(long fd) { COMMON_SYSCALL_FD_CLOSE((int)fd); }

POST_SYSCALL(close)(long res, long fd) {}

PRE_SYSCALL(access)(const void *filename, long mode) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(access)(long res, const void *filename, long mode) {}

PRE_SYSCALL(vhangup)() {}

POST_SYSCALL(vhangup)(long res) {}

PRE_SYSCALL(chown)(const void *filename, long user, long group) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(chown)(long res, const void *filename, long user, long group) {}

PRE_SYSCALL(lchown)(const void *filename, long user, long group) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(lchown)(long res, const void *filename, long user, long group) {}

PRE_SYSCALL(fchown)(long fd, long user, long group) {}

POST_SYSCALL(fchown)(long res, long fd, long user, long group) {}

#  if SANITIZER_USES_UID16_SYSCALLS
PRE_SYSCALL(chown16)(const void *filename, long user, long group) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(chown16)(long res, const void *filename, long user, long group) {}

PRE_SYSCALL(lchown16)(const void *filename, long user, long group) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(lchown16)(long res, const void *filename, long user, long group) {}

PRE_SYSCALL(fchown16)(long fd, long user, long group) {}

POST_SYSCALL(fchown16)(long res, long fd, long user, long group) {}

PRE_SYSCALL(setregid16)(long rgid, long egid) {}

POST_SYSCALL(setregid16)(long res, long rgid, long egid) {}

PRE_SYSCALL(setgid16)(long gid) {}

POST_SYSCALL(setgid16)(long res, long gid) {}

PRE_SYSCALL(setreuid16)(long ruid, long euid) {}

POST_SYSCALL(setreuid16)(long res, long ruid, long euid) {}

PRE_SYSCALL(setuid16)(long uid) {}

POST_SYSCALL(setuid16)(long res, long uid) {}

PRE_SYSCALL(setresuid16)(long ruid, long euid, long suid) {}

POST_SYSCALL(setresuid16)(long res, long ruid, long euid, long suid) {}

PRE_SYSCALL(getresuid16)(void *ruid, void *euid, void *suid) {}

POST_SYSCALL(getresuid16)
(long res, __sanitizer___kernel_old_uid_t *ruid,
 __sanitizer___kernel_old_uid_t *euid, __sanitizer___kernel_old_uid_t *suid) {
  if (res >= 0) {
    if (ruid)
      POST_WRITE(ruid, sizeof(*ruid));
    if (euid)
      POST_WRITE(euid, sizeof(*euid));
    if (suid)
      POST_WRITE(suid, sizeof(*suid));
  }
}

PRE_SYSCALL(setresgid16)(long rgid, long egid, long sgid) {}

POST_SYSCALL(setresgid16)(long res, long rgid, long egid, long sgid) {}

PRE_SYSCALL(getresgid16)(void *rgid, void *egid, void *sgid) {}

POST_SYSCALL(getresgid16)
(long res, __sanitizer___kernel_old_gid_t *rgid,
 __sanitizer___kernel_old_gid_t *egid, __sanitizer___kernel_old_gid_t *sgid) {
  if (res >= 0) {
    if (rgid)
      POST_WRITE(rgid, sizeof(*rgid));
    if (egid)
      POST_WRITE(egid, sizeof(*egid));
    if (sgid)
      POST_WRITE(sgid, sizeof(*sgid));
  }
}

PRE_SYSCALL(setfsuid16)(long uid) {}

POST_SYSCALL(setfsuid16)(long res, long uid) {}

PRE_SYSCALL(setfsgid16)(long gid) {}

POST_SYSCALL(setfsgid16)(long res, long gid) {}

PRE_SYSCALL(getgroups16)
(long gidsetsize, __sanitizer___kernel_old_gid_t *grouplist) {}

POST_SYSCALL(getgroups16)
(long res, long gidsetsize, __sanitizer___kernel_old_gid_t *grouplist) {
  if (res >= 0) {
    if (grouplist)
      POST_WRITE(grouplist, res * sizeof(*grouplist));
  }
}

PRE_SYSCALL(setgroups16)
(long gidsetsize, __sanitizer___kernel_old_gid_t *grouplist) {
  if (grouplist)
    POST_WRITE(grouplist, gidsetsize * sizeof(*grouplist));
}

POST_SYSCALL(setgroups16)
(long res, long gidsetsize, __sanitizer___kernel_old_gid_t *grouplist) {}

PRE_SYSCALL(getuid16)() {}

POST_SYSCALL(getuid16)(long res) {}

PRE_SYSCALL(geteuid16)() {}

POST_SYSCALL(geteuid16)(long res) {}

PRE_SYSCALL(getgid16)() {}

POST_SYSCALL(getgid16)(long res) {}

PRE_SYSCALL(getegid16)() {}

POST_SYSCALL(getegid16)(long res) {}
#  endif  // SANITIZER_USES_UID16_SYSCALLS

PRE_SYSCALL(utime)(void *filename, void *times) {}

POST_SYSCALL(utime)(long res, void *filename, void *times) {
  if (res >= 0) {
    if (filename)
      POST_WRITE(filename,
                 __sanitizer::internal_strlen((const char *)filename) + 1);
    if (times)
      POST_WRITE(times, struct_utimbuf_sz);
  }
}

PRE_SYSCALL(utimes)(void *filename, void *utimes) {}

POST_SYSCALL(utimes)(long res, void *filename, void *utimes) {
  if (res >= 0) {
    if (filename)
      POST_WRITE(filename,
                 __sanitizer::internal_strlen((const char *)filename) + 1);
    if (utimes)
      POST_WRITE(utimes, timeval_sz);
  }
}

PRE_SYSCALL(lseek)(long fd, long offset, long origin) {}

POST_SYSCALL(lseek)(long res, long fd, long offset, long origin) {}

PRE_SYSCALL(llseek)
(long fd, long offset_high, long offset_low, void *result, long origin) {}

POST_SYSCALL(llseek)
(long res, long fd, long offset_high, long offset_low, void *result,
 long origin) {
  if (res >= 0) {
    if (result)
      POST_WRITE(result, sizeof(long long));
  }
}

PRE_SYSCALL(readv)(long fd, const __sanitizer_iovec *vec, long vlen) {}

POST_SYSCALL(readv)
(long res, long fd, const __sanitizer_iovec *vec, long vlen) {
  if (res >= 0) {
    if (vec)
      kernel_write_iovec(vec, vlen, res);
  }
}

PRE_SYSCALL(write)(long fd, const void *buf, long count) {
  if (buf)
    PRE_READ(buf, count);
}

POST_SYSCALL(write)(long res, long fd, const void *buf, long count) {}

PRE_SYSCALL(writev)(long fd, const __sanitizer_iovec *vec, long vlen) {}

POST_SYSCALL(writev)
(long res, long fd, const __sanitizer_iovec *vec, long vlen) {
  if (res >= 0) {
    if (vec)
      kernel_read_iovec(vec, vlen, res);
  }
}

#  ifdef _LP64
PRE_SYSCALL(pread64)(long fd, void *buf, long count, long pos) {}

POST_SYSCALL(pread64)(long res, long fd, void *buf, long count, long pos) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, res);
  }
}

PRE_SYSCALL(pwrite64)(long fd, const void *buf, long count, long pos) {
  if (buf)
    PRE_READ(buf, count);
}

POST_SYSCALL(pwrite64)
(long res, long fd, const void *buf, long count, long pos) {}
#  else
PRE_SYSCALL(pread64)(long fd, void *buf, long count, long pos0, long pos1) {}

POST_SYSCALL(pread64)
(long res, long fd, void *buf, long count, long pos0, long pos1) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, res);
  }
}

PRE_SYSCALL(pwrite64)
(long fd, const void *buf, long count, long pos0, long pos1) {
  if (buf)
    PRE_READ(buf, count);
}

POST_SYSCALL(pwrite64)
(long res, long fd, const void *buf, long count, long pos0, long pos1) {}
#  endif

PRE_SYSCALL(preadv)
(long fd, const __sanitizer_iovec *vec, long vlen, long pos_l, long pos_h) {}

POST_SYSCALL(preadv)
(long res, long fd, const __sanitizer_iovec *vec, long vlen, long pos_l,
 long pos_h) {
  if (res >= 0) {
    if (vec)
      kernel_write_iovec(vec, vlen, res);
  }
}

PRE_SYSCALL(pwritev)
(long fd, const __sanitizer_iovec *vec, long vlen, long pos_l, long pos_h) {}

POST_SYSCALL(pwritev)
(long res, long fd, const __sanitizer_iovec *vec, long vlen, long pos_l,
 long pos_h) {
  if (res >= 0) {
    if (vec)
      kernel_read_iovec(vec, vlen, res);
  }
}

PRE_SYSCALL(getcwd)(void *buf, long size) {}

POST_SYSCALL(getcwd)(long res, void *buf, long size) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, __sanitizer::internal_strlen((const char *)buf) + 1);
  }
}

PRE_SYSCALL(mkdir)(const void *pathname, long mode) {
  if (pathname)
    PRE_READ(pathname,
             __sanitizer::internal_strlen((const char *)pathname) + 1);
}

POST_SYSCALL(mkdir)(long res, const void *pathname, long mode) {}

PRE_SYSCALL(chdir)(const void *filename) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(chdir)(long res, const void *filename) {}

PRE_SYSCALL(fchdir)(long fd) {}

POST_SYSCALL(fchdir)(long res, long fd) {}

PRE_SYSCALL(rmdir)(const void *pathname) {
  if (pathname)
    PRE_READ(pathname,
             __sanitizer::internal_strlen((const char *)pathname) + 1);
}

POST_SYSCALL(rmdir)(long res, const void *pathname) {}

PRE_SYSCALL(lookup_dcookie)(u64 cookie64, void *buf, long len) {}

POST_SYSCALL(lookup_dcookie)(long res, u64 cookie64, void *buf, long len) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, __sanitizer::internal_strlen((const char *)buf) + 1);
  }
}

PRE_SYSCALL(quotactl)(long cmd, const void *special, long id, void *addr) {
  if (special)
    PRE_READ(special, __sanitizer::internal_strlen((const char *)special) + 1);
}

POST_SYSCALL(quotactl)
(long res, long cmd, const void *special, long id, void *addr) {}

PRE_SYSCALL(getdents)(long fd, void *dirent, long count) {}

POST_SYSCALL(getdents)(long res, long fd, void *dirent, long count) {
  if (res >= 0) {
    if (dirent)
      POST_WRITE(dirent, res);
  }
}

PRE_SYSCALL(getdents64)(long fd, void *dirent, long count) {}

POST_SYSCALL(getdents64)(long res, long fd, void *dirent, long count) {
  if (res >= 0) {
    if (dirent)
      POST_WRITE(dirent, res);
  }
}

PRE_SYSCALL(setsockopt)
(long fd, long level, long optname, void *optval, long optlen) {}

POST_SYSCALL(setsockopt)
(long res, long fd, long level, long optname, void *optval, long optlen) {
  if (res >= 0) {
    if (optval)
      POST_WRITE(optval,
                 __sanitizer::internal_strlen((const char *)optval) + 1);
  }
}

PRE_SYSCALL(getsockopt)
(long fd, long level, long optname, void *optval, void *optlen) {}

POST_SYSCALL(getsockopt)
(long res, long fd, long level, long optname, void *optval, void *optlen) {
  if (res >= 0) {
    if (optval)
      POST_WRITE(optval,
                 __sanitizer::internal_strlen((const char *)optval) + 1);
    if (optlen)
      POST_WRITE(optlen, sizeof(int));
  }
}

PRE_SYSCALL(bind)(long arg0, sanitizer_kernel_sockaddr *arg1, long arg2) {}

POST_SYSCALL(bind)
(long res, long arg0, sanitizer_kernel_sockaddr *arg1, long arg2) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
  }
}

PRE_SYSCALL(connect)(long arg0, sanitizer_kernel_sockaddr *arg1, long arg2) {}

POST_SYSCALL(connect)
(long res, long arg0, sanitizer_kernel_sockaddr *arg1, long arg2) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
  }
}

PRE_SYSCALL(accept)(long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2) {}

POST_SYSCALL(accept)
(long res, long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
    if (arg2)
      POST_WRITE(arg2, sizeof(unsigned));
  }
}

PRE_SYSCALL(accept4)
(long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2, long arg3) {}

POST_SYSCALL(accept4)
(long res, long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2, long arg3) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
    if (arg2)
      POST_WRITE(arg2, sizeof(unsigned));
  }
}

PRE_SYSCALL(getsockname)
(long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2) {}

POST_SYSCALL(getsockname)
(long res, long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
    if (arg2)
      POST_WRITE(arg2, sizeof(unsigned));
  }
}

PRE_SYSCALL(getpeername)
(long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2) {}

POST_SYSCALL(getpeername)
(long res, long arg0, sanitizer_kernel_sockaddr *arg1, void *arg2) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
    if (arg2)
      POST_WRITE(arg2, sizeof(unsigned));
  }
}

PRE_SYSCALL(send)(long arg0, void *arg1, long arg2, long arg3) {}

POST_SYSCALL(send)(long res, long arg0, void *arg1, long arg2, long arg3) {
  if (res) {
    if (arg1)
      POST_READ(arg1, res);
  }
}

PRE_SYSCALL(sendto)
(long arg0, void *arg1, long arg2, long arg3, sanitizer_kernel_sockaddr *arg4,
 long arg5) {}

POST_SYSCALL(sendto)
(long res, long arg0, void *arg1, long arg2, long arg3,
 sanitizer_kernel_sockaddr *arg4, long arg5) {
  if (res >= 0) {
    if (arg1)
      POST_READ(arg1, res);
    if (arg4)
      POST_WRITE(arg4, sizeof(*arg4));
  }
}

PRE_SYSCALL(sendmsg)(long fd, void *msg, long flags) {}

POST_SYSCALL(sendmsg)(long res, long fd, void *msg, long flags) {
  // FIXME: POST_READ
}

PRE_SYSCALL(sendmmsg)(long fd, void *msg, long vlen, long flags) {}

POST_SYSCALL(sendmmsg)(long res, long fd, void *msg, long vlen, long flags) {
  // FIXME: POST_READ
}

PRE_SYSCALL(recv)(long arg0, void *buf, long len, long flags) {}

POST_SYSCALL(recv)(long res, void *buf, long len, long flags) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, res);
  }
}

PRE_SYSCALL(recvfrom)
(long arg0, void *buf, long len, long flags, sanitizer_kernel_sockaddr *arg4,
 void *arg5) {}

POST_SYSCALL(recvfrom)
(long res, long arg0, void *buf, long len, long flags,
 sanitizer_kernel_sockaddr *arg4, void *arg5) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, res);
    if (arg4)
      POST_WRITE(arg4, sizeof(*arg4));
    if (arg5)
      POST_WRITE(arg5, sizeof(int));
  }
}

PRE_SYSCALL(socket)(long arg0, long arg1, long arg2) {}

POST_SYSCALL(socket)(long res, long arg0, long arg1, long arg2) {}

PRE_SYSCALL(socketpair)(long arg0, long arg1, long arg2, int *sv) {}

POST_SYSCALL(socketpair)(long res, long arg0, long arg1, long arg2, int *sv) {
  if (res >= 0)
    if (sv)
      POST_WRITE(sv, sizeof(int) * 2);
}

PRE_SYSCALL(socketcall)(long call, void *args) {}

POST_SYSCALL(socketcall)(long res, long call, void *args) {
  if (res >= 0) {
    if (args)
      POST_WRITE(args, sizeof(long));
  }
}

PRE_SYSCALL(listen)(long arg0, long arg1) {}

POST_SYSCALL(listen)(long res, long arg0, long arg1) {}

PRE_SYSCALL(poll)(void *ufds, long nfds, long timeout) {}

POST_SYSCALL(poll)
(long res, __sanitizer_pollfd *ufds, long nfds, long timeout) {
  if (res >= 0) {
    if (ufds)
      POST_WRITE(ufds, nfds * sizeof(*ufds));
  }
}

PRE_SYSCALL(select)
(long n, __sanitizer___kernel_fd_set *inp, __sanitizer___kernel_fd_set *outp,
 __sanitizer___kernel_fd_set *exp, void *tvp) {}

POST_SYSCALL(select)
(long res, long n, __sanitizer___kernel_fd_set *inp,
 __sanitizer___kernel_fd_set *outp, __sanitizer___kernel_fd_set *exp,
 void *tvp) {
  if (res >= 0) {
    if (inp)
      POST_WRITE(inp, sizeof(*inp));
    if (outp)
      POST_WRITE(outp, sizeof(*outp));
    if (exp)
      POST_WRITE(exp, sizeof(*exp));
    if (tvp)
      POST_WRITE(tvp, timeval_sz);
  }
}

PRE_SYSCALL(old_select)(void *arg) {}

POST_SYSCALL(old_select)(long res, void *arg) {}

PRE_SYSCALL(epoll_create)(long size) {}

POST_SYSCALL(epoll_create)(long res, long size) {}

PRE_SYSCALL(epoll_create1)(long flags) {}

POST_SYSCALL(epoll_create1)(long res, long flags) {}

PRE_SYSCALL(epoll_ctl)(long epfd, long op, long fd, void *event) {}

POST_SYSCALL(epoll_ctl)(long res, long epfd, long op, long fd, void *event) {
  if (res >= 0) {
    if (event)
      POST_WRITE(event, struct_epoll_event_sz);
  }
}

PRE_SYSCALL(epoll_wait)
(long epfd, void *events, long maxevents, long timeout) {}

POST_SYSCALL(epoll_wait)
(long res, long epfd, void *events, long maxevents, long timeout) {
  if (res >= 0) {
    COMMON_SYSCALL_FD_ACQUIRE(epfd);
    if (events)
      POST_WRITE(events, res * struct_epoll_event_sz);
  }
}

PRE_SYSCALL(epoll_pwait)
(long epfd, void *events, long maxevents, long timeout,
 const kernel_sigset_t *sigmask, long sigsetsize) {
  if (sigmask)
    PRE_READ(sigmask, sigsetsize);
}

POST_SYSCALL(epoll_pwait)
(long res, long epfd, void *events, long maxevents, long timeout,
 const void *sigmask, long sigsetsize) {
  if (res >= 0) {
    COMMON_SYSCALL_FD_ACQUIRE(epfd);
    if (events)
      POST_WRITE(events, res * struct_epoll_event_sz);
  }
}

PRE_SYSCALL(epoll_pwait2)
(long epfd, void *events, long maxevents,
 const sanitizer_kernel_timespec *timeout, const kernel_sigset_t *sigmask,
 long sigsetsize) {
  if (timeout)
    PRE_READ(timeout, sizeof(*timeout));
  if (sigmask)
    PRE_READ(sigmask, sigsetsize);
}

POST_SYSCALL(epoll_pwait2)
(long res, long epfd, void *events, long maxevents,
 const sanitizer_kernel_timespec *timeout, const void *sigmask,
 long sigsetsize) {
  if (res >= 0) {
    COMMON_SYSCALL_FD_ACQUIRE(epfd);
    if (events)
      POST_WRITE(events, res * struct_epoll_event_sz);
  }
}

PRE_SYSCALL(gethostname)(void *name, long len) {}

POST_SYSCALL(gethostname)(long res, void *name, long len) {
  if (res >= 0) {
    if (name)
      POST_WRITE(name, __sanitizer::internal_strlen((const char *)name) + 1);
  }
}

PRE_SYSCALL(sethostname)(void *name, long len) {}

POST_SYSCALL(sethostname)(long res, void *name, long len) {
  if (res >= 0) {
    if (name)
      POST_WRITE(name, __sanitizer::internal_strlen((const char *)name) + 1);
  }
}

PRE_SYSCALL(setdomainname)(void *name, long len) {}

POST_SYSCALL(setdomainname)(long res, void *name, long len) {
  if (res >= 0) {
    if (name)
      POST_WRITE(name, __sanitizer::internal_strlen((const char *)name) + 1);
  }
}

PRE_SYSCALL(newuname)(void *name) {}

POST_SYSCALL(newuname)(long res, void *name) {
  if (res >= 0) {
    if (name)
      POST_WRITE(name, struct_new_utsname_sz);
  }
}

PRE_SYSCALL(uname)(void *arg0) {}

POST_SYSCALL(uname)(long res, void *arg0) {
  if (res >= 0) {
    if (arg0)
      POST_WRITE(arg0, struct_old_utsname_sz);
  }
}

PRE_SYSCALL(olduname)(void *arg0) {}

POST_SYSCALL(olduname)(long res, void *arg0) {
  if (res >= 0) {
    if (arg0)
      POST_WRITE(arg0, struct_oldold_utsname_sz);
  }
}

PRE_SYSCALL(getrlimit)(long resource, void *rlim) {}

POST_SYSCALL(getrlimit)(long res, long resource, void *rlim) {
  if (res >= 0) {
    if (rlim)
      POST_WRITE(rlim, struct_rlimit_sz);
  }
}

PRE_SYSCALL(old_getrlimit)(long resource, void *rlim) {}

POST_SYSCALL(old_getrlimit)(long res, long resource, void *rlim) {
  if (res >= 0) {
    if (rlim)
      POST_WRITE(rlim, struct_rlimit_sz);
  }
}

PRE_SYSCALL(setrlimit)(long resource, void *rlim) {}

POST_SYSCALL(setrlimit)(long res, long resource, void *rlim) {
  if (res >= 0) {
    if (rlim)
      POST_WRITE(rlim, struct_rlimit_sz);
  }
}

#  if SANITIZER_GLIBC
PRE_SYSCALL(prlimit64)
(long pid, long resource, const void *new_rlim, void *old_rlim) {
  if (new_rlim)
    PRE_READ(new_rlim, struct_rlimit64_sz);
}

POST_SYSCALL(prlimit64)
(long res, long pid, long resource, const void *new_rlim, void *old_rlim) {
  if (res >= 0) {
    if (old_rlim)
      POST_WRITE(old_rlim, struct_rlimit64_sz);
  }
}
#  endif

PRE_SYSCALL(getrusage)(long who, void *ru) {}

POST_SYSCALL(getrusage)(long res, long who, void *ru) {
  if (res >= 0) {
    if (ru)
      POST_WRITE(ru, struct_rusage_sz);
  }
}

PRE_SYSCALL(umask)(long mask) {}

POST_SYSCALL(umask)(long res, long mask) {}

PRE_SYSCALL(msgget)(long key, long msgflg) {}

POST_SYSCALL(msgget)(long res, long key, long msgflg) {}

PRE_SYSCALL(msgsnd)(long msqid, void *msgp, long msgsz, long msgflg) {
  if (msgp)
    PRE_READ(msgp, msgsz);
}

POST_SYSCALL(msgsnd)
(long res, long msqid, void *msgp, long msgsz, long msgflg) {}

PRE_SYSCALL(msgrcv)
(long msqid, void *msgp, long msgsz, long msgtyp, long msgflg) {}

POST_SYSCALL(msgrcv)
(long res, long msqid, void *msgp, long msgsz, long msgtyp, long msgflg) {
  if (res >= 0) {
    if (msgp)
      POST_WRITE(msgp, res);
  }
}

#  if !SANITIZER_ANDROID
PRE_SYSCALL(msgctl)(long msqid, long cmd, void *buf) {}

POST_SYSCALL(msgctl)(long res, long msqid, long cmd, void *buf) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, struct_msqid_ds_sz);
  }
}
#  endif

PRE_SYSCALL(semget)(long key, long nsems, long semflg) {}

POST_SYSCALL(semget)(long res, long key, long nsems, long semflg) {}

PRE_SYSCALL(semop)(long semid, void *sops, long nsops) {}

POST_SYSCALL(semop)(long res, long semid, void *sops, long nsops) {}

PRE_SYSCALL(semctl)(long semid, long semnum, long cmd, void *arg) {}

POST_SYSCALL(semctl)(long res, long semid, long semnum, long cmd, void *arg) {}

PRE_SYSCALL(semtimedop)
(long semid, void *sops, long nsops, const void *timeout) {
  if (timeout)
    PRE_READ(timeout, struct_timespec_sz);
}

POST_SYSCALL(semtimedop)
(long res, long semid, void *sops, long nsops, const void *timeout) {}

PRE_SYSCALL(shmat)(long shmid, void *shmaddr, long shmflg) {}

POST_SYSCALL(shmat)(long res, long shmid, void *shmaddr, long shmflg) {
  if (res >= 0) {
    if (shmaddr)
      POST_WRITE(shmaddr,
                 __sanitizer::internal_strlen((const char *)shmaddr) + 1);
  }
}

PRE_SYSCALL(shmget)(long key, long size, long flag) {}

POST_SYSCALL(shmget)(long res, long key, long size, long flag) {}

PRE_SYSCALL(shmdt)(void *shmaddr) {}

POST_SYSCALL(shmdt)(long res, void *shmaddr) {
  if (res >= 0) {
    if (shmaddr)
      POST_WRITE(shmaddr,
                 __sanitizer::internal_strlen((const char *)shmaddr) + 1);
  }
}

PRE_SYSCALL(ipc)
(long call, long first, long second, long third, void *ptr, long fifth) {}

POST_SYSCALL(ipc)
(long res, long call, long first, long second, long third, void *ptr,
 long fifth) {}

#  if !SANITIZER_ANDROID
PRE_SYSCALL(shmctl)(long shmid, long cmd, void *buf) {}

POST_SYSCALL(shmctl)(long res, long shmid, long cmd, void *buf) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, sizeof(__sanitizer_shmid_ds));
  }
}

PRE_SYSCALL(mq_open)(const void *name, long oflag, long mode, void *attr) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(mq_open)
(long res, const void *name, long oflag, long mode, void *attr) {
  if (res >= 0) {
    if (attr)
      POST_WRITE(attr, struct_mq_attr_sz);
  }
}

PRE_SYSCALL(mq_unlink)(const void *name) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(mq_unlink)(long res, const void *name) {}

PRE_SYSCALL(mq_timedsend)
(long mqdes, const void *msg_ptr, long msg_len, long msg_prio,
 const void *abs_timeout) {
  if (msg_ptr)
    PRE_READ(msg_ptr, msg_len);
  if (abs_timeout)
    PRE_READ(abs_timeout, struct_timespec_sz);
}

POST_SYSCALL(mq_timedsend)
(long res, long mqdes, const void *msg_ptr, long msg_len, long msg_prio,
 const void *abs_timeout) {}

PRE_SYSCALL(mq_timedreceive)
(long mqdes, void *msg_ptr, long msg_len, void *msg_prio,
 const void *abs_timeout) {
  if (abs_timeout)
    PRE_READ(abs_timeout, struct_timespec_sz);
}

POST_SYSCALL(mq_timedreceive)
(long res, long mqdes, void *msg_ptr, long msg_len, int *msg_prio,
 const void *abs_timeout) {
  if (res >= 0) {
    if (msg_ptr)
      POST_WRITE(msg_ptr, res);
    if (msg_prio)
      POST_WRITE(msg_prio, sizeof(*msg_prio));
  }
}

PRE_SYSCALL(mq_notify)(long mqdes, const void *notification) {
  if (notification)
    PRE_READ(notification, struct_sigevent_sz);
}

POST_SYSCALL(mq_notify)(long res, long mqdes, const void *notification) {}

PRE_SYSCALL(mq_getsetattr)(long mqdes, const void *mqstat, void *omqstat) {
  if (mqstat)
    PRE_READ(mqstat, struct_mq_attr_sz);
}

POST_SYSCALL(mq_getsetattr)
(long res, long mqdes, const void *mqstat, void *omqstat) {
  if (res >= 0) {
    if (omqstat)
      POST_WRITE(omqstat, struct_mq_attr_sz);
  }
}
#  endif  // SANITIZER_ANDROID

PRE_SYSCALL(pciconfig_iobase)(long which, long bus, long devfn) {}

POST_SYSCALL(pciconfig_iobase)(long res, long which, long bus, long devfn) {}

PRE_SYSCALL(pciconfig_read)
(long bus, long dfn, long off, long len, void *buf) {}

POST_SYSCALL(pciconfig_read)
(long res, long bus, long dfn, long off, long len, void *buf) {}

PRE_SYSCALL(pciconfig_write)
(long bus, long dfn, long off, long len, void *buf) {}

POST_SYSCALL(pciconfig_write)
(long res, long bus, long dfn, long off, long len, void *buf) {}

PRE_SYSCALL(swapon)(const void *specialfile, long swap_flags) {
  if (specialfile)
    PRE_READ(specialfile,
             __sanitizer::internal_strlen((const char *)specialfile) + 1);
}

POST_SYSCALL(swapon)(long res, const void *specialfile, long swap_flags) {}

PRE_SYSCALL(swapoff)(const void *specialfile) {
  if (specialfile)
    PRE_READ(specialfile,
             __sanitizer::internal_strlen((const char *)specialfile) + 1);
}

POST_SYSCALL(swapoff)(long res, const void *specialfile) {}

PRE_SYSCALL(sysctl)(__sanitizer___sysctl_args *args) {
  if (args) {
    if (args->name)
      PRE_READ(args->name, args->nlen * sizeof(*args->name));
    if (args->newval)
      PRE_READ(args->name, args->newlen);
  }
}

POST_SYSCALL(sysctl)(long res, __sanitizer___sysctl_args *args) {
  if (res >= 0) {
    if (args && args->oldval && args->oldlenp) {
      POST_WRITE(args->oldlenp, sizeof(*args->oldlenp));
      POST_WRITE(args->oldval, *args->oldlenp);
    }
  }
}

PRE_SYSCALL(sysinfo)(void *info) {}

POST_SYSCALL(sysinfo)(long res, void *info) {
  if (res >= 0) {
    if (info)
      POST_WRITE(info, struct_sysinfo_sz);
  }
}

PRE_SYSCALL(sysfs)(long option, long arg1, long arg2) {}

POST_SYSCALL(sysfs)(long res, long option, long arg1, long arg2) {}

PRE_SYSCALL(syslog)(long type, void *buf, long len) {}

POST_SYSCALL(syslog)(long res, long type, void *buf, long len) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, __sanitizer::internal_strlen((const char *)buf) + 1);
  }
}

PRE_SYSCALL(uselib)(const void *library) {
  if (library)
    PRE_READ(library, __sanitizer::internal_strlen((const char *)library) + 1);
}

POST_SYSCALL(uselib)(long res, const void *library) {}

PRE_SYSCALL(ni_syscall)() {}

POST_SYSCALL(ni_syscall)(long res) {}

PRE_SYSCALL(ptrace)(long request, long pid, long addr, long data) {
#  if !SANITIZER_ANDROID &&                                                   \
      (defined(__i386) || defined(__x86_64) || defined(__mips64) ||           \
       defined(__powerpc64__) || defined(__aarch64__) || defined(__s390__) || \
       defined(__loongarch__) || SANITIZER_RISCV64)
  if (data) {
    if (request == ptrace_setregs) {
      PRE_READ((void *)data, struct_user_regs_struct_sz);
    } else if (request == ptrace_setfpregs) {
      PRE_READ((void *)data, struct_user_fpregs_struct_sz);
    } else if (request == ptrace_setfpxregs) {
      PRE_READ((void *)data, struct_user_fpxregs_struct_sz);
    } else if (request == ptrace_setsiginfo) {
      PRE_READ((void *)data, siginfo_t_sz);
    } else if (request == ptrace_setregset) {
      __sanitizer_iovec *iov = (__sanitizer_iovec *)data;
      PRE_READ(iov->iov_base, iov->iov_len);
    }
  }
#  endif
}

POST_SYSCALL(ptrace)(long res, long request, long pid, long addr, long data) {
#  if !SANITIZER_ANDROID &&                                                   \
      (defined(__i386) || defined(__x86_64) || defined(__mips64) ||           \
       defined(__powerpc64__) || defined(__aarch64__) || defined(__s390__) || \
       defined(__loongarch__) || SANITIZER_RISCV64)
  if (res >= 0 && data) {
    // Note that this is different from the interceptor in
    // sanitizer_common_interceptors.inc.
    // PEEK* requests return resulting values through data pointer.
    if (request == ptrace_getregs) {
      POST_WRITE((void *)data, struct_user_regs_struct_sz);
    } else if (request == ptrace_getfpregs) {
      POST_WRITE((void *)data, struct_user_fpregs_struct_sz);
    } else if (request == ptrace_getfpxregs) {
      POST_WRITE((void *)data, struct_user_fpxregs_struct_sz);
    } else if (request == ptrace_getsiginfo) {
      POST_WRITE((void *)data, siginfo_t_sz);
    } else if (request == ptrace_getregset) {
      __sanitizer_iovec *iov = (__sanitizer_iovec *)data;
      POST_WRITE(iov->iov_base, iov->iov_len);
    } else if (request == ptrace_peekdata || request == ptrace_peektext ||
               request == ptrace_peekuser) {
      POST_WRITE((void *)data, sizeof(void *));
    }
  }
#  endif
}

PRE_SYSCALL(add_key)
(const void *_type, const void *_description, const void *_payload, long plen,
 long destringid) {
  if (_type)
    PRE_READ(_type, __sanitizer::internal_strlen((const char *)_type) + 1);
  if (_description)
    PRE_READ(_description,
             __sanitizer::internal_strlen((const char *)_description) + 1);
}

POST_SYSCALL(add_key)
(long res, const void *_type, const void *_description, const void *_payload,
 long plen, long destringid) {}

PRE_SYSCALL(request_key)
(const void *_type, const void *_description, const void *_callout_info,
 long destringid) {
  if (_type)
    PRE_READ(_type, __sanitizer::internal_strlen((const char *)_type) + 1);
  if (_description)
    PRE_READ(_description,
             __sanitizer::internal_strlen((const char *)_description) + 1);
  if (_callout_info)
    PRE_READ(_callout_info,
             __sanitizer::internal_strlen((const char *)_callout_info) + 1);
}

POST_SYSCALL(request_key)
(long res, const void *_type, const void *_description,
 const void *_callout_info, long destringid) {}

PRE_SYSCALL(keyctl)(long cmd, long arg2, long arg3, long arg4, long arg5) {}

POST_SYSCALL(keyctl)
(long res, long cmd, long arg2, long arg3, long arg4, long arg5) {}

PRE_SYSCALL(ioprio_set)(long which, long who, long ioprio) {}

POST_SYSCALL(ioprio_set)(long res, long which, long who, long ioprio) {}

PRE_SYSCALL(ioprio_get)(long which, long who) {}

POST_SYSCALL(ioprio_get)(long res, long which, long who) {}

PRE_SYSCALL(set_mempolicy)(long mode, void *nmask, long maxnode) {}

POST_SYSCALL(set_mempolicy)(long res, long mode, void *nmask, long maxnode) {
  if (res >= 0) {
    if (nmask)
      POST_WRITE(nmask, sizeof(long));
  }
}

PRE_SYSCALL(migrate_pages)
(long pid, long maxnode, const void *from, const void *to) {
  if (from)
    PRE_READ(from, sizeof(long));
  if (to)
    PRE_READ(to, sizeof(long));
}

POST_SYSCALL(migrate_pages)
(long res, long pid, long maxnode, const void *from, const void *to) {}

PRE_SYSCALL(move_pages)
(long pid, long nr_pages, const void **pages, const int *nodes, int *status,
 long flags) {
  if (pages)
    PRE_READ(pages, nr_pages * sizeof(*pages));
  if (nodes)
    PRE_READ(nodes, nr_pages * sizeof(*nodes));
}

POST_SYSCALL(move_pages)
(long res, long pid, long nr_pages, const void **pages, const int *nodes,
 int *status, long flags) {
  if (res >= 0) {
    if (status)
      POST_WRITE(status, nr_pages * sizeof(*status));
  }
}

PRE_SYSCALL(mbind)
(long start, long len, long mode, void *nmask, long maxnode, long flags) {}

POST_SYSCALL(mbind)
(long res, long start, long len, long mode, void *nmask, long maxnode,
 long flags) {
  if (res >= 0) {
    if (nmask)
      POST_WRITE(nmask, sizeof(long));
  }
}

PRE_SYSCALL(get_mempolicy)
(void *policy, void *nmask, long maxnode, long addr, long flags) {}

POST_SYSCALL(get_mempolicy)
(long res, void *policy, void *nmask, long maxnode, long addr, long flags) {
  if (res >= 0) {
    if (policy)
      POST_WRITE(policy, sizeof(int));
    if (nmask)
      POST_WRITE(nmask, sizeof(long));
  }
}

PRE_SYSCALL(inotify_init)() {}

POST_SYSCALL(inotify_init)(long res) {}

PRE_SYSCALL(inotify_init1)(long flags) {}

POST_SYSCALL(inotify_init1)(long res, long flags) {}

PRE_SYSCALL(inotify_add_watch)(long fd, const void *path, long mask) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(inotify_add_watch)
(long res, long fd, const void *path, long mask) {}

PRE_SYSCALL(inotify_rm_watch)(long fd, long wd) {}

POST_SYSCALL(inotify_rm_watch)(long res, long fd, long wd) {}

PRE_SYSCALL(spu_run)(long fd, void *unpc, void *ustatus) {}

POST_SYSCALL(spu_run)(long res, long fd, unsigned *unpc, unsigned *ustatus) {
  if (res >= 0) {
    if (unpc)
      POST_WRITE(unpc, sizeof(*unpc));
    if (ustatus)
      POST_WRITE(ustatus, sizeof(*ustatus));
  }
}

PRE_SYSCALL(spu_create)(const void *name, long flags, long mode, long fd) {
  if (name)
    PRE_READ(name, __sanitizer::internal_strlen((const char *)name) + 1);
}

POST_SYSCALL(spu_create)
(long res, const void *name, long flags, long mode, long fd) {}

PRE_SYSCALL(mknodat)(long dfd, const void *filename, long mode, long dev) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(mknodat)
(long res, long dfd, const void *filename, long mode, long dev) {}

PRE_SYSCALL(mkdirat)(long dfd, const void *pathname, long mode) {
  if (pathname)
    PRE_READ(pathname,
             __sanitizer::internal_strlen((const char *)pathname) + 1);
}

POST_SYSCALL(mkdirat)(long res, long dfd, const void *pathname, long mode) {}

PRE_SYSCALL(unlinkat)(long dfd, const void *pathname, long flag) {
  if (pathname)
    PRE_READ(pathname,
             __sanitizer::internal_strlen((const char *)pathname) + 1);
}

POST_SYSCALL(unlinkat)(long res, long dfd, const void *pathname, long flag) {}

PRE_SYSCALL(symlinkat)(const void *oldname, long newdfd, const void *newname) {
  if (oldname)
    PRE_READ(oldname, __sanitizer::internal_strlen((const char *)oldname) + 1);
  if (newname)
    PRE_READ(newname, __sanitizer::internal_strlen((const char *)newname) + 1);
}

POST_SYSCALL(symlinkat)
(long res, const void *oldname, long newdfd, const void *newname) {}

PRE_SYSCALL(linkat)
(long olddfd, const void *oldname, long newdfd, const void *newname,
 long flags) {
  if (oldname)
    PRE_READ(oldname, __sanitizer::internal_strlen((const char *)oldname) + 1);
  if (newname)
    PRE_READ(newname, __sanitizer::internal_strlen((const char *)newname) + 1);
}

POST_SYSCALL(linkat)
(long res, long olddfd, const void *oldname, long newdfd, const void *newname,
 long flags) {}

PRE_SYSCALL(renameat)
(long olddfd, const void *oldname, long newdfd, const void *newname) {
  if (oldname)
    PRE_READ(oldname, __sanitizer::internal_strlen((const char *)oldname) + 1);
  if (newname)
    PRE_READ(newname, __sanitizer::internal_strlen((const char *)newname) + 1);
}

POST_SYSCALL(renameat)
(long res, long olddfd, const void *oldname, long newdfd, const void *newname) {
}

PRE_SYSCALL(futimesat)(long dfd, const void *filename, void *utimes) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(futimesat)
(long res, long dfd, const void *filename, void *utimes) {
  if (res >= 0) {
    if (utimes)
      POST_WRITE(utimes, timeval_sz);
  }
}

PRE_SYSCALL(faccessat)(long dfd, const void *filename, long mode) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(faccessat)(long res, long dfd, const void *filename, long mode) {}

PRE_SYSCALL(fchmodat)(long dfd, const void *filename, long mode) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(fchmodat)(long res, long dfd, const void *filename, long mode) {}

PRE_SYSCALL(fchownat)
(long dfd, const void *filename, long user, long group, long flag) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(fchownat)
(long res, long dfd, const void *filename, long user, long group, long flag) {}

PRE_SYSCALL(fchmodat2)(long dfd, const void *filename, long mode, long flag) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(fchmodat2)
(long res, long dfd, const void *filename, long mode, long flag) {}

PRE_SYSCALL(openat)(long dfd, const void *filename, long flags, long mode) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(openat)
(long res, long dfd, const void *filename, long flags, long mode) {}

PRE_SYSCALL(newfstatat)
(long dfd, const void *filename, void *statbuf, long flag) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(newfstatat)
(long res, long dfd, const void *filename, void *statbuf, long flag) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat_sz);
  }
}

PRE_SYSCALL(fstatat64)
(long dfd, const void *filename, void *statbuf, long flag) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(fstatat64)
(long res, long dfd, const void *filename, void *statbuf, long flag) {
  if (res >= 0) {
    if (statbuf)
      POST_WRITE(statbuf, struct_kernel_stat64_sz);
  }
}

PRE_SYSCALL(readlinkat)(long dfd, const void *path, void *buf, long bufsiz) {
  if (path)
    PRE_READ(path, __sanitizer::internal_strlen((const char *)path) + 1);
}

POST_SYSCALL(readlinkat)
(long res, long dfd, const void *path, void *buf, long bufsiz) {
  if (res >= 0) {
    if (buf)
      POST_WRITE(buf, __sanitizer::internal_strlen((const char *)buf) + 1);
  }
}

PRE_SYSCALL(utimensat)
(long dfd, const void *filename, void *utimes, long flags) {
  if (filename)
    PRE_READ(filename,
             __sanitizer::internal_strlen((const char *)filename) + 1);
}

POST_SYSCALL(utimensat)
(long res, long dfd, const void *filename, void *utimes, long flags) {
  if (res >= 0) {
    if (utimes)
      POST_WRITE(utimes, struct_timespec_sz);
  }
}

PRE_SYSCALL(unshare)(long unshare_flags) {}

POST_SYSCALL(unshare)(long res, long unshare_flags) {}

PRE_SYSCALL(splice)
(long fd_in, void *off_in, long fd_out, void *off_out, long len, long flags) {}

POST_SYSCALL(splice)
(long res, long fd_in, void *off_in, long fd_out, void *off_out, long len,
 long flags) {
  if (res >= 0) {
    if (off_in)
      POST_WRITE(off_in, sizeof(long long));
    if (off_out)
      POST_WRITE(off_out, sizeof(long long));
  }
}

PRE_SYSCALL(vmsplice)
(long fd, const __sanitizer_iovec *iov, long nr_segs, long flags) {}

POST_SYSCALL(vmsplice)
(long res, long fd, const __sanitizer_iovec *iov, long nr_segs, long flags) {
  if (res >= 0) {
    if (iov)
      kernel_read_iovec(iov, nr_segs, res);
  }
}

PRE_SYSCALL(tee)(long fdin, long fdout, long len, long flags) {}

POST_SYSCALL(tee)(long res, long fdin, long fdout, long len, long flags) {}

PRE_SYSCALL(get_robust_list)(long pid, void *head_ptr, void *len_ptr) {}

POST_SYSCALL(get_robust_list)
(long res, long pid, void *head_ptr, void *len_ptr) {}

PRE_SYSCALL(set_robust_list)(void *head, long len) {}

POST_SYSCALL(set_robust_list)(long res, void *head, long len) {}

PRE_SYSCALL(getcpu)(void *cpu, void *node, void *cache) {}

POST_SYSCALL(getcpu)(long res, void *cpu, void *node, void *cache) {
  if (res >= 0) {
    if (cpu)
      POST_WRITE(cpu, sizeof(unsigned));
    if (node)
      POST_WRITE(node, sizeof(unsigned));
    // The third argument to this system call is nowadays unused.
  }
}

PRE_SYSCALL(signalfd)(long ufd, void *user_mask, long sizemask) {}

POST_SYSCALL(signalfd)
(long res, long ufd, kernel_sigset_t *user_mask, long sizemask) {
  if (res >= 0) {
    if (user_mask)
      POST_WRITE(user_mask, sizemask);
  }
}

PRE_SYSCALL(signalfd4)(long ufd, void *user_mask, long sizemask, long flags) {}

POST_SYSCALL(signalfd4)
(long res, long ufd, kernel_sigset_t *user_mask, long sizemask, long flags) {
  if (res >= 0) {
    if (user_mask)
      POST_WRITE(user_mask, sizemask);
  }
}

PRE_SYSCALL(timerfd_create)(long clockid, long flags) {}

POST_SYSCALL(timerfd_create)(long res, long clockid, long flags) {}

PRE_SYSCALL(timerfd_settime)
(long ufd, long flags, const void *utmr, void *otmr) {
  if (utmr)
    PRE_READ(utmr, struct_itimerspec_sz);
}

POST_SYSCALL(timerfd_settime)
(long res, long ufd, long flags, const void *utmr, void *otmr) {
  if (res >= 0) {
    if (otmr)
      POST_WRITE(otmr, struct_itimerspec_sz);
  }
}

PRE_SYSCALL(timerfd_gettime)(long ufd, void *otmr) {}

POST_SYSCALL(timerfd_gettime)(long res, long ufd, void *otmr) {
  if (res >= 0) {
    if (otmr)
      POST_WRITE(otmr, struct_itimerspec_sz);
  }
}

PRE_SYSCALL(eventfd)(long count) {}

POST_SYSCALL(eventfd)(long res, long count) {}

PRE_SYSCALL(eventfd2)(long count, long flags) {}

POST_SYSCALL(eventfd2)(long res, long count, long flags) {}

PRE_SYSCALL(old_readdir)(long arg0, void *arg1, long arg2) {}

POST_SYSCALL(old_readdir)(long res, long arg0, void *arg1, long arg2) {
  // Missing definition of 'struct old_linux_dirent'.
}

PRE_SYSCALL(pselect6)
(long arg0, __sanitizer___kernel_fd_set *arg1,
 __sanitizer___kernel_fd_set *arg2, __sanitizer___kernel_fd_set *arg3,
 void *arg4, void *arg5) {}

POST_SYSCALL(pselect6)
(long res, long arg0, __sanitizer___kernel_fd_set *arg1,
 __sanitizer___kernel_fd_set *arg2, __sanitizer___kernel_fd_set *arg3,
 void *arg4, void *arg5) {
  if (res >= 0) {
    if (arg1)
      POST_WRITE(arg1, sizeof(*arg1));
    if (arg2)
      POST_WRITE(arg2, sizeof(*arg2));
    if (arg3)
      POST_WRITE(arg3, sizeof(*arg3));
    if (arg4)
      POST_WRITE(arg4, struct_timespec_sz);
  }
}

PRE_SYSCALL(ppoll)
(__sanitizer_pollfd *arg0, long arg1, void *arg2, const kernel_sigset_t *arg3,
 long arg4) {
  if (arg3)
    PRE_READ(arg3, arg4);
}

POST_SYSCALL(ppoll)
(long res, __sanitizer_pollfd *arg0, long arg1, void *arg2, const void *arg3,
 long arg4) {
  if (res >= 0) {
    if (arg0)
      POST_WRITE(arg0, sizeof(*arg0));
    if (arg2)
      POST_WRITE(arg2, struct_timespec_sz);
  }
}

PRE_SYSCALL(syncfs)(long fd) {}

POST_SYSCALL(syncfs)(long res, long fd) {}

PRE_SYSCALL(perf_event_open)
(__sanitizer_perf_event_attr *attr_uptr, long pid, long cpu, long group_fd,
 long flags) {
  if (attr_uptr)
    PRE_READ(attr_uptr, attr_uptr->size);
}

POST_SYSCALL(perf_event_open)
(long res, __sanitizer_perf_event_attr *attr_uptr, long pid, long cpu,
 long group_fd, long flags) {}

PRE_SYSCALL(mmap_pgoff)
(long addr, long len, long prot, long flags, long fd, long pgoff) {}

POST_SYSCALL(mmap_pgoff)
(long res, long addr, long len, long prot, long flags, long fd, long pgoff) {}

PRE_SYSCALL(old_mmap)(void *arg) {}

POST_SYSCALL(old_mmap)(long res, void *arg) {}

PRE_SYSCALL(name_to_handle_at)
(long dfd, const void *name, void *handle, void *mnt_id, long flag) {}

POST_SYSCALL(name_to_handle_at)
(long res, long dfd, const void *name, void *handle, void *mnt_id, long flag) {}

PRE_SYSCALL(open_by_handle_at)(long mountdirfd, void *handle, long flags) {}

POST_SYSCALL(open_by_handle_at)
(long res, long mountdirfd, void *handle, long flags) {}

PRE_SYSCALL(setns)(long fd, long nstype) {}

POST_SYSCALL(setns)(long res, long fd, long nstype) {}

PRE_SYSCALL(process_vm_readv)
(long pid, const __sanitizer_iovec *lvec, long liovcnt, const void *rvec,
 long riovcnt, long flags) {}

POST_SYSCALL(process_vm_readv)
(long res, long pid, const __sanitizer_iovec *lvec, long liovcnt,
 const void *rvec, long riovcnt, long flags) {
  if (res >= 0) {
    if (lvec)
      kernel_write_iovec(lvec, liovcnt, res);
  }
}

PRE_SYSCALL(process_vm_writev)
(long pid, const __sanitizer_iovec *lvec, long liovcnt, const void *rvec,
 long riovcnt, long flags) {}

POST_SYSCALL(process_vm_writev)
(long res, long pid, const __sanitizer_iovec *lvec, long liovcnt,
 const void *rvec, long riovcnt, long flags) {
  if (res >= 0) {
    if (lvec)
      kernel_read_iovec(lvec, liovcnt, res);
  }
}

PRE_SYSCALL(fork)() { COMMON_SYSCALL_PRE_FORK(); }

POST_SYSCALL(fork)(long res) { COMMON_SYSCALL_POST_FORK(res); }

PRE_SYSCALL(vfork)() { COMMON_SYSCALL_PRE_FORK(); }

POST_SYSCALL(vfork)(long res) { COMMON_SYSCALL_POST_FORK(res); }

PRE_SYSCALL(sigaction)
(long signum, const __sanitizer_kernel_sigaction_t *act,
 __sanitizer_kernel_sigaction_t *oldact) {
  if (act) {
    PRE_READ(&act->sigaction, sizeof(act->sigaction));
    PRE_READ(&act->sa_flags, sizeof(act->sa_flags));
    PRE_READ(&act->sa_mask, sizeof(act->sa_mask));
  }
}

POST_SYSCALL(sigaction)
(long res, long signum, const __sanitizer_kernel_sigaction_t *act,
 __sanitizer_kernel_sigaction_t *oldact) {
  if (res >= 0 && oldact)
    POST_WRITE(oldact, sizeof(*oldact));
}

PRE_SYSCALL(rt_sigaction)
(long signum, const __sanitizer_kernel_sigaction_t *act,
 __sanitizer_kernel_sigaction_t *oldact, SIZE_T sz) {
  if (act) {
    PRE_READ(&act->sigaction, sizeof(act->sigaction));
    PRE_READ(&act->sa_flags, sizeof(act->sa_flags));
    PRE_READ(&act->sa_mask, sz);
  }
}

POST_SYSCALL(rt_sigaction)
(long res, long signum, const __sanitizer_kernel_sigaction_t *act,
 __sanitizer_kernel_sigaction_t *oldact, SIZE_T sz) {
  if (res >= 0 && oldact) {
    SIZE_T oldact_sz = ((char *)&oldact->sa_mask) - ((char *)oldact) + sz;
    POST_WRITE(oldact, oldact_sz);
  }
}

PRE_SYSCALL(getrandom)(void *buf, uptr count, long flags) {
  if (buf) {
    PRE_WRITE(buf, count);
  }
}

POST_SYSCALL(getrandom)(long res, void *buf, uptr count, long flags) {
  if (res > 0 && buf) {
    POST_WRITE(buf, res);
  }
}

PRE_SYSCALL(sigaltstack)(const void *ss, void *oss) {
  if (ss != nullptr) {
    PRE_READ(ss, struct_stack_t_sz);
  }
  if (oss != nullptr) {
    PRE_WRITE(oss, struct_stack_t_sz);
  }
}

POST_SYSCALL(sigaltstack)(long res, void *ss, void *oss) {
  if (res == 0) {
    if (oss != nullptr) {
      POST_WRITE(oss, struct_stack_t_sz);
    }
  }
}

PRE_SYSCALL(futex)
(void *uaddr, long futex_op, long val, void *timeout, void *uaddr2, long val3) {
  COMMON_SYSCALL_BLOCKING_START();
}

POST_SYSCALL(futex)
(long res, void *uaddr, long futex_op, long val, void *timeout, void *uaddr2,
 long val3) {
  COMMON_SYSCALL_BLOCKING_END();
}

}  // extern "C"

#  undef PRE_SYSCALL
#  undef PRE_READ
#  undef PRE_WRITE
#  undef POST_SYSCALL
#  undef POST_READ
#  undef POST_WRITE

#endif  // SANITIZER_LINUX
