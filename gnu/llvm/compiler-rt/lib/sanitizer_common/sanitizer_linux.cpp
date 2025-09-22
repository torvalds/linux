//===-- sanitizer_linux.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements linux-specific functions from
// sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
    SANITIZER_OPENBSD || SANITIZER_SOLARIS

#  include "sanitizer_common.h"
#  include "sanitizer_flags.h"
#  include "sanitizer_getauxval.h"
#  include "sanitizer_internal_defs.h"
#  include "sanitizer_libc.h"
#  include "sanitizer_linux.h"
#  include "sanitizer_mutex.h"
#  include "sanitizer_placement_new.h"
#  include "sanitizer_procmaps.h"

#  if SANITIZER_LINUX && !SANITIZER_GO
#    include <asm/param.h>
#  endif

// For mips64, syscall(__NR_stat) fills the buffer in the 'struct kernel_stat'
// format. Struct kernel_stat is defined as 'struct stat' in asm/stat.h. To
// access stat from asm/stat.h, without conflicting with definition in
// sys/stat.h, we use this trick.  sparc64 is similar, using
// syscall(__NR_stat64) and struct kernel_stat64.
#  if SANITIZER_LINUX && (SANITIZER_MIPS64 || SANITIZER_SPARC64)
#    include <asm/unistd.h>
#    include <sys/types.h>
#    define stat kernel_stat
#    if SANITIZER_SPARC64
#      define stat64 kernel_stat64
#    endif
#    if SANITIZER_GO
#      undef st_atime
#      undef st_mtime
#      undef st_ctime
#      define st_atime st_atim
#      define st_mtime st_mtim
#      define st_ctime st_ctim
#    endif
#    include <asm/stat.h>
#    undef stat
#    undef stat64
#  endif

#  include <dlfcn.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <link.h>
#  include <pthread.h>
#  include <sched.h>
#  include <signal.h>
#  include <sys/mman.h>
#  if !SANITIZER_SOLARIS
#    include <sys/ptrace.h>
#  endif
#  include <sys/resource.h>
#  include <sys/stat.h>
#  include <sys/syscall.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <ucontext.h>
#  include <unistd.h>

#  if SANITIZER_LINUX
#    include <sys/utsname.h>
#  endif

#  if SANITIZER_LINUX && !SANITIZER_ANDROID
#    include <sys/personality.h>
#  endif

#  if SANITIZER_LINUX && defined(__loongarch__)
#    include <sys/sysmacros.h>
#  endif

#  if SANITIZER_FREEBSD
#    include <machine/atomic.h>
#    include <sys/exec.h>
#    include <sys/procctl.h>
#    include <sys/sysctl.h>
extern "C" {
// <sys/umtx.h> must be included after <errno.h> and <sys/types.h> on
// FreeBSD 9.2 and 10.0.
#    include <sys/umtx.h>
}
#    include <sys/thr.h>
#  endif  // SANITIZER_FREEBSD

#  if SANITIZER_NETBSD
#    include <limits.h>  // For NAME_MAX
#    include <sys/exec.h>
#    include <sys/sysctl.h>
extern struct ps_strings *__ps_strings;
#  endif  // SANITIZER_NETBSD

#  if SANITIZER_SOLARIS
#    include <stdlib.h>
#    include <thread.h>
#    define environ _environ
#  endif

extern char **environ;

#  if SANITIZER_LINUX
// <linux/time.h>
struct kernel_timeval {
  long tv_sec;
  long tv_usec;
};

// <linux/futex.h> is broken on some linux distributions.
const int FUTEX_WAIT = 0;
const int FUTEX_WAKE = 1;
const int FUTEX_PRIVATE_FLAG = 128;
const int FUTEX_WAIT_PRIVATE = FUTEX_WAIT | FUTEX_PRIVATE_FLAG;
const int FUTEX_WAKE_PRIVATE = FUTEX_WAKE | FUTEX_PRIVATE_FLAG;
#  endif  // SANITIZER_LINUX

// Are we using 32-bit or 64-bit Linux syscalls?
// x32 (which defines __x86_64__) has SANITIZER_WORDSIZE == 32
// but it still needs to use 64-bit syscalls.
#  if SANITIZER_LINUX && (defined(__x86_64__) || defined(__powerpc64__) || \
                          SANITIZER_WORDSIZE == 64 ||                      \
                          (defined(__mips__) && _MIPS_SIM == _ABIN32))
#    define SANITIZER_LINUX_USES_64BIT_SYSCALLS 1
#  else
#    define SANITIZER_LINUX_USES_64BIT_SYSCALLS 0
#  endif

// Note : FreeBSD implemented both Linux and OpenBSD apis.
#  if SANITIZER_LINUX && defined(__NR_getrandom)
#    if !defined(GRND_NONBLOCK)
#      define GRND_NONBLOCK 1
#    endif
#    define SANITIZER_USE_GETRANDOM 1
#  else
#    define SANITIZER_USE_GETRANDOM 0
#  endif  // SANITIZER_LINUX && defined(__NR_getrandom)

#  if SANITIZER_FREEBSD
#    define SANITIZER_USE_GETENTROPY 1
#  endif

namespace __sanitizer {

void SetSigProcMask(__sanitizer_sigset_t *set, __sanitizer_sigset_t *oldset) {
  CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, set, oldset));
}

#  if SANITIZER_LINUX
// Deletes the specified signal from newset, if it is not present in oldset
// Equivalently: newset[signum] = newset[signum] & oldset[signum]
static void KeepUnblocked(__sanitizer_sigset_t &newset,
                          __sanitizer_sigset_t &oldset, int signum) {
  // FIXME: https://github.com/google/sanitizers/issues/1816
  if (SANITIZER_ANDROID || !internal_sigismember(&oldset, signum))
    internal_sigdelset(&newset, signum);
}
#  endif

// Block asynchronous signals
void BlockSignals(__sanitizer_sigset_t *oldset) {
  __sanitizer_sigset_t newset;
  internal_sigfillset(&newset);

#  if SANITIZER_LINUX
  __sanitizer_sigset_t currentset;

#    if !SANITIZER_ANDROID
  // FIXME: https://github.com/google/sanitizers/issues/1816
  SetSigProcMask(NULL, &currentset);

  // Glibc uses SIGSETXID signal during setuid call. If this signal is blocked
  // on any thread, setuid call hangs.
  // See test/sanitizer_common/TestCases/Linux/setuid.c.
  KeepUnblocked(newset, currentset, 33);
#    endif  // !SANITIZER_ANDROID

  // Seccomp-BPF-sandboxed processes rely on SIGSYS to handle trapped syscalls.
  // If this signal is blocked, such calls cannot be handled and the process may
  // hang.
  KeepUnblocked(newset, currentset, 31);

#    if !SANITIZER_ANDROID
  // Don't block synchronous signals
  // but also don't unblock signals that the user had deliberately blocked.
  // FIXME: https://github.com/google/sanitizers/issues/1816
  KeepUnblocked(newset, currentset, SIGSEGV);
  KeepUnblocked(newset, currentset, SIGBUS);
  KeepUnblocked(newset, currentset, SIGILL);
  KeepUnblocked(newset, currentset, SIGTRAP);
  KeepUnblocked(newset, currentset, SIGABRT);
  KeepUnblocked(newset, currentset, SIGFPE);
  KeepUnblocked(newset, currentset, SIGPIPE);
#    endif  //! SANITIZER_ANDROID

#  endif  // SANITIZER_LINUX

  SetSigProcMask(&newset, oldset);
}

ScopedBlockSignals::ScopedBlockSignals(__sanitizer_sigset_t *copy) {
  BlockSignals(&saved_);
  if (copy)
    internal_memcpy(copy, &saved_, sizeof(saved_));
}

ScopedBlockSignals::~ScopedBlockSignals() { SetSigProcMask(&saved_, nullptr); }

#  if SANITIZER_LINUX && defined(__x86_64__)
#    include "sanitizer_syscall_linux_x86_64.inc"
#  elif SANITIZER_LINUX && SANITIZER_RISCV64
#    include "sanitizer_syscall_linux_riscv64.inc"
#  elif SANITIZER_LINUX && defined(__aarch64__)
#    include "sanitizer_syscall_linux_aarch64.inc"
#  elif SANITIZER_LINUX && defined(__arm__)
#    include "sanitizer_syscall_linux_arm.inc"
#  elif SANITIZER_LINUX && defined(__hexagon__)
#    include "sanitizer_syscall_linux_hexagon.inc"
#  elif SANITIZER_LINUX && SANITIZER_LOONGARCH64
#    include "sanitizer_syscall_linux_loongarch64.inc"
#  else
#    include "sanitizer_syscall_generic.inc"
#  endif

// --------------- sanitizer_libc.h
#  if !SANITIZER_SOLARIS && !SANITIZER_NETBSD
#    if !SANITIZER_S390
uptr internal_mmap(void *addr, uptr length, int prot, int flags, int fd,
                   u64 offset) {
#      if SANITIZER_FREEBSD || SANITIZER_LINUX_USES_64BIT_SYSCALLS
  return internal_syscall(SYSCALL(mmap), (uptr)addr, length, prot, flags, fd,
                          offset);
#      else
  // mmap2 specifies file offset in 4096-byte units.
  CHECK(IsAligned(offset, 4096));
  return internal_syscall(SYSCALL(mmap2), addr, length, prot, flags, fd,
                          (OFF_T)(offset / 4096));
#      endif
}
#    endif  // !SANITIZER_S390

uptr internal_munmap(void *addr, uptr length) {
  return internal_syscall(SYSCALL(munmap), (uptr)addr, length);
}

#    if SANITIZER_LINUX
uptr internal_mremap(void *old_address, uptr old_size, uptr new_size, int flags,
                     void *new_address) {
  return internal_syscall(SYSCALL(mremap), (uptr)old_address, old_size,
                          new_size, flags, (uptr)new_address);
}
#    endif

int internal_mprotect(void *addr, uptr length, int prot) {
  return internal_syscall(SYSCALL(mprotect), (uptr)addr, length, prot);
}

int internal_madvise(uptr addr, uptr length, int advice) {
  return internal_syscall(SYSCALL(madvise), addr, length, advice);
}

uptr internal_close(fd_t fd) { return internal_syscall(SYSCALL(close), fd); }

uptr internal_open(const char *filename, int flags) {
#    if SANITIZER_LINUX
  return internal_syscall(SYSCALL(openat), AT_FDCWD, (uptr)filename, flags);
#    else
  return internal_syscall(SYSCALL(open), (uptr)filename, flags);
#    endif
}

uptr internal_open(const char *filename, int flags, u32 mode) {
#    if SANITIZER_LINUX
  return internal_syscall(SYSCALL(openat), AT_FDCWD, (uptr)filename, flags,
                          mode);
#    else
  return internal_syscall(SYSCALL(open), (uptr)filename, flags, mode);
#    endif
}

uptr internal_read(fd_t fd, void *buf, uptr count) {
  sptr res;
  HANDLE_EINTR(res,
               (sptr)internal_syscall(SYSCALL(read), fd, (uptr)buf, count));
  return res;
}

uptr internal_write(fd_t fd, const void *buf, uptr count) {
  sptr res;
  HANDLE_EINTR(res,
               (sptr)internal_syscall(SYSCALL(write), fd, (uptr)buf, count));
  return res;
}

uptr internal_ftruncate(fd_t fd, uptr size) {
  sptr res;
  HANDLE_EINTR(res,
               (sptr)internal_syscall(SYSCALL(ftruncate), fd, (OFF_T)size));
  return res;
}

#    if !SANITIZER_LINUX_USES_64BIT_SYSCALLS && SANITIZER_LINUX
static void stat64_to_stat(struct stat64 *in, struct stat *out) {
  internal_memset(out, 0, sizeof(*out));
  out->st_dev = in->st_dev;
  out->st_ino = in->st_ino;
  out->st_mode = in->st_mode;
  out->st_nlink = in->st_nlink;
  out->st_uid = in->st_uid;
  out->st_gid = in->st_gid;
  out->st_rdev = in->st_rdev;
  out->st_size = in->st_size;
  out->st_blksize = in->st_blksize;
  out->st_blocks = in->st_blocks;
  out->st_atime = in->st_atime;
  out->st_mtime = in->st_mtime;
  out->st_ctime = in->st_ctime;
}
#    endif

