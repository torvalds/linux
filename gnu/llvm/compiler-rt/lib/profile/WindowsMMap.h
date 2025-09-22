/*===- WindowsMMap.h - Support library for PGO instrumentation ------------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILING_WINDOWS_MMAP_H
#define PROFILE_INSTRPROFILING_WINDOWS_MMAP_H

#if defined(_WIN32)

#include <basetsd.h>
#include <io.h>
#include <sys/types.h>

/*
 * mmap() flags
 */
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x0

#define MAP_FILE      0x00
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *) -1)

/*
 * msync() flags
 */
#define MS_ASYNC        0x0001  /* return immediately */
#define MS_INVALIDATE   0x0002  /* invalidate all cached data */
#define MS_SYNC         0x0010  /* msync synchronously */

/*
 * madvise() flags
 */

#define MADV_NORMAL     0   /* no special treatment */
#define MADV_WILLNEED   3   /* expect access in the near future */
#define MADV_DONTNEED   4   /* do not expect access in the near future */

/*
 * flock() operations
 */
#define   LOCK_SH   1    /* shared lock */
#define   LOCK_EX   2    /* exclusive lock */
#define   LOCK_NB   4    /* don't block when locking */
#define   LOCK_UN   8    /* unlock */

#ifdef __USE_FILE_OFFSET64
# define DWORD_HI(x) (x >> 32)
# define DWORD_LO(x) ((x) & 0xffffffff)
#else
# define DWORD_HI(x) (0)
# define DWORD_LO(x) (x)
#endif

#define mmap __llvm_profile_mmap
#define munmap __llvm_profile_munmap
#define msync __llvm_profile_msync
#define madvise __llvm_profile_madvise
#define flock __llvm_profile_flock

void *mmap(void *start, size_t length, int prot, int flags, int fd,
           off_t offset);

void munmap(void *addr, size_t length);

int msync(void *addr, size_t length, int flags);

int madvise(void *addr, size_t length, int advice);

int flock(int fd, int operation);

#endif /* _WIN32 */

#endif /* PROFILE_INSTRPROFILING_WINDOWS_MMAP_H */
