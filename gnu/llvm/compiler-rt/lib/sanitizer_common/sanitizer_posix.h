//===-- sanitizer_posix.h -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and declares some useful POSIX-specific functions.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_POSIX_H
#define SANITIZER_POSIX_H

// ----------- ATTENTION -------------
// This header should NOT include any other headers from sanitizer runtime.
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform_limits_freebsd.h"
#include "sanitizer_platform_limits_netbsd.h"
#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_platform_limits_solaris.h"

#if SANITIZER_POSIX

namespace __sanitizer {

// I/O
// Don't use directly, use __sanitizer::OpenFile() instead.
uptr internal_open(const char *filename, int flags);
uptr internal_open(const char *filename, int flags, u32 mode);
uptr internal_close(fd_t fd);

uptr internal_read(fd_t fd, void *buf, uptr count);
uptr internal_write(fd_t fd, const void *buf, uptr count);

// Memory
uptr internal_mmap(void *addr, uptr length, int prot, int flags,
                   int fd, u64 offset);
uptr internal_munmap(void *addr, uptr length);
#if SANITIZER_LINUX
uptr internal_mremap(void *old_address, uptr old_size, uptr new_size, int flags,
                     void *new_address);
#endif
int internal_mprotect(void *addr, uptr length, int prot);
int internal_madvise(uptr addr, uptr length, int advice);

// OS
uptr internal_filesize(fd_t fd);  // -1 on error.
uptr internal_stat(const char *path, void *buf);
uptr internal_lstat(const char *path, void *buf);
uptr internal_fstat(fd_t fd, void *buf);
uptr internal_dup(int oldfd);
uptr internal_dup2(int oldfd, int newfd);
uptr internal_readlink(const char *path, char *buf, uptr bufsize);
uptr internal_unlink(const char *path);
uptr internal_rename(const char *oldpath, const char *newpath);
uptr internal_lseek(fd_t fd, OFF_T offset, int whence);

#if SANITIZER_NETBSD
uptr internal_ptrace(int request, int pid, void *addr, int data);
#else
uptr internal_ptrace(int request, int pid, void *addr, void *data);
#endif
uptr internal_waitpid(int pid, int *status, int options);

int internal_fork();
fd_t internal_spawn(const char *argv[], const char *envp[], pid_t *pid);

int internal_sysctl(const int *name, unsigned int namelen, void *oldp,
                    uptr *oldlenp, const void *newp, uptr newlen);
int internal_sysctlbyname(const char *sname, void *oldp, uptr *oldlenp,
                          const void *newp, uptr newlen);

// These functions call appropriate pthread_ functions directly, bypassing
// the interceptor. They are weak and may not be present in some tools.
SANITIZER_WEAK_ATTRIBUTE
int internal_pthread_create(void *th, void *attr, void *(*callback)(void *),
                            void *param);
SANITIZER_WEAK_ATTRIBUTE
int internal_pthread_join(void *th, void **ret);

#  define DEFINE_INTERNAL_PTHREAD_FUNCTIONS                               \
    namespace __sanitizer {                                               \
    int internal_pthread_create(void *th, void *attr,                     \
                                void *(*callback)(void *), void *param) { \
      return REAL(pthread_create)(th, attr, callback, param);             \
    }                                                                     \
    int internal_pthread_join(void *th, void **ret) {                     \
      return REAL(pthread_join(th, ret));                                 \
    }                                                                     \
    }  // namespace __sanitizer

int internal_pthread_attr_getstack(void *attr, void **addr, uptr *size);

// A routine named real_sigaction() must be implemented by each sanitizer in
// order for internal_sigaction() to bypass interceptors.
int internal_sigaction(int signum, const void *act, void *oldact);
void internal_sigfillset(__sanitizer_sigset_t *set);
void internal_sigemptyset(__sanitizer_sigset_t *set);
bool internal_sigismember(__sanitizer_sigset_t *set, int signum);

uptr internal_execve(const char *filename, char *const argv[],
                     char *const envp[]);

bool IsStateDetached(int state);

// Move the fd out of {0, 1, 2} range.
fd_t ReserveStandardFds(fd_t fd);

bool ShouldMockFailureToOpen(const char *path);

// Create a non-file mapping with a given /proc/self/maps name.
uptr MmapNamed(void *addr, uptr length, int prot, int flags, const char *name);

// Platforms should implement at most one of these.
// 1. Provide a pre-decorated file descriptor to use instead of an anonymous
// mapping.
int GetNamedMappingFd(const char *name, uptr size, int *flags);
// 2. Add name to an existing anonymous mapping. The caller must keep *name
// alive at least as long as the mapping exists.
void DecorateMapping(uptr addr, uptr size, const char *name);

#  if !SANITIZER_FREEBSD
#    define __sanitizer_dirsiz(dp) ((dp)->d_reclen)
#  endif

}  // namespace __sanitizer

#endif  // SANITIZER_POSIX

#endif  // SANITIZER_POSIX_H
