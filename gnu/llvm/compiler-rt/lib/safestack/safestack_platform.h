//===-- safestack_platform.h ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements platform specific parts of SafeStack runtime.
// Don't use equivalent functionality from sanitizer_common to avoid dragging
// a large codebase into security sensitive code.
//
//===----------------------------------------------------------------------===//

#ifndef SAFESTACK_PLATFORM_H
#define SAFESTACK_PLATFORM_H

#include "safestack_util.h"
#include "sanitizer_common/sanitizer_platform.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#if !(SANITIZER_NETBSD || SANITIZER_FREEBSD || SANITIZER_LINUX || \
      SANITIZER_SOLARIS)
#  error "Support for your platform has not been implemented"
#endif

#if SANITIZER_NETBSD
#include <lwp.h>

extern "C" void *__mmap(void *, size_t, int, int, int, int, off_t);
#endif

#if SANITIZER_FREEBSD
#include <sys/thr.h>
#endif

#if SANITIZER_SOLARIS
#  include <thread.h>
#endif

// Keep in sync with sanitizer_linux.cpp.
//
// Are we using 32-bit or 64-bit Linux syscalls?
// x32 (which defines __x86_64__) has SANITIZER_WORDSIZE == 32
// but it still needs to use 64-bit syscalls.
#if SANITIZER_LINUX &&                                \
    (defined(__x86_64__) || defined(__powerpc64__) || \
     SANITIZER_WORDSIZE == 64 || (defined(__mips__) && _MIPS_SIM == _ABIN32))
#  define SANITIZER_LINUX_USES_64BIT_SYSCALLS 1
#else
#  define SANITIZER_LINUX_USES_64BIT_SYSCALLS 0
#endif

namespace safestack {

#if SANITIZER_NETBSD
static void *GetRealLibcAddress(const char *symbol) {
  void *real = dlsym(RTLD_NEXT, symbol);
  if (!real)
    real = dlsym(RTLD_DEFAULT, symbol);
  if (!real) {
    fprintf(stderr, "safestack GetRealLibcAddress failed for symbol=%s",
            symbol);
    abort();
  }
  return real;
}

#define _REAL(func, ...) real##_##func(__VA_ARGS__)
#define DEFINE__REAL(ret_type, func, ...)                              \
  static ret_type (*real_##func)(__VA_ARGS__) = NULL;                  \
  if (!real_##func) {                                                  \
    real_##func = (ret_type(*)(__VA_ARGS__))GetRealLibcAddress(#func); \
  }                                                                    \
  SFS_CHECK(real_##func);
#endif

#if SANITIZER_SOLARIS
#  define _REAL(func) _##func
#  define DEFINE__REAL(ret_type, func, ...) \
    extern "C" ret_type _REAL(func)(__VA_ARGS__)

#  if !defined(_LP64) && _FILE_OFFSET_BITS == 64
#    define _REAL64(func) _##func##64
#  else
#    define _REAL64(func) _REAL(func)
#  endif
#  define DEFINE__REAL64(ret_type, func, ...) \
    extern "C" ret_type _REAL64(func)(__VA_ARGS__)

DEFINE__REAL64(void *, mmap, void *a, size_t b, int c, int d, int e, off_t f);
DEFINE__REAL(int, munmap, void *a, size_t b);
DEFINE__REAL(int, mprotect, void *a, size_t b, int c);
#endif

using ThreadId = uint64_t;

inline ThreadId GetTid() {
#if SANITIZER_NETBSD
  DEFINE__REAL(int, _lwp_self);
  return _REAL(_lwp_self);
#elif SANITIZER_FREEBSD
  long Tid;
  thr_self(&Tid);
  return Tid;
#elif SANITIZER_SOLARIS
  return thr_self();
#else
  return syscall(SYS_gettid);
#endif
}

inline int TgKill(pid_t pid, ThreadId tid, int sig) {
#if SANITIZER_NETBSD
  DEFINE__REAL(int, _lwp_kill, int a, int b);
  (void)pid;
  return _REAL(_lwp_kill, tid, sig);
#elif SANITIZER_SOLARIS
  (void)pid;
  errno = thr_kill(tid, sig);
  // TgKill is expected to return -1 on error, not an errno.
  return errno != 0 ? -1 : 0;
#elif SANITIZER_FREEBSD
  return syscall(SYS_thr_kill2, pid, tid, sig);
#else
  // tid is pid_t (int), not ThreadId (uint64_t).
  return syscall(SYS_tgkill, pid, (pid_t)tid, sig);
#endif
}

inline void *Mmap(void *addr, size_t length, int prot, int flags, int fd,
                  off_t offset) {
#if SANITIZER_NETBSD
  return __mmap(addr, length, prot, flags, fd, 0, offset);
#elif SANITIZER_FREEBSD && (defined(__aarch64__) || defined(__x86_64__))
  return (void *)__syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
#elif SANITIZER_FREEBSD && (defined(__i386__))
  return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
#elif SANITIZER_SOLARIS
  return _REAL64(mmap)(addr, length, prot, flags, fd, offset);
#elif SANITIZER_LINUX_USES_64BIT_SYSCALLS
  return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
#else
  // mmap2 specifies file offset in 4096-byte units.
  SFS_CHECK(IsAligned(offset, 4096));
  return (void *)syscall(SYS_mmap2, addr, length, prot, flags, fd,
                         offset / 4096);
#endif
}

inline int Munmap(void *addr, size_t length) {
#if SANITIZER_NETBSD
  DEFINE__REAL(int, munmap, void *a, size_t b);
  return _REAL(munmap, addr, length);
#elif SANITIZER_SOLARIS
  return _REAL(munmap)(addr, length);
#else
  return syscall(SYS_munmap, addr, length);
#endif
}

inline int Mprotect(void *addr, size_t length, int prot) {
#if SANITIZER_NETBSD
  DEFINE__REAL(int, mprotect, void *a, size_t b, int c);
  return _REAL(mprotect, addr, length, prot);
#elif SANITIZER_SOLARIS
  return _REAL(mprotect)(addr, length, prot);
#else
  return syscall(SYS_mprotect, addr, length, prot);
#endif
}

}  // namespace safestack

#endif  // SAFESTACK_PLATFORM_H
