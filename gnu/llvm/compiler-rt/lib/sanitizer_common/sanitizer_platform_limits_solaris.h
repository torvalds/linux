//===-- sanitizer_platform_limits_solaris.h -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer common code.
//
// Sizes and layouts of platform-specific Solaris data structures.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_PLATFORM_LIMITS_SOLARIS_H
#define SANITIZER_PLATFORM_LIMITS_SOLARIS_H

#if SANITIZER_SOLARIS

#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"

namespace __sanitizer {
extern unsigned struct_utsname_sz;
extern unsigned struct_stat_sz;
extern unsigned struct_stat64_sz;
extern unsigned struct_rusage_sz;
extern unsigned siginfo_t_sz;
extern unsigned struct_itimerval_sz;
extern unsigned pthread_t_sz;
extern unsigned pthread_mutex_t_sz;
extern unsigned pthread_cond_t_sz;
extern unsigned pid_t_sz;
extern unsigned timeval_sz;
extern unsigned uid_t_sz;
extern unsigned gid_t_sz;
extern unsigned mbstate_t_sz;
extern unsigned struct_timezone_sz;
extern unsigned struct_tms_sz;
extern unsigned struct_itimerspec_sz;
extern unsigned struct_sigevent_sz;
extern unsigned struct_stack_t_sz;
extern unsigned struct_sched_param_sz;
extern unsigned struct_statfs64_sz;
extern unsigned struct_statfs_sz;
extern unsigned struct_sockaddr_sz;
unsigned ucontext_t_sz(void *ctx);

extern unsigned struct_timespec_sz;
extern unsigned struct_rlimit_sz;
extern unsigned struct_utimbuf_sz;

struct __sanitizer_sem_t {
  //u64 data[6];
  u32 sem_count;
  u16 sem_type;
  u16 sem_magic;
  u64 sem_pad1[3];
  u64 sem_pad2[2];
};

struct __sanitizer_ipc_perm {
  unsigned int uid;           // uid_t
  unsigned int gid;           // gid_t
  unsigned int cuid;          // uid_t
  unsigned int cgid;          // gid_t
  unsigned int mode;          // mode_t
  unsigned int seq;           // uint_t
  int key;                    // key_t
#if !defined(_LP64)
  int pad[4];
#endif
};

struct __sanitizer_shmid_ds {
  __sanitizer_ipc_perm shm_perm;
  unsigned long shm_segsz;    // size_t
  unsigned long shm_flags;    // uintptr_t
  unsigned short shm_lkcnt;   // ushort_t
  int shm_lpid;               // pid_t
  int shm_cpid;               // pid_t
  unsigned long shm_nattch;   // shmatt_t
  unsigned long shm_cnattch;  // ulong_t
#if defined(_LP64)
  long shm_atime;             // time_t
  long shm_dtime;
  long shm_ctime;
  void *shm_amp;
  u64 shm_gransize;           // uint64_t
  u64 shm_allocated;          // uint64_t
  u64 shm_pad4[1];            // int64_t
#else
  long shm_atime;             // time_t
  int shm_pad1;               // int32_t
  long shm_dtime;             // time_t
  int shm_pad2;               // int32_t
  long shm_ctime;             // time_t
  void *shm_amp;
  u64 shm_gransize;           // uint64_t
  u64 shm_allocated;          // uint64_t
#endif
};

extern unsigned struct_statvfs_sz;
#if SANITIZER_SOLARIS32
extern unsigned struct_statvfs64_sz;
#endif

struct __sanitizer_iovec {
  void *iov_base;
  uptr iov_len;
};

struct __sanitizer_ifaddrs {
  struct __sanitizer_ifaddrs *ifa_next;
  char *ifa_name;
  u64 ifa_flags;     // uint64_t
  void *ifa_addr;    // (struct sockaddr *)
  void *ifa_netmask; // (struct sockaddr *)
  // This is a union on Linux.
# ifdef ifa_dstaddr
# undef ifa_dstaddr
# endif
  void *ifa_dstaddr; // (struct sockaddr *)
  void *ifa_data;
};

typedef unsigned __sanitizer_pthread_key_t;

struct __sanitizer_XDR {
  int x_op;
  void *x_ops;
  uptr x_public;
  uptr x_private;
  uptr x_base;
  unsigned x_handy;
};

const int __sanitizer_XDR_ENCODE = 0;
const int __sanitizer_XDR_DECODE = 1;
const int __sanitizer_XDR_FREE = 2;

struct __sanitizer_passwd {
  char *pw_name;
  char *pw_passwd;
  unsigned int pw_uid;    // uid_t
  unsigned int pw_gid;    // gid_t
  char *pw_age;
  char *pw_comment;
  char *pw_gecos;
  char *pw_dir;
  char *pw_shell;
};

struct __sanitizer_group {
  char *gr_name;
  char *gr_passwd;
  int gr_gid;
  char **gr_mem;
};

typedef long __sanitizer_time_t;

typedef long __sanitizer_suseconds_t;

struct __sanitizer_timeval {
  __sanitizer_time_t tv_sec;
  __sanitizer_suseconds_t tv_usec;
};

struct __sanitizer_itimerval {
  struct __sanitizer_timeval it_interval;
  struct __sanitizer_timeval it_value;
};

struct __sanitizer_timeb {
  __sanitizer_time_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
};

struct __sanitizer_ether_addr {
  u8 octet[6];
};

struct __sanitizer_tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

struct __sanitizer_msghdr {
  void *msg_name;
  unsigned msg_namelen;
  struct __sanitizer_iovec *msg_iov;
  unsigned msg_iovlen;
  void *msg_control;
  unsigned msg_controllen;
  int msg_flags;
};
struct __sanitizer_cmsghdr {
  unsigned cmsg_len;
  int cmsg_level;
  int cmsg_type;
};

#if SANITIZER_SOLARIS && (defined(_LP64) || _FILE_OFFSET_BITS == 64)
struct __sanitizer_dirent {
  unsigned long long d_ino;
  long long d_off;
  unsigned short d_reclen;
  // more fields that we don't care about
};
#else
struct __sanitizer_dirent {
  unsigned long d_ino;
  long d_off;
  unsigned short d_reclen;
  // more fields that we don't care about
};
#endif

struct __sanitizer_dirent64 {
  unsigned long long d_ino;
  unsigned long long d_off;
  unsigned short d_reclen;
  // more fields that we don't care about
};

typedef long __sanitizer_clock_t;
typedef int __sanitizer_clockid_t;

// This thing depends on the platform. We are only interested in the upper
// limit. Verified with a compiler assert in .cpp.
union __sanitizer_pthread_attr_t {
  char size[128];
  void *align;
};

struct __sanitizer_sigset_t {
  // uint32_t * 4
  unsigned int __bits[4];
};

struct __sanitizer_siginfo {
  // The size is determined by looking at sizeof of real siginfo_t on linux.
  u64 opaque[128 / sizeof(u64)];
};

using __sanitizer_sighandler_ptr = void (*)(int sig);
using __sanitizer_sigactionhandler_ptr =
    void (*)(int sig, __sanitizer_siginfo *siginfo, void *uctx);

struct __sanitizer_sigaction {
  int sa_flags;
  union {
    __sanitizer_sigactionhandler_ptr sigaction;
    __sanitizer_sighandler_ptr handler;
  };
  __sanitizer_sigset_t sa_mask;
#if !defined(_LP64)
  int sa_resv[2];
#endif
};

struct __sanitizer_kernel_sigset_t {
  u8 sig[8];
};

struct __sanitizer_kernel_sigaction_t {
  union {
    void (*handler)(int signo);
    void (*sigaction)(int signo, __sanitizer_siginfo *info, void *ctx);
  };
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  __sanitizer_kernel_sigset_t sa_mask;
};

extern const uptr sig_ign;
extern const uptr sig_dfl;
extern const uptr sig_err;
extern const uptr sa_siginfo;

extern int af_inet;
extern int af_inet6;
uptr __sanitizer_in_addr_sz(int af);

struct __sanitizer_dl_phdr_info {
  uptr dlpi_addr;
  const char *dlpi_name;
  const void *dlpi_phdr;
  short dlpi_phnum;
};

extern unsigned struct_ElfW_Phdr_sz;

struct __sanitizer_addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
#if defined(__sparcv9)
  int _ai_pad;
#endif
  unsigned ai_addrlen;
  char *ai_canonname;
  void *ai_addr;
  struct __sanitizer_addrinfo *ai_next;
};