#    if SANITIZER_LINUX && defined(__loongarch__)
static void statx_to_stat(struct statx *in, struct stat *out) {
  internal_memset(out, 0, sizeof(*out));
  out->st_dev = makedev(in->stx_dev_major, in->stx_dev_minor);
  out->st_ino = in->stx_ino;
  out->st_mode = in->stx_mode;
  out->st_nlink = in->stx_nlink;
  out->st_uid = in->stx_uid;
  out->st_gid = in->stx_gid;
  out->st_rdev = makedev(in->stx_rdev_major, in->stx_rdev_minor);
  out->st_size = in->stx_size;
  out->st_blksize = in->stx_blksize;
  out->st_blocks = in->stx_blocks;
  out->st_atime = in->stx_atime.tv_sec;
  out->st_atim.tv_nsec = in->stx_atime.tv_nsec;
  out->st_mtime = in->stx_mtime.tv_sec;
  out->st_mtim.tv_nsec = in->stx_mtime.tv_nsec;
  out->st_ctime = in->stx_ctime.tv_sec;
  out->st_ctim.tv_nsec = in->stx_ctime.tv_nsec;
}
#    endif

#    if SANITIZER_MIPS64 || SANITIZER_SPARC64
#      if SANITIZER_MIPS64
typedef struct kernel_stat kstat_t;
#      else
typedef struct kernel_stat64 kstat_t;
#      endif
// Undefine compatibility macros from <sys/stat.h>
// so that they would not clash with the kernel_stat
// st_[a|m|c]time fields
#      if !SANITIZER_GO
#        undef st_atime
#        undef st_mtime
#        undef st_ctime
#      endif
#      if defined(SANITIZER_ANDROID)
// Bionic sys/stat.h defines additional macros
// for compatibility with the old NDKs and
// they clash with the kernel_stat structure
// st_[a|m|c]time_nsec fields.
#        undef st_atime_nsec
#        undef st_mtime_nsec
#        undef st_ctime_nsec
#      endif
static void kernel_stat_to_stat(kstat_t *in, struct stat *out) {
  internal_memset(out, 0, sizeof(*out));
  out->st_dev = in->st_dev;
  out->st_ino = in->st_ino;
  out->st_mode = in->st_mode;
  out->st_nlink = in->st_nlink;
  out->st_uid = in->st_uid;
  out->st_gid = in->st_gid;
  out->st_rdev = in->st_rdev;
  out->st_size = in->st_size;
  out->st_blksize = in->st_blksize;
  out->st_blocks = in->st_blocks;
#      if defined(__USE_MISC) || defined(__USE_XOPEN2K8) || \
          defined(SANITIZER_ANDROID)
  out->st_atim.tv_sec = in->st_atime;
  out->st_atim.tv_nsec = in->st_atime_nsec;
  out->st_mtim.tv_sec = in->st_mtime;
  out->st_mtim.tv_nsec = in->st_mtime_nsec;
  out->st_ctim.tv_sec = in->st_ctime;
  out->st_ctim.tv_nsec = in->st_ctime_nsec;
#      else
  out->st_atime = in->st_atime;
  out->st_atimensec = in->st_atime_nsec;
  out->st_mtime = in->st_mtime;
  out->st_mtimensec = in->st_mtime_nsec;
  out->st_ctime = in->st_ctime;
  out->st_atimensec = in->st_ctime_nsec;
#      endif
}
#    endif

uptr internal_stat(const char *path, void *buf) {
#    if SANITIZER_FREEBSD
  return internal_syscall(SYSCALL(fstatat), AT_FDCWD, (uptr)path, (uptr)buf, 0);
#    elif SANITIZER_LINUX
#      if defined(__loongarch__)
  struct statx bufx;
  int res = internal_syscall(SYSCALL(statx), AT_FDCWD, (uptr)path,
                             AT_NO_AUTOMOUNT, STATX_BASIC_STATS, (uptr)&bufx);
  statx_to_stat(&bufx, (struct stat *)buf);
  return res;
#      elif (SANITIZER_WORDSIZE == 64 || SANITIZER_X32 ||    \
             (defined(__mips__) && _MIPS_SIM == _ABIN32)) && \
          !SANITIZER_SPARC
  return internal_syscall(SYSCALL(newfstatat), AT_FDCWD, (uptr)path, (uptr)buf,
                          0);
#      elif SANITIZER_SPARC64
  kstat_t buf64;
  int res = internal_syscall(SYSCALL(fstatat64), AT_FDCWD, (uptr)path,
                             (uptr)&buf64, 0);
  kernel_stat_to_stat(&buf64, (struct stat *)buf);
  return res;
#      else
  struct stat64 buf64;
  int res = internal_syscall(SYSCALL(fstatat64), AT_FDCWD, (uptr)path,
                             (uptr)&buf64, 0);
  stat64_to_stat(&buf64, (struct stat *)buf);
  return res;
#      endif
#    else
  struct stat64 buf64;
  int res = internal_syscall(SYSCALL(stat64), path, &buf64);
  stat64_to_stat(&buf64, (struct stat *)buf);
  return res;
#    endif
}

uptr internal_lstat(const char *path, void *buf) {
#    if SANITIZER_FREEBSD
  return internal_syscall(SYSCALL(fstatat), AT_FDCWD, (uptr)path, (uptr)buf,
                          AT_SYMLINK_NOFOLLOW);
#    elif SANITIZER_LINUX
#      if defined(__loongarch__)
  struct statx bufx;
  int res = internal_syscall(SYSCALL(statx), AT_FDCWD, (uptr)path,
                             AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT,
                             STATX_BASIC_STATS, (uptr)&bufx);
  statx_to_stat(&bufx, (struct stat *)buf);
  return res;
#      elif (defined(_LP64) || SANITIZER_X32 ||              \
             (defined(__mips__) && _MIPS_SIM == _ABIN32)) && \
          !SANITIZER_SPARC
  return internal_syscall(SYSCALL(newfstatat), AT_FDCWD, (uptr)path, (uptr)buf,
                          AT_SYMLINK_NOFOLLOW);
#      elif SANITIZER_SPARC64
  kstat_t buf64;
  int res = internal_syscall(SYSCALL(fstatat64), AT_FDCWD, (uptr)path,
                             (uptr)&buf64, AT_SYMLINK_NOFOLLOW);
  kernel_stat_to_stat(&buf64, (struct stat *)buf);
  return res;
#      else
  struct stat64 buf64;
  int res = internal_syscall(SYSCALL(fstatat64), AT_FDCWD, (uptr)path,
                             (uptr)&buf64, AT_SYMLINK_NOFOLLOW);
  stat64_to_stat(&buf64, (struct stat *)buf);
  return res;
#      endif
#    else
  struct stat64 buf64;
  int res = internal_syscall(SYSCALL(lstat64), path, &buf64);
  stat64_to_stat(&buf64, (struct stat *)buf);
  return res;
#    endif
}

uptr internal_fstat(fd_t fd, void *buf) {
#    if SANITIZER_FREEBSD || SANITIZER_LINUX_USES_64BIT_SYSCALLS
#      if SANITIZER_MIPS64
  // For mips64, fstat syscall fills buffer in the format of kernel_stat
  kstat_t kbuf;
  int res = internal_syscall(SYSCALL(fstat), fd, &kbuf);
  kernel_stat_to_stat(&kbuf, (struct stat *)buf);
  return res;
#      elif SANITIZER_LINUX && SANITIZER_SPARC64
  // For sparc64, fstat64 syscall fills buffer in the format of kernel_stat64
  kstat_t kbuf;
  int res = internal_syscall(SYSCALL(fstat64), fd, &kbuf);
  kernel_stat_to_stat(&kbuf, (struct stat *)buf);
  return res;
#      elif SANITIZER_LINUX && defined(__loongarch__)
  struct statx bufx;
  int res = internal_syscall(SYSCALL(statx), fd, "", AT_EMPTY_PATH,
                             STATX_BASIC_STATS, (uptr)&bufx);
  statx_to_stat(&bufx, (struct stat *)buf);
  return res;
#      else
  return internal_syscall(SYSCALL(fstat), fd, (uptr)buf);
#      endif
#    else
  struct stat64 buf64;
  int res = internal_syscall(SYSCALL(fstat64), fd, &buf64);
  stat64_to_stat(&buf64, (struct stat *)buf);
  return res;
#    endif
}

uptr internal_filesize(fd_t fd) {
  struct stat st;
  if (internal_fstat(fd, &st))
    return -1;
  return (uptr)st.st_size;
}

uptr internal_dup(int oldfd) { return internal_syscall(SYSCALL(dup), oldfd); }

uptr internal_dup2(int oldfd, int newfd) {
#    if SANITIZER_LINUX
  return internal_syscall(SYSCALL(dup3), oldfd, newfd, 0);
#    else
  return internal_syscall(SYSCALL(dup2), oldfd, newfd);
#    endif
}

uptr internal_readlink(const char *path, char *buf, uptr bufsize) {
#    if SANITIZER_LINUX
  return internal_syscall(SYSCALL(readlinkat), AT_FDCWD, (uptr)path, (uptr)buf,
                          bufsize);
#    else
  return internal_syscall(SYSCALL(readlink), (uptr)path, (uptr)buf, bufsize);
#    endif
}

uptr internal_unlink(const char *path) {
#    if SANITIZER_LINUX
  return internal_syscall(SYSCALL(unlinkat), AT_FDCWD, (uptr)path, 0);
#    else
  return internal_syscall(SYSCALL(unlink), (uptr)path);
#    endif
}

uptr internal_rename(const char *oldpath, const char *newpath) {
#    if (defined(__riscv) || defined(__loongarch__)) && defined(__linux__)
  return internal_syscall(SYSCALL(renameat2), AT_FDCWD, (uptr)oldpath, AT_FDCWD,
                          (uptr)newpath, 0);
#    elif SANITIZER_LINUX
  return internal_syscall(SYSCALL(renameat), AT_FDCWD, (uptr)oldpath, AT_FDCWD,
                          (uptr)newpath);
#    else
  return internal_syscall(SYSCALL(rename), (uptr)oldpath, (uptr)newpath);
#    endif
}

uptr internal_sched_yield() { return internal_syscall(SYSCALL(sched_yield)); }

void internal_usleep(u64 useconds) {
  struct timespec ts;
  ts.tv_sec = useconds / 1000000;
  ts.tv_nsec = (useconds % 1000000) * 1000;
  internal_syscall(SYSCALL(nanosleep), &ts, &ts);
}

uptr internal_execve(const char *filename, char *const argv[],
                     char *const envp[]) {
  return internal_syscall(SYSCALL(execve), (uptr)filename, (uptr)argv,
                          (uptr)envp);
}
#  endif  // !SANITIZER_SOLARIS && !SANITIZER_NETBSD

#  if !SANITIZER_NETBSD
void internal__exit(int exitcode) {
#    if SANITIZER_FREEBSD || SANITIZER_SOLARIS
  internal_syscall(SYSCALL(exit), exitcode);
#    else
  internal_syscall(SYSCALL(exit_group), exitcode);
#    endif
  Die();  // Unreachable.
}
#  endif  // !SANITIZER_NETBSD

// ----------------- sanitizer_common.h
bool FileExists(const char *filename) {
  if (ShouldMockFailureToOpen(filename))
    return false;
  struct stat st;
  if (internal_stat(filename, &st))
    return false;
  // Sanity check: filename is a regular file.
  return S_ISREG(st.st_mode);
}

bool DirExists(const char *path) {
  struct stat st;
  if (internal_stat(path, &st))
    return false;
  return S_ISDIR(st.st_mode);
}

#  if !SANITIZER_NETBSD
tid_t GetTid() {
#    if SANITIZER_FREEBSD
  long Tid;
  thr_self(&Tid);
  return Tid;
#    elif SANITIZER_SOLARIS
  return thr_self();
#    else
  return internal_syscall(SYSCALL(gettid));
#    endif
}

int TgKill(pid_t pid, tid_t tid, int sig) {
#    if SANITIZER_LINUX
  return internal_syscall(SYSCALL(tgkill), pid, tid, sig);
#    elif SANITIZER_FREEBSD
  return internal_syscall(SYSCALL(thr_kill2), pid, tid, sig);
#    elif SANITIZER_SOLARIS
  (void)pid;
  errno = thr_kill(tid, sig);
  // TgKill is expected to return -1 on error, not an errno.
  return errno != 0 ? -1 : 0;
#    endif
}
#  endif

#  if SANITIZER_GLIBC
u64 NanoTime() {
  kernel_timeval tv;
  internal_memset(&tv, 0, sizeof(tv));
  internal_syscall(SYSCALL(gettimeofday), &tv, 0);
  return (u64)tv.tv_sec * 1000 * 1000 * 1000 + tv.tv_usec * 1000;
}
// Used by real_clock_gettime.
uptr internal_clock_gettime(__sanitizer_clockid_t clk_id, void *tp) {
  return internal_syscall(SYSCALL(clock_gettime), clk_id, tp);
}
#  elif !SANITIZER_SOLARIS && !SANITIZER_NETBSD
u64 NanoTime() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (u64)ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}
#  endif

