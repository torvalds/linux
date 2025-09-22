//===-- sanitizer_syscalls_netbsd.inc ---------------------------*- C++ -*-===//
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
//   COMMON_SYSCALL_POST_FORK(long long res)
//          Called after fork syscall.
//
// DO NOT EDIT! THIS FILE HAS BEEN GENERATED!
//
// Generated with: generate_netbsd_syscalls.awk
// Generated date: 2020-09-10
// Generated from: syscalls.master,v 1.306 2020/08/14 00:53:16 riastradh Exp
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_NETBSD

#include "sanitizer_libc.h"

#define PRE_SYSCALL(name)                                                      \
  SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_syscall_pre_impl_##name
#define PRE_READ(p, s) COMMON_SYSCALL_PRE_READ_RANGE(p, s)
#define PRE_WRITE(p, s) COMMON_SYSCALL_PRE_WRITE_RANGE(p, s)

#define POST_SYSCALL(name)                                                     \
  SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_syscall_post_impl_##name
#define POST_READ(p, s) COMMON_SYSCALL_POST_READ_RANGE(p, s)
#define POST_WRITE(p, s) COMMON_SYSCALL_POST_WRITE_RANGE(p, s)

#ifndef COMMON_SYSCALL_ACQUIRE
#define COMMON_SYSCALL_ACQUIRE(addr) ((void)(addr))
#endif

#ifndef COMMON_SYSCALL_RELEASE
#define COMMON_SYSCALL_RELEASE(addr) ((void)(addr))
#endif

#ifndef COMMON_SYSCALL_FD_CLOSE
#define COMMON_SYSCALL_FD_CLOSE(fd) ((void)(fd))
#endif

#ifndef COMMON_SYSCALL_FD_ACQUIRE
#define COMMON_SYSCALL_FD_ACQUIRE(fd) ((void)(fd))
#endif

#ifndef COMMON_SYSCALL_FD_RELEASE
#define COMMON_SYSCALL_FD_RELEASE(fd) ((void)(fd))
#endif

#ifndef COMMON_SYSCALL_PRE_FORK
#define COMMON_SYSCALL_PRE_FORK()                                              \
  {}
#endif

#ifndef COMMON_SYSCALL_POST_FORK
#define COMMON_SYSCALL_POST_FORK(res)                                          \
  {}
#endif

// FIXME: do some kind of PRE_READ for all syscall arguments (int(s) and such).