struct __sanitizer_hostent {
  char *h_name;
  char **h_aliases;
  int h_addrtype;
  int h_length;
  char **h_addr_list;
};

struct __sanitizer_pollfd {
  int fd;
  short events;
  short revents;
};

typedef unsigned long __sanitizer_nfds_t;

struct __sanitizer_glob_t {
  uptr gl_pathc;
  char **gl_pathv;
  uptr gl_offs;
  char **gl_pathp;
  int gl_pathn;
};

extern int glob_nomatch;
extern int glob_altdirfunc;
extern const int wordexp_wrde_dooffs;

extern unsigned path_max;

struct __sanitizer_wordexp_t {
  uptr we_wordc;
  char **we_wordv;
  uptr we_offs;
  char **we_wordp;
  int we_wordn;
};

typedef void __sanitizer_FILE;
#define SANITIZER_HAS_STRUCT_FILE 0

// This simplifies generic code
#define struct_shminfo_sz -1
#define struct_shm_info_sz -1
#define shmctl_shm_stat -1
#define shmctl_ipc_info -1
#define shmctl_shm_info -1

extern int shmctl_ipc_stat;

extern unsigned struct_utmp_sz;
extern unsigned struct_utmpx_sz;

extern int map_fixed;

// ioctl arguments
struct __sanitizer_ifconf {
  int ifc_len;
  union {
    void *ifcu_req;
  } ifc_ifcu;
};