// Like getenv, but reads env directly from /proc (on Linux) or parses the
// 'environ' array (on some others) and does not use libc. This function
// should be called first inside __asan_init.
const char *GetEnv(const char *name) {
#  if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_SOLARIS
  if (::environ != 0) {
    uptr NameLen = internal_strlen(name);
    for (char **Env = ::environ; *Env != 0; Env++) {
      if (internal_strncmp(*Env, name, NameLen) == 0 && (*Env)[NameLen] == '=')
        return (*Env) + NameLen + 1;
    }
  }
  return 0;  // Not found.
#  elif SANITIZER_LINUX
  static char *environ;
  static uptr len;
  static bool inited;
  if (!inited) {
    inited = true;
    uptr environ_size;
    if (!ReadFileToBuffer("/proc/self/environ", &environ, &environ_size, &len))
      environ = nullptr;
  }
  if (!environ || len == 0)
    return nullptr;
  uptr namelen = internal_strlen(name);
  const char *p = environ;
  while (*p != '\0') {  // will happen at the \0\0 that terminates the buffer
    // proc file has the format NAME=value\0NAME=value\0NAME=value\0...
    const char *endp = (char *)internal_memchr(p, '\0', len - (p - environ));
    if (!endp)  // this entry isn't NUL terminated
      return nullptr;
    else if (!internal_memcmp(p, name, namelen) && p[namelen] == '=')  // Match.
      return p + namelen + 1;  // point after =
    p = endp + 1;
  }
  return nullptr;  // Not found.
#  else
#    error "Unsupported platform"
#  endif
}

#  if !SANITIZER_FREEBSD && !SANITIZER_NETBSD && !SANITIZER_GO
extern "C" {
SANITIZER_WEAK_ATTRIBUTE extern void *__libc_stack_end;
}
#  endif

#  if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
static void ReadNullSepFileToArray(const char *path, char ***arr,
                                   int arr_size) {
  char *buff;
  uptr buff_size;
  uptr buff_len;
  *arr = (char **)MmapOrDie(arr_size * sizeof(char *), "NullSepFileArray");
  if (!ReadFileToBuffer(path, &buff, &buff_size, &buff_len, 1024 * 1024)) {
    (*arr)[0] = nullptr;
    return;
  }
  (*arr)[0] = buff;
  int count, i;
  for (count = 1, i = 1;; i++) {
    if (buff[i] == 0) {
      if (buff[i + 1] == 0)
        break;
      (*arr)[count] = &buff[i + 1];
      CHECK_LE(count, arr_size - 1);  // FIXME: make this more flexible.
      count++;
    }
  }
  (*arr)[count] = nullptr;
}
#  endif

static void GetArgsAndEnv(char ***argv, char ***envp) {
#  if SANITIZER_FREEBSD
  // On FreeBSD, retrieving the argument and environment arrays is done via the
  // kern.ps_strings sysctl, which returns a pointer to a structure containing
  // this information. See also <sys/exec.h>.
  ps_strings *pss;
  uptr sz = sizeof(pss);
  if (internal_sysctlbyname("kern.ps_strings", &pss, &sz, NULL, 0) == -1) {
    Printf("sysctl kern.ps_strings failed\n");
    Die();
  }
  *argv = pss->ps_argvstr;
  *envp = pss->ps_envstr;
#  elif SANITIZER_NETBSD
  *argv = __ps_strings->ps_argvstr;
  *envp = __ps_strings->ps_envstr;
#  else  // SANITIZER_FREEBSD
#    if !SANITIZER_GO
  if (&__libc_stack_end) {
    uptr *stack_end = (uptr *)__libc_stack_end;
    // Normally argc can be obtained from *stack_end, however, on ARM glibc's
    // _start clobbers it:
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/arm/start.S;hb=refs/heads/release/2.31/master#l75
    // Do not special-case ARM and infer argc from argv everywhere.
    int argc = 0;
    while (stack_end[argc + 1]) argc++;
    *argv = (char **)(stack_end + 1);
    *envp = (char **)(stack_end + argc + 2);
  } else {
#    endif  // !SANITIZER_GO
    static const int kMaxArgv = 2000, kMaxEnvp = 2000;
    ReadNullSepFileToArray("/proc/self/cmdline", argv, kMaxArgv);
    ReadNullSepFileToArray("/proc/self/environ", envp, kMaxEnvp);
#    if !SANITIZER_GO
  }
#    endif  // !SANITIZER_GO
#  endif    // SANITIZER_FREEBSD
}

char **GetArgv() {
  char **argv, **envp;
  GetArgsAndEnv(&argv, &envp);
  return argv;
}

char **GetEnviron() {
  char **argv, **envp;
  GetArgsAndEnv(&argv, &envp);
  return envp;
}

#  if !SANITIZER_SOLARIS
void FutexWait(atomic_uint32_t *p, u32 cmp) {
#    if SANITIZER_FREEBSD
  _umtx_op(p, UMTX_OP_WAIT_UINT, cmp, 0, 0);
#    elif SANITIZER_NETBSD
  sched_yield(); /* No userspace futex-like synchronization */
#    else
  internal_syscall(SYSCALL(futex), (uptr)p, FUTEX_WAIT_PRIVATE, cmp, 0, 0, 0);
#    endif
}

void FutexWake(atomic_uint32_t *p, u32 count) {
#    if SANITIZER_FREEBSD
  _umtx_op(p, UMTX_OP_WAKE, count, 0, 0);
#    elif SANITIZER_NETBSD
  /* No userspace futex-like synchronization */
#    else
  internal_syscall(SYSCALL(futex), (uptr)p, FUTEX_WAKE_PRIVATE, count, 0, 0, 0);
#    endif
}

#  endif  // !SANITIZER_SOLARIS

// ----------------- sanitizer_linux.h
// The actual size of this structure is specified by d_reclen.
// Note that getdents64 uses a different structure format. We only provide the
// 32-bit syscall here.
#  if SANITIZER_NETBSD
// Not used
#  else
struct linux_dirent {
#    if SANITIZER_X32 || SANITIZER_LINUX
  u64 d_ino;
  u64 d_off;
#    else
  unsigned long d_ino;
  unsigned long d_off;
#    endif
  unsigned short d_reclen;
#    if SANITIZER_LINUX
  unsigned char d_type;
#    endif
  char d_name[256];
};
#  endif

#  if !SANITIZER_SOLARIS && !SANITIZER_NETBSD
// Syscall wrappers.
uptr internal_ptrace(int request, int pid, void *addr, void *data) {
  return internal_syscall(SYSCALL(ptrace), request, pid, (uptr)addr,
                          (uptr)data);
}

uptr internal_waitpid(int pid, int *status, int options) {
  return internal_syscall(SYSCALL(wait4), pid, (uptr)status, options,
                          0 /* rusage */);
}

uptr internal_getpid() { return internal_syscall(SYSCALL(getpid)); }

uptr internal_getppid() { return internal_syscall(SYSCALL(getppid)); }

int internal_dlinfo(void *handle, int request, void *p) {
#    if SANITIZER_FREEBSD
  return dlinfo(handle, request, p);
#    else
  UNIMPLEMENTED();
#    endif
}

uptr internal_getdents(fd_t fd, struct linux_dirent *dirp, unsigned int count) {
#    if SANITIZER_FREEBSD
  return internal_syscall(SYSCALL(getdirentries), fd, (uptr)dirp, count, NULL);
#    elif SANITIZER_LINUX
  return internal_syscall(SYSCALL(getdents64), fd, (uptr)dirp, count);
#    else
  return internal_syscall(SYSCALL(getdents), fd, (uptr)dirp, count);
#    endif
}

uptr internal_lseek(fd_t fd, OFF_T offset, int whence) {
  return internal_syscall(SYSCALL(lseek), fd, offset, whence);
}

#    if SANITIZER_LINUX
uptr internal_prctl(int option, uptr arg2, uptr arg3, uptr arg4, uptr arg5) {
  return internal_syscall(SYSCALL(prctl), option, arg2, arg3, arg4, arg5);
}
#      if defined(__x86_64__)
#        include <asm/unistd_64.h>
// Currently internal_arch_prctl() is only needed on x86_64.
uptr internal_arch_prctl(int option, uptr arg2) {
  return internal_syscall(__NR_arch_prctl, option, arg2);
}
#      endif
#    endif

uptr internal_sigaltstack(const void *ss, void *oss) {
  return internal_syscall(SYSCALL(sigaltstack), (uptr)ss, (uptr)oss);
}

extern "C" pid_t __fork(void);

int internal_fork() {
#    if SANITIZER_LINUX
#      if SANITIZER_S390
  return internal_syscall(SYSCALL(clone), 0, SIGCHLD);
#      elif SANITIZER_SPARC
  // The clone syscall interface on SPARC differs massively from the rest,
  // so fall back to __fork.
  return __fork();
#      else
  return internal_syscall(SYSCALL(clone), SIGCHLD, 0);
#      endif
#    else
  return internal_syscall(SYSCALL(fork));
#    endif
}

#    if SANITIZER_FREEBSD
int internal_sysctl(const int *name, unsigned int namelen, void *oldp,
                    uptr *oldlenp, const void *newp, uptr newlen) {
  return internal_syscall(SYSCALL(__sysctl), name, namelen, oldp,
                          (size_t *)oldlenp, newp, (size_t)newlen);
}

int internal_sysctlbyname(const char *sname, void *oldp, uptr *oldlenp,
                          const void *newp, uptr newlen) {
  // Note: this function can be called during startup, so we need to avoid
  // calling any interceptable functions. On FreeBSD >= 1300045 sysctlbyname()
  // is a real syscall, but for older versions it calls sysctlnametomib()
  // followed by sysctl(). To avoid calling the intercepted version and
  // asserting if this happens during startup, call the real sysctlnametomib()
  // followed by internal_sysctl() if the syscall is not available.
#      ifdef SYS___sysctlbyname
  return internal_syscall(SYSCALL(__sysctlbyname), sname,
                          internal_strlen(sname), oldp, (size_t *)oldlenp, newp,
                          (size_t)newlen);
#      else
  static decltype(sysctlnametomib) *real_sysctlnametomib = nullptr;
  if (!real_sysctlnametomib)
    real_sysctlnametomib =
        (decltype(sysctlnametomib) *)dlsym(RTLD_NEXT, "sysctlnametomib");
  CHECK(real_sysctlnametomib);

  int oid[CTL_MAXNAME];
  size_t len = CTL_MAXNAME;
  if (real_sysctlnametomib(sname, oid, &len) == -1)
    return (-1);
  return internal_sysctl(oid, len, oldp, oldlenp, newp, newlen);
#      endif
}
#    endif

#    if SANITIZER_LINUX
#      define SA_RESTORER 0x04000000
// Doesn't set sa_restorer if the caller did not set it, so use with caution
//(see below).
int internal_sigaction_norestorer(int signum, const void *act, void *oldact) {
  __sanitizer_kernel_sigaction_t k_act, k_oldact;
  internal_memset(&k_act, 0, sizeof(__sanitizer_kernel_sigaction_t));
  internal_memset(&k_oldact, 0, sizeof(__sanitizer_kernel_sigaction_t));
  const __sanitizer_sigaction *u_act = (const __sanitizer_sigaction *)act;
  __sanitizer_sigaction *u_oldact = (__sanitizer_sigaction *)oldact;
  if (u_act) {
    k_act.handler = u_act->handler;
    k_act.sigaction = u_act->sigaction;
    internal_memcpy(&k_act.sa_mask, &u_act->sa_mask,
                    sizeof(__sanitizer_kernel_sigset_t));
    // Without SA_RESTORER kernel ignores the calls (probably returns EINVAL).
    k_act.sa_flags = u_act->sa_flags | SA_RESTORER;
    // FIXME: most often sa_restorer is unset, however the kernel requires it
    // to point to a valid signal restorer that calls the rt_sigreturn syscall.
    // If sa_restorer passed to the kernel is NULL, the program may crash upon
    // signal delivery or fail to unwind the stack in the signal handler.
    // libc implementation of sigaction() passes its own restorer to
    // rt_sigaction, so we need to do the same (we'll need to reimplement the
    // restorers; for x86_64 the restorer address can be obtained from
    // oldact->sa_restorer upon a call to sigaction(xxx, NULL, oldact).
#      if !SANITIZER_ANDROID || !SANITIZER_MIPS32
    k_act.sa_restorer = u_act->sa_restorer;
#      endif
  }

  uptr result = internal_syscall(SYSCALL(rt_sigaction), (uptr)signum,
                                 (uptr)(u_act ? &k_act : nullptr),
                                 (uptr)(u_oldact ? &k_oldact : nullptr),
                                 (uptr)sizeof(__sanitizer_kernel_sigset_t));

  if ((result == 0) && u_oldact) {
    u_oldact->handler = k_oldact.handler;
    u_oldact->sigaction = k_oldact.sigaction;
    internal_memcpy(&u_oldact->sa_mask, &k_oldact.sa_mask,
                    sizeof(__sanitizer_kernel_sigset_t));
    u_oldact->sa_flags = k_oldact.sa_flags;
#      if !SANITIZER_ANDROID || !SANITIZER_MIPS32
    u_oldact->sa_restorer = k_oldact.sa_restorer;
#      endif
  }
  return result;
}
#    endif  // SANITIZER_LINUX