extern "C" {
#define SYS_MAXSYSARGS 8
PRE_SYSCALL(syscall)(long long code_, long long args_[SYS_MAXSYSARGS]) {
  /* Nothing to do */
}
POST_SYSCALL(syscall)
(long long res, long long code_, long long args_[SYS_MAXSYSARGS]) {
  /* Nothing to do */
}
PRE_SYSCALL(exit)(long long rval_) { /* Nothing to do */ }
POST_SYSCALL(exit)(long long res, long long rval_) { /* Nothing to do */ }
PRE_SYSCALL(fork)(void) { COMMON_SYSCALL_PRE_FORK(); }
POST_SYSCALL(fork)(long long res) { COMMON_SYSCALL_POST_FORK(res); }
PRE_SYSCALL(read)(long long fd_, void *buf_, long long nbyte_) {
  if (buf_) {
    PRE_WRITE(buf_, nbyte_);
  }
}
POST_SYSCALL(read)(long long res, long long fd_, void *buf_, long long nbyte_) {
  if (res > 0) {
    POST_WRITE(buf_, res);
  }
}
PRE_SYSCALL(write)(long long fd_, void *buf_, long long nbyte_) {
  if (buf_) {
    PRE_READ(buf_, nbyte_);
  }
}
POST_SYSCALL(write)
(long long res, long long fd_, void *buf_, long long nbyte_) {
  if (res > 0) {
    POST_READ(buf_, res);
  }
}
PRE_SYSCALL(open)(void *path_, long long flags_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(open)
(long long res, void *path_, long long flags_, long long mode_) {
  if (res > 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(close)(long long fd_) { COMMON_SYSCALL_FD_CLOSE((int)fd_); }
POST_SYSCALL(close)(long long res, long long fd_) { /* Nothing to do */ }
PRE_SYSCALL(compat_50_wait4)
(long long pid_, void *status_, long long options_, void *rusage_) {
  /* TODO */
}
POST_SYSCALL(compat_50_wait4)
(long long res, long long pid_, void *status_, long long options_,
  void *rusage_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ocreat)(void *path_, long long mode_) { /* TODO */ }
POST_SYSCALL(compat_43_ocreat)(long long res, void *path_, long long mode_) {
  /* TODO */
}
PRE_SYSCALL(link)(void *path_, void *link_) {
  const char *path = (const char *)path_;
  const char *link = (const char *)link_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (link) {
    PRE_READ(path, __sanitizer::internal_strlen(link) + 1);
  }
}
POST_SYSCALL(link)(long long res, void *path_, void *link_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    const char *link = (const char *)link_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
    if (link) {
      POST_READ(path, __sanitizer::internal_strlen(link) + 1);
    }
  }
}
PRE_SYSCALL(unlink)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(unlink)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
/* syscall 11 has been skipped */
PRE_SYSCALL(chdir)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(chdir)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(fchdir)(long long fd_) { /* Nothing to do */ }
POST_SYSCALL(fchdir)(long long res, long long fd_) { /* Nothing to do */ }
PRE_SYSCALL(compat_50_mknod)(void *path_, long long mode_, long long dev_) {
  /* TODO */
}
POST_SYSCALL(compat_50_mknod)
(long long res, void *path_, long long mode_, long long dev_) {
  /* TODO */
}
PRE_SYSCALL(chmod)(void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(chmod)(long long res, void *path_, long long mode_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(chown)(void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(chown)
(long long res, void *path_, long long uid_, long long gid_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(break)(void *nsize_) { /* Nothing to do */ }
POST_SYSCALL(break)(long long res, void *nsize_) { /* Nothing to do */ }
PRE_SYSCALL(compat_20_getfsstat)
(void *buf_, long long bufsize_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(compat_20_getfsstat)
(long long res, void *buf_, long long bufsize_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_olseek)
(long long fd_, long long offset_, long long whence_) {
  /* TODO */
}
POST_SYSCALL(compat_43_olseek)
(long long res, long long fd_, long long offset_, long long whence_) {
  /* TODO */
}
PRE_SYSCALL(getpid)(void) { /* Nothing to do */ }
POST_SYSCALL(getpid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(compat_40_mount)
(void *type_, void *path_, long long flags_, void *data_) {
  /* TODO */
}
POST_SYSCALL(compat_40_mount)
(long long res, void *type_, void *path_, long long flags_, void *data_) {
  /* TODO */
}
PRE_SYSCALL(unmount)(void *path_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(unmount)(long long res, void *path_, long long flags_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(setuid)(long long uid_) { /* Nothing to do */ }
POST_SYSCALL(setuid)(long long res, long long uid_) { /* Nothing to do */ }
PRE_SYSCALL(getuid)(void) { /* Nothing to do */ }
POST_SYSCALL(getuid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(geteuid)(void) { /* Nothing to do */ }
POST_SYSCALL(geteuid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(ptrace)
(long long req_, long long pid_, void *addr_, long long data_) {
  if (req_ == ptrace_pt_io) {
    struct __sanitizer_ptrace_io_desc *addr =
        (struct __sanitizer_ptrace_io_desc *)addr_;
    PRE_READ(addr, struct_ptrace_ptrace_io_desc_struct_sz);
    if (addr->piod_op == ptrace_piod_write_d ||
        addr->piod_op == ptrace_piod_write_i) {
      PRE_READ(addr->piod_addr, addr->piod_len);
    }
    if (addr->piod_op == ptrace_piod_read_d ||
        addr->piod_op == ptrace_piod_read_i ||
        addr->piod_op == ptrace_piod_read_auxv) {
      PRE_WRITE(addr->piod_addr, addr->piod_len);
    }
  } else if (req_ == ptrace_pt_lwpinfo) {
    struct __sanitizer_ptrace_lwpinfo *addr =
        (struct __sanitizer_ptrace_lwpinfo *)addr_;
    PRE_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));
    PRE_WRITE(addr, struct_ptrace_ptrace_lwpinfo_struct_sz);
  } else if (req_ == ptrace_pt_set_event_mask) {
    PRE_READ(addr_, struct_ptrace_ptrace_event_struct_sz);
  } else if (req_ == ptrace_pt_get_event_mask) {
    PRE_WRITE(addr_, struct_ptrace_ptrace_event_struct_sz);
  } else if (req_ == ptrace_pt_set_siginfo) {
    PRE_READ(addr_, struct_ptrace_ptrace_siginfo_struct_sz);
  } else if (req_ == ptrace_pt_get_siginfo) {
    PRE_WRITE(addr_, struct_ptrace_ptrace_siginfo_struct_sz);
  } else if (req_ == ptrace_pt_lwpstatus) {
    struct __sanitizer_ptrace_lwpstatus *addr =
        (struct __sanitizer_ptrace_lwpstatus *)addr_;
    PRE_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));
    PRE_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);
  } else if (req_ == ptrace_pt_lwpnext) {
    struct __sanitizer_ptrace_lwpstatus *addr =
        (struct __sanitizer_ptrace_lwpstatus *)addr_;
    PRE_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));
    PRE_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);
  } else if (req_ == ptrace_pt_setregs) {
    PRE_READ(addr_, struct_ptrace_reg_struct_sz);
  } else if (req_ == ptrace_pt_getregs) {
    PRE_WRITE(addr_, struct_ptrace_reg_struct_sz);
  } else if (req_ == ptrace_pt_setfpregs) {
    PRE_READ(addr_, struct_ptrace_fpreg_struct_sz);
  } else if (req_ == ptrace_pt_getfpregs) {
    PRE_WRITE(addr_, struct_ptrace_fpreg_struct_sz);
  } else if (req_ == ptrace_pt_setdbregs) {
    PRE_READ(addr_, struct_ptrace_dbreg_struct_sz);
  } else if (req_ == ptrace_pt_getdbregs) {
    PRE_WRITE(addr_, struct_ptrace_dbreg_struct_sz);
  }
}
POST_SYSCALL(ptrace)
(long long res, long long req_, long long pid_, void *addr_, long long data_) {
  if (res == 0) {
    if (req_ == ptrace_pt_io) {
      struct __sanitizer_ptrace_io_desc *addr =
          (struct __sanitizer_ptrace_io_desc *)addr_;
      POST_READ(addr, struct_ptrace_ptrace_io_desc_struct_sz);
      if (addr->piod_op == ptrace_piod_write_d ||
          addr->piod_op == ptrace_piod_write_i) {
        POST_READ(addr->piod_addr, addr->piod_len);
      }
      if (addr->piod_op == ptrace_piod_read_d ||
          addr->piod_op == ptrace_piod_read_i ||
          addr->piod_op == ptrace_piod_read_auxv) {
        POST_WRITE(addr->piod_addr, addr->piod_len);
      }
    } else if (req_ == ptrace_pt_lwpinfo) {
      struct __sanitizer_ptrace_lwpinfo *addr =
          (struct __sanitizer_ptrace_lwpinfo *)addr_;
      POST_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));
      POST_WRITE(addr, struct_ptrace_ptrace_lwpinfo_struct_sz);
    } else if (req_ == ptrace_pt_set_event_mask) {
      POST_READ(addr_, struct_ptrace_ptrace_event_struct_sz);
    } else if (req_ == ptrace_pt_get_event_mask) {
      POST_WRITE(addr_, struct_ptrace_ptrace_event_struct_sz);
    } else if (req_ == ptrace_pt_set_siginfo) {
      POST_READ(addr_, struct_ptrace_ptrace_siginfo_struct_sz);
    } else if (req_ == ptrace_pt_get_siginfo) {
      POST_WRITE(addr_, struct_ptrace_ptrace_siginfo_struct_sz);
    } else if (req_ == ptrace_pt_lwpstatus) {
      struct __sanitizer_ptrace_lwpstatus *addr =
          (struct __sanitizer_ptrace_lwpstatus *)addr_;
      POST_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));
      POST_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);
    } else if (req_ == ptrace_pt_lwpnext) {
      struct __sanitizer_ptrace_lwpstatus *addr =
          (struct __sanitizer_ptrace_lwpstatus *)addr_;
      POST_READ(&addr->pl_lwpid, sizeof(__sanitizer_lwpid_t));
      POST_WRITE(addr, struct_ptrace_ptrace_lwpstatus_struct_sz);
    } else if (req_ == ptrace_pt_setregs) {
      POST_READ(addr_, struct_ptrace_reg_struct_sz);
    } else if (req_ == ptrace_pt_getregs) {
      POST_WRITE(addr_, struct_ptrace_reg_struct_sz);
    } else if (req_ == ptrace_pt_setfpregs) {
      POST_READ(addr_, struct_ptrace_fpreg_struct_sz);
    } else if (req_ == ptrace_pt_getfpregs) {
      POST_WRITE(addr_, struct_ptrace_fpreg_struct_sz);
    } else if (req_ == ptrace_pt_setdbregs) {
      POST_READ(addr_, struct_ptrace_dbreg_struct_sz);
    } else if (req_ == ptrace_pt_getdbregs) {
      POST_WRITE(addr_, struct_ptrace_dbreg_struct_sz);
    }
  }
}
PRE_SYSCALL(recvmsg)(long long s_, void *msg_, long long flags_) {
  PRE_WRITE(msg_, sizeof(__sanitizer_msghdr));
}
POST_SYSCALL(recvmsg)
(long long res, long long s_, void *msg_, long long flags_) {
  if (res > 0) {
    POST_WRITE(msg_, sizeof(__sanitizer_msghdr));
  }
}
PRE_SYSCALL(sendmsg)(long long s_, void *msg_, long long flags_) {
  PRE_READ(msg_, sizeof(__sanitizer_msghdr));
}
POST_SYSCALL(sendmsg)
(long long res, long long s_, void *msg_, long long flags_) {
  if (res > 0) {
    POST_READ(msg_, sizeof(__sanitizer_msghdr));
  }
}
PRE_SYSCALL(recvfrom)
(long long s_, void *buf_, long long len_, long long flags_, void *from_,
  void *fromlenaddr_) {
  PRE_WRITE(buf_, len_);
  PRE_WRITE(from_, struct_sockaddr_sz);
  PRE_WRITE(fromlenaddr_, sizeof(__sanitizer_socklen_t));
}
POST_SYSCALL(recvfrom)
(long long res, long long s_, void *buf_, long long len_, long long flags_,
  void *from_, void *fromlenaddr_) {
  if (res >= 0) {
    POST_WRITE(buf_, res);
    POST_WRITE(from_, struct_sockaddr_sz);
    POST_WRITE(fromlenaddr_, sizeof(__sanitizer_socklen_t));
  }
}
PRE_SYSCALL(accept)(long long s_, void *name_, void *anamelen_) {
  PRE_WRITE(name_, struct_sockaddr_sz);
  PRE_WRITE(anamelen_, sizeof(__sanitizer_socklen_t));
}
POST_SYSCALL(accept)
(long long res, long long s_, void *name_, void *anamelen_) {
  if (res == 0) {
    POST_WRITE(name_, struct_sockaddr_sz);
    POST_WRITE(anamelen_, sizeof(__sanitizer_socklen_t));
  }
}
PRE_SYSCALL(getpeername)(long long fdes_, void *asa_, void *alen_) {
  PRE_WRITE(asa_, struct_sockaddr_sz);
  PRE_WRITE(alen_, sizeof(__sanitizer_socklen_t));
}
POST_SYSCALL(getpeername)
(long long res, long long fdes_, void *asa_, void *alen_) {
  if (res == 0) {
    POST_WRITE(asa_, struct_sockaddr_sz);
    POST_WRITE(alen_, sizeof(__sanitizer_socklen_t));
  }
}
PRE_SYSCALL(getsockname)(long long fdes_, void *asa_, void *alen_) {
  PRE_WRITE(asa_, struct_sockaddr_sz);
  PRE_WRITE(alen_, sizeof(__sanitizer_socklen_t));
}
POST_SYSCALL(getsockname)
(long long res, long long fdes_, void *asa_, void *alen_) {
  if (res == 0) {
    POST_WRITE(asa_, struct_sockaddr_sz);
    POST_WRITE(alen_, sizeof(__sanitizer_socklen_t));
  }
}
PRE_SYSCALL(access)(void *path_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(access)(long long res, void *path_, long long flags_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(chflags)(void *path_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(chflags)(long long res, void *path_, long long flags_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(fchflags)(long long fd_, long long flags_) { /* Nothing to do */ }
POST_SYSCALL(fchflags)(long long res, long long fd_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(sync)(void) { /* Nothing to do */ }
POST_SYSCALL(sync)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(kill)(long long pid_, long long signum_) { /* Nothing to do */ }
POST_SYSCALL(kill)(long long res, long long pid_, long long signum_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_43_stat43)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_43_stat43)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(getppid)(void) { /* Nothing to do */ }
POST_SYSCALL(getppid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(compat_43_lstat43)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_43_lstat43)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(dup)(long long fd_) { /* Nothing to do */ }
POST_SYSCALL(dup)(long long res, long long fd_) { /* Nothing to do */ }
PRE_SYSCALL(pipe)(void) {
  /* pipe returns two descriptors through two returned values */
}
POST_SYSCALL(pipe)(long long res) {
  /* pipe returns two descriptors through two returned values */
}
PRE_SYSCALL(getegid)(void) { /* Nothing to do */ }
POST_SYSCALL(getegid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(profil)
(void *samples_, long long size_, long long offset_, long long scale_) {
  if (samples_) {
    PRE_WRITE(samples_, size_);
  }
}
POST_SYSCALL(profil)
(long long res, void *samples_, long long size_, long long offset_,
  long long scale_) {
  if (res == 0) {
    if (samples_) {
      POST_WRITE(samples_, size_);
    }
  }
}
PRE_SYSCALL(ktrace)
(void *fname_, long long ops_, long long facs_, long long pid_) {
  const char *fname = (const char *)fname_;
  if (fname) {
    PRE_READ(fname, __sanitizer::internal_strlen(fname) + 1);
  }
}
POST_SYSCALL(ktrace)
(long long res, void *fname_, long long ops_, long long facs_, long long pid_) {
  const char *fname = (const char *)fname_;
  if (res == 0) {
    if (fname) {
      POST_READ(fname, __sanitizer::internal_strlen(fname) + 1);
    }
  }
}
PRE_SYSCALL(compat_13_sigaction13)(long long signum_, void *nsa_, void *osa_) {
  /* TODO */
}
POST_SYSCALL(compat_13_sigaction13)
(long long res, long long signum_, void *nsa_, void *osa_) {
  /* TODO */
}
PRE_SYSCALL(getgid)(void) { /* Nothing to do */ }
POST_SYSCALL(getgid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(compat_13_sigprocmask13)(long long how_, long long mask_) {
  /* TODO */
}
POST_SYSCALL(compat_13_sigprocmask13)
(long long res, long long how_, long long mask_) {
  /* TODO */
}
PRE_SYSCALL(__getlogin)(void *namebuf_, long long namelen_) {
  if (namebuf_) {
    PRE_WRITE(namebuf_, namelen_);
  }
}
POST_SYSCALL(__getlogin)(long long res, void *namebuf_, long long namelen_) {
  if (res == 0) {
    if (namebuf_) {
      POST_WRITE(namebuf_, namelen_);
    }
  }
}
PRE_SYSCALL(__setlogin)(void *namebuf_) {
  const char *namebuf = (const char *)namebuf_;
  if (namebuf) {
    PRE_READ(namebuf, __sanitizer::internal_strlen(namebuf) + 1);
  }
}
POST_SYSCALL(__setlogin)(long long res, void *namebuf_) {
  if (res == 0) {
    const char *namebuf = (const char *)namebuf_;
    if (namebuf) {
      POST_READ(namebuf, __sanitizer::internal_strlen(namebuf) + 1);
    }
  }
}
PRE_SYSCALL(acct)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(acct)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(compat_13_sigpending13)(void) { /* TODO */ }
POST_SYSCALL(compat_13_sigpending13)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_13_sigaltstack13)(void *nss_, void *oss_) { /* TODO */ }
POST_SYSCALL(compat_13_sigaltstack13)(long long res, void *nss_, void *oss_) {
  /* TODO */
}
PRE_SYSCALL(ioctl)(long long fd_, long long com_, void *data_) {
  /* Nothing to do */
}
POST_SYSCALL(ioctl)(long long res, long long fd_, long long com_, void *data_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_12_oreboot)(long long opt_) { /* TODO */ }
POST_SYSCALL(compat_12_oreboot)(long long res, long long opt_) { /* TODO */ }
PRE_SYSCALL(revoke)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(revoke)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(symlink)(void *path_, void *link_) {
  const char *path = (const char *)path_;
  const char *link = (const char *)link_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (link) {
    PRE_READ(link, __sanitizer::internal_strlen(link) + 1);
  }
}
POST_SYSCALL(symlink)(long long res, void *path_, void *link_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    const char *link = (const char *)link_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
    if (link) {
      POST_READ(link, __sanitizer::internal_strlen(link) + 1);
    }
  }
}
PRE_SYSCALL(readlink)(void *path_, void *buf_, long long count_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (buf_) {
    PRE_WRITE(buf_, count_);
  }
}
POST_SYSCALL(readlink)
(long long res, void *path_, void *buf_, long long count_) {
  if (res > 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
    if (buf_) {
      PRE_WRITE(buf_, res);
    }
  }
}
PRE_SYSCALL(execve)(void *path_, void *argp_, void *envp_) {
  const char *path = (const char *)path_;
  char **argp = (char **)argp_;
  char **envp = (char **)envp_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (argp && argp[0]) {
    char *a = argp[0];
    while (a++) {
      PRE_READ(a, __sanitizer::internal_strlen(a) + 1);
    }
  }
  if (envp && envp[0]) {
    char *e = envp[0];
    while (e++) {
      PRE_READ(e, __sanitizer::internal_strlen(e) + 1);
    }
  }
}
POST_SYSCALL(execve)(long long res, void *path_, void *argp_, void *envp_) {
  /* If we are here, something went wrong */
  const char *path = (const char *)path_;
  char **argp = (char **)argp_;
  char **envp = (char **)envp_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (argp && argp[0]) {
    char *a = argp[0];
    while (a++) {
      POST_READ(a, __sanitizer::internal_strlen(a) + 1);
    }
  }
  if (envp && envp[0]) {
    char *e = envp[0];
    while (e++) {
      POST_READ(e, __sanitizer::internal_strlen(e) + 1);
    }
  }
}
PRE_SYSCALL(umask)(long long newmask_) { /* Nothing to do */ }
POST_SYSCALL(umask)(long long res, long long newmask_) { /* Nothing to do */ }
PRE_SYSCALL(chroot)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(chroot)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(compat_43_fstat43)(long long fd_, void *sb_) { /* TODO */ }
POST_SYSCALL(compat_43_fstat43)(long long res, long long fd_, void *sb_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogetkerninfo)
(long long op_, void *where_, void *size_, long long arg_) {
  /* TODO */
}
POST_SYSCALL(compat_43_ogetkerninfo)
(long long res, long long op_, void *where_, void *size_, long long arg_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogetpagesize)(void) { /* TODO */ }
POST_SYSCALL(compat_43_ogetpagesize)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_12_msync)(void *addr_, long long len_) { /* TODO */ }
POST_SYSCALL(compat_12_msync)(long long res, void *addr_, long long len_) {
  /* TODO */
}
PRE_SYSCALL(vfork)(void) { /* Nothing to do */ }
POST_SYSCALL(vfork)(long long res) { /* Nothing to do */ }
/* syscall 67 has been skipped */
/* syscall 68 has been skipped */
/* syscall 69 has been skipped */
/* syscall 70 has been skipped */
PRE_SYSCALL(compat_43_ommap)
(void *addr_, long long len_, long long prot_, long long flags_, long long fd_,
  long long pos_) {
  /* TODO */
}
POST_SYSCALL(compat_43_ommap)
(long long res, void *addr_, long long len_, long long prot_, long long flags_,
  long long fd_, long long pos_) {
  /* TODO */
}
PRE_SYSCALL(vadvise)(long long anom_) { /* Nothing to do */ }
POST_SYSCALL(vadvise)(long long res, long long anom_) { /* Nothing to do */ }
PRE_SYSCALL(munmap)(void *addr_, long long len_) { /* Nothing to do */ }
POST_SYSCALL(munmap)(long long res, void *addr_, long long len_) {
  /* Nothing to do */
}
PRE_SYSCALL(mprotect)(void *addr_, long long len_, long long prot_) {
  /* Nothing to do */
}
POST_SYSCALL(mprotect)
(long long res, void *addr_, long long len_, long long prot_) {
  /* Nothing to do */
}
PRE_SYSCALL(madvise)(void *addr_, long long len_, long long behav_) {
  /* Nothing to do */
}
POST_SYSCALL(madvise)
(long long res, void *addr_, long long len_, long long behav_) {
  /* Nothing to do */
}
/* syscall 76 has been skipped */
/* syscall 77 has been skipped */
PRE_SYSCALL(mincore)(void *addr_, long long len_, void *vec_) {
  /* Nothing to do */
}
POST_SYSCALL(mincore)(long long res, void *addr_, long long len_, void *vec_) {
  /* Nothing to do */
}
PRE_SYSCALL(getgroups)(long long gidsetsize_, void *gidset_) {
  unsigned int *gidset = (unsigned int *)gidset_;
  if (gidset) {
    PRE_WRITE(gidset, sizeof(*gidset) * gidsetsize_);
  }
}
POST_SYSCALL(getgroups)(long long res, long long gidsetsize_, void *gidset_) {
  if (res == 0) {
    unsigned int *gidset = (unsigned int *)gidset_;
    if (gidset) {
      POST_WRITE(gidset, sizeof(*gidset) * gidsetsize_);
    }
  }
}
PRE_SYSCALL(setgroups)(long long gidsetsize_, void *gidset_) {
  unsigned int *gidset = (unsigned int *)gidset_;
  if (gidset) {
    PRE_READ(gidset, sizeof(*gidset) * gidsetsize_);
  }
}
POST_SYSCALL(setgroups)(long long res, long long gidsetsize_, void *gidset_) {
  if (res == 0) {
    unsigned int *gidset = (unsigned int *)gidset_;
    if (gidset) {
      POST_READ(gidset, sizeof(*gidset) * gidsetsize_);
    }
  }
}
PRE_SYSCALL(getpgrp)(void) { /* Nothing to do */ }
POST_SYSCALL(getpgrp)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(setpgid)(long long pid_, long long pgid_) { /* Nothing to do */ }
POST_SYSCALL(setpgid)(long long res, long long pid_, long long pgid_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50_setitimer)(long long which_, void *itv_, void *oitv_) {
  /* TODO */
}
POST_SYSCALL(compat_50_setitimer)
(long long res, long long which_, void *itv_, void *oitv_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_owait)(void) { /* TODO */ }
POST_SYSCALL(compat_43_owait)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_12_oswapon)(void *name_) { /* TODO */ }
POST_SYSCALL(compat_12_oswapon)(long long res, void *name_) { /* TODO */ }
PRE_SYSCALL(compat_50_getitimer)(long long which_, void *itv_) { /* TODO */ }
POST_SYSCALL(compat_50_getitimer)(long long res, long long which_, void *itv_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogethostname)(void *hostname_, long long len_) {
  /* TODO */
}
POST_SYSCALL(compat_43_ogethostname)
(long long res, void *hostname_, long long len_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_osethostname)(void *hostname_, long long len_) {
  /* TODO */
}
POST_SYSCALL(compat_43_osethostname)
(long long res, void *hostname_, long long len_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogetdtablesize)(void) { /* TODO */ }
POST_SYSCALL(compat_43_ogetdtablesize)(long long res) { /* TODO */ }
PRE_SYSCALL(dup2)(long long from_, long long to_) { /* Nothing to do */ }
POST_SYSCALL(dup2)(long long res, long long from_, long long to_) {
  /* Nothing to do */
}
PRE_SYSCALL(getrandom)(void *buf_, long long buflen_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(getrandom)
(long long res, void *buf_, long long buflen_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(fcntl)(long long fd_, long long cmd_, void *arg_) {
  /* Nothing to do */
}
POST_SYSCALL(fcntl)(long long res, long long fd_, long long cmd_, void *arg_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50_select)
(long long nd_, void *in_, void *ou_, void *ex_, void *tv_) {
  /* TODO */
}
POST_SYSCALL(compat_50_select)
(long long res, long long nd_, void *in_, void *ou_, void *ex_, void *tv_) {
  /* TODO */
}
/* syscall 94 has been skipped */
PRE_SYSCALL(fsync)(long long fd_) { /* Nothing to do */ }
POST_SYSCALL(fsync)(long long res, long long fd_) { /* Nothing to do */ }
PRE_SYSCALL(setpriority)(long long which_, long long who_, long long prio_) {
  /* Nothing to do */
}
POST_SYSCALL(setpriority)
(long long res, long long which_, long long who_, long long prio_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_30_socket)
(long long domain_, long long type_, long long protocol_) {
  /* TODO */
}
POST_SYSCALL(compat_30_socket)
(long long res, long long domain_, long long type_, long long protocol_) {
  /* TODO */
}
PRE_SYSCALL(connect)(long long s_, void *name_, long long namelen_) {
  PRE_READ(name_, namelen_);
}
POST_SYSCALL(connect)
(long long res, long long s_, void *name_, long long namelen_) {
  if (res == 0) {
    POST_READ(name_, namelen_);
  }
}
PRE_SYSCALL(compat_43_oaccept)(long long s_, void *name_, void *anamelen_) {
  /* TODO */
}
POST_SYSCALL(compat_43_oaccept)
(long long res, long long s_, void *name_, void *anamelen_) {
  /* TODO */
}
PRE_SYSCALL(getpriority)(long long which_, long long who_) {
  /* Nothing to do */
}
POST_SYSCALL(getpriority)(long long res, long long which_, long long who_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_43_osend)
(long long s_, void *buf_, long long len_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(compat_43_osend)
(long long res, long long s_, void *buf_, long long len_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_orecv)
(long long s_, void *buf_, long long len_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(compat_43_orecv)
(long long res, long long s_, void *buf_, long long len_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(compat_13_sigreturn13)(void *sigcntxp_) { /* TODO */ }
POST_SYSCALL(compat_13_sigreturn13)(long long res, void *sigcntxp_) {
  /* TODO */
}
PRE_SYSCALL(bind)(long long s_, void *name_, long long namelen_) {
  PRE_READ(name_, namelen_);
}
POST_SYSCALL(bind)
(long long res, long long s_, void *name_, long long namelen_) {
  if (res == 0) {
    PRE_READ(name_, namelen_);
  }
}
PRE_SYSCALL(setsockopt)
(long long s_, long long level_, long long name_, void *val_,
  long long valsize_) {
  if (val_) {
    PRE_READ(val_, valsize_);
  }
}
POST_SYSCALL(setsockopt)
(long long res, long long s_, long long level_, long long name_, void *val_,
  long long valsize_) {
  if (res == 0) {
    if (val_) {
      POST_READ(val_, valsize_);
    }
  }
}
PRE_SYSCALL(listen)(long long s_, long long backlog_) { /* Nothing to do */ }
POST_SYSCALL(listen)(long long res, long long s_, long long backlog_) {
  /* Nothing to do */
}
/* syscall 107 has been skipped */
PRE_SYSCALL(compat_43_osigvec)(long long signum_, void *nsv_, void *osv_) {
  /* TODO */
}
POST_SYSCALL(compat_43_osigvec)
(long long res, long long signum_, void *nsv_, void *osv_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_osigblock)(long long mask_) { /* TODO */ }
POST_SYSCALL(compat_43_osigblock)(long long res, long long mask_) { /* TODO */ }
PRE_SYSCALL(compat_43_osigsetmask)(long long mask_) { /* TODO */ }
POST_SYSCALL(compat_43_osigsetmask)(long long res, long long mask_) {
  /* TODO */
}
PRE_SYSCALL(compat_13_sigsuspend13)(long long mask_) { /* TODO */ }
POST_SYSCALL(compat_13_sigsuspend13)(long long res, long long mask_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_osigstack)(void *nss_, void *oss_) { /* TODO */ }
POST_SYSCALL(compat_43_osigstack)(long long res, void *nss_, void *oss_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_orecvmsg)(long long s_, void *msg_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(compat_43_orecvmsg)
(long long res, long long s_, void *msg_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_osendmsg)(long long s_, void *msg_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(compat_43_osendmsg)
(long long res, long long s_, void *msg_, long long flags_) {
  /* TODO */
}
/* syscall 115 has been skipped */
PRE_SYSCALL(compat_50_gettimeofday)(void *tp_, void *tzp_) { /* TODO */ }
POST_SYSCALL(compat_50_gettimeofday)(long long res, void *tp_, void *tzp_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_getrusage)(long long who_, void *rusage_) { /* TODO */ }
POST_SYSCALL(compat_50_getrusage)
(long long res, long long who_, void *rusage_) {
  /* TODO */
}
PRE_SYSCALL(getsockopt)
(long long s_, long long level_, long long name_, void *val_, void *avalsize_) {
  /* TODO */
}
POST_SYSCALL(getsockopt)
(long long res, long long s_, long long level_, long long name_, void *val_,
  void *avalsize_) {
  /* TODO */
}
/* syscall 119 has been skipped */
PRE_SYSCALL(readv)(long long fd_, void *iovp_, long long iovcnt_) {
  struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;
  int i;
  if (iovp) {
    PRE_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);
    for (i = 0; i < iovcnt_; i++) {
      PRE_WRITE(iovp[i].iov_base, iovp[i].iov_len);
    }
  }
}
POST_SYSCALL(readv)
(long long res, long long fd_, void *iovp_, long long iovcnt_) {
  struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;
  int i;
  uptr m, n = res;
  if (res > 0) {
    if (iovp) {
      POST_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);
      for (i = 0; i < iovcnt_ && n > 0; i++) {
        m = n > iovp[i].iov_len ? iovp[i].iov_len : n;
        POST_WRITE(iovp[i].iov_base, m);
        n -= m;
      }
    }
  }
}
PRE_SYSCALL(writev)(long long fd_, void *iovp_, long long iovcnt_) {
  struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;
  int i;
  if (iovp) {
    PRE_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);
    for (i = 0; i < iovcnt_; i++) {
      PRE_READ(iovp[i].iov_base, iovp[i].iov_len);
    }
  }
}
POST_SYSCALL(writev)
(long long res, long long fd_, void *iovp_, long long iovcnt_) {
  struct __sanitizer_iovec *iovp = (struct __sanitizer_iovec *)iovp_;
  int i;
  uptr m, n = res;
  if (res > 0) {
    if (iovp) {
      POST_READ(iovp, sizeof(struct __sanitizer_iovec) * iovcnt_);
      for (i = 0; i < iovcnt_ && n > 0; i++) {
        m = n > iovp[i].iov_len ? iovp[i].iov_len : n;
        POST_READ(iovp[i].iov_base, m);
        n -= m;
      }
    }
  }
}
PRE_SYSCALL(compat_50_settimeofday)(void *tv_, void *tzp_) { /* TODO */ }
POST_SYSCALL(compat_50_settimeofday)(long long res, void *tv_, void *tzp_) {
  /* TODO */
}
PRE_SYSCALL(fchown)(long long fd_, long long uid_, long long gid_) {
  /* Nothing to do */
}
POST_SYSCALL(fchown)
(long long res, long long fd_, long long uid_, long long gid_) {
  /* Nothing to do */
}
PRE_SYSCALL(fchmod)(long long fd_, long long mode_) { /* Nothing to do */ }
POST_SYSCALL(fchmod)(long long res, long long fd_, long long mode_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_43_orecvfrom)
(long long s_, void *buf_, long long len_, long long flags_, void *from_,
  void *fromlenaddr_) {
  /* TODO */
}
POST_SYSCALL(compat_43_orecvfrom)
(long long res, long long s_, void *buf_, long long len_, long long flags_,
  void *from_, void *fromlenaddr_) {
  /* TODO */
}
PRE_SYSCALL(setreuid)(long long ruid_, long long euid_) { /* Nothing to do */ }
POST_SYSCALL(setreuid)(long long res, long long ruid_, long long euid_) {
  /* Nothing to do */
}
PRE_SYSCALL(setregid)(long long rgid_, long long egid_) { /* Nothing to do */ }
POST_SYSCALL(setregid)(long long res, long long rgid_, long long egid_) {
  /* Nothing to do */
}
PRE_SYSCALL(rename)(void *from_, void *to_) {
  const char *from = (const char *)from_;
  const char *to = (const char *)to_;
  if (from) {
    PRE_READ(from, __sanitizer::internal_strlen(from) + 1);
  }
  if (to) {
    PRE_READ(to, __sanitizer::internal_strlen(to) + 1);
  }
}
POST_SYSCALL(rename)(long long res, void *from_, void *to_) {
  if (res == 0) {
    const char *from = (const char *)from_;
    const char *to = (const char *)to_;
    if (from) {
      POST_READ(from, __sanitizer::internal_strlen(from) + 1);
    }
    if (to) {
      POST_READ(to, __sanitizer::internal_strlen(to) + 1);
    }
  }
}
PRE_SYSCALL(compat_43_otruncate)(void *path_, long long length_) { /* TODO */ }
POST_SYSCALL(compat_43_otruncate)
(long long res, void *path_, long long length_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_oftruncate)(long long fd_, long long length_) {
  /* TODO */
}
POST_SYSCALL(compat_43_oftruncate)
(long long res, long long fd_, long long length_) {
  /* TODO */
}
PRE_SYSCALL(flock)(long long fd_, long long how_) { /* Nothing to do */ }
POST_SYSCALL(flock)(long long res, long long fd_, long long how_) {
  /* Nothing to do */
}
PRE_SYSCALL(mkfifo)(void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(mkfifo)(long long res, void *path_, long long mode_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(sendto)
(long long s_, void *buf_, long long len_, long long flags_, void *to_,
  long long tolen_) {
  PRE_READ(buf_, len_);
  PRE_READ(to_, tolen_);
}
POST_SYSCALL(sendto)
(long long res, long long s_, void *buf_, long long len_, long long flags_,
  void *to_, long long tolen_) {
  if (res >= 0) {
    POST_READ(buf_, len_);
    POST_READ(to_, tolen_);
  }
}
PRE_SYSCALL(shutdown)(long long s_, long long how_) { /* Nothing to do */ }
POST_SYSCALL(shutdown)(long long res, long long s_, long long how_) {
  /* Nothing to do */
}
PRE_SYSCALL(socketpair)
(long long domain_, long long type_, long long protocol_, void *rsv_) {
  PRE_WRITE(rsv_, 2 * sizeof(int));
}
POST_SYSCALL(socketpair)
(long long res, long long domain_, long long type_, long long protocol_,
  void *rsv_) {
  if (res == 0) {
    POST_WRITE(rsv_, 2 * sizeof(int));
  }
}
PRE_SYSCALL(mkdir)(void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(mkdir)(long long res, void *path_, long long mode_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(rmdir)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(rmdir)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(compat_50_utimes)(void *path_, void *tptr_) { /* TODO */ }
POST_SYSCALL(compat_50_utimes)(long long res, void *path_, void *tptr_) {
  /* TODO */
}
/* syscall 139 has been skipped */
PRE_SYSCALL(compat_50_adjtime)(void *delta_, void *olddelta_) { /* TODO */ }
POST_SYSCALL(compat_50_adjtime)(long long res, void *delta_, void *olddelta_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogetpeername)(long long fdes_, void *asa_, void *alen_) {
  /* TODO */
}
POST_SYSCALL(compat_43_ogetpeername)
(long long res, long long fdes_, void *asa_, void *alen_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogethostid)(void) { /* TODO */ }
POST_SYSCALL(compat_43_ogethostid)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_43_osethostid)(long long hostid_) { /* TODO */ }
POST_SYSCALL(compat_43_osethostid)(long long res, long long hostid_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_ogetrlimit)(long long which_, void *rlp_) { /* TODO */ }
POST_SYSCALL(compat_43_ogetrlimit)
(long long res, long long which_, void *rlp_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_osetrlimit)(long long which_, void *rlp_) { /* TODO */ }
POST_SYSCALL(compat_43_osetrlimit)
(long long res, long long which_, void *rlp_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_okillpg)(long long pgid_, long long signum_) {
  /* TODO */
}
POST_SYSCALL(compat_43_okillpg)
(long long res, long long pgid_, long long signum_) {
  /* TODO */
}
PRE_SYSCALL(setsid)(void) { /* Nothing to do */ }
POST_SYSCALL(setsid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(compat_50_quotactl)
(void *path_, long long cmd_, long long uid_, void *arg_) {
  /* TODO */
}
POST_SYSCALL(compat_50_quotactl)
(long long res, void *path_, long long cmd_, long long uid_, void *arg_) {
  /* TODO */
}
PRE_SYSCALL(compat_43_oquota)(void) { /* TODO */ }
POST_SYSCALL(compat_43_oquota)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_43_ogetsockname)(long long fdec_, void *asa_, void *alen_) {
  /* TODO */
}
POST_SYSCALL(compat_43_ogetsockname)
(long long res, long long fdec_, void *asa_, void *alen_) {
  /* TODO */
}
/* syscall 151 has been skipped */
/* syscall 152 has been skipped */
/* syscall 153 has been skipped */
/* syscall 154 has been skipped */
PRE_SYSCALL(nfssvc)(long long flag_, void *argp_) { /* Nothing to do */ }
POST_SYSCALL(nfssvc)(long long res, long long flag_, void *argp_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_43_ogetdirentries)
(long long fd_, void *buf_, long long count_, void *basep_) {
  /* TODO */
}
POST_SYSCALL(compat_43_ogetdirentries)
(long long res, long long fd_, void *buf_, long long count_, void *basep_) {
  /* TODO */
}
PRE_SYSCALL(compat_20_statfs)(void *path_, void *buf_) { /* TODO */ }
POST_SYSCALL(compat_20_statfs)(long long res, void *path_, void *buf_) {
  /* TODO */
}
PRE_SYSCALL(compat_20_fstatfs)(long long fd_, void *buf_) { /* TODO */ }
POST_SYSCALL(compat_20_fstatfs)(long long res, long long fd_, void *buf_) {
  /* TODO */
}
/* syscall 159 has been skipped */
/* syscall 160 has been skipped */
PRE_SYSCALL(compat_30_getfh)(void *fname_, void *fhp_) { /* TODO */ }
POST_SYSCALL(compat_30_getfh)(long long res, void *fname_, void *fhp_) {
  /* TODO */
}
PRE_SYSCALL(compat_09_ogetdomainname)(void *domainname_, long long len_) {
  /* TODO */
}
POST_SYSCALL(compat_09_ogetdomainname)
(long long res, void *domainname_, long long len_) {
  /* TODO */
}
PRE_SYSCALL(compat_09_osetdomainname)(void *domainname_, long long len_) {
  /* TODO */
}
POST_SYSCALL(compat_09_osetdomainname)
(long long res, void *domainname_, long long len_) {
  /* TODO */
}
PRE_SYSCALL(compat_09_ouname)(void *name_) { /* TODO */ }
POST_SYSCALL(compat_09_ouname)(long long res, void *name_) { /* TODO */ }
PRE_SYSCALL(sysarch)(long long op_, void *parms_) { /* TODO */ }
POST_SYSCALL(sysarch)(long long res, long long op_, void *parms_) { /* TODO */ }
PRE_SYSCALL(__futex)
(void *uaddr_, long long op_, long long val_, void *timeout_, void *uaddr2_,
  long long val2_, long long val3_) {
  /* TODO */
}
POST_SYSCALL(__futex)
(long long res, void *uaddr_, long long op_, long long val_, void *timeout_,
  void *uaddr2_, long long val2_, long long val3_) {
  /* TODO */
}
PRE_SYSCALL(__futex_set_robust_list)(void *head_, long long len_) { /* TODO */ }
POST_SYSCALL(__futex_set_robust_list)
(long long res, void *head_, long long len_) {
  /* TODO */
}
PRE_SYSCALL(__futex_get_robust_list)
(long long lwpid_, void **headp_, void *lenp_) {
  /* TODO */
}
POST_SYSCALL(__futex_get_robust_list)
(long long res, long long lwpid_, void **headp_, void *lenp_) {
  /* TODO */
}
#if !defined(_LP64)
PRE_SYSCALL(compat_10_osemsys)
(long long which_, long long a2_, long long a3_, long long a4_, long long a5_) {
  /* TODO */
}
POST_SYSCALL(compat_10_osemsys)
(long long res, long long which_, long long a2_, long long a3_, long long a4_,
  long long a5_) {
  /* TODO */
}
#else
/* syscall 169 has been skipped */
#endif
#if !defined(_LP64)
PRE_SYSCALL(compat_10_omsgsys)
(long long which_, long long a2_, long long a3_, long long a4_, long long a5_,
  long long a6_) {
  /* TODO */
}
POST_SYSCALL(compat_10_omsgsys)
(long long res, long long which_, long long a2_, long long a3_, long long a4_,
  long long a5_, long long a6_) {
  /* TODO */
}
#else
/* syscall 170 has been skipped */
#endif
#if !defined(_LP64)
PRE_SYSCALL(compat_10_oshmsys)
(long long which_, long long a2_, long long a3_, long long a4_) {
  /* TODO */
}
POST_SYSCALL(compat_10_oshmsys)
(long long res, long long which_, long long a2_, long long a3_, long long a4_) {
  /* TODO */
}
#else
/* syscall 171 has been skipped */
#endif
/* syscall 172 has been skipped */
PRE_SYSCALL(pread)
(long long fd_, void *buf_, long long nbyte_, long long PAD_,
  long long offset_) {
  if (buf_) {
    PRE_WRITE(buf_, nbyte_);
  }
}
POST_SYSCALL(pread)
(long long res, long long fd_, void *buf_, long long nbyte_, long long PAD_,
  long long offset_) {
  if (res > 0) {
    POST_WRITE(buf_, res);
  }
}
PRE_SYSCALL(pwrite)
(long long fd_, void *buf_, long long nbyte_, long long PAD_,
  long long offset_) {
  if (buf_) {
    PRE_READ(buf_, nbyte_);
  }
}
POST_SYSCALL(pwrite)
(long long res, long long fd_, void *buf_, long long nbyte_, long long PAD_,
  long long offset_) {
  if (res > 0) {
    POST_READ(buf_, res);
  }
}
PRE_SYSCALL(compat_30_ntp_gettime)(void *ntvp_) { /* TODO */ }
POST_SYSCALL(compat_30_ntp_gettime)(long long res, void *ntvp_) { /* TODO */ }
#if defined(NTP) || !defined(_KERNEL_OPT)
PRE_SYSCALL(ntp_adjtime)(void *tp_) { /* Nothing to do */ }
POST_SYSCALL(ntp_adjtime)(long long res, void *tp_) { /* Nothing to do */ }
#else
/* syscall 176 has been skipped */
#endif
/* syscall 177 has been skipped */
/* syscall 178 has been skipped */
/* syscall 179 has been skipped */
/* syscall 180 has been skipped */
PRE_SYSCALL(setgid)(long long gid_) { /* Nothing to do */ }
POST_SYSCALL(setgid)(long long res, long long gid_) { /* Nothing to do */ }
PRE_SYSCALL(setegid)(long long egid_) { /* Nothing to do */ }
POST_SYSCALL(setegid)(long long res, long long egid_) { /* Nothing to do */ }
PRE_SYSCALL(seteuid)(long long euid_) { /* Nothing to do */ }
POST_SYSCALL(seteuid)(long long res, long long euid_) { /* Nothing to do */ }
PRE_SYSCALL(lfs_bmapv)(void *fsidp_, void *blkiov_, long long blkcnt_) {
  /* TODO */
}
POST_SYSCALL(lfs_bmapv)
(long long res, void *fsidp_, void *blkiov_, long long blkcnt_) {
  /* TODO */
}
PRE_SYSCALL(lfs_markv)(void *fsidp_, void *blkiov_, long long blkcnt_) {
  /* TODO */
}
POST_SYSCALL(lfs_markv)
(long long res, void *fsidp_, void *blkiov_, long long blkcnt_) {
  /* TODO */
}
PRE_SYSCALL(lfs_segclean)(void *fsidp_, long long segment_) { /* TODO */ }
POST_SYSCALL(lfs_segclean)(long long res, void *fsidp_, long long segment_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_lfs_segwait)(void *fsidp_, void *tv_) { /* TODO */ }
POST_SYSCALL(compat_50_lfs_segwait)(long long res, void *fsidp_, void *tv_) {
  /* TODO */
}
PRE_SYSCALL(compat_12_stat12)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_12_stat12)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(compat_12_fstat12)(long long fd_, void *sb_) { /* TODO */ }
POST_SYSCALL(compat_12_fstat12)(long long res, long long fd_, void *sb_) {
  /* TODO */
}
PRE_SYSCALL(compat_12_lstat12)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_12_lstat12)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(pathconf)(void *path_, long long name_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(pathconf)(long long res, void *path_, long long name_) {
  if (res != -1) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(fpathconf)(long long fd_, long long name_) { /* Nothing to do */ }
POST_SYSCALL(fpathconf)(long long res, long long fd_, long long name_) {
  /* Nothing to do */
}
PRE_SYSCALL(getsockopt2)
(long long s_, long long level_, long long name_, void *val_, void *avalsize_) {
  /* TODO */
}
POST_SYSCALL(getsockopt2)
(long long res, long long s_, long long level_, long long name_, void *val_,
  void *avalsize_) {
  /* TODO */
}
PRE_SYSCALL(getrlimit)(long long which_, void *rlp_) {
  PRE_WRITE(rlp_, struct_rlimit_sz);
}
POST_SYSCALL(getrlimit)(long long res, long long which_, void *rlp_) {
  if (res == 0) {
    POST_WRITE(rlp_, struct_rlimit_sz);
  }
}
PRE_SYSCALL(setrlimit)(long long which_, void *rlp_) {
  PRE_READ(rlp_, struct_rlimit_sz);
}
POST_SYSCALL(setrlimit)(long long res, long long which_, void *rlp_) {
  if (res == 0) {
    POST_READ(rlp_, struct_rlimit_sz);
  }
}
PRE_SYSCALL(compat_12_getdirentries)
(long long fd_, void *buf_, long long count_, void *basep_) {
  /* TODO */
}
POST_SYSCALL(compat_12_getdirentries)
(long long res, long long fd_, void *buf_, long long count_, void *basep_) {
  /* TODO */
}
PRE_SYSCALL(mmap)
(void *addr_, long long len_, long long prot_, long long flags_, long long fd_,
  long long PAD_, long long pos_) {
  /* Nothing to do */
}
POST_SYSCALL(mmap)
(long long res, void *addr_, long long len_, long long prot_, long long flags_,
  long long fd_, long long PAD_, long long pos_) {
  /* Nothing to do */
}
PRE_SYSCALL(__syscall)(long long code_, long long args_[SYS_MAXSYSARGS]) {
  /* Nothing to do */
}
POST_SYSCALL(__syscall)
(long long res, long long code_, long long args_[SYS_MAXSYSARGS]) {
  /* Nothing to do */
}
PRE_SYSCALL(lseek)
(long long fd_, long long PAD_, long long offset_, long long whence_) {
  /* Nothing to do */
}
POST_SYSCALL(lseek)
(long long res, long long fd_, long long PAD_, long long offset_,
  long long whence_) {
  /* Nothing to do */
}
PRE_SYSCALL(truncate)(void *path_, long long PAD_, long long length_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(truncate)
(long long res, void *path_, long long PAD_, long long length_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(ftruncate)(long long fd_, long long PAD_, long long length_) {
  /* Nothing to do */
}
POST_SYSCALL(ftruncate)
(long long res, long long fd_, long long PAD_, long long length_) {
  /* Nothing to do */
}
PRE_SYSCALL(__sysctl)
(void *name_, long long namelen_, void *oldv_, void *oldlenp_, void *newv_,
  long long newlen_) {
  const int *name = (const int *)name_;
  if (name) {
    PRE_READ(name, namelen_ * sizeof(*name));
  }
  if (newv_) {
    PRE_READ(name, newlen_);
  }
}
POST_SYSCALL(__sysctl)
(long long res, void *name_, long long namelen_, void *oldv_, void *oldlenp_,
  void *newv_, long long newlen_) {
  if (res == 0) {
    const int *name = (const int *)name_;
    if (name) {
      POST_READ(name, namelen_ * sizeof(*name));
    }
    if (newv_) {
      POST_READ(name, newlen_);
    }
  }
}
PRE_SYSCALL(mlock)(void *addr_, long long len_) { /* Nothing to do */ }
POST_SYSCALL(mlock)(long long res, void *addr_, long long len_) {
  /* Nothing to do */
}
PRE_SYSCALL(munlock)(void *addr_, long long len_) { /* Nothing to do */ }
POST_SYSCALL(munlock)(long long res, void *addr_, long long len_) {
  /* Nothing to do */
}
PRE_SYSCALL(undelete)(void *path_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(undelete)(long long res, void *path_) {
  if (res == 0) {
    const char *path = (const char *)path_;
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(compat_50_futimes)(long long fd_, void *tptr_) { /* TODO */ }
POST_SYSCALL(compat_50_futimes)(long long res, long long fd_, void *tptr_) {
  /* TODO */
}
PRE_SYSCALL(getpgid)(long long pid_) { /* Nothing to do */ }
POST_SYSCALL(getpgid)(long long res, long long pid_) { /* Nothing to do */ }
PRE_SYSCALL(reboot)(long long opt_, void *bootstr_) {
  const char *bootstr = (const char *)bootstr_;
  if (bootstr) {
    PRE_READ(bootstr, __sanitizer::internal_strlen(bootstr) + 1);
  }
}
POST_SYSCALL(reboot)(long long res, long long opt_, void *bootstr_) {
  /* This call should never return */
  const char *bootstr = (const char *)bootstr_;
  if (bootstr) {
    POST_READ(bootstr, __sanitizer::internal_strlen(bootstr) + 1);
  }
}
PRE_SYSCALL(poll)(void *fds_, long long nfds_, long long timeout_) {
  /* Nothing to do */
}
POST_SYSCALL(poll)
(long long res, void *fds_, long long nfds_, long long timeout_) {
  /* Nothing to do */
}
PRE_SYSCALL(afssys)
(long long id_, long long a1_, long long a2_, long long a3_, long long a4_,
  long long a5_, long long a6_) {
  /* TODO */
}
POST_SYSCALL(afssys)
(long long res, long long id_, long long a1_, long long a2_, long long a3_,
  long long a4_, long long a5_, long long a6_) {
  /* TODO */
}
/* syscall 211 has been skipped */
/* syscall 212 has been skipped */
/* syscall 213 has been skipped */
/* syscall 214 has been skipped */
/* syscall 215 has been skipped */
/* syscall 216 has been skipped */
/* syscall 217 has been skipped */
/* syscall 218 has been skipped */
/* syscall 219 has been skipped */
PRE_SYSCALL(compat_14___semctl)
(long long semid_, long long semnum_, long long cmd_, void *arg_) {
  /* TODO */
}
POST_SYSCALL(compat_14___semctl)
(long long res, long long semid_, long long semnum_, long long cmd_,
  void *arg_) {
  /* TODO */
}
PRE_SYSCALL(semget)(long long key_, long long nsems_, long long semflg_) {
  /* Nothing to do */
}
POST_SYSCALL(semget)
(long long res, long long key_, long long nsems_, long long semflg_) {
  /* Nothing to do */
}
PRE_SYSCALL(semop)(long long semid_, void *sops_, long long nsops_) {
  if (sops_) {
    PRE_READ(sops_, nsops_ * struct_sembuf_sz);
  }
}
POST_SYSCALL(semop)
(long long res, long long semid_, void *sops_, long long nsops_) {
  if (res == 0) {
    if (sops_) {
      POST_READ(sops_, nsops_ * struct_sembuf_sz);
    }
  }
}
PRE_SYSCALL(semconfig)(long long flag_) { /* Nothing to do */ }
POST_SYSCALL(semconfig)(long long res, long long flag_) { /* Nothing to do */ }
PRE_SYSCALL(compat_14_msgctl)(long long msqid_, long long cmd_, void *buf_) {
  /* TODO */
}
POST_SYSCALL(compat_14_msgctl)
(long long res, long long msqid_, long long cmd_, void *buf_) {
  /* TODO */
}
PRE_SYSCALL(msgget)(long long key_, long long msgflg_) { /* Nothing to do */ }
POST_SYSCALL(msgget)(long long res, long long key_, long long msgflg_) {
  /* Nothing to do */
}
PRE_SYSCALL(msgsnd)
(long long msqid_, void *msgp_, long long msgsz_, long long msgflg_) {
  if (msgp_) {
    PRE_READ(msgp_, msgsz_);
  }
}
POST_SYSCALL(msgsnd)
(long long res, long long msqid_, void *msgp_, long long msgsz_,
  long long msgflg_) {
  if (res == 0) {
    if (msgp_) {
      POST_READ(msgp_, msgsz_);
    }
  }
}
PRE_SYSCALL(msgrcv)
(long long msqid_, void *msgp_, long long msgsz_, long long msgtyp_,
  long long msgflg_) {
  /* Nothing to do */
}
POST_SYSCALL(msgrcv)
(long long res, long long msqid_, void *msgp_, long long msgsz_,
  long long msgtyp_, long long msgflg_) {
  /* Nothing to do */
}
PRE_SYSCALL(shmat)(long long shmid_, void *shmaddr_, long long shmflg_) {
  /* Nothing to do */
}
POST_SYSCALL(shmat)
(long long res, long long shmid_, void *shmaddr_, long long shmflg_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_14_shmctl)(long long shmid_, long long cmd_, void *buf_) {
  /* TODO */
}
POST_SYSCALL(compat_14_shmctl)
(long long res, long long shmid_, long long cmd_, void *buf_) {
  /* TODO */
}
PRE_SYSCALL(shmdt)(void *shmaddr_) { /* Nothing to do */ }
POST_SYSCALL(shmdt)(long long res, void *shmaddr_) { /* Nothing to do */ }
PRE_SYSCALL(shmget)(long long key_, long long size_, long long shmflg_) {
  /* Nothing to do */
}
POST_SYSCALL(shmget)
(long long res, long long key_, long long size_, long long shmflg_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50_clock_gettime)(long long clock_id_, void *tp_) {
  /* TODO */
}
POST_SYSCALL(compat_50_clock_gettime)
(long long res, long long clock_id_, void *tp_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_clock_settime)(long long clock_id_, void *tp_) {
  /* TODO */
}
POST_SYSCALL(compat_50_clock_settime)
(long long res, long long clock_id_, void *tp_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_clock_getres)(long long clock_id_, void *tp_) {
  /* TODO */
}
POST_SYSCALL(compat_50_clock_getres)
(long long res, long long clock_id_, void *tp_) {
  /* TODO */
}
PRE_SYSCALL(timer_create)(long long clock_id_, void *evp_, void *timerid_) {
  /* Nothing to do */
}
POST_SYSCALL(timer_create)
(long long res, long long clock_id_, void *evp_, void *timerid_) {
  /* Nothing to do */
}
PRE_SYSCALL(timer_delete)(long long timerid_) { /* Nothing to do */ }
POST_SYSCALL(timer_delete)(long long res, long long timerid_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50_timer_settime)
(long long timerid_, long long flags_, void *value_, void *ovalue_) {
  /* TODO */
}
POST_SYSCALL(compat_50_timer_settime)
(long long res, long long timerid_, long long flags_, void *value_,
  void *ovalue_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_timer_gettime)(long long timerid_, void *value_) {
  /* TODO */
}
POST_SYSCALL(compat_50_timer_gettime)
(long long res, long long timerid_, void *value_) {
  /* TODO */
}
PRE_SYSCALL(timer_getoverrun)(long long timerid_) { /* Nothing to do */ }
POST_SYSCALL(timer_getoverrun)(long long res, long long timerid_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50_nanosleep)(void *rqtp_, void *rmtp_) { /* TODO */ }
POST_SYSCALL(compat_50_nanosleep)(long long res, void *rqtp_, void *rmtp_) {
  /* TODO */
}
PRE_SYSCALL(fdatasync)(long long fd_) { /* Nothing to do */ }
POST_SYSCALL(fdatasync)(long long res, long long fd_) { /* Nothing to do */ }
PRE_SYSCALL(mlockall)(long long flags_) { /* Nothing to do */ }
POST_SYSCALL(mlockall)(long long res, long long flags_) { /* Nothing to do */ }
PRE_SYSCALL(munlockall)(void) { /* Nothing to do */ }
POST_SYSCALL(munlockall)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(compat_50___sigtimedwait)(void *set_, void *info_, void *timeout_) {
  /* TODO */
}
POST_SYSCALL(compat_50___sigtimedwait)
(long long res, void *set_, void *info_, void *timeout_) {
  /* TODO */
}
PRE_SYSCALL(sigqueueinfo)(long long pid_, void *info_) {
  if (info_) {
    PRE_READ(info_, siginfo_t_sz);
  }
}
POST_SYSCALL(sigqueueinfo)(long long res, long long pid_, void *info_) {}
PRE_SYSCALL(modctl)(long long cmd_, void *arg_) { /* TODO */ }
POST_SYSCALL(modctl)(long long res, long long cmd_, void *arg_) { /* TODO */ }
PRE_SYSCALL(_ksem_init)(long long value_, void *idp_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_init)(long long res, long long value_, void *idp_) {
  /* Nothing to do */
}
PRE_SYSCALL(_ksem_open)
(void *name_, long long oflag_, long long mode_, long long value_, void *idp_) {
  const char *name = (const char *)name_;
  if (name) {
    PRE_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
POST_SYSCALL(_ksem_open)
(long long res, void *name_, long long oflag_, long long mode_,
  long long value_, void *idp_) {
  const char *name = (const char *)name_;
  if (name) {
    POST_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
PRE_SYSCALL(_ksem_unlink)(void *name_) {
  const char *name = (const char *)name_;
  if (name) {
    PRE_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
POST_SYSCALL(_ksem_unlink)(long long res, void *name_) {
  const char *name = (const char *)name_;
  if (name) {
    POST_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
PRE_SYSCALL(_ksem_close)(long long id_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_close)(long long res, long long id_) { /* Nothing to do */ }
PRE_SYSCALL(_ksem_post)(long long id_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_post)(long long res, long long id_) { /* Nothing to do */ }
PRE_SYSCALL(_ksem_wait)(long long id_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_wait)(long long res, long long id_) { /* Nothing to do */ }
PRE_SYSCALL(_ksem_trywait)(long long id_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_trywait)(long long res, long long id_) {
  /* Nothing to do */
}
PRE_SYSCALL(_ksem_getvalue)(long long id_, void *value_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_getvalue)(long long res, long long id_, void *value_) {
  /* Nothing to do */
}
PRE_SYSCALL(_ksem_destroy)(long long id_) { /* Nothing to do */ }
POST_SYSCALL(_ksem_destroy)(long long res, long long id_) {
  /* Nothing to do */
}
PRE_SYSCALL(_ksem_timedwait)(long long id_, void *abstime_) {
  if (abstime_) {
    PRE_READ(abstime_, struct_timespec_sz);
  }
}
POST_SYSCALL(_ksem_timedwait)(long long res, long long id_, void *abstime_) {}
PRE_SYSCALL(mq_open)
(void *name_, long long oflag_, long long mode_, void *attr_) {
  const char *name = (const char *)name_;
  if (name) {
    PRE_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
POST_SYSCALL(mq_open)
(long long res, void *name_, long long oflag_, long long mode_, void *attr_) {
  const char *name = (const char *)name_;
  if (name) {
    POST_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
PRE_SYSCALL(mq_close)(long long mqdes_) { /* Nothing to do */ }
POST_SYSCALL(mq_close)(long long res, long long mqdes_) { /* Nothing to do */ }
PRE_SYSCALL(mq_unlink)(void *name_) {
  const char *name = (const char *)name_;
  if (name) {
    PRE_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
POST_SYSCALL(mq_unlink)(long long res, void *name_) {
  const char *name = (const char *)name_;
  if (name) {
    POST_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
PRE_SYSCALL(mq_getattr)(long long mqdes_, void *mqstat_) { /* Nothing to do */ }
POST_SYSCALL(mq_getattr)(long long res, long long mqdes_, void *mqstat_) {
  /* Nothing to do */
}
PRE_SYSCALL(mq_setattr)(long long mqdes_, void *mqstat_, void *omqstat_) {
  if (mqstat_) {
    PRE_READ(mqstat_, struct_mq_attr_sz);
  }
}
POST_SYSCALL(mq_setattr)
(long long res, long long mqdes_, void *mqstat_, void *omqstat_) {}
PRE_SYSCALL(mq_notify)(long long mqdes_, void *notification_) {
  if (notification_) {
    PRE_READ(notification_, struct_sigevent_sz);
  }
}
POST_SYSCALL(mq_notify)(long long res, long long mqdes_, void *notification_) {}
PRE_SYSCALL(mq_send)
(long long mqdes_, void *msg_ptr_, long long msg_len_, long long msg_prio_) {
  if (msg_ptr_) {
    PRE_READ(msg_ptr_, msg_len_);
  }
}
POST_SYSCALL(mq_send)
(long long res, long long mqdes_, void *msg_ptr_, long long msg_len_,
  long long msg_prio_) {}
PRE_SYSCALL(mq_receive)
(long long mqdes_, void *msg_ptr_, long long msg_len_, void *msg_prio_) {
  /* Nothing to do */
}
POST_SYSCALL(mq_receive)
(long long res, long long mqdes_, void *msg_ptr_, long long msg_len_,
  void *msg_prio_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50_mq_timedsend)
(long long mqdes_, void *msg_ptr_, long long msg_len_, long long msg_prio_,
  void *abs_timeout_) {
  /* TODO */
}
POST_SYSCALL(compat_50_mq_timedsend)
(long long res, long long mqdes_, void *msg_ptr_, long long msg_len_,
  long long msg_prio_, void *abs_timeout_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_mq_timedreceive)
(long long mqdes_, void *msg_ptr_, long long msg_len_, void *msg_prio_,
  void *abs_timeout_) {
  /* TODO */
}
POST_SYSCALL(compat_50_mq_timedreceive)
(long long res, long long mqdes_, void *msg_ptr_, long long msg_len_,
  void *msg_prio_, void *abs_timeout_) {
  /* TODO */
}
/* syscall 267 has been skipped */
/* syscall 268 has been skipped */
/* syscall 269 has been skipped */
PRE_SYSCALL(__posix_rename)(void *from_, void *to_) {
  const char *from = (const char *)from_;
  const char *to = (const char *)to_;
  if (from_) {
    PRE_READ(from, __sanitizer::internal_strlen(from) + 1);
  }
  if (to) {
    PRE_READ(to, __sanitizer::internal_strlen(to) + 1);
  }
}
POST_SYSCALL(__posix_rename)(long long res, void *from_, void *to_) {
  const char *from = (const char *)from_;
  const char *to = (const char *)to_;
  if (from) {
    POST_READ(from, __sanitizer::internal_strlen(from) + 1);
  }
  if (to) {
    POST_READ(to, __sanitizer::internal_strlen(to) + 1);
  }
}
PRE_SYSCALL(swapctl)(long long cmd_, void *arg_, long long misc_) { /* TODO */ }
POST_SYSCALL(swapctl)
(long long res, long long cmd_, void *arg_, long long misc_) {
  /* TODO */
}
PRE_SYSCALL(compat_30_getdents)(long long fd_, void *buf_, long long count_) {
  /* TODO */
}
POST_SYSCALL(compat_30_getdents)
(long long res, long long fd_, void *buf_, long long count_) {
  /* TODO */
}
PRE_SYSCALL(minherit)(void *addr_, long long len_, long long inherit_) {
  /* Nothing to do */
}
POST_SYSCALL(minherit)
(long long res, void *addr_, long long len_, long long inherit_) {
  /* Nothing to do */
}
PRE_SYSCALL(lchmod)(void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(lchmod)(long long res, void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(lchown)(void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(lchown)
(long long res, void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(compat_50_lutimes)(void *path_, void *tptr_) { /* TODO */ }
POST_SYSCALL(compat_50_lutimes)(long long res, void *path_, void *tptr_) {
  /* TODO */
}
PRE_SYSCALL(__msync13)(void *addr_, long long len_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(__msync13)
(long long res, void *addr_, long long len_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_30___stat13)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_30___stat13)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(compat_30___fstat13)(long long fd_, void *sb_) { /* TODO */ }
POST_SYSCALL(compat_30___fstat13)(long long res, long long fd_, void *sb_) {
  /* TODO */
}
PRE_SYSCALL(compat_30___lstat13)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_30___lstat13)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(__sigaltstack14)(void *nss_, void *oss_) {
  if (nss_) {
    PRE_READ(nss_, struct_sigaltstack_sz);
  }
  if (oss_) {
    PRE_READ(oss_, struct_sigaltstack_sz);
  }
}
POST_SYSCALL(__sigaltstack14)(long long res, void *nss_, void *oss_) {}
PRE_SYSCALL(__vfork14)(void) { /* Nothing to do */ }
POST_SYSCALL(__vfork14)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(__posix_chown)(void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__posix_chown)
(long long res, void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(__posix_fchown)(long long fd_, long long uid_, long long gid_) {
  /* Nothing to do */
}
POST_SYSCALL(__posix_fchown)
(long long res, long long fd_, long long uid_, long long gid_) {
  /* Nothing to do */
}
PRE_SYSCALL(__posix_lchown)(void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__posix_lchown)
(long long res, void *path_, long long uid_, long long gid_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(getsid)(long long pid_) { /* Nothing to do */ }
POST_SYSCALL(getsid)(long long res, long long pid_) { /* Nothing to do */ }
PRE_SYSCALL(__clone)(long long flags_, void *stack_) { /* Nothing to do */ }
POST_SYSCALL(__clone)(long long res, long long flags_, void *stack_) {
  /* Nothing to do */
}
PRE_SYSCALL(fktrace)
(long long fd_, long long ops_, long long facs_, long long pid_) {
  /* Nothing to do */
}
POST_SYSCALL(fktrace)
(long long res, long long fd_, long long ops_, long long facs_,
  long long pid_) {
  /* Nothing to do */
}
PRE_SYSCALL(preadv)
(long long fd_, void *iovp_, long long iovcnt_, long long PAD_,
  long long offset_) {
  /* Nothing to do */
}
POST_SYSCALL(preadv)
(long long res, long long fd_, void *iovp_, long long iovcnt_, long long PAD_,
  long long offset_) {
  /* Nothing to do */
}
PRE_SYSCALL(pwritev)
(long long fd_, void *iovp_, long long iovcnt_, long long PAD_,
  long long offset_) {
  /* Nothing to do */
}
POST_SYSCALL(pwritev)
(long long res, long long fd_, void *iovp_, long long iovcnt_, long long PAD_,
  long long offset_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_16___sigaction14)
(long long signum_, void *nsa_, void *osa_) {
  /* TODO */
}
POST_SYSCALL(compat_16___sigaction14)
(long long res, long long signum_, void *nsa_, void *osa_) {
  /* TODO */
}
PRE_SYSCALL(__sigpending14)(void *set_) { /* Nothing to do */ }
POST_SYSCALL(__sigpending14)(long long res, void *set_) { /* Nothing to do */ }
PRE_SYSCALL(__sigprocmask14)(long long how_, void *set_, void *oset_) {
  /* Nothing to do */
}
POST_SYSCALL(__sigprocmask14)
(long long res, long long how_, void *set_, void *oset_) {
  /* Nothing to do */
}
PRE_SYSCALL(__sigsuspend14)(void *set_) {
  if (set_) {
    PRE_READ(set_, sizeof(__sanitizer_sigset_t));
  }
}
POST_SYSCALL(__sigsuspend14)(long long res, void *set_) {
  if (set_) {
    PRE_READ(set_, sizeof(__sanitizer_sigset_t));
  }
}
PRE_SYSCALL(compat_16___sigreturn14)(void *sigcntxp_) { /* TODO */ }
POST_SYSCALL(compat_16___sigreturn14)(long long res, void *sigcntxp_) {
  /* TODO */
}
PRE_SYSCALL(__getcwd)(void *bufp_, long long length_) { /* Nothing to do */ }
POST_SYSCALL(__getcwd)(long long res, void *bufp_, long long length_) {
  /* Nothing to do */
}
PRE_SYSCALL(fchroot)(long long fd_) { /* Nothing to do */ }
POST_SYSCALL(fchroot)(long long res, long long fd_) { /* Nothing to do */ }
PRE_SYSCALL(compat_30_fhopen)(void *fhp_, long long flags_) { /* TODO */ }
POST_SYSCALL(compat_30_fhopen)(long long res, void *fhp_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(compat_30_fhstat)(void *fhp_, void *sb_) { /* TODO */ }
POST_SYSCALL(compat_30_fhstat)(long long res, void *fhp_, void *sb_) {
  /* TODO */
}
PRE_SYSCALL(compat_20_fhstatfs)(void *fhp_, void *buf_) { /* TODO */ }
POST_SYSCALL(compat_20_fhstatfs)(long long res, void *fhp_, void *buf_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_____semctl13)
(long long semid_, long long semnum_, long long cmd_, void *arg_) {
  /* TODO */
}
POST_SYSCALL(compat_50_____semctl13)
(long long res, long long semid_, long long semnum_, long long cmd_,
  void *arg_) {
  /* TODO */
}
PRE_SYSCALL(compat_50___msgctl13)
(long long msqid_, long long cmd_, void *buf_) {
  /* TODO */
}
POST_SYSCALL(compat_50___msgctl13)
(long long res, long long msqid_, long long cmd_, void *buf_) {
  /* TODO */
}
PRE_SYSCALL(compat_50___shmctl13)
(long long shmid_, long long cmd_, void *buf_) {
  /* TODO */
}
POST_SYSCALL(compat_50___shmctl13)
(long long res, long long shmid_, long long cmd_, void *buf_) {
  /* TODO */
}
PRE_SYSCALL(lchflags)(void *path_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(lchflags)(long long res, void *path_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(issetugid)(void) { /* Nothing to do */ }
POST_SYSCALL(issetugid)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(utrace)(void *label_, void *addr_, long long len_) {
  const char *label = (const char *)label_;
  if (label) {
    PRE_READ(label, __sanitizer::internal_strlen(label) + 1);
  }
  if (addr_) {
    PRE_READ(addr_, len_);
  }
}
POST_SYSCALL(utrace)(long long res, void *label_, void *addr_, long long len_) {
  const char *label = (const char *)label_;
  if (label) {
    POST_READ(label, __sanitizer::internal_strlen(label) + 1);
  }
  if (addr_) {
    POST_READ(addr_, len_);
  }
}
PRE_SYSCALL(getcontext)(void *ucp_) { /* Nothing to do */ }
POST_SYSCALL(getcontext)(long long res, void *ucp_) { /* Nothing to do */ }
PRE_SYSCALL(setcontext)(void *ucp_) {
  if (ucp_) {
    PRE_READ(ucp_, ucontext_t_sz(ucp_));
  }
}
POST_SYSCALL(setcontext)(long long res, void *ucp_) {}
PRE_SYSCALL(_lwp_create)(void *ucp_, long long flags_, void *new_lwp_) {
  if (ucp_) {
    PRE_READ(ucp_, ucontext_t_sz(ucp_));
  }
}
POST_SYSCALL(_lwp_create)
(long long res, void *ucp_, long long flags_, void *new_lwp_) {}
PRE_SYSCALL(_lwp_exit)(void) { /* Nothing to do */ }
POST_SYSCALL(_lwp_exit)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(_lwp_self)(void) { /* Nothing to do */ }
POST_SYSCALL(_lwp_self)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(_lwp_wait)(long long wait_for_, void *departed_) {
  /* Nothing to do */
}
POST_SYSCALL(_lwp_wait)(long long res, long long wait_for_, void *departed_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_suspend)(long long target_) { /* Nothing to do */ }
POST_SYSCALL(_lwp_suspend)(long long res, long long target_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_continue)(long long target_) { /* Nothing to do */ }
POST_SYSCALL(_lwp_continue)(long long res, long long target_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_wakeup)(long long target_) { /* Nothing to do */ }
POST_SYSCALL(_lwp_wakeup)(long long res, long long target_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_getprivate)(void) { /* Nothing to do */ }
POST_SYSCALL(_lwp_getprivate)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(_lwp_setprivate)(void *ptr_) { /* Nothing to do */ }
POST_SYSCALL(_lwp_setprivate)(long long res, void *ptr_) { /* Nothing to do */ }
PRE_SYSCALL(_lwp_kill)(long long target_, long long signo_) {
  /* Nothing to do */
}
POST_SYSCALL(_lwp_kill)(long long res, long long target_, long long signo_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_detach)(long long target_) { /* Nothing to do */ }
POST_SYSCALL(_lwp_detach)(long long res, long long target_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_50__lwp_park)
(void *ts_, long long unpark_, void *hint_, void *unparkhint_) {
  /* TODO */
}
POST_SYSCALL(compat_50__lwp_park)
(long long res, void *ts_, long long unpark_, void *hint_, void *unparkhint_) {
  /* TODO */
}
PRE_SYSCALL(_lwp_unpark)(long long target_, void *hint_) { /* Nothing to do */ }
POST_SYSCALL(_lwp_unpark)(long long res, long long target_, void *hint_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_unpark_all)(void *targets_, long long ntargets_, void *hint_) {
  if (targets_) {
    PRE_READ(targets_, ntargets_ * sizeof(__sanitizer_lwpid_t));
  }
}
POST_SYSCALL(_lwp_unpark_all)
(long long res, void *targets_, long long ntargets_, void *hint_) {}
PRE_SYSCALL(_lwp_setname)(long long target_, void *name_) {
  const char *name = (const char *)name_;
  if (name) {
    PRE_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
POST_SYSCALL(_lwp_setname)(long long res, long long target_, void *name_) {
  const char *name = (const char *)name_;
  if (name) {
    POST_READ(name, __sanitizer::internal_strlen(name) + 1);
  }
}
PRE_SYSCALL(_lwp_getname)(long long target_, void *name_, long long len_) {
  /* Nothing to do */
}
POST_SYSCALL(_lwp_getname)
(long long res, long long target_, void *name_, long long len_) {
  /* Nothing to do */
}
PRE_SYSCALL(_lwp_ctl)(long long features_, void **address_) {
  /* Nothing to do */
}
POST_SYSCALL(_lwp_ctl)(long long res, long long features_, void **address_) {
  /* Nothing to do */
}
/* syscall 326 has been skipped */
/* syscall 327 has been skipped */
/* syscall 328 has been skipped */
/* syscall 329 has been skipped */
PRE_SYSCALL(compat_60_sa_register)
(void *newv_, void **oldv_, long long flags_, long long stackinfo_offset_) {
  /* TODO */
}
POST_SYSCALL(compat_60_sa_register)
(long long res, void *newv_, void **oldv_, long long flags_,
  long long stackinfo_offset_) {
  /* TODO */
}
PRE_SYSCALL(compat_60_sa_stacks)(long long num_, void *stacks_) { /* TODO */ }
POST_SYSCALL(compat_60_sa_stacks)
(long long res, long long num_, void *stacks_) {
  /* TODO */
}
PRE_SYSCALL(compat_60_sa_enable)(void) { /* TODO */ }
POST_SYSCALL(compat_60_sa_enable)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_60_sa_setconcurrency)(long long concurrency_) { /* TODO */ }
POST_SYSCALL(compat_60_sa_setconcurrency)
(long long res, long long concurrency_) {
  /* TODO */
}
PRE_SYSCALL(compat_60_sa_yield)(void) { /* TODO */ }
POST_SYSCALL(compat_60_sa_yield)(long long res) { /* TODO */ }
PRE_SYSCALL(compat_60_sa_preempt)(long long sa_id_) { /* TODO */ }
POST_SYSCALL(compat_60_sa_preempt)(long long res, long long sa_id_) {
  /* TODO */
}
/* syscall 336 has been skipped */
/* syscall 337 has been skipped */
/* syscall 338 has been skipped */
/* syscall 339 has been skipped */
PRE_SYSCALL(__sigaction_sigtramp)
(long long signum_, void *nsa_, void *osa_, void *tramp_, long long vers_) {
  if (nsa_) {
    PRE_READ(nsa_, sizeof(__sanitizer_sigaction));
  }
}
POST_SYSCALL(__sigaction_sigtramp)
(long long res, long long signum_, void *nsa_, void *osa_, void *tramp_,
  long long vers_) {
  if (nsa_) {
    PRE_READ(nsa_, sizeof(__sanitizer_sigaction));
  }
}
/* syscall 341 has been skipped */
/* syscall 342 has been skipped */
PRE_SYSCALL(rasctl)(void *addr_, long long len_, long long op_) {
  /* Nothing to do */
}
POST_SYSCALL(rasctl)
(long long res, void *addr_, long long len_, long long op_) {
  /* Nothing to do */
}
PRE_SYSCALL(kqueue)(void) { /* Nothing to do */ }
POST_SYSCALL(kqueue)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(compat_50_kevent)
(long long fd_, void *changelist_, long long nchanges_, void *eventlist_,
  long long nevents_, void *timeout_) {
  /* TODO */
}
POST_SYSCALL(compat_50_kevent)
(long long res, long long fd_, void *changelist_, long long nchanges_,
  void *eventlist_, long long nevents_, void *timeout_) {
  /* TODO */
}
PRE_SYSCALL(_sched_setparam)
(long long pid_, long long lid_, long long policy_, void *params_) {
  if (params_) {
    PRE_READ(params_, struct_sched_param_sz);
  }
}
POST_SYSCALL(_sched_setparam)
(long long res, long long pid_, long long lid_, long long policy_,
  void *params_) {
  if (params_) {
    PRE_READ(params_, struct_sched_param_sz);
  }
}
PRE_SYSCALL(_sched_getparam)
(long long pid_, long long lid_, void *policy_, void *params_) {
  /* Nothing to do */
}
POST_SYSCALL(_sched_getparam)
(long long res, long long pid_, long long lid_, void *policy_, void *params_) {
  /* Nothing to do */
}
PRE_SYSCALL(_sched_setaffinity)
(long long pid_, long long lid_, long long size_, void *cpuset_) {
  if (cpuset_) {
    PRE_READ(cpuset_, size_);
  }
}
POST_SYSCALL(_sched_setaffinity)
(long long res, long long pid_, long long lid_, long long size_,
  void *cpuset_) {
  if (cpuset_) {
    PRE_READ(cpuset_, size_);
  }
}
PRE_SYSCALL(_sched_getaffinity)
(long long pid_, long long lid_, long long size_, void *cpuset_) {
  /* Nothing to do */
}
POST_SYSCALL(_sched_getaffinity)
(long long res, long long pid_, long long lid_, long long size_,
  void *cpuset_) {
  /* Nothing to do */
}
PRE_SYSCALL(sched_yield)(void) { /* Nothing to do */ }
POST_SYSCALL(sched_yield)(long long res) { /* Nothing to do */ }
PRE_SYSCALL(_sched_protect)(long long priority_) { /* Nothing to do */ }
POST_SYSCALL(_sched_protect)(long long res, long long priority_) {
  /* Nothing to do */
}
/* syscall 352 has been skipped */
/* syscall 353 has been skipped */
PRE_SYSCALL(fsync_range)
(long long fd_, long long flags_, long long start_, long long length_) {
  /* Nothing to do */
}
POST_SYSCALL(fsync_range)
(long long res, long long fd_, long long flags_, long long start_,
  long long length_) {
  /* Nothing to do */
}
PRE_SYSCALL(uuidgen)(void *store_, long long count_) { /* Nothing to do */ }
POST_SYSCALL(uuidgen)(long long res, void *store_, long long count_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_90_getvfsstat)
(void *buf_, long long bufsize_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(compat_90_getvfsstat)
(long long res, void *buf_, long long bufsize_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_90_statvfs1)(void *path_, void *buf_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(compat_90_statvfs1)
(long long res, void *path_, void *buf_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(compat_90_fstatvfs1)(long long fd_, void *buf_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(compat_90_fstatvfs1)
(long long res, long long fd_, void *buf_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(compat_30_fhstatvfs1)(void *fhp_, void *buf_, long long flags_) {
  /* TODO */
}
POST_SYSCALL(compat_30_fhstatvfs1)
(long long res, void *fhp_, void *buf_, long long flags_) {
  /* TODO */
}
PRE_SYSCALL(extattrctl)
(void *path_, long long cmd_, void *filename_, long long attrnamespace_,
  void *attrname_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattrctl)
(long long res, void *path_, long long cmd_, void *filename_,
  long long attrnamespace_, void *attrname_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_set_file)
(void *path_, long long attrnamespace_, void *attrname_, void *data_,
  long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_set_file)
(long long res, void *path_, long long attrnamespace_, void *attrname_,
  void *data_, long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_get_file)
(void *path_, long long attrnamespace_, void *attrname_, void *data_,
  long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_get_file)
(long long res, void *path_, long long attrnamespace_, void *attrname_,
  void *data_, long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_delete_file)
(void *path_, long long attrnamespace_, void *attrname_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_delete_file)
(long long res, void *path_, long long attrnamespace_, void *attrname_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_set_fd)
(long long fd_, long long attrnamespace_, void *attrname_, void *data_,
  long long nbytes_) {
  /* TODO */
}
POST_SYSCALL(extattr_set_fd)
(long long res, long long fd_, long long attrnamespace_, void *attrname_,
  void *data_, long long nbytes_) {
  /* TODO */
}
PRE_SYSCALL(extattr_get_fd)
(long long fd_, long long attrnamespace_, void *attrname_, void *data_,
  long long nbytes_) {
  /* TODO */
}
POST_SYSCALL(extattr_get_fd)
(long long res, long long fd_, long long attrnamespace_, void *attrname_,
  void *data_, long long nbytes_) {
  /* TODO */
}
PRE_SYSCALL(extattr_delete_fd)
(long long fd_, long long attrnamespace_, void *attrname_) {
  /* TODO */
}
POST_SYSCALL(extattr_delete_fd)
(long long res, long long fd_, long long attrnamespace_, void *attrname_) {
  /* TODO */
}
PRE_SYSCALL(extattr_set_link)
(void *path_, long long attrnamespace_, void *attrname_, void *data_,
  long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_set_link)
(long long res, void *path_, long long attrnamespace_, void *attrname_,
  void *data_, long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_get_link)
(void *path_, long long attrnamespace_, void *attrname_, void *data_,
  long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_get_link)
(long long res, void *path_, long long attrnamespace_, void *attrname_,
  void *data_, long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_delete_link)
(void *path_, long long attrnamespace_, void *attrname_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_delete_link)
(long long res, void *path_, long long attrnamespace_, void *attrname_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_list_fd)
(long long fd_, long long attrnamespace_, void *data_, long long nbytes_) {
  /* TODO */
}
POST_SYSCALL(extattr_list_fd)
(long long res, long long fd_, long long attrnamespace_, void *data_,
  long long nbytes_) {
  /* TODO */
}
PRE_SYSCALL(extattr_list_file)
(void *path_, long long attrnamespace_, void *data_, long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_list_file)
(long long res, void *path_, long long attrnamespace_, void *data_,
  long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(extattr_list_link)
(void *path_, long long attrnamespace_, void *data_, long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(extattr_list_link)
(long long res, void *path_, long long attrnamespace_, void *data_,
  long long nbytes_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(compat_50_pselect)
(long long nd_, void *in_, void *ou_, void *ex_, void *ts_, void *mask_) {
  /* TODO */
}
POST_SYSCALL(compat_50_pselect)
(long long res, long long nd_, void *in_, void *ou_, void *ex_, void *ts_,
  void *mask_) {
  /* TODO */
}
PRE_SYSCALL(compat_50_pollts)
(void *fds_, long long nfds_, void *ts_, void *mask_) {
  /* TODO */
}
POST_SYSCALL(compat_50_pollts)
(long long res, void *fds_, long long nfds_, void *ts_, void *mask_) {
  /* TODO */
}
PRE_SYSCALL(setxattr)
(void *path_, void *name_, void *value_, long long size_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(setxattr)
(long long res, void *path_, void *name_, void *value_, long long size_,
  long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(lsetxattr)
(void *path_, void *name_, void *value_, long long size_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(lsetxattr)
(long long res, void *path_, void *name_, void *value_, long long size_,
  long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(fsetxattr)
(long long fd_, void *name_, void *value_, long long size_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(fsetxattr)
(long long res, long long fd_, void *name_, void *value_, long long size_,
  long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(getxattr)(void *path_, void *name_, void *value_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(getxattr)
(long long res, void *path_, void *name_, void *value_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(lgetxattr)
(void *path_, void *name_, void *value_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(lgetxattr)
(long long res, void *path_, void *name_, void *value_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(fgetxattr)
(long long fd_, void *name_, void *value_, long long size_) {
  /* Nothing to do */
}
POST_SYSCALL(fgetxattr)
(long long res, long long fd_, void *name_, void *value_, long long size_) {
  /* Nothing to do */
}
PRE_SYSCALL(listxattr)(void *path_, void *list_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(listxattr)
(long long res, void *path_, void *list_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(llistxattr)(void *path_, void *list_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(llistxattr)
(long long res, void *path_, void *list_, long long size_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(flistxattr)(long long fd_, void *list_, long long size_) {
  /* TODO */
}
POST_SYSCALL(flistxattr)
(long long res, long long fd_, void *list_, long long size_) {
  /* TODO */
}
PRE_SYSCALL(removexattr)(void *path_, void *name_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(removexattr)(long long res, void *path_, void *name_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(lremovexattr)(void *path_, void *name_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(lremovexattr)(long long res, void *path_, void *name_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(fremovexattr)(long long fd_, void *name_) { /* TODO */ }
POST_SYSCALL(fremovexattr)(long long res, long long fd_, void *name_) {
  /* TODO */
}
PRE_SYSCALL(compat_50___stat30)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_50___stat30)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(compat_50___fstat30)(long long fd_, void *sb_) { /* TODO */ }
POST_SYSCALL(compat_50___fstat30)(long long res, long long fd_, void *sb_) {
  /* TODO */
}
PRE_SYSCALL(compat_50___lstat30)(void *path_, void *ub_) { /* TODO */ }
POST_SYSCALL(compat_50___lstat30)(long long res, void *path_, void *ub_) {
  /* TODO */
}
PRE_SYSCALL(__getdents30)(long long fd_, void *buf_, long long count_) {
  /* Nothing to do */
}
POST_SYSCALL(__getdents30)
(long long res, long long fd_, void *buf_, long long count_) {
  /* Nothing to do */
}
PRE_SYSCALL(posix_fadvise)(long long) { /* Nothing to do */ }
POST_SYSCALL(posix_fadvise)(long long res, long long) { /* Nothing to do */ }
PRE_SYSCALL(compat_30___fhstat30)(void *fhp_, void *sb_) { /* TODO */ }
POST_SYSCALL(compat_30___fhstat30)(long long res, void *fhp_, void *sb_) {
  /* TODO */
}
PRE_SYSCALL(compat_50___ntp_gettime30)(void *ntvp_) { /* TODO */ }
POST_SYSCALL(compat_50___ntp_gettime30)(long long res, void *ntvp_) {
  /* TODO */
}
PRE_SYSCALL(__socket30)
(long long domain_, long long type_, long long protocol_) {
  /* Nothing to do */
}
POST_SYSCALL(__socket30)
(long long res, long long domain_, long long type_, long long protocol_) {
  /* Nothing to do */
}
PRE_SYSCALL(__getfh30)(void *fname_, void *fhp_, void *fh_size_) {
  const char *fname = (const char *)fname_;
  if (fname) {
    PRE_READ(fname, __sanitizer::internal_strlen(fname) + 1);
  }
}
POST_SYSCALL(__getfh30)
(long long res, void *fname_, void *fhp_, void *fh_size_) {
  const char *fname = (const char *)fname_;
  if (res == 0) {
    if (fname) {
      POST_READ(fname, __sanitizer::internal_strlen(fname) + 1);
    }
  }
}
PRE_SYSCALL(__fhopen40)(void *fhp_, long long fh_size_, long long flags_) {
  if (fhp_) {
    PRE_READ(fhp_, fh_size_);
  }
}
POST_SYSCALL(__fhopen40)
(long long res, void *fhp_, long long fh_size_, long long flags_) {}
PRE_SYSCALL(compat_90_fhstatvfs1)
(void *fhp_, long long fh_size_, void *buf_, long long flags_) {
  if (fhp_) {
    PRE_READ(fhp_, fh_size_);
  }
}
POST_SYSCALL(compat_90_fhstatvfs1)
(long long res, void *fhp_, long long fh_size_, void *buf_, long long flags_) {}
PRE_SYSCALL(compat_50___fhstat40)(void *fhp_, long long fh_size_, void *sb_) {
  if (fhp_) {
    PRE_READ(fhp_, fh_size_);
  }
}
POST_SYSCALL(compat_50___fhstat40)
(long long res, void *fhp_, long long fh_size_, void *sb_) {}
PRE_SYSCALL(aio_cancel)(long long fildes_, void *aiocbp_) {
  if (aiocbp_) {
    PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));
  }
}
POST_SYSCALL(aio_cancel)(long long res, long long fildes_, void *aiocbp_) {}
PRE_SYSCALL(aio_error)(void *aiocbp_) {
  if (aiocbp_) {
    PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));
  }
}
POST_SYSCALL(aio_error)(long long res, void *aiocbp_) {}
PRE_SYSCALL(aio_fsync)(long long op_, void *aiocbp_) {
  if (aiocbp_) {
    PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));
  }
}
POST_SYSCALL(aio_fsync)(long long res, long long op_, void *aiocbp_) {}
PRE_SYSCALL(aio_read)(void *aiocbp_) {
  if (aiocbp_) {
    PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));
  }
}
POST_SYSCALL(aio_read)(long long res, void *aiocbp_) {}
PRE_SYSCALL(aio_return)(void *aiocbp_) {
  if (aiocbp_) {
    PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));
  }
}
POST_SYSCALL(aio_return)(long long res, void *aiocbp_) {}
PRE_SYSCALL(compat_50_aio_suspend)
(void *list_, long long nent_, void *timeout_) {
  /* TODO */
}
POST_SYSCALL(compat_50_aio_suspend)
(long long res, void *list_, long long nent_, void *timeout_) {
  /* TODO */
}
PRE_SYSCALL(aio_write)(void *aiocbp_) {
  if (aiocbp_) {
    PRE_READ(aiocbp_, sizeof(struct __sanitizer_aiocb));
  }
}
POST_SYSCALL(aio_write)(long long res, void *aiocbp_) {}
PRE_SYSCALL(lio_listio)
(long long mode_, void *list_, long long nent_, void *sig_) {
  /* Nothing to do */
}
POST_SYSCALL(lio_listio)
(long long res, long long mode_, void *list_, long long nent_, void *sig_) {
  /* Nothing to do */
}
/* syscall 407 has been skipped */
/* syscall 408 has been skipped */
/* syscall 409 has been skipped */
PRE_SYSCALL(__mount50)
(void *type_, void *path_, long long flags_, void *data_, long long data_len_) {
  const char *type = (const char *)type_;
  const char *path = (const char *)path_;
  if (type) {
    PRE_READ(type, __sanitizer::internal_strlen(type) + 1);
  }
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (data_) {
    PRE_READ(data_, data_len_);
  }
}
POST_SYSCALL(__mount50)
(long long res, void *type_, void *path_, long long flags_, void *data_,
  long long data_len_) {
  const char *type = (const char *)type_;
  const char *path = (const char *)path_;
  if (type) {
    POST_READ(type, __sanitizer::internal_strlen(type) + 1);
  }
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (data_) {
    POST_READ(data_, data_len_);
  }
}
PRE_SYSCALL(mremap)
(void *old_address_, long long old_size_, void *new_address_,
  long long new_size_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(mremap)
(long long res, void *old_address_, long long old_size_, void *new_address_,
  long long new_size_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(pset_create)(void *psid_) { /* Nothing to do */ }
POST_SYSCALL(pset_create)(long long res, void *psid_) { /* Nothing to do */ }
PRE_SYSCALL(pset_destroy)(long long psid_) { /* Nothing to do */ }
POST_SYSCALL(pset_destroy)(long long res, long long psid_) {
  /* Nothing to do */
}
PRE_SYSCALL(pset_assign)(long long psid_, long long cpuid_, void *opsid_) {
  /* Nothing to do */
}
POST_SYSCALL(pset_assign)
(long long res, long long psid_, long long cpuid_, void *opsid_) {
  /* Nothing to do */
}
PRE_SYSCALL(_pset_bind)
(long long idtype_, long long first_id_, long long second_id_, long long psid_,
  void *opsid_) {
  /* Nothing to do */
}
POST_SYSCALL(_pset_bind)
(long long res, long long idtype_, long long first_id_, long long second_id_,
  long long psid_, void *opsid_) {
  /* Nothing to do */
}
PRE_SYSCALL(__posix_fadvise50)
(long long fd_, long long PAD_, long long offset_, long long len_,
  long long advice_) {
  /* Nothing to do */
}
POST_SYSCALL(__posix_fadvise50)
(long long res, long long fd_, long long PAD_, long long offset_,
  long long len_, long long advice_) {
  /* Nothing to do */
}
PRE_SYSCALL(__select50)
(long long nd_, void *in_, void *ou_, void *ex_, void *tv_) {
  /* Nothing to do */
}
POST_SYSCALL(__select50)
(long long res, long long nd_, void *in_, void *ou_, void *ex_, void *tv_) {
  /* Nothing to do */
}
PRE_SYSCALL(__gettimeofday50)(void *tp_, void *tzp_) { /* Nothing to do */ }
POST_SYSCALL(__gettimeofday50)(long long res, void *tp_, void *tzp_) {
  /* Nothing to do */
}
PRE_SYSCALL(__settimeofday50)(void *tv_, void *tzp_) {
  if (tv_) {
    PRE_READ(tv_, timeval_sz);
  }
  if (tzp_) {
    PRE_READ(tzp_, struct_timezone_sz);
  }
}
POST_SYSCALL(__settimeofday50)(long long res, void *tv_, void *tzp_) {}
PRE_SYSCALL(__utimes50)(void *path_, void *tptr_) {
  struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (tptr) {
    PRE_READ(tptr[0], struct_timespec_sz);
    PRE_READ(tptr[1], struct_timespec_sz);
  }
}
POST_SYSCALL(__utimes50)(long long res, void *path_, void *tptr_) {}
PRE_SYSCALL(__adjtime50)(void *delta_, void *olddelta_) {
  if (delta_) {
    PRE_READ(delta_, timeval_sz);
  }
}
POST_SYSCALL(__adjtime50)(long long res, void *delta_, void *olddelta_) {}
PRE_SYSCALL(__lfs_segwait50)(void *fsidp_, void *tv_) { /* TODO */ }
POST_SYSCALL(__lfs_segwait50)(long long res, void *fsidp_, void *tv_) {
  /* TODO */
}
PRE_SYSCALL(__futimes50)(long long fd_, void *tptr_) {
  struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;
  if (tptr) {
    PRE_READ(tptr[0], struct_timespec_sz);
    PRE_READ(tptr[1], struct_timespec_sz);
  }
}
POST_SYSCALL(__futimes50)(long long res, long long fd_, void *tptr_) {}
PRE_SYSCALL(__lutimes50)(void *path_, void *tptr_) {
  struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (tptr) {
    PRE_READ(tptr[0], struct_timespec_sz);
    PRE_READ(tptr[1], struct_timespec_sz);
  }
}
POST_SYSCALL(__lutimes50)(long long res, void *path_, void *tptr_) {
  struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (tptr) {
    POST_READ(tptr[0], struct_timespec_sz);
    POST_READ(tptr[1], struct_timespec_sz);
  }
}
PRE_SYSCALL(__setitimer50)(long long which_, void *itv_, void *oitv_) {
  struct __sanitizer_itimerval *itv = (struct __sanitizer_itimerval *)itv_;
  if (itv) {
    PRE_READ(&itv->it_interval.tv_sec, sizeof(__sanitizer_time_t));
    PRE_READ(&itv->it_interval.tv_usec, sizeof(__sanitizer_suseconds_t));
    PRE_READ(&itv->it_value.tv_sec, sizeof(__sanitizer_time_t));
    PRE_READ(&itv->it_value.tv_usec, sizeof(__sanitizer_suseconds_t));
  }
}
POST_SYSCALL(__setitimer50)
(long long res, long long which_, void *itv_, void *oitv_) {}
PRE_SYSCALL(__getitimer50)(long long which_, void *itv_) { /* Nothing to do */ }
POST_SYSCALL(__getitimer50)(long long res, long long which_, void *itv_) {
  /* Nothing to do */
}
PRE_SYSCALL(__clock_gettime50)(long long clock_id_, void *tp_) {
  /* Nothing to do */
}
POST_SYSCALL(__clock_gettime50)(long long res, long long clock_id_, void *tp_) {
  /* Nothing to do */
}
PRE_SYSCALL(__clock_settime50)(long long clock_id_, void *tp_) {
  if (tp_) {
    PRE_READ(tp_, struct_timespec_sz);
  }
}
POST_SYSCALL(__clock_settime50)
(long long res, long long clock_id_, void *tp_) {}
PRE_SYSCALL(__clock_getres50)(long long clock_id_, void *tp_) {
  /* Nothing to do */
}
POST_SYSCALL(__clock_getres50)(long long res, long long clock_id_, void *tp_) {
  /* Nothing to do */
}
PRE_SYSCALL(__nanosleep50)(void *rqtp_, void *rmtp_) {
  if (rqtp_) {
    PRE_READ(rqtp_, struct_timespec_sz);
  }
}
POST_SYSCALL(__nanosleep50)(long long res, void *rqtp_, void *rmtp_) {}
PRE_SYSCALL(____sigtimedwait50)(void *set_, void *info_, void *timeout_) {
  if (set_) {
    PRE_READ(set_, sizeof(__sanitizer_sigset_t));
  }
  if (timeout_) {
    PRE_READ(timeout_, struct_timespec_sz);
  }
}
POST_SYSCALL(____sigtimedwait50)
(long long res, void *set_, void *info_, void *timeout_) {}
PRE_SYSCALL(__mq_timedsend50)
(long long mqdes_, void *msg_ptr_, long long msg_len_, long long msg_prio_,
  void *abs_timeout_) {
  if (msg_ptr_) {
    PRE_READ(msg_ptr_, msg_len_);
  }
  if (abs_timeout_) {
    PRE_READ(abs_timeout_, struct_timespec_sz);
  }
}
POST_SYSCALL(__mq_timedsend50)
(long long res, long long mqdes_, void *msg_ptr_, long long msg_len_,
  long long msg_prio_, void *abs_timeout_) {}
PRE_SYSCALL(__mq_timedreceive50)
(long long mqdes_, void *msg_ptr_, long long msg_len_, void *msg_prio_,
  void *abs_timeout_) {
  if (msg_ptr_) {
    PRE_READ(msg_ptr_, msg_len_);
  }
  if (abs_timeout_) {
    PRE_READ(abs_timeout_, struct_timespec_sz);
  }
}
POST_SYSCALL(__mq_timedreceive50)
(long long res, long long mqdes_, void *msg_ptr_, long long msg_len_,
  void *msg_prio_, void *abs_timeout_) {}
PRE_SYSCALL(compat_60__lwp_park)
(void *ts_, long long unpark_, void *hint_, void *unparkhint_) {
  /* TODO */
}
POST_SYSCALL(compat_60__lwp_park)
(long long res, void *ts_, long long unpark_, void *hint_, void *unparkhint_) {
  /* TODO */
}
PRE_SYSCALL(__kevent50)
(long long fd_, void *changelist_, long long nchanges_, void *eventlist_,
  long long nevents_, void *timeout_) {
  if (changelist_) {
    PRE_READ(changelist_, nchanges_ * struct_kevent_sz);
  }
  if (timeout_) {
    PRE_READ(timeout_, struct_timespec_sz);
  }
}
POST_SYSCALL(__kevent50)
(long long res, long long fd_, void *changelist_, long long nchanges_,
  void *eventlist_, long long nevents_, void *timeout_) {}
PRE_SYSCALL(__pselect50)
(long long nd_, void *in_, void *ou_, void *ex_, void *ts_, void *mask_) {
  if (ts_) {
    PRE_READ(ts_, struct_timespec_sz);
  }
  if (mask_) {
    PRE_READ(mask_, sizeof(struct __sanitizer_sigset_t));
  }
}
POST_SYSCALL(__pselect50)
(long long res, long long nd_, void *in_, void *ou_, void *ex_, void *ts_,
  void *mask_) {}
PRE_SYSCALL(__pollts50)(void *fds_, long long nfds_, void *ts_, void *mask_) {
  if (ts_) {
    PRE_READ(ts_, struct_timespec_sz);
  }
  if (mask_) {
    PRE_READ(mask_, sizeof(struct __sanitizer_sigset_t));
  }
}
POST_SYSCALL(__pollts50)
(long long res, void *fds_, long long nfds_, void *ts_, void *mask_) {}
PRE_SYSCALL(__aio_suspend50)(void *list_, long long nent_, void *timeout_) {
  int i;
  const struct aiocb *const *list = (const struct aiocb *const *)list_;
  if (list) {
    for (i = 0; i < nent_; i++) {
      if (list[i]) {
        PRE_READ(list[i], sizeof(struct __sanitizer_aiocb));
      }
    }
  }
  if (timeout_) {
    PRE_READ(timeout_, struct_timespec_sz);
  }
}
POST_SYSCALL(__aio_suspend50)
(long long res, void *list_, long long nent_, void *timeout_) {}
PRE_SYSCALL(__stat50)(void *path_, void *ub_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__stat50)(long long res, void *path_, void *ub_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(__fstat50)(long long fd_, void *sb_) { /* Nothing to do */ }
POST_SYSCALL(__fstat50)(long long res, long long fd_, void *sb_) {
  /* Nothing to do */
}
PRE_SYSCALL(__lstat50)(void *path_, void *ub_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__lstat50)(long long res, void *path_, void *ub_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(____semctl50)
(long long semid_, long long semnum_, long long cmd_, void *arg_) {
  /* Nothing to do */
}
POST_SYSCALL(____semctl50)
(long long res, long long semid_, long long semnum_, long long cmd_,
  void *arg_) {
  /* Nothing to do */
}
PRE_SYSCALL(__shmctl50)(long long shmid_, long long cmd_, void *buf_) {
  /* Nothing to do */
}
POST_SYSCALL(__shmctl50)
(long long res, long long shmid_, long long cmd_, void *buf_) {
  /* Nothing to do */
}
PRE_SYSCALL(__msgctl50)(long long msqid_, long long cmd_, void *buf_) {
  /* Nothing to do */
}
POST_SYSCALL(__msgctl50)
(long long res, long long msqid_, long long cmd_, void *buf_) {
  /* Nothing to do */
}
PRE_SYSCALL(__getrusage50)(long long who_, void *rusage_) {
  /* Nothing to do */
}
POST_SYSCALL(__getrusage50)(long long res, long long who_, void *rusage_) {
  /* Nothing to do */
}
PRE_SYSCALL(__timer_settime50)
(long long timerid_, long long flags_, void *value_, void *ovalue_) {
  struct __sanitizer_itimerval *value = (struct __sanitizer_itimerval *)value_;
  if (value) {
    PRE_READ(&value->it_interval.tv_sec, sizeof(__sanitizer_time_t));
    PRE_READ(&value->it_interval.tv_usec, sizeof(__sanitizer_suseconds_t));
    PRE_READ(&value->it_value.tv_sec, sizeof(__sanitizer_time_t));
    PRE_READ(&value->it_value.tv_usec, sizeof(__sanitizer_suseconds_t));
  }
}
POST_SYSCALL(__timer_settime50)
(long long res, long long timerid_, long long flags_, void *value_,
  void *ovalue_) {
  struct __sanitizer_itimerval *value = (struct __sanitizer_itimerval *)value_;
  if (res == 0) {
    if (value) {
      POST_READ(&value->it_interval.tv_sec, sizeof(__sanitizer_time_t));
      POST_READ(&value->it_interval.tv_usec, sizeof(__sanitizer_suseconds_t));
      POST_READ(&value->it_value.tv_sec, sizeof(__sanitizer_time_t));
      POST_READ(&value->it_value.tv_usec, sizeof(__sanitizer_suseconds_t));
    }
  }
}
PRE_SYSCALL(__timer_gettime50)(long long timerid_, void *value_) {
  /* Nothing to do */
}
POST_SYSCALL(__timer_gettime50)
(long long res, long long timerid_, void *value_) {
  /* Nothing to do */
}
#if defined(NTP) || !defined(_KERNEL_OPT)
PRE_SYSCALL(__ntp_gettime50)(void *ntvp_) { /* Nothing to do */ }
POST_SYSCALL(__ntp_gettime50)(long long res, void *ntvp_) {
  /* Nothing to do */
}
#else
/* syscall 448 has been skipped */
#endif
PRE_SYSCALL(__wait450)
(long long pid_, void *status_, long long options_, void *rusage_) {
  /* Nothing to do */
}
POST_SYSCALL(__wait450)
(long long res, long long pid_, void *status_, long long options_,
  void *rusage_) {
  /* Nothing to do */
}
PRE_SYSCALL(__mknod50)(void *path_, long long mode_, long long dev_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__mknod50)
(long long res, void *path_, long long mode_, long long dev_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(__fhstat50)(void *fhp_, long long fh_size_, void *sb_) {
  if (fhp_) {
    PRE_READ(fhp_, fh_size_);
  }
}
POST_SYSCALL(__fhstat50)
(long long res, void *fhp_, long long fh_size_, void *sb_) {
  if (res == 0) {
    if (fhp_) {
      POST_READ(fhp_, fh_size_);
    }
  }
}
/* syscall 452 has been skipped */
PRE_SYSCALL(pipe2)(void *fildes_, long long flags_) { /* Nothing to do */ }
POST_SYSCALL(pipe2)(long long res, void *fildes_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(dup3)(long long from_, long long to_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(dup3)
(long long res, long long from_, long long to_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(kqueue1)(long long flags_) { /* Nothing to do */ }
POST_SYSCALL(kqueue1)(long long res, long long flags_) { /* Nothing to do */ }
PRE_SYSCALL(paccept)
(long long s_, void *name_, void *anamelen_, void *mask_, long long flags_) {
  if (mask_) {
    PRE_READ(mask_, sizeof(__sanitizer_sigset_t));
  }
}
POST_SYSCALL(paccept)
(long long res, long long s_, void *name_, void *anamelen_, void *mask_,
  long long flags_) {
  if (res >= 0) {
    if (mask_) {
      PRE_READ(mask_, sizeof(__sanitizer_sigset_t));
    }
  }
}
PRE_SYSCALL(linkat)
(long long fd1_, void *name1_, long long fd2_, void *name2_, long long flags_) {
  const char *name1 = (const char *)name1_;
  const char *name2 = (const char *)name2_;
  if (name1) {
    PRE_READ(name1, __sanitizer::internal_strlen(name1) + 1);
  }
  if (name2) {
    PRE_READ(name2, __sanitizer::internal_strlen(name2) + 1);
  }
}
POST_SYSCALL(linkat)
(long long res, long long fd1_, void *name1_, long long fd2_, void *name2_,
  long long flags_) {
  const char *name1 = (const char *)name1_;
  const char *name2 = (const char *)name2_;
  if (res == 0) {
    if (name1) {
      POST_READ(name1, __sanitizer::internal_strlen(name1) + 1);
    }
    if (name2) {
      POST_READ(name2, __sanitizer::internal_strlen(name2) + 1);
    }
  }
}
PRE_SYSCALL(renameat)
(long long fromfd_, void *from_, long long tofd_, void *to_) {
  const char *from = (const char *)from_;
  const char *to = (const char *)to_;
  if (from) {
    PRE_READ(from, __sanitizer::internal_strlen(from) + 1);
  }
  if (to) {
    PRE_READ(to, __sanitizer::internal_strlen(to) + 1);
  }
}
POST_SYSCALL(renameat)
(long long res, long long fromfd_, void *from_, long long tofd_, void *to_) {
  const char *from = (const char *)from_;
  const char *to = (const char *)to_;
  if (res == 0) {
    if (from) {
      POST_READ(from, __sanitizer::internal_strlen(from) + 1);
    }
    if (to) {
      POST_READ(to, __sanitizer::internal_strlen(to) + 1);
    }
  }
}
PRE_SYSCALL(mkfifoat)(long long fd_, void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(mkfifoat)
(long long res, long long fd_, void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(mknodat)
(long long fd_, void *path_, long long mode_, long long PAD_, long long dev_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(mknodat)
(long long res, long long fd_, void *path_, long long mode_, long long PAD_,
  long long dev_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(mkdirat)(long long fd_, void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(mkdirat)
(long long res, long long fd_, void *path_, long long mode_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(faccessat)
(long long fd_, void *path_, long long amode_, long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(faccessat)
(long long res, long long fd_, void *path_, long long amode_, long long flag_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(fchmodat)
(long long fd_, void *path_, long long mode_, long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(fchmodat)
(long long res, long long fd_, void *path_, long long mode_, long long flag_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(fchownat)
(long long fd_, void *path_, long long owner_, long long group_,
  long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(fchownat)
(long long res, long long fd_, void *path_, long long owner_, long long group_,
  long long flag_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(fexecve)(long long fd_, void *argp_, void *envp_) { /* TODO */ }
POST_SYSCALL(fexecve)(long long res, long long fd_, void *argp_, void *envp_) {
  /* TODO */
}
PRE_SYSCALL(fstatat)(long long fd_, void *path_, void *buf_, long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(fstatat)
(long long res, long long fd_, void *path_, void *buf_, long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(utimensat)
(long long fd_, void *path_, void *tptr_, long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
  if (tptr_) {
    PRE_READ(tptr_, struct_timespec_sz);
  }
}
POST_SYSCALL(utimensat)
(long long res, long long fd_, void *path_, void *tptr_, long long flag_) {
  const char *path = (const char *)path_;
  if (res > 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
    if (tptr_) {
      POST_READ(tptr_, struct_timespec_sz);
    }
  }
}
PRE_SYSCALL(openat)
(long long fd_, void *path_, long long oflags_, long long mode_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(openat)
(long long res, long long fd_, void *path_, long long oflags_,
  long long mode_) {
  const char *path = (const char *)path_;
  if (res > 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(readlinkat)
(long long fd_, void *path_, void *buf_, long long bufsize_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(readlinkat)
(long long res, long long fd_, void *path_, void *buf_, long long bufsize_) {
  const char *path = (const char *)path_;
  if (res > 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(symlinkat)(void *path1_, long long fd_, void *path2_) {
  const char *path1 = (const char *)path1_;
  const char *path2 = (const char *)path2_;
  if (path1) {
    PRE_READ(path1, __sanitizer::internal_strlen(path1) + 1);
  }
  if (path2) {
    PRE_READ(path2, __sanitizer::internal_strlen(path2) + 1);
  }
}
POST_SYSCALL(symlinkat)
(long long res, void *path1_, long long fd_, void *path2_) {
  const char *path1 = (const char *)path1_;
  const char *path2 = (const char *)path2_;
  if (res == 0) {
    if (path1) {
      POST_READ(path1, __sanitizer::internal_strlen(path1) + 1);
    }
    if (path2) {
      POST_READ(path2, __sanitizer::internal_strlen(path2) + 1);
    }
  }
}
PRE_SYSCALL(unlinkat)(long long fd_, void *path_, long long flag_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(unlinkat)
(long long res, long long fd_, void *path_, long long flag_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(futimens)(long long fd_, void *tptr_) {
  struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;
  if (tptr) {
    PRE_READ(tptr[0], struct_timespec_sz);
    PRE_READ(tptr[1], struct_timespec_sz);
  }
}
POST_SYSCALL(futimens)(long long res, long long fd_, void *tptr_) {
  struct __sanitizer_timespec **tptr = (struct __sanitizer_timespec **)tptr_;
  if (res == 0) {
    if (tptr) {
      POST_READ(tptr[0], struct_timespec_sz);
      POST_READ(tptr[1], struct_timespec_sz);
    }
  }
}
PRE_SYSCALL(__quotactl)(void *path_, void *args_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__quotactl)(long long res, void *path_, void *args_) {
  const char *path = (const char *)path_;
  if (res == 0) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(posix_spawn)
(void *pid_, void *path_, void *file_actions_, void *attrp_, void *argv_,
  void *envp_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(posix_spawn)
(long long res, void *pid_, void *path_, void *file_actions_, void *attrp_,
  void *argv_, void *envp_) {
  const char *path = (const char *)path_;
  if (pid_) {
    if (path) {
      POST_READ(path, __sanitizer::internal_strlen(path) + 1);
    }
  }
}
PRE_SYSCALL(recvmmsg)
(long long s_, void *mmsg_, long long vlen_, long long flags_, void *timeout_) {
  if (timeout_) {
    PRE_READ(timeout_, struct_timespec_sz);
  }
}
POST_SYSCALL(recvmmsg)
(long long res, long long s_, void *mmsg_, long long vlen_, long long flags_,
  void *timeout_) {
  if (res >= 0) {
    if (timeout_) {
      POST_READ(timeout_, struct_timespec_sz);
    }
  }
}
PRE_SYSCALL(sendmmsg)
(long long s_, void *mmsg_, long long vlen_, long long flags_) {
  struct __sanitizer_mmsghdr *mmsg = (struct __sanitizer_mmsghdr *)mmsg_;
  if (mmsg) {
    PRE_READ(mmsg, sizeof(struct __sanitizer_mmsghdr) *
                       (vlen_ > 1024 ? 1024 : vlen_));
  }
}
POST_SYSCALL(sendmmsg)
(long long res, long long s_, void *mmsg_, long long vlen_, long long flags_) {
  struct __sanitizer_mmsghdr *mmsg = (struct __sanitizer_mmsghdr *)mmsg_;
  if (res >= 0) {
    if (mmsg) {
      POST_READ(mmsg, sizeof(struct __sanitizer_mmsghdr) *
                          (vlen_ > 1024 ? 1024 : vlen_));
    }
  }
}
PRE_SYSCALL(clock_nanosleep)
(long long clock_id_, long long flags_, void *rqtp_, void *rmtp_) {
  if (rqtp_) {
    PRE_READ(rqtp_, struct_timespec_sz);
  }
}
POST_SYSCALL(clock_nanosleep)
(long long res, long long clock_id_, long long flags_, void *rqtp_,
  void *rmtp_) {
  if (rqtp_) {
    POST_READ(rqtp_, struct_timespec_sz);
  }
}
PRE_SYSCALL(___lwp_park60)
(long long clock_id_, long long flags_, void *ts_, long long unpark_,
  void *hint_, void *unparkhint_) {
  if (ts_) {
    PRE_READ(ts_, struct_timespec_sz);
  }
}
POST_SYSCALL(___lwp_park60)
(long long res, long long clock_id_, long long flags_, void *ts_,
  long long unpark_, void *hint_, void *unparkhint_) {
  if (res == 0) {
    if (ts_) {
      POST_READ(ts_, struct_timespec_sz);
    }
  }
}
PRE_SYSCALL(posix_fallocate)
(long long fd_, long long PAD_, long long pos_, long long len_) {
  /* Nothing to do */
}
POST_SYSCALL(posix_fallocate)
(long long res, long long fd_, long long PAD_, long long pos_, long long len_) {
  /* Nothing to do */
}
PRE_SYSCALL(fdiscard)
(long long fd_, long long PAD_, long long pos_, long long len_) {
  /* Nothing to do */
}
POST_SYSCALL(fdiscard)
(long long res, long long fd_, long long PAD_, long long pos_, long long len_) {
  /* Nothing to do */
}
PRE_SYSCALL(wait6)
(long long idtype_, long long id_, void *status_, long long options_,
  void *wru_, void *info_) {
  /* Nothing to do */
}
POST_SYSCALL(wait6)
(long long res, long long idtype_, long long id_, void *status_,
  long long options_, void *wru_, void *info_) {
  /* Nothing to do */
}
PRE_SYSCALL(clock_getcpuclockid2)
(long long idtype_, long long id_, void *clock_id_) {
  /* Nothing to do */
}
POST_SYSCALL(clock_getcpuclockid2)
(long long res, long long idtype_, long long id_, void *clock_id_) {
  /* Nothing to do */
}
PRE_SYSCALL(__getvfsstat90)(void *buf_, long long bufsize_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(__getvfsstat90)
(long long res, void *buf_, long long bufsize_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(__statvfs190)(void *path_, void *buf_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    PRE_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
POST_SYSCALL(__statvfs190)
(long long res, void *path_, void *buf_, long long flags_) {
  const char *path = (const char *)path_;
  if (path) {
    POST_READ(path, __sanitizer::internal_strlen(path) + 1);
  }
}
PRE_SYSCALL(__fstatvfs190)(long long fd_, void *buf_, long long flags_) {
  /* Nothing to do */
}
POST_SYSCALL(__fstatvfs190)
(long long res, long long fd_, void *buf_, long long flags_) {
  /* Nothing to do */
}
PRE_SYSCALL(__fhstatvfs190)
(void *fhp_, long long fh_size_, void *buf_, long long flags_) {
  if (fhp_) {
    PRE_READ(fhp_, fh_size_);
  }
}
POST_SYSCALL(__fhstatvfs190)
(long long res, void *fhp_, long long fh_size_, void *buf_, long long flags_) {}
PRE_SYSCALL(__acl_get_link)(void *path_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_get_link)
(long long res, void *path_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_set_link)(void *path_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_set_link)
(long long res, void *path_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_delete_link)(void *path_, long long type_) { /* TODO */ }
POST_SYSCALL(__acl_delete_link)(long long res, void *path_, long long type_) {
  /* TODO */
}
PRE_SYSCALL(__acl_aclcheck_link)(void *path_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_aclcheck_link)
(long long res, void *path_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_get_file)(void *path_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_get_file)
(long long res, void *path_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_set_file)(void *path_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_set_file)
(long long res, void *path_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_get_fd)(long long filedes_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_get_fd)
(long long res, long long filedes_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_set_fd)(long long filedes_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_set_fd)
(long long res, long long filedes_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_delete_file)(void *path_, long long type_) { /* TODO */ }
POST_SYSCALL(__acl_delete_file)(long long res, void *path_, long long type_) {
  /* TODO */
}
PRE_SYSCALL(__acl_delete_fd)(long long filedes_, long long type_) { /* TODO */ }
POST_SYSCALL(__acl_delete_fd)
(long long res, long long filedes_, long long type_) {
  /* TODO */
}
PRE_SYSCALL(__acl_aclcheck_file)(void *path_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_aclcheck_file)
(long long res, void *path_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(__acl_aclcheck_fd)
(long long filedes_, long long type_, void *aclp_) {
  /* TODO */
}
POST_SYSCALL(__acl_aclcheck_fd)
(long long res, long long filedes_, long long type_, void *aclp_) {
  /* TODO */
}
PRE_SYSCALL(lpathconf)(void *path_, long long name_) { /* TODO */ }
POST_SYSCALL(lpathconf)(long long res, void *path_, long long name_) {
  /* TODO */
}
#undef SYS_MAXSYSARGS
} // extern "C"

#undef PRE_SYSCALL
#undef PRE_READ
#undef PRE_WRITE
#undef POST_SYSCALL
#undef POST_READ
#undef POST_WRITE

#endif // SANITIZER_NETBSD