// <sys/ioccom.h>
#define IOC_NRBITS 8
#define IOC_TYPEBITS 8
#define IOC_SIZEBITS 12
#define IOC_DIRBITS 4
#undef IOC_NONE
#define IOC_NONE 2U     // IOC_VOID
#define IOC_READ 4U     // IOC_OUT
#define IOC_WRITE 8U    // IOC_IN

#define IOC_NRMASK ((1 << IOC_NRBITS) - 1)
#define IOC_TYPEMASK ((1 << IOC_TYPEBITS) - 1)
#define IOC_SIZEMASK ((1 << IOC_SIZEBITS) - 1)
#define IOC_DIRMASK ((1 << IOC_DIRBITS) - 1)
#define IOC_NRSHIFT 0
#define IOC_TYPESHIFT (IOC_NRSHIFT + IOC_NRBITS)
#define IOC_SIZESHIFT (IOC_TYPESHIFT + IOC_TYPEBITS)
#define IOC_DIRSHIFT (IOC_SIZESHIFT + IOC_SIZEBITS)

#define IOC_DIR(nr) (((nr) >> IOC_DIRSHIFT) & IOC_DIRMASK)
#define IOC_TYPE(nr) (((nr) >> IOC_TYPESHIFT) & IOC_TYPEMASK)
#define IOC_NR(nr) (((nr) >> IOC_NRSHIFT) & IOC_NRMASK)

#if defined(__sparc__)
// In sparc the 14 bits SIZE field overlaps with the
// least significant bit of DIR, so either IOC_READ or
// IOC_WRITE shall be 1 in order to get a non-zero SIZE.
#define IOC_SIZE(nr) \
  ((((((nr) >> 29) & 0x7) & (4U | 2U)) == 0) ? 0 : (((nr) >> 16) & 0x3fff))
#else
#define IOC_SIZE(nr) (((nr) >> IOC_SIZESHIFT) & IOC_SIZEMASK)
#endif

extern unsigned struct_ifreq_sz;
extern unsigned struct_termios_sz;
extern unsigned struct_winsize_sz;

extern unsigned struct_sioc_sg_req_sz;
extern unsigned struct_sioc_vif_req_sz;

// ioctl request identifiers