uptr internal_sigprocmask(int how, __sanitizer_sigset_t *set,
                          __sanitizer_sigset_t *oldset) {
#    if SANITIZER_FREEBSD
  return internal_syscall(SYSCALL(sigprocmask), how, set, oldset);
#    else
  __sanitizer_kernel_sigset_t *k_set = (__sanitizer_kernel_sigset_t *)set;
  __sanitizer_kernel_sigset_t *k_oldset = (__sanitizer_kernel_sigset_t *)oldset;
  return internal_syscall(SYSCALL(rt_sigprocmask), (uptr)how, (uptr)k_set,
                          (uptr)k_oldset, sizeof(__sanitizer_kernel_sigset_t));
#    endif
}

void internal_sigfillset(__sanitizer_sigset_t *set) {
  internal_memset(set, 0xff, sizeof(*set));
}

void internal_sigemptyset(__sanitizer_sigset_t *set) {
  internal_memset(set, 0, sizeof(*set));
}

#    if SANITIZER_LINUX
void internal_sigdelset(__sanitizer_sigset_t *set, int signum) {
  signum -= 1;
  CHECK_GE(signum, 0);
  CHECK_LT(signum, sizeof(*set) * 8);
  __sanitizer_kernel_sigset_t *k_set = (__sanitizer_kernel_sigset_t *)set;
  const uptr idx = signum / (sizeof(k_set->sig[0]) * 8);
  const uptr bit = signum % (sizeof(k_set->sig[0]) * 8);
  k_set->sig[idx] &= ~((uptr)1 << bit);
}

bool internal_sigismember(__sanitizer_sigset_t *set, int signum) {
  signum -= 1;
  CHECK_GE(signum, 0);
  CHECK_LT(signum, sizeof(*set) * 8);
  __sanitizer_kernel_sigset_t *k_set = (__sanitizer_kernel_sigset_t *)set;
  const uptr idx = signum / (sizeof(k_set->sig[0]) * 8);
  const uptr bit = signum % (sizeof(k_set->sig[0]) * 8);
  return k_set->sig[idx] & ((uptr)1 << bit);
}
#    elif SANITIZER_FREEBSD
uptr internal_procctl(int type, int id, int cmd, void *data) {
  return internal_syscall(SYSCALL(procctl), type, id, cmd, data);
}

void internal_sigdelset(__sanitizer_sigset_t *set, int signum) {
  sigset_t *rset = reinterpret_cast<sigset_t *>(set);
  sigdelset(rset, signum);
}

bool internal_sigismember(__sanitizer_sigset_t *set, int signum) {
  sigset_t *rset = reinterpret_cast<sigset_t *>(set);
  return sigismember(rset, signum);
}
#    endif
#  endif  // !SANITIZER_SOLARIS

#  if !SANITIZER_NETBSD
// ThreadLister implementation.
ThreadLister::ThreadLister(pid_t pid) : pid_(pid), buffer_(4096) {
  char task_directory_path[80];
  internal_snprintf(task_directory_path, sizeof(task_directory_path),
                    "/proc/%d/task/", pid);
  descriptor_ = internal_open(task_directory_path, O_RDONLY | O_DIRECTORY);
  if (internal_iserror(descriptor_)) {
    Report("Can't open /proc/%d/task for reading.\n", pid);
  }
}

ThreadLister::Result ThreadLister::ListThreads(
    InternalMmapVector<tid_t> *threads) {
  if (internal_iserror(descriptor_))
    return Error;
  internal_lseek(descriptor_, 0, SEEK_SET);
  threads->clear();

  Result result = Ok;
  for (bool first_read = true;; first_read = false) {
    // Resize to max capacity if it was downsized by IsAlive.
    buffer_.resize(buffer_.capacity());
    CHECK_GE(buffer_.size(), 4096);
    uptr read = internal_getdents(
        descriptor_, (struct linux_dirent *)buffer_.data(), buffer_.size());
    if (!read)
      return result;
    if (internal_iserror(read)) {
      Report("Can't read directory entries from /proc/%d/task.\n", pid_);
      return Error;
    }

    for (uptr begin = (uptr)buffer_.data(), end = begin + read; begin < end;) {
      struct linux_dirent *entry = (struct linux_dirent *)begin;
      begin += entry->d_reclen;
      if (entry->d_ino == 1) {
        // Inode 1 is for bad blocks and also can be a reason for early return.
        // Should be emitted if kernel tried to output terminating thread.
        // See proc_task_readdir implementation in Linux.
        result = Incomplete;
      }
      if (entry->d_ino && *entry->d_name >= '0' && *entry->d_name <= '9')
        threads->push_back(internal_atoll(entry->d_name));
    }

    // Now we are going to detect short-read or early EOF. In such cases Linux
    // can return inconsistent list with missing alive threads.
    // Code will just remember that the list can be incomplete but it will
    // continue reads to return as much as possible.
    if (!first_read) {
      // The first one was a short-read by definition.
      result = Incomplete;
    } else if (read > buffer_.size() - 1024) {
      // Read was close to the buffer size. So double the size and assume the
      // worst.
      buffer_.resize(buffer_.size() * 2);
      result = Incomplete;
    } else if (!threads->empty() && !IsAlive(threads->back())) {
      // Maybe Linux early returned from read on terminated thread (!pid_alive)
      // and failed to restore read position.
      // See next_tid and proc_task_instantiate in Linux.
      result = Incomplete;
    }
  }
}

bool ThreadLister::IsAlive(int tid) {
  // /proc/%d/task/%d/status uses same call to detect alive threads as
  // proc_task_readdir. See task_state implementation in Linux.
  char path[80];
  internal_snprintf(path, sizeof(path), "/proc/%d/task/%d/status", pid_, tid);
  if (!ReadFileToVector(path, &buffer_) || buffer_.empty())
    return false;
  buffer_.push_back(0);
  static const char kPrefix[] = "\nPPid:";
  const char *field = internal_strstr(buffer_.data(), kPrefix);
  if (!field)
    return false;
  field += internal_strlen(kPrefix);
  return (int)internal_atoll(field) != 0;
}

ThreadLister::~ThreadLister() {
  if (!internal_iserror(descriptor_))
    internal_close(descriptor_);
}
#  endif

#  if SANITIZER_WORDSIZE == 32
// Take care of unusable kernel area in top gigabyte.
static uptr GetKernelAreaSize() {
#    if SANITIZER_LINUX && !SANITIZER_X32
  const uptr gbyte = 1UL << 30;

  // Firstly check if there are writable segments
  // mapped to top gigabyte (e.g. stack).
  MemoryMappingLayout proc_maps(/*cache_enabled*/ true);
  if (proc_maps.Error())
    return 0;
  MemoryMappedSegment segment;
  while (proc_maps.Next(&segment)) {
    if ((segment.end >= 3 * gbyte) && segment.IsWritable())
      return 0;
  }

#      if !SANITIZER_ANDROID
  // Even if nothing is mapped, top Gb may still be accessible
  // if we are running on 64-bit kernel.
  // Uname may report misleading results if personality type
  // is modified (e.g. under schroot) so check this as well.
  struct utsname uname_info;
  int pers = personality(0xffffffffUL);
  if (!(pers & PER_MASK) && internal_uname(&uname_info) == 0 &&
      internal_strstr(uname_info.machine, "64"))
    return 0;
#      endif  // SANITIZER_ANDROID

  // Top gigabyte is reserved for kernel.
  return gbyte;
#    else
  return 0;
#    endif  // SANITIZER_LINUX && !SANITIZER_X32
}
#  endif  // SANITIZER_WORDSIZE == 32

uptr GetMaxVirtualAddress() {
#  if SANITIZER_NETBSD && defined(__x86_64__)
  return 0x7f7ffffff000ULL;  // (0x00007f8000000000 - PAGE_SIZE)
#  elif SANITIZER_WORDSIZE == 64
#    if defined(__powerpc64__) || defined(__aarch64__) || \
        defined(__loongarch__) || SANITIZER_RISCV64
  // On PowerPC64 we have two different address space layouts: 44- and 46-bit.
  // We somehow need to figure out which one we are using now and choose
  // one of 0x00000fffffffffffUL and 0x00003fffffffffffUL.
  // Note that with 'ulimit -s unlimited' the stack is moved away from the top
  // of the address space, so simply checking the stack address is not enough.
  // This should (does) work for both PowerPC64 Endian modes.
  // Similarly, aarch64 has multiple address space layouts: 39, 42 and 47-bit.
  // loongarch64 also has multiple address space layouts: default is 47-bit.
  // RISC-V 64 also has multiple address space layouts: 39, 48 and 57-bit.
  return (1ULL << (MostSignificantSetBitIndex(GET_CURRENT_FRAME()) + 1)) - 1;
#    elif SANITIZER_MIPS64
  return (1ULL << 40) - 1;  // 0x000000ffffffffffUL;
#    elif defined(__s390x__)
  return (1ULL << 53) - 1;  // 0x001fffffffffffffUL;
#    elif defined(__sparc__)
  return ~(uptr)0;
#    else
  return (1ULL << 47) - 1;  // 0x00007fffffffffffUL;
#    endif
#  else  // SANITIZER_WORDSIZE == 32
#    if defined(__s390__)
  return (1ULL << 31) - 1;  // 0x7fffffff;
#    else
  return (1ULL << 32) - 1;  // 0xffffffff;
#    endif
#  endif  // SANITIZER_WORDSIZE
}

uptr GetMaxUserVirtualAddress() {
  uptr addr = GetMaxVirtualAddress();
#  if SANITIZER_WORDSIZE == 32 && !defined(__s390__)
  if (!common_flags()->full_address_space)
    addr -= GetKernelAreaSize();
  CHECK_LT(reinterpret_cast<uptr>(&addr), addr);
#  endif
  return addr;
}

#  if !SANITIZER_ANDROID || defined(__aarch64__)
uptr GetPageSize() {
#    if SANITIZER_LINUX && (defined(__x86_64__) || defined(__i386__)) && \
        defined(EXEC_PAGESIZE)
  return EXEC_PAGESIZE;
#    elif SANITIZER_FREEBSD || SANITIZER_NETBSD
  // Use sysctl as sysconf can trigger interceptors internally.
  int pz = 0;
  uptr pzl = sizeof(pz);
  int mib[2] = {CTL_HW, HW_PAGESIZE};
  int rv = internal_sysctl(mib, 2, &pz, &pzl, nullptr, 0);
  CHECK_EQ(rv, 0);
  return (uptr)pz;
#    elif SANITIZER_USE_GETAUXVAL
  return getauxval(AT_PAGESZ);
#    else
  return sysconf(_SC_PAGESIZE);  // EXEC_PAGESIZE may not be trustworthy.
#    endif
}
#  endif

uptr ReadBinaryName(/*out*/ char *buf, uptr buf_len) {
#  if SANITIZER_SOLARIS
  const char *default_module_name = getexecname();
  CHECK_NE(default_module_name, NULL);
  return internal_snprintf(buf, buf_len, "%s", default_module_name);
#  else
#    if SANITIZER_FREEBSD || SANITIZER_NETBSD
#      if SANITIZER_FREEBSD
  const int Mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
#      else
  const int Mib[4] = {CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME};
#      endif
  const char *default_module_name = "kern.proc.pathname";
  uptr Size = buf_len;
  bool IsErr =
      (internal_sysctl(Mib, ARRAY_SIZE(Mib), buf, &Size, NULL, 0) != 0);
  int readlink_error = IsErr ? errno : 0;
  uptr module_name_len = Size;
#    else
  const char *default_module_name = "/proc/self/exe";
  uptr module_name_len = internal_readlink(default_module_name, buf, buf_len);
  int readlink_error;
  bool IsErr = internal_iserror(module_name_len, &readlink_error);
#    endif
  if (IsErr) {
    // We can't read binary name for some reason, assume it's unknown.
    Report(
        "WARNING: reading executable name failed with errno %d, "
        "some stack frames may not be symbolized\n",
        readlink_error);
    module_name_len =
        internal_snprintf(buf, buf_len, "%s", default_module_name);
    CHECK_LT(module_name_len, buf_len);
  }
  return module_name_len;
#  endif
}

