//===-- sanitizer_netbsd.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between Sanitizer run-time libraries and implements
// NetBSD-specific functions from sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_NETBSD

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_getauxval.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_linux.h"
#include "sanitizer_mutex.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_procmaps.h"

#include <sys/param.h>
#include <sys/types.h>

#include <sys/exec.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <lwp.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

extern "C" void *__mmap(void *, size_t, int, int, int, int,
                        off_t) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int __sysctl(const int *, unsigned int, void *, size_t *,
                        const void *, size_t) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys_close(int) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys_open(const char *, int, ...) SANITIZER_WEAK_ATTRIBUTE;
extern "C" ssize_t _sys_read(int, void *, size_t) SANITIZER_WEAK_ATTRIBUTE;
extern "C" ssize_t _sys_write(int, const void *,
                              size_t) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int __ftruncate(int, int, off_t) SANITIZER_WEAK_ATTRIBUTE;
extern "C" ssize_t _sys_readlink(const char *, char *,
                                 size_t) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys_sched_yield() SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys___nanosleep50(const void *,
                                  void *) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys_execve(const char *, char *const[],
                           char *const[]) SANITIZER_WEAK_ATTRIBUTE;
extern "C" off_t __lseek(int, int, off_t, int) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int __fork() SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys___sigprocmask14(int, const void *,
                                    void *) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int _sys___wait450(int wpid, int *, int,
                              void *) SANITIZER_WEAK_ATTRIBUTE;