// A special value to mark ioctls that are not present on the target platform,
// when it can not be determined without including any system headers.
extern const unsigned IOCTL_NOT_PRESENT;

extern unsigned IOCTL_FIOASYNC;
extern unsigned IOCTL_FIOCLEX;
extern unsigned IOCTL_FIOGETOWN;
extern unsigned IOCTL_FIONBIO;
extern unsigned IOCTL_FIONCLEX;
extern unsigned IOCTL_FIOSETOWN;
extern unsigned IOCTL_SIOCADDMULTI;
extern unsigned IOCTL_SIOCATMARK;
extern unsigned IOCTL_SIOCDELMULTI;
extern unsigned IOCTL_SIOCGIFADDR;
extern unsigned IOCTL_SIOCGIFBRDADDR;
extern unsigned IOCTL_SIOCGIFCONF;
extern unsigned IOCTL_SIOCGIFDSTADDR;
extern unsigned IOCTL_SIOCGIFFLAGS;
extern unsigned IOCTL_SIOCGIFMETRIC;
extern unsigned IOCTL_SIOCGIFMTU;
extern unsigned IOCTL_SIOCGIFNETMASK;
extern unsigned IOCTL_SIOCGPGRP;
extern unsigned IOCTL_SIOCSIFADDR;
extern unsigned IOCTL_SIOCSIFBRDADDR;
extern unsigned IOCTL_SIOCSIFDSTADDR;
extern unsigned IOCTL_SIOCSIFFLAGS;
extern unsigned IOCTL_SIOCSIFMETRIC;
extern unsigned IOCTL_SIOCSIFMTU;
extern unsigned IOCTL_SIOCSIFNETMASK;
extern unsigned IOCTL_SIOCSPGRP;
extern unsigned IOCTL_TIOCEXCL;
extern unsigned IOCTL_TIOCGETD;
extern unsigned IOCTL_TIOCGPGRP;
extern unsigned IOCTL_TIOCGWINSZ;
extern unsigned IOCTL_TIOCMBIC;
extern unsigned IOCTL_TIOCMBIS;
extern unsigned IOCTL_TIOCMGET;
extern unsigned IOCTL_TIOCMSET;
extern unsigned IOCTL_TIOCNOTTY;
extern unsigned IOCTL_TIOCNXCL;
extern unsigned IOCTL_TIOCOUTQ;
extern unsigned IOCTL_TIOCPKT;
extern unsigned IOCTL_TIOCSCTTY;
extern unsigned IOCTL_TIOCSETD;
extern unsigned IOCTL_TIOCSPGRP;
extern unsigned IOCTL_TIOCSTI;
extern unsigned IOCTL_TIOCSWINSZ;
extern unsigned IOCTL_MTIOCGET;
extern unsigned IOCTL_MTIOCTOP;

extern const int si_SEGV_MAPERR;
extern const int si_SEGV_ACCERR;
}  // namespace __sanitizer

#define CHECK_TYPE_SIZE(TYPE) \
  COMPILER_CHECK(sizeof(__sanitizer_##TYPE) == sizeof(TYPE))

#define CHECK_SIZE_AND_OFFSET(CLASS, MEMBER)                       \
  COMPILER_CHECK(sizeof(((__sanitizer_##CLASS *) NULL)->MEMBER) == \
                 sizeof(((CLASS *) NULL)->MEMBER));                \
  COMPILER_CHECK(offsetof(__sanitizer_##CLASS, MEMBER) ==          \
                 offsetof(CLASS, MEMBER))

// For sigaction, which is a function and struct at the same time,
// and thus requires explicit "struct" in sizeof() expression.
#define CHECK_STRUCT_SIZE_AND_OFFSET(CLASS, MEMBER)                       \
  COMPILER_CHECK(sizeof(((struct __sanitizer_##CLASS *) NULL)->MEMBER) == \
                 sizeof(((struct CLASS *) NULL)->MEMBER));                \
  COMPILER_CHECK(offsetof(struct __sanitizer_##CLASS, MEMBER) ==          \
                 offsetof(struct CLASS, MEMBER))

#endif  // SANITIZER_SOLARIS

#endif