uptr ReadLongProcessName(/*out*/ char *buf, uptr buf_len) {
#  if SANITIZER_LINUX
  char *tmpbuf;
  uptr tmpsize;
  uptr tmplen;
  if (ReadFileToBuffer("/proc/self/cmdline", &tmpbuf, &tmpsize, &tmplen,
                       1024 * 1024)) {
    internal_strncpy(buf, tmpbuf, buf_len);
    UnmapOrDie(tmpbuf, tmpsize);
    return internal_strlen(buf);
  }
#  endif
  return ReadBinaryName(buf, buf_len);
}

// Match full names of the form /path/to/base_name{-,.}*
bool LibraryNameIs(const char *full_name, const char *base_name) {
  const char *name = full_name;
  // Strip path.
  while (*name != '\0') name++;
  while (name > full_name && *name != '/') name--;
  if (*name == '/')
    name++;
  uptr base_name_length = internal_strlen(base_name);
  if (internal_strncmp(name, base_name, base_name_length))
    return false;
  return (name[base_name_length] == '-' || name[base_name_length] == '.');
}

#  if !SANITIZER_ANDROID
// Call cb for each region mapped by map.
void ForEachMappedRegion(link_map *map, void (*cb)(const void *, uptr)) {
  CHECK_NE(map, nullptr);
#    if !SANITIZER_FREEBSD
  typedef ElfW(Phdr) Elf_Phdr;
  typedef ElfW(Ehdr) Elf_Ehdr;
#    endif  // !SANITIZER_FREEBSD
  char *base = (char *)map->l_addr;
  Elf_Ehdr *ehdr = (Elf_Ehdr *)base;
  char *phdrs = base + ehdr->e_phoff;
  char *phdrs_end = phdrs + ehdr->e_phnum * ehdr->e_phentsize;

  // Find the segment with the minimum base so we can "relocate" the p_vaddr
  // fields.  Typically ET_DYN objects (DSOs) have base of zero and ET_EXEC
  // objects have a non-zero base.
  uptr preferred_base = (uptr)-1;
  for (char *iter = phdrs; iter != phdrs_end; iter += ehdr->e_phentsize) {
    Elf_Phdr *phdr = (Elf_Phdr *)iter;
    if (phdr->p_type == PT_LOAD && preferred_base > (uptr)phdr->p_vaddr)
      preferred_base = (uptr)phdr->p_vaddr;
  }

  // Compute the delta from the real base to get a relocation delta.
  sptr delta = (uptr)base - preferred_base;
  // Now we can figure out what the loader really mapped.
  for (char *iter = phdrs; iter != phdrs_end; iter += ehdr->e_phentsize) {
    Elf_Phdr *phdr = (Elf_Phdr *)iter;
    if (phdr->p_type == PT_LOAD) {
      uptr seg_start = phdr->p_vaddr + delta;
      uptr seg_end = seg_start + phdr->p_memsz;
      // None of these values are aligned.  We consider the ragged edges of the
      // load command as defined, since they are mapped from the file.
      seg_start = RoundDownTo(seg_start, GetPageSizeCached());
      seg_end = RoundUpTo(seg_end, GetPageSizeCached());
      cb((void *)seg_start, seg_end - seg_start);
    }
  }
}
#  endif

#  if SANITIZER_LINUX
#    if defined(__x86_64__)
// We cannot use glibc's clone wrapper, because it messes with the child
// task's TLS. It writes the PID and TID of the child task to its thread
// descriptor, but in our case the child task shares the thread descriptor with
// the parent (because we don't know how to allocate a new thread
// descriptor to keep glibc happy). So the stock version of clone(), when
// used with CLONE_VM, would end up corrupting the parent's thread descriptor.
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  long long res;
  if (!fn || !child_stack)
    return -EINVAL;
  CHECK_EQ(0, (uptr)child_stack % 16);
  child_stack = (char *)child_stack - 2 * sizeof(unsigned long long);
  ((unsigned long long *)child_stack)[0] = (uptr)fn;
  ((unsigned long long *)child_stack)[1] = (uptr)arg;
  register void *r8 __asm__("r8") = newtls;
  register int *r10 __asm__("r10") = child_tidptr;
  __asm__ __volatile__(
      /* %rax = syscall(%rax = SYSCALL(clone),
       *                %rdi = flags,
       *                %rsi = child_stack,
       *                %rdx = parent_tidptr,
       *                %r8  = new_tls,
       *                %r10 = child_tidptr)
       */
      "syscall\n"

      /* if (%rax != 0)
       *   return;
       */
      "testq  %%rax,%%rax\n"
      "jnz    1f\n"

      /* In the child. Terminate unwind chain. */
      // XXX: We should also terminate the CFI unwind chain
      // here. Unfortunately clang 3.2 doesn't support the
      // necessary CFI directives, so we skip that part.
      "xorq   %%rbp,%%rbp\n"

      /* Call "fn(arg)". */
      "popq   %%rax\n"
      "popq   %%rdi\n"
      "call   *%%rax\n"

      /* Call _exit(%rax). */
      "movq   %%rax,%%rdi\n"
      "movq   %2,%%rax\n"
      "syscall\n"

      /* Return to parent. */
      "1:\n"
      : "=a"(res)
      : "a"(SYSCALL(clone)), "i"(SYSCALL(exit)), "S"(child_stack), "D"(flags),
        "d"(parent_tidptr), "r"(r8), "r"(r10)
      : "memory", "r11", "rcx");
  return res;
}
#    elif defined(__mips__)
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  long long res;
  if (!fn || !child_stack)
    return -EINVAL;
  CHECK_EQ(0, (uptr)child_stack % 16);
  child_stack = (char *)child_stack - 2 * sizeof(unsigned long long);
  ((unsigned long long *)child_stack)[0] = (uptr)fn;
  ((unsigned long long *)child_stack)[1] = (uptr)arg;
  register void *a3 __asm__("$7") = newtls;
  register int *a4 __asm__("$8") = child_tidptr;
  // We don't have proper CFI directives here because it requires alot of code
  // for very marginal benefits.
  __asm__ __volatile__(
      /* $v0 = syscall($v0 = __NR_clone,
       * $a0 = flags,
       * $a1 = child_stack,
       * $a2 = parent_tidptr,
       * $a3 = new_tls,
       * $a4 = child_tidptr)
       */
      ".cprestore 16;\n"
      "move $4,%1;\n"
      "move $5,%2;\n"
      "move $6,%3;\n"
      "move $7,%4;\n"
  /* Store the fifth argument on stack
   * if we are using 32-bit abi.
   */
#      if SANITIZER_WORDSIZE == 32
      "lw %5,16($29);\n"
#      else
      "move $8,%5;\n"
#      endif
      "li $2,%6;\n"
      "syscall;\n"

      /* if ($v0 != 0)
       * return;
       */
      "bnez $2,1f;\n"

  /* Call "fn(arg)". */
#      if SANITIZER_WORDSIZE == 32
#        ifdef __BIG_ENDIAN__
      "lw $25,4($29);\n"
      "lw $4,12($29);\n"
#        else
      "lw $25,0($29);\n"
      "lw $4,8($29);\n"
#        endif
#      else
      "ld $25,0($29);\n"
      "ld $4,8($29);\n"
#      endif
      "jal $25;\n"

      /* Call _exit($v0). */
      "move $4,$2;\n"
      "li $2,%7;\n"
      "syscall;\n"

      /* Return to parent. */
      "1:\n"
      : "=r"(res)
      : "r"(flags), "r"(child_stack), "r"(parent_tidptr), "r"(a3), "r"(a4),
        "i"(__NR_clone), "i"(__NR_exit)
      : "memory", "$29");
  return res;
}
#    elif SANITIZER_RISCV64
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  if (!fn || !child_stack)
    return -EINVAL;

  CHECK_EQ(0, (uptr)child_stack % 16);

  register int res __asm__("a0");
  register int __flags __asm__("a0") = flags;
  register void *__stack __asm__("a1") = child_stack;
  register int *__ptid __asm__("a2") = parent_tidptr;
  register void *__tls __asm__("a3") = newtls;
  register int *__ctid __asm__("a4") = child_tidptr;
  register int (*__fn)(void *) __asm__("a5") = fn;
  register void *__arg __asm__("a6") = arg;
  register int nr_clone __asm__("a7") = __NR_clone;

  __asm__ __volatile__(
      "ecall\n"

      /* if (a0 != 0)
       *   return a0;
       */
      "bnez a0, 1f\n"

      // In the child, now. Call "fn(arg)".
      "mv a0, a6\n"
      "jalr a5\n"

      // Call _exit(a0).
      "addi a7, zero, %9\n"
      "ecall\n"
      "1:\n"

      : "=r"(res)
      : "0"(__flags), "r"(__stack), "r"(__ptid), "r"(__tls), "r"(__ctid),
        "r"(__fn), "r"(__arg), "r"(nr_clone), "i"(__NR_exit)
      : "memory");
  return res;
}
#    elif defined(__aarch64__)
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  register long long res __asm__("x0");
  if (!fn || !child_stack)
    return -EINVAL;
  CHECK_EQ(0, (uptr)child_stack % 16);
  child_stack = (char *)child_stack - 2 * sizeof(unsigned long long);
  ((unsigned long long *)child_stack)[0] = (uptr)fn;
  ((unsigned long long *)child_stack)[1] = (uptr)arg;

  register int (*__fn)(void *) __asm__("x0") = fn;
  register void *__stack __asm__("x1") = child_stack;
  register int __flags __asm__("x2") = flags;
  register void *__arg __asm__("x3") = arg;
  register int *__ptid __asm__("x4") = parent_tidptr;
  register void *__tls __asm__("x5") = newtls;
  register int *__ctid __asm__("x6") = child_tidptr;

  __asm__ __volatile__(
      "mov x0,x2\n" /* flags  */
      "mov x2,x4\n" /* ptid  */
      "mov x3,x5\n" /* tls  */
      "mov x4,x6\n" /* ctid  */
      "mov x8,%9\n" /* clone  */

      "svc 0x0\n"

      /* if (%r0 != 0)
       *   return %r0;
       */
      "cmp x0, #0\n"
      "bne 1f\n"

      /* In the child, now. Call "fn(arg)". */
      "ldp x1, x0, [sp], #16\n"
      "blr x1\n"

      /* Call _exit(%r0).  */
      "mov x8, %10\n"
      "svc 0x0\n"
      "1:\n"

      : "=r"(res)
      : "i"(-EINVAL), "r"(__fn), "r"(__stack), "r"(__flags), "r"(__arg),
        "r"(__ptid), "r"(__tls), "r"(__ctid), "i"(__NR_clone), "i"(__NR_exit)
      : "x30", "memory");
  return res;
}
#    elif SANITIZER_LOONGARCH64
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  if (!fn || !child_stack)
    return -EINVAL;

  CHECK_EQ(0, (uptr)child_stack % 16);

  register int res __asm__("$a0");
  register int __flags __asm__("$a0") = flags;
  register void *__stack __asm__("$a1") = child_stack;
  register int *__ptid __asm__("$a2") = parent_tidptr;
  register int *__ctid __asm__("$a3") = child_tidptr;
  register void *__tls __asm__("$a4") = newtls;
  register int (*__fn)(void *) __asm__("$a5") = fn;
  register void *__arg __asm__("$a6") = arg;
  register int nr_clone __asm__("$a7") = __NR_clone;

  __asm__ __volatile__(
      "syscall 0\n"

      // if ($a0 != 0)
      //   return $a0;
      "bnez $a0, 1f\n"

      // In the child, now. Call "fn(arg)".
      "move $a0, $a6\n"
      "jirl $ra, $a5, 0\n"

      // Call _exit($a0).
      "addi.d $a7, $zero, %9\n"
      "syscall 0\n"

      "1:\n"

      : "=r"(res)
      : "0"(__flags), "r"(__stack), "r"(__ptid), "r"(__ctid), "r"(__tls),
        "r"(__fn), "r"(__arg), "r"(nr_clone), "i"(__NR_exit)
      : "memory", "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
        "$t8");
  return res;
}
#    elif defined(__powerpc64__)
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  long long res;
// Stack frame structure.
#      if SANITIZER_PPC64V1
  //   Back chain == 0        (SP + 112)
  // Frame (112 bytes):
  //   Parameter save area    (SP + 48), 8 doublewords
  //   TOC save area          (SP + 40)
  //   Link editor doubleword (SP + 32)
  //   Compiler doubleword    (SP + 24)
  //   LR save area           (SP + 16)
  //   CR save area           (SP + 8)
  //   Back chain             (SP + 0)