namespace __sanitizer {

static void *GetRealLibcAddress(const char *symbol) {
  void *real = dlsym(RTLD_NEXT, symbol);
  if (!real)
    real = dlsym(RTLD_DEFAULT, symbol);
  if (!real) {
    Printf("GetRealLibcAddress failed for symbol=%s", symbol);
    Die();
  }
  return real;
}

#define _REAL(func, ...) real##_##func(__VA_ARGS__)
#define DEFINE__REAL(ret_type, func, ...)                              \
  static ret_type (*real_##func)(__VA_ARGS__) = NULL;                  \
  if (!real_##func) {                                                  \
    real_##func = (ret_type(*)(__VA_ARGS__))GetRealLibcAddress(#func); \
  }                                                                    \
  CHECK(real_##func);

// --------------- sanitizer_libc.h
uptr internal_mmap(void *addr, uptr length, int prot, int flags, int fd,
                   u64 offset) {
  CHECK(&__mmap);
  return (uptr)__mmap(addr, length, prot, flags, fd, 0, offset);
}

uptr internal_munmap(void *addr, uptr length) {
  DEFINE__REAL(int, munmap, void *a, uptr b);
  return _REAL(munmap, addr, length);
}

uptr internal_mremap(void *old_address, uptr old_size, uptr new_size, int flags,
                     void *new_address) {
  CHECK(false && "internal_mremap is unimplemented on NetBSD");
  return 0;
}

int internal_mprotect(void *addr, uptr length, int prot) {
  DEFINE__REAL(int, mprotect, void *a, uptr b, int c);
  return _REAL(mprotect, addr, length, prot);
}

int internal_madvise(uptr addr, uptr length, int advice) {
  DEFINE__REAL(int, madvise, void *a, uptr b, int c);
  return _REAL(madvise, (void *)addr, length, advice);
}

uptr internal_close(fd_t fd) {
  CHECK(&_sys_close);
  return _sys_close(fd);
}

uptr internal_open(const char *filename, int flags) {
  CHECK(&_sys_open);
  return _sys_open(filename, flags);
}

uptr internal_open(const char *filename, int flags, u32 mode) {
  CHECK(&_sys_open);
  return _sys_open(filename, flags, mode);
}

uptr internal_read(fd_t fd, void *buf, uptr count) {
  sptr res;
  CHECK(&_sys_read);
  HANDLE_EINTR(res, (sptr)_sys_read(fd, buf, (size_t)count));
  return res;
}

uptr internal_write(fd_t fd, const void *buf, uptr count) {
  sptr res;
  CHECK(&_sys_write);
  HANDLE_EINTR(res, (sptr)_sys_write(fd, buf, count));
  return res;
}

uptr internal_ftruncate(fd_t fd, uptr size) {
  sptr res;
  CHECK(&__ftruncate);
  HANDLE_EINTR(res, __ftruncate(fd, 0, (s64)size));
  return res;
}

uptr internal_stat(const char *path, void *buf) {
  DEFINE__REAL(int, __stat50, const char *a, void *b);
  return _REAL(__stat50, path, buf);
}

uptr internal_lstat(const char *path, void *buf) {
  DEFINE__REAL(int, __lstat50, const char *a, void *b);
  return _REAL(__lstat50, path, buf);
}

uptr internal_fstat(fd_t fd, void *buf) {
  DEFINE__REAL(int, __fstat50, int a, void *b);
  return _REAL(__fstat50, fd, buf);
}

uptr internal_filesize(fd_t fd) {
  struct stat st;
  if (internal_fstat(fd, &st))
    return -1;
  return (uptr)st.st_size;
}

uptr internal_dup(int oldfd) {
  DEFINE__REAL(int, dup, int a);
  return _REAL(dup, oldfd);
}

uptr internal_dup2(int oldfd, int newfd) {
  DEFINE__REAL(int, dup2, int a, int b);
  return _REAL(dup2, oldfd, newfd);
}

uptr internal_readlink(const char *path, char *buf, uptr bufsize) {
  CHECK(&_sys_readlink);
  return (uptr)_sys_readlink(path, buf, bufsize);
}

uptr internal_unlink(const char *path) {
  DEFINE__REAL(int, unlink, const char *a);
  return _REAL(unlink, path);
}

uptr internal_rename(const char *oldpath, const char *newpath) {
  DEFINE__REAL(int, rename, const char *a, const char *b);
  return _REAL(rename, oldpath, newpath);
}

uptr internal_sched_yield() {
  CHECK(&_sys_sched_yield);
  return _sys_sched_yield();
}

void internal__exit(int exitcode) {
  DEFINE__REAL(void, _exit, int a);
  _REAL(_exit, exitcode);
  Die();  // Unreachable.
}

void internal_usleep(u64 useconds) {
  struct timespec ts;
  ts.tv_sec = useconds / 1000000;
  ts.tv_nsec = (useconds % 1000000) * 1000;
  CHECK(&_sys___nanosleep50);
  _sys___nanosleep50(&ts, &ts);
}

uptr internal_execve(const char *filename, char *const argv[],
                     char *const envp[]) {
  CHECK(&_sys_execve);
  return _sys_execve(filename, argv, envp);
}

tid_t GetTid() {
  DEFINE__REAL(int, _lwp_self);
  return _REAL(_lwp_self);
}

int TgKill(pid_t pid, tid_t tid, int sig) {
  DEFINE__REAL(int, _lwp_kill, int a, int b);
  (void)pid;
  return _REAL(_lwp_kill, tid, sig);
}

u64 NanoTime() {
  timeval tv;
  DEFINE__REAL(int, __gettimeofday50, void *a, void *b);
  internal_memset(&tv, 0, sizeof(tv));
  _REAL(__gettimeofday50, &tv, 0);
  return (u64)tv.tv_sec * 1000 * 1000 * 1000 + tv.tv_usec * 1000;
}

uptr internal_clock_gettime(__sanitizer_clockid_t clk_id, void *tp) {
  DEFINE__REAL(int, __clock_gettime50, __sanitizer_clockid_t a, void *b);
  return _REAL(__clock_gettime50, clk_id, tp);
}

uptr internal_ptrace(int request, int pid, void *addr, int data) {
  DEFINE__REAL(int, ptrace, int a, int b, void *c, int d);
  return _REAL(ptrace, request, pid, addr, data);
}

uptr internal_waitpid(int pid, int *status, int options) {
  CHECK(&_sys___wait450);
  return _sys___wait450(pid, status, options, 0 /* rusage */);
}

uptr internal_getpid() {
  DEFINE__REAL(int, getpid);
  return _REAL(getpid);
}

uptr internal_getppid() {
  DEFINE__REAL(int, getppid);
  return _REAL(getppid);
}

int internal_dlinfo(void *handle, int request, void *p) {
  DEFINE__REAL(int, dlinfo, void *a, int b, void *c);
  return _REAL(dlinfo, handle, request, p);
}

uptr internal_getdents(fd_t fd, void *dirp, unsigned int count) {
  DEFINE__REAL(int, __getdents30, int a, void *b, size_t c);
  return _REAL(__getdents30, fd, dirp, count);
}

uptr internal_lseek(fd_t fd, OFF_T offset, int whence) {
  CHECK(&__lseek);
  return __lseek(fd, 0, offset, whence);
}

uptr internal_prctl(int option, uptr arg2, uptr arg3, uptr arg4, uptr arg5) {
  Printf("internal_prctl not implemented for NetBSD");
  Die();
  return 0;
}

uptr internal_sigaltstack(const void *ss, void *oss) {
  DEFINE__REAL(int, __sigaltstack14, const void *a, void *b);
  return _REAL(__sigaltstack14, ss, oss);
}

int internal_fork() {
  CHECK(&__fork);
  return __fork();
}

int internal_sysctl(const int *name, unsigned int namelen, void *oldp,
                    uptr *oldlenp, const void *newp, uptr newlen) {
  CHECK(&__sysctl);
  return __sysctl(name, namelen, oldp, (size_t *)oldlenp, newp, (size_t)newlen);
}

int internal_sysctlbyname(const char *sname, void *oldp, uptr *oldlenp,
                          const void *newp, uptr newlen) {
  DEFINE__REAL(int, sysctlbyname, const char *a, void *b, size_t *c,
               const void *d, size_t e);
  return _REAL(sysctlbyname, sname, oldp, (size_t *)oldlenp, newp,
               (size_t)newlen);
}

uptr internal_sigprocmask(int how, __sanitizer_sigset_t *set,
                          __sanitizer_sigset_t *oldset) {
  CHECK(&_sys___sigprocmask14);
  return _sys___sigprocmask14(how, set, oldset);
}

void internal_sigfillset(__sanitizer_sigset_t *set) {
  DEFINE__REAL(int, __sigfillset14, const void *a);
  (void)_REAL(__sigfillset14, set);
}

void internal_sigemptyset(__sanitizer_sigset_t *set) {
  DEFINE__REAL(int, __sigemptyset14, const void *a);
  (void)_REAL(__sigemptyset14, set);
}

void internal_sigdelset(__sanitizer_sigset_t *set, int signo) {
  DEFINE__REAL(int, __sigdelset14, const void *a, int b);
  (void)_REAL(__sigdelset14, set, signo);
}

uptr internal_clone(int (*fn)(void *), void *child_stack, int flags,
                    void *arg) {
  DEFINE__REAL(int, clone, int (*a)(void *b), void *c, int d, void *e);

  return _REAL(clone, fn, child_stack, flags, arg);
}

}  // namespace __sanitizer

#endif