#        define FRAME_SIZE 112
#        define FRAME_TOC_SAVE_OFFSET 40
#      elif SANITIZER_PPC64V2
  //   Back chain == 0        (SP + 32)
  // Frame (32 bytes):
  //   TOC save area          (SP + 24)
  //   LR save area           (SP + 16)
  //   CR save area           (SP + 8)
  //   Back chain             (SP + 0)
#        define FRAME_SIZE 32
#        define FRAME_TOC_SAVE_OFFSET 24
#      else
#        error "Unsupported PPC64 ABI"
#      endif
  if (!fn || !child_stack)
    return -EINVAL;
  CHECK_EQ(0, (uptr)child_stack % 16);

  register int (*__fn)(void *) __asm__("r3") = fn;
  register void *__cstack __asm__("r4") = child_stack;
  register int __flags __asm__("r5") = flags;
  register void *__arg __asm__("r6") = arg;
  register int *__ptidptr __asm__("r7") = parent_tidptr;
  register void *__newtls __asm__("r8") = newtls;
  register int *__ctidptr __asm__("r9") = child_tidptr;

  __asm__ __volatile__(
      /* fn and arg are saved across the syscall */
      "mr 28, %5\n\t"
      "mr 27, %8\n\t"

      /* syscall
        r0 == __NR_clone
        r3 == flags
        r4 == child_stack
        r5 == parent_tidptr
        r6 == newtls
        r7 == child_tidptr */
      "mr 3, %7\n\t"
      "mr 5, %9\n\t"
      "mr 6, %10\n\t"
      "mr 7, %11\n\t"
      "li 0, %3\n\t"
      "sc\n\t"

      /* Test if syscall was successful */
      "cmpdi  cr1, 3, 0\n\t"
      "crandc cr1*4+eq, cr1*4+eq, cr0*4+so\n\t"
      "bne-   cr1, 1f\n\t"

      /* Set up stack frame */
      "li    29, 0\n\t"
      "stdu  29, -8(1)\n\t"
      "stdu  1, -%12(1)\n\t"
      /* Do the function call */
      "std   2, %13(1)\n\t"
#      if SANITIZER_PPC64V1
      "ld    0, 0(28)\n\t"
      "ld    2, 8(28)\n\t"
      "mtctr 0\n\t"
#      elif SANITIZER_PPC64V2
      "mr    12, 28\n\t"
      "mtctr 12\n\t"
#      else
#        error "Unsupported PPC64 ABI"
#      endif
      "mr    3, 27\n\t"
      "bctrl\n\t"
      "ld    2, %13(1)\n\t"

      /* Call _exit(r3) */
      "li 0, %4\n\t"
      "sc\n\t"

      /* Return to parent */
      "1:\n\t"
      "mr %0, 3\n\t"
      : "=r"(res)
      : "0"(-1), "i"(EINVAL), "i"(__NR_clone), "i"(__NR_exit), "r"(__fn),
        "r"(__cstack), "r"(__flags), "r"(__arg), "r"(__ptidptr), "r"(__newtls),
        "r"(__ctidptr), "i"(FRAME_SIZE), "i"(FRAME_TOC_SAVE_OFFSET)
      : "cr0", "cr1", "memory", "ctr", "r0", "r27", "r28", "r29");
  return res;
}
#    elif defined(__i386__)
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  int res;
  if (!fn || !child_stack)
    return -EINVAL;
  CHECK_EQ(0, (uptr)child_stack % 16);
  child_stack = (char *)child_stack - 7 * sizeof(unsigned int);
  ((unsigned int *)child_stack)[0] = (uptr)flags;
  ((unsigned int *)child_stack)[1] = (uptr)0;
  ((unsigned int *)child_stack)[2] = (uptr)fn;
  ((unsigned int *)child_stack)[3] = (uptr)arg;
  __asm__ __volatile__(
      /* %eax = syscall(%eax = SYSCALL(clone),
       *                %ebx = flags,
       *                %ecx = child_stack,
       *                %edx = parent_tidptr,
       *                %esi  = new_tls,
       *                %edi = child_tidptr)
       */

      /* Obtain flags */
      "movl    (%%ecx), %%ebx\n"
      /* Do the system call */
      "pushl   %%ebx\n"
      "pushl   %%esi\n"
      "pushl   %%edi\n"
      /* Remember the flag value.  */
      "movl    %%ebx, (%%ecx)\n"
      "int     $0x80\n"
      "popl    %%edi\n"
      "popl    %%esi\n"
      "popl    %%ebx\n"

      /* if (%eax != 0)
       *   return;
       */

      "test    %%eax,%%eax\n"
      "jnz    1f\n"

      /* terminate the stack frame */
      "xorl   %%ebp,%%ebp\n"
      /* Call FN. */
      "call    *%%ebx\n"
#      ifdef PIC
      "call    here\n"
      "here:\n"
      "popl    %%ebx\n"
      "addl    $_GLOBAL_OFFSET_TABLE_+[.-here], %%ebx\n"
#      endif
      /* Call exit */
      "movl    %%eax, %%ebx\n"
      "movl    %2, %%eax\n"
      "int     $0x80\n"
      "1:\n"
      : "=a"(res)
      : "a"(SYSCALL(clone)), "i"(SYSCALL(exit)), "c"(child_stack),
        "d"(parent_tidptr), "S"(newtls), "D"(child_tidptr)
      : "memory");
  return res;
}
#    elif defined(__arm__)
uptr internal_clone(int (*fn)(void *), void *child_stack, int flags, void *arg,
                    int *parent_tidptr, void *newtls, int *child_tidptr) {
  unsigned int res;
  if (!fn || !child_stack)
    return -EINVAL;
  child_stack = (char *)child_stack - 2 * sizeof(unsigned int);
  ((unsigned int *)child_stack)[0] = (uptr)fn;
  ((unsigned int *)child_stack)[1] = (uptr)arg;
  register int r0 __asm__("r0") = flags;
  register void *r1 __asm__("r1") = child_stack;
  register int *r2 __asm__("r2") = parent_tidptr;
  register void *r3 __asm__("r3") = newtls;
  register int *r4 __asm__("r4") = child_tidptr;
  register int r7 __asm__("r7") = __NR_clone;

#      if __ARM_ARCH > 4 || defined(__ARM_ARCH_4T__)
#        define ARCH_HAS_BX
#      endif
#      if __ARM_ARCH > 4
#        define ARCH_HAS_BLX
#      endif

#      ifdef ARCH_HAS_BX
#        ifdef ARCH_HAS_BLX
#          define BLX(R) "blx " #R "\n"
#        else
#          define BLX(R) "mov lr, pc; bx " #R "\n"
#        endif
#      else
#        define BLX(R) "mov lr, pc; mov pc," #R "\n"
#      endif

  __asm__ __volatile__(
      /* %r0 = syscall(%r7 = SYSCALL(clone),
       *               %r0 = flags,
       *               %r1 = child_stack,
       *               %r2 = parent_tidptr,
       *               %r3  = new_tls,
       *               %r4 = child_tidptr)
       */

      /* Do the system call */
      "swi 0x0\n"

      /* if (%r0 != 0)
       *   return %r0;
       */
      "cmp r0, #0\n"
      "bne 1f\n"

      /* In the child, now. Call "fn(arg)". */
      "ldr r0, [sp, #4]\n"
      "ldr ip, [sp], #8\n" BLX(ip)
      /* Call _exit(%r0). */
      "mov r7, %7\n"
      "swi 0x0\n"
      "1:\n"
      "mov %0, r0\n"
      : "=r"(res)
      : "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r7), "i"(__NR_exit)
      : "memory");
  return res;
}
#    endif
#  endif  // SANITIZER_LINUX

#  if SANITIZER_LINUX
int internal_uname(struct utsname *buf) {
  return internal_syscall(SYSCALL(uname), buf);
}
#  endif

#  if SANITIZER_ANDROID
#    if __ANDROID_API__ < 21
extern "C" __attribute__((weak)) int dl_iterate_phdr(
    int (*)(struct dl_phdr_info *, size_t, void *), void *);
#    endif

static int dl_iterate_phdr_test_cb(struct dl_phdr_info *info, size_t size,
                                   void *data) {
  // Any name starting with "lib" indicates a bug in L where library base names
  // are returned instead of paths.
  if (info->dlpi_name && info->dlpi_name[0] == 'l' &&
      info->dlpi_name[1] == 'i' && info->dlpi_name[2] == 'b') {
    *(bool *)data = true;
    return 1;
  }
  return 0;
}

static atomic_uint32_t android_api_level;

static AndroidApiLevel AndroidDetectApiLevelStatic() {
#    if __ANDROID_API__ <= 19
  return ANDROID_KITKAT;
#    elif __ANDROID_API__ <= 22
  return ANDROID_LOLLIPOP_MR1;
#    else
  return ANDROID_POST_LOLLIPOP;
#    endif
}

static AndroidApiLevel AndroidDetectApiLevel() {
  if (!&dl_iterate_phdr)
    return ANDROID_KITKAT;  // K or lower
  bool base_name_seen = false;
  dl_iterate_phdr(dl_iterate_phdr_test_cb, &base_name_seen);
  if (base_name_seen)
    return ANDROID_LOLLIPOP_MR1;  // L MR1
  return ANDROID_POST_LOLLIPOP;   // post-L
  // Plain L (API level 21) is completely broken wrt ASan and not very
  // interesting to detect.
}

extern "C" __attribute__((weak)) void *_DYNAMIC;

AndroidApiLevel AndroidGetApiLevel() {
  AndroidApiLevel level =
      (AndroidApiLevel)atomic_load(&android_api_level, memory_order_relaxed);
  if (level)
    return level;
  level = &_DYNAMIC == nullptr ? AndroidDetectApiLevelStatic()
                               : AndroidDetectApiLevel();
  atomic_store(&android_api_level, level, memory_order_relaxed);
  return level;
}

#  endif

static HandleSignalMode GetHandleSignalModeImpl(int signum) {
  switch (signum) {
    case SIGABRT:
      return common_flags()->handle_abort;
    case SIGILL:
      return common_flags()->handle_sigill;
    case SIGTRAP:
      return common_flags()->handle_sigtrap;
    case SIGFPE:
      return common_flags()->handle_sigfpe;
    case SIGSEGV:
      return common_flags()->handle_segv;
    case SIGBUS:
      return common_flags()->handle_sigbus;
  }
  return kHandleSignalNo;
}

HandleSignalMode GetHandleSignalMode(int signum) {
  HandleSignalMode result = GetHandleSignalModeImpl(signum);
  if (result == kHandleSignalYes && !common_flags()->allow_user_segv_handler)
    return kHandleSignalExclusive;
  return result;
}

#  if !SANITIZER_GO
void *internal_start_thread(void *(*func)(void *arg), void *arg) {
  if (&internal_pthread_create == 0)
    return nullptr;
  // Start the thread with signals blocked, otherwise it can steal user signals.
  ScopedBlockSignals block(nullptr);
  void *th;
  internal_pthread_create(&th, nullptr, func, arg);
  return th;
}

void internal_join_thread(void *th) {
  if (&internal_pthread_join)
    internal_pthread_join(th, nullptr);
}
#  else
void *internal_start_thread(void *(*func)(void *), void *arg) { return 0; }

void internal_join_thread(void *th) {}
#  endif

#  if SANITIZER_LINUX && defined(__aarch64__)
// Android headers in the older NDK releases miss this definition.
struct __sanitizer_esr_context {
  struct _aarch64_ctx head;
  uint64_t esr;
};

static bool Aarch64GetESR(ucontext_t *ucontext, u64 *esr) {
  static const u32 kEsrMagic = 0x45535201;
  u8 *aux = reinterpret_cast<u8 *>(ucontext->uc_mcontext.__reserved);
  while (true) {
    _aarch64_ctx *ctx = (_aarch64_ctx *)aux;
    if (ctx->size == 0)
      break;
    if (ctx->magic == kEsrMagic) {
      *esr = ((__sanitizer_esr_context *)ctx)->esr;
      return true;
    }
    aux += ctx->size;
  }
  return false;
}
#  elif SANITIZER_FREEBSD && defined(__aarch64__)
// FreeBSD doesn't provide ESR in the ucontext.
static bool Aarch64GetESR(ucontext_t *ucontext, u64 *esr) { return false; }
#  endif

using Context = ucontext_t;

SignalContext::WriteFlag SignalContext::GetWriteFlag() const {
  Context *ucontext = (Context *)context;
#  if defined(__x86_64__) || defined(__i386__)
  static const uptr PF_WRITE = 1U << 1;
#    if SANITIZER_FREEBSD
  uptr err = ucontext->uc_mcontext.mc_err;
#    elif SANITIZER_NETBSD
  uptr err = ucontext->uc_mcontext.__gregs[_REG_ERR];
#    elif SANITIZER_SOLARIS && defined(__i386__)
  const int Err = 13;
  uptr err = ucontext->uc_mcontext.gregs[Err];
#    else
  uptr err = ucontext->uc_mcontext.gregs[REG_ERR];
#    endif  // SANITIZER_FREEBSD
  return err & PF_WRITE ? Write : Read;
#  elif defined(__mips__)
  uint32_t *exception_source;
  uint32_t faulty_instruction;
  uint32_t op_code;

  exception_source = (uint32_t *)ucontext->uc_mcontext.pc;
  faulty_instruction = (uint32_t)(*exception_source);

  op_code = (faulty_instruction >> 26) & 0x3f;

  // FIXME: Add support for FPU, microMIPS, DSP, MSA memory instructions.
  switch (op_code) {
    case 0x28:  // sb
    case 0x29:  // sh
    case 0x2b:  // sw
    case 0x3f:  // sd
#    if __mips_isa_rev < 6
    case 0x2c:  // sdl
    case 0x2d:  // sdr
    case 0x2a:  // swl
    case 0x2e:  // swr
#    endif
      return SignalContext::Write;

    case 0x20:  // lb
    case 0x24:  // lbu
    case 0x21:  // lh
    case 0x25:  // lhu
    case 0x23:  // lw
    case 0x27:  // lwu
    case 0x37:  // ld
#    if __mips_isa_rev < 6
    case 0x1a:  // ldl
    case 0x1b:  // ldr
    case 0x22:  // lwl
    case 0x26:  // lwr
#    endif
      return SignalContext::Read;
#    if __mips_isa_rev == 6
    case 0x3b:  // pcrel
      op_code = (faulty_instruction >> 19) & 0x3;
      switch (op_code) {
        case 0x1:  // lwpc
        case 0x2:  // lwupc
          return SignalContext::Read;
      }
#    endif
  }
  return SignalContext::Unknown;
#  elif defined(__arm__)
  static const uptr FSR_WRITE = 1U << 11;
  uptr fsr = ucontext->uc_mcontext.error_code;
  return fsr & FSR_WRITE ? Write : Read;
#  elif defined(__aarch64__)
  static const u64 ESR_ELx_WNR = 1U << 6;
  u64 esr;
  if (!Aarch64GetESR(ucontext, &esr))
    return Unknown;
  return esr & ESR_ELx_WNR ? Write : Read;
#  elif defined(__loongarch__)
  // In the musl environment, the Linux kernel uapi sigcontext.h is not
  // included in signal.h. To avoid missing the SC_ADDRERR_{RD,WR} macros,
  // copy them here. The LoongArch Linux kernel uapi is already stable,
  // so there's no need to worry about the value changing.
#    ifndef SC_ADDRERR_RD
  // Address error was due to memory load
#      define SC_ADDRERR_RD (1 << 30)
#    endif
#    ifndef SC_ADDRERR_WR
  // Address error was due to memory store
#      define SC_ADDRERR_WR (1 << 31)
#    endif
  u32 flags = ucontext->uc_mcontext.__flags;
  if (flags & SC_ADDRERR_RD)
    return SignalContext::Read;
  if (flags & SC_ADDRERR_WR)
    return SignalContext::Write;
  return SignalContext::Unknown;
#  elif defined(__sparc__)
  // Decode the instruction to determine the access type.
  // From OpenSolaris $SRC/uts/sun4/os/trap.c (get_accesstype).
#    if SANITIZER_SOLARIS
  uptr pc = ucontext->uc_mcontext.gregs[REG_PC];
#    else
  // Historical BSDism here.
  struct sigcontext *scontext = (struct sigcontext *)context;
#      if defined(__arch64__)
  uptr pc = scontext->sigc_regs.tpc;
#      else
  uptr pc = scontext->si_regs.pc;
#      endif
#    endif
  u32 instr = *(u32 *)pc;
  return (instr >> 21) & 1 ? Write : Read;
#  elif defined(__riscv)
#    if SANITIZER_FREEBSD
  unsigned long pc = ucontext->uc_mcontext.mc_gpregs.gp_sepc;
#    else
  unsigned long pc = ucontext->uc_mcontext.__gregs[REG_PC];
#    endif
  unsigned faulty_instruction = *(uint16_t *)pc;

#    if defined(__riscv_compressed)
  if ((faulty_instruction & 0x3) != 0x3) {  // it's a compressed instruction
    // set op_bits to the instruction bits [1, 0, 15, 14, 13]
    unsigned op_bits =
        ((faulty_instruction & 0x3) << 3) | (faulty_instruction >> 13);
    unsigned rd = faulty_instruction & 0xF80;  // bits 7-11, inclusive
    switch (op_bits) {
      case 0b10'010:  // c.lwsp (rd != x0)
#      if __riscv_xlen == 64
      case 0b10'011:  // c.ldsp (rd != x0)
#      endif
        return rd ? SignalContext::Read : SignalContext::Unknown;
      case 0b00'010:  // c.lw
#      if __riscv_flen >= 32 && __riscv_xlen == 32
      case 0b10'011:  // c.flwsp
#      endif
#      if __riscv_flen >= 32 || __riscv_xlen == 64
      case 0b00'011:  // c.flw / c.ld
#      endif
#      if __riscv_flen == 64
      case 0b00'001:  // c.fld
      case 0b10'001:  // c.fldsp
#      endif
        return SignalContext::Read;
      case 0b00'110:  // c.sw
      case 0b10'110:  // c.swsp
#      if __riscv_flen >= 32 || __riscv_xlen == 64
      case 0b00'111:  // c.fsw / c.sd
      case 0b10'111:  // c.fswsp / c.sdsp
#      endif
#      if __riscv_flen == 64
      case 0b00'101:  // c.fsd
      case 0b10'101:  // c.fsdsp
#      endif
        return SignalContext::Write;
      default:
        return SignalContext::Unknown;
    }
  }
#    endif

  unsigned opcode = faulty_instruction & 0x7f;         // lower 7 bits
  unsigned funct3 = (faulty_instruction >> 12) & 0x7;  // bits 12-14, inclusive
  switch (opcode) {
    case 0b0000011:  // loads
      switch (funct3) {
        case 0b000:  // lb
        case 0b001:  // lh
        case 0b010:  // lw
#    if __riscv_xlen == 64
        case 0b011:  // ld
#    endif
        case 0b100:  // lbu
        case 0b101:  // lhu
          return SignalContext::Read;
        default:
          return SignalContext::Unknown;
      }
    case 0b0100011:  // stores
      switch (funct3) {
        case 0b000:  // sb
        case 0b001:  // sh
        case 0b010:  // sw
#    if __riscv_xlen == 64
        case 0b011:  // sd
#    endif
          return SignalContext::Write;
        default:
          return SignalContext::Unknown;
      }
#    if __riscv_flen >= 32
    case 0b0000111:  // floating-point loads
      switch (funct3) {
        case 0b010:  // flw
#      if __riscv_flen == 64
        case 0b011:  // fld
#      endif
          return SignalContext::Read;
        default:
          return SignalContext::Unknown;
      }
    case 0b0100111:  // floating-point stores
      switch (funct3) {
        case 0b010:  // fsw
#      if __riscv_flen == 64
        case 0b011:  // fsd
#      endif
          return SignalContext::Write;
        default:
          return SignalContext::Unknown;
      }
#    endif
    default:
      return SignalContext::Unknown;
  }
#  else
  (void)ucontext;
  return Unknown;  // FIXME: Implement.
#  endif
}

bool SignalContext::IsTrueFaultingAddress() const {
  auto si = static_cast<const siginfo_t *>(siginfo);
  // SIGSEGV signals without a true fault address have si_code set to 128.
  return si->si_signo == SIGSEGV && si->si_code != 128;
}

UNUSED
static const char *RegNumToRegName(int reg) {
  switch (reg) {
#  if SANITIZER_LINUX
#    if defined(__x86_64__)
    case REG_RAX:
      return "rax";
    case REG_RBX:
      return "rbx";
    case REG_RCX:
      return "rcx";
    case REG_RDX:
      return "rdx";
    case REG_RDI:
      return "rdi";
    case REG_RSI:
      return "rsi";
    case REG_RBP:
      return "rbp";
    case REG_RSP:
      return "rsp";
    case REG_R8:
      return "r8";
    case REG_R9:
      return "r9";
    case REG_R10:
      return "r10";
    case REG_R11:
      return "r11";
    case REG_R12:
      return "r12";
    case REG_R13:
      return "r13";
    case REG_R14:
      return "r14";
    case REG_R15:
      return "r15";
#    elif defined(__i386__)
    case REG_EAX:
      return "eax";
    case REG_EBX:
      return "ebx";
    case REG_ECX:
      return "ecx";
    case REG_EDX:
      return "edx";
    case REG_EDI:
      return "edi";
    case REG_ESI:
      return "esi";
    case REG_EBP:
      return "ebp";
    case REG_ESP:
      return "esp";
#    endif
#  endif
    default:
      return NULL;
  }
  return NULL;
}

#  if SANITIZER_LINUX
UNUSED
static void DumpSingleReg(ucontext_t *ctx, int RegNum) {
  const char *RegName = RegNumToRegName(RegNum);
#    if defined(__x86_64__)
  Printf("%s%s = 0x%016llx  ", internal_strlen(RegName) == 2 ? " " : "",
         RegName, ctx->uc_mcontext.gregs[RegNum]);
#    elif defined(__i386__)
  Printf("%s = 0x%08x  ", RegName, ctx->uc_mcontext.gregs[RegNum]);
#    else
  (void)RegName;
#    endif
}
#  endif

void SignalContext::DumpAllRegisters(void *context) {
  ucontext_t *ucontext = (ucontext_t *)context;
#  if SANITIZER_LINUX
#    if defined(__x86_64__)
  Report("Register values:\n");
  DumpSingleReg(ucontext, REG_RAX);
  DumpSingleReg(ucontext, REG_RBX);
  DumpSingleReg(ucontext, REG_RCX);
  DumpSingleReg(ucontext, REG_RDX);
  Printf("\n");
  DumpSingleReg(ucontext, REG_RDI);
  DumpSingleReg(ucontext, REG_RSI);
  DumpSingleReg(ucontext, REG_RBP);
  DumpSingleReg(ucontext, REG_RSP);
  Printf("\n");
  DumpSingleReg(ucontext, REG_R8);
  DumpSingleReg(ucontext, REG_R9);
  DumpSingleReg(ucontext, REG_R10);
  DumpSingleReg(ucontext, REG_R11);
  Printf("\n");
  DumpSingleReg(ucontext, REG_R12);
  DumpSingleReg(ucontext, REG_R13);
  DumpSingleReg(ucontext, REG_R14);
  DumpSingleReg(ucontext, REG_R15);
  Printf("\n");
#    elif defined(__i386__)
  // Duplication of this report print is caused by partial support
  // of register values dumping. In case of unsupported yet architecture let's
  // avoid printing 'Register values:' without actual values in the following
  // output.
  Report("Register values:\n");
  DumpSingleReg(ucontext, REG_EAX);
  DumpSingleReg(ucontext, REG_EBX);
  DumpSingleReg(ucontext, REG_ECX);
  DumpSingleReg(ucontext, REG_EDX);
  Printf("\n");
  DumpSingleReg(ucontext, REG_EDI);
  DumpSingleReg(ucontext, REG_ESI);
  DumpSingleReg(ucontext, REG_EBP);
  DumpSingleReg(ucontext, REG_ESP);
  Printf("\n");
#    else
  (void)ucontext;
#    endif
#  elif SANITIZER_FREEBSD
#    if defined(__x86_64__)
  Report("Register values:\n");
  Printf("rax = 0x%016lx  ", ucontext->uc_mcontext.mc_rax);
  Printf("rbx = 0x%016lx  ", ucontext->uc_mcontext.mc_rbx);
  Printf("rcx = 0x%016lx  ", ucontext->uc_mcontext.mc_rcx);
  Printf("rdx = 0x%016lx  ", ucontext->uc_mcontext.mc_rdx);
  Printf("\n");
  Printf("rdi = 0x%016lx  ", ucontext->uc_mcontext.mc_rdi);
  Printf("rsi = 0x%016lx  ", ucontext->uc_mcontext.mc_rsi);
  Printf("rbp = 0x%016lx  ", ucontext->uc_mcontext.mc_rbp);
  Printf("rsp = 0x%016lx  ", ucontext->uc_mcontext.mc_rsp);
  Printf("\n");
  Printf(" r8 = 0x%016lx  ", ucontext->uc_mcontext.mc_r8);
  Printf(" r9 = 0x%016lx  ", ucontext->uc_mcontext.mc_r9);
  Printf("r10 = 0x%016lx  ", ucontext->uc_mcontext.mc_r10);
  Printf("r11 = 0x%016lx  ", ucontext->uc_mcontext.mc_r11);
  Printf("\n");
  Printf("r12 = 0x%016lx  ", ucontext->uc_mcontext.mc_r12);
  Printf("r13 = 0x%016lx  ", ucontext->uc_mcontext.mc_r13);
  Printf("r14 = 0x%016lx  ", ucontext->uc_mcontext.mc_r14);
  Printf("r15 = 0x%016lx  ", ucontext->uc_mcontext.mc_r15);
  Printf("\n");
#    elif defined(__i386__)
  Report("Register values:\n");
  Printf("eax = 0x%08x  ", ucontext->uc_mcontext.mc_eax);
  Printf("ebx = 0x%08x  ", ucontext->uc_mcontext.mc_ebx);
  Printf("ecx = 0x%08x  ", ucontext->uc_mcontext.mc_ecx);
  Printf("edx = 0x%08x  ", ucontext->uc_mcontext.mc_edx);
  Printf("\n");
  Printf("edi = 0x%08x  ", ucontext->uc_mcontext.mc_edi);
  Printf("esi = 0x%08x  ", ucontext->uc_mcontext.mc_esi);
  Printf("ebp = 0x%08x  ", ucontext->uc_mcontext.mc_ebp);
  Printf("esp = 0x%08x  ", ucontext->uc_mcontext.mc_esp);
  Printf("\n");
#    else
  (void)ucontext;
#    endif
#  endif
  // FIXME: Implement this for other OSes and architectures.
}

static void GetPcSpBp(void *context, uptr *pc, uptr *sp, uptr *bp) {
#  if SANITIZER_NETBSD
  // This covers all NetBSD architectures
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = _UC_MACHINE_PC(ucontext);
  *bp = _UC_MACHINE_FP(ucontext);
  *sp = _UC_MACHINE_SP(ucontext);
#  elif defined(__arm__)
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.arm_pc;
  *bp = ucontext->uc_mcontext.arm_fp;
  *sp = ucontext->uc_mcontext.arm_sp;
#  elif defined(__aarch64__)
#    if SANITIZER_FREEBSD
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.mc_gpregs.gp_elr;
  *bp = ucontext->uc_mcontext.mc_gpregs.gp_x[29];
  *sp = ucontext->uc_mcontext.mc_gpregs.gp_sp;
#    else
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.pc;
  *bp = ucontext->uc_mcontext.regs[29];
  *sp = ucontext->uc_mcontext.sp;
#    endif
#  elif defined(__hppa__)
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.sc_iaoq[0];
  /* GCC uses %r3 whenever a frame pointer is needed.  */
  *bp = ucontext->uc_mcontext.sc_gr[3];
  *sp = ucontext->uc_mcontext.sc_gr[30];
#  elif defined(__x86_64__)
#    if SANITIZER_FREEBSD
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.mc_rip;
  *bp = ucontext->uc_mcontext.mc_rbp;
  *sp = ucontext->uc_mcontext.mc_rsp;
#    else
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.gregs[REG_RIP];
  *bp = ucontext->uc_mcontext.gregs[REG_RBP];
  *sp = ucontext->uc_mcontext.gregs[REG_RSP];
#    endif
#  elif defined(__i386__)
#    if SANITIZER_FREEBSD
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.mc_eip;
  *bp = ucontext->uc_mcontext.mc_ebp;
  *sp = ucontext->uc_mcontext.mc_esp;
#    else
  ucontext_t *ucontext = (ucontext_t *)context;
#      if SANITIZER_SOLARIS
  /* Use the numeric values: the symbolic ones are undefined by llvm
     include/llvm/Support/Solaris.h.  */
#        ifndef REG_EIP
#          define REG_EIP 14  // REG_PC
#        endif
#        ifndef REG_EBP
#          define REG_EBP 6  // REG_FP
#        endif
#        ifndef REG_UESP
#          define REG_UESP 17  // REG_SP
#        endif
#      endif
  *pc = ucontext->uc_mcontext.gregs[REG_EIP];
  *bp = ucontext->uc_mcontext.gregs[REG_EBP];
  *sp = ucontext->uc_mcontext.gregs[REG_UESP];
#    endif
#  elif defined(__powerpc__) || defined(__powerpc64__)
#    if SANITIZER_FREEBSD
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.mc_srr0;
  *sp = ucontext->uc_mcontext.mc_frame[1];
  *bp = ucontext->uc_mcontext.mc_frame[31];
#    else
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.regs->nip;
  *sp = ucontext->uc_mcontext.regs->gpr[PT_R1];
  // The powerpc{,64}-linux ABIs do not specify r31 as the frame
  // pointer, but GCC always uses r31 when we need a frame pointer.
  *bp = ucontext->uc_mcontext.regs->gpr[PT_R31];
#    endif
#  elif defined(__sparc__)
#    if defined(__arch64__) || defined(__sparcv9)
#      define STACK_BIAS 2047
#    else
#      define STACK_BIAS 0
#    endif
#    if SANITIZER_SOLARIS
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.gregs[REG_PC];
  *sp = ucontext->uc_mcontext.gregs[REG_O6] + STACK_BIAS;
#    else
  // Historical BSDism here.
  struct sigcontext *scontext = (struct sigcontext *)context;
#      if defined(__arch64__)
  *pc = scontext->sigc_regs.tpc;
  *sp = scontext->sigc_regs.u_regs[14] + STACK_BIAS;
#      else
  *pc = scontext->si_regs.pc;
  *sp = scontext->si_regs.u_regs[14];
#      endif
#    endif
  *bp = (uptr)((uhwptr *)*sp)[14] + STACK_BIAS;
#  elif defined(__mips__)
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.pc;
  *bp = ucontext->uc_mcontext.gregs[30];
  *sp = ucontext->uc_mcontext.gregs[29];
#  elif defined(__s390__)
  ucontext_t *ucontext = (ucontext_t *)context;
#    if defined(__s390x__)
  *pc = ucontext->uc_mcontext.psw.addr;
#    else
  *pc = ucontext->uc_mcontext.psw.addr & 0x7fffffff;
#    endif
  *bp = ucontext->uc_mcontext.gregs[11];
  *sp = ucontext->uc_mcontext.gregs[15];
#  elif defined(__riscv)
  ucontext_t *ucontext = (ucontext_t *)context;
#    if SANITIZER_FREEBSD
  *pc = ucontext->uc_mcontext.mc_gpregs.gp_sepc;
  *bp = ucontext->uc_mcontext.mc_gpregs.gp_s[0];
  *sp = ucontext->uc_mcontext.mc_gpregs.gp_sp;
#    else
  *pc = ucontext->uc_mcontext.__gregs[REG_PC];
  *bp = ucontext->uc_mcontext.__gregs[REG_S0];
  *sp = ucontext->uc_mcontext.__gregs[REG_SP];
#    endif
#  elif defined(__hexagon__)
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.pc;
  *bp = ucontext->uc_mcontext.r30;
  *sp = ucontext->uc_mcontext.r29;
#  elif defined(__loongarch__)
  ucontext_t *ucontext = (ucontext_t *)context;
  *pc = ucontext->uc_mcontext.__pc;
  *bp = ucontext->uc_mcontext.__gregs[22];
  *sp = ucontext->uc_mcontext.__gregs[3];
#  else
#    error "Unsupported arch"
#  endif
}

void SignalContext::InitPcSpBp() { GetPcSpBp(context, &pc, &sp, &bp); }

void InitializePlatformEarly() {
  // Do nothing.
}

void CheckASLR() {
#  if SANITIZER_NETBSD
  int mib[3];
  int paxflags;
  uptr len = sizeof(paxflags);

  mib[0] = CTL_PROC;
  mib[1] = internal_getpid();
  mib[2] = PROC_PID_PAXFLAGS;

  if (UNLIKELY(internal_sysctl(mib, 3, &paxflags, &len, NULL, 0) == -1)) {
    Printf("sysctl failed\n");
    Die();
  }

  if (UNLIKELY(paxflags & CTL_PROC_PAXFLAGS_ASLR)) {
    Printf(
        "This sanitizer is not compatible with enabled ASLR.\n"
        "To disable ASLR, please run \"paxctl +a %s\" and try again.\n",
        GetArgv()[0]);
    Die();
  }
#  elif SANITIZER_FREEBSD
  int aslr_status;
  int r = internal_procctl(P_PID, 0, PROC_ASLR_STATUS, &aslr_status);
  if (UNLIKELY(r == -1)) {
    // We're making things less 'dramatic' here since
    // the cmd is not necessarily guaranteed to be here
    // just yet regarding FreeBSD release
    return;
  }
  if ((aslr_status & PROC_ASLR_ACTIVE) != 0) {
    VReport(1,
            "This sanitizer is not compatible with enabled ASLR "
            "and binaries compiled with PIE\n"
            "ASLR will be disabled and the program re-executed.\n");
    int aslr_ctl = PROC_ASLR_FORCE_DISABLE;
    CHECK_NE(internal_procctl(P_PID, 0, PROC_ASLR_CTL, &aslr_ctl), -1);
    ReExec();
  }
#  elif SANITIZER_PPC64V2
  // Disable ASLR for Linux PPC64LE.
  int old_personality = personality(0xffffffff);
  if (old_personality != -1 && (old_personality & ADDR_NO_RANDOMIZE) == 0) {
    VReport(1,
            "WARNING: Program is being run with address space layout "
            "randomization (ASLR) enabled which prevents the thread and "
            "memory sanitizers from working on powerpc64le.\n"
            "ASLR will be disabled and the program re-executed.\n");
    CHECK_NE(personality(old_personality | ADDR_NO_RANDOMIZE), -1);
    ReExec();
  }
#  else
  // Do nothing
#  endif
}

void CheckMPROTECT() {
#  if SANITIZER_NETBSD
  int mib[3];
  int paxflags;
  uptr len = sizeof(paxflags);

  mib[0] = CTL_PROC;
  mib[1] = internal_getpid();
  mib[2] = PROC_PID_PAXFLAGS;

  if (UNLIKELY(internal_sysctl(mib, 3, &paxflags, &len, NULL, 0) == -1)) {
    Printf("sysctl failed\n");
    Die();
  }

  if (UNLIKELY(paxflags & CTL_PROC_PAXFLAGS_MPROTECT)) {
    Printf("This sanitizer is not compatible with enabled MPROTECT\n");
    Die();
  }
#  else
  // Do nothing
#  endif
}

void CheckNoDeepBind(const char *filename, int flag) {
#  ifdef RTLD_DEEPBIND
  if (flag & RTLD_DEEPBIND) {
    Report(
        "You are trying to dlopen a %s shared library with RTLD_DEEPBIND flag"
        " which is incompatible with sanitizer runtime "
        "(see https://github.com/google/sanitizers/issues/611 for details"
        "). If you want to run %s library under sanitizers please remove "
        "RTLD_DEEPBIND from dlopen flags.\n",
        filename, filename);
    Die();
  }
#  endif
}

uptr FindAvailableMemoryRange(uptr size, uptr alignment, uptr left_padding,
                              uptr *largest_gap_found,
                              uptr *max_occupied_addr) {
  UNREACHABLE("FindAvailableMemoryRange is not available");
  return 0;
}

bool GetRandom(void *buffer, uptr length, bool blocking) {
  if (!buffer || !length || length > 256)
    return false;
#  if SANITIZER_USE_GETENTROPY
  uptr rnd = getentropy(buffer, length);
  int rverrno = 0;
  if (internal_iserror(rnd, &rverrno) && rverrno == EFAULT)
    return false;
  else if (rnd == 0)
    return true;
#  endif  // SANITIZER_USE_GETENTROPY

#  if SANITIZER_USE_GETRANDOM
  static atomic_uint8_t skip_getrandom_syscall;
  if (!atomic_load_relaxed(&skip_getrandom_syscall)) {
    // Up to 256 bytes, getrandom will not be interrupted.
    uptr res = internal_syscall(SYSCALL(getrandom), buffer, length,
                                blocking ? 0 : GRND_NONBLOCK);
    int rverrno = 0;
    if (internal_iserror(res, &rverrno) && rverrno == ENOSYS)
      atomic_store_relaxed(&skip_getrandom_syscall, 1);
    else if (res == length)
      return true;
  }
#  endif  // SANITIZER_USE_GETRANDOM
  // Up to 256 bytes, a read off /dev/urandom will not be interrupted.
  // blocking is moot here, O_NONBLOCK has no effect when opening /dev/urandom.
  uptr fd = internal_open("/dev/urandom", O_RDONLY);
  if (internal_iserror(fd))
    return false;
  uptr res = internal_read(fd, buffer, length);
  if (internal_iserror(res))
    return false;
  internal_close(fd);
  return true;
}

}  // namespace __sanitizer

#endif
