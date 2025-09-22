/*===- InstrProfilingUtil.h - Support library for PGO instrumentation -----===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILINGUTIL_H
#define PROFILE_INSTRPROFILINGUTIL_H

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

/*! \brief Create a directory tree. */
void __llvm_profile_recursive_mkdir(char *Pathname);

/*! Set the mode used when creating profile directories. */
void __llvm_profile_set_dir_mode(unsigned Mode);

/*! Return the directory creation mode. */
unsigned __llvm_profile_get_dir_mode(void);

int lprofLockFd(int fd);
int lprofUnlockFd(int fd);
int lprofLockFileHandle(FILE *F);
int lprofUnlockFileHandle(FILE *F);

/*! Open file \c Filename for read+write with write
 * lock for exclusive access. The caller will block
 * if the lock is already held by another process. */
FILE *lprofOpenFileEx(const char *Filename);
/* PS4 doesn't have setenv/getenv/fork. Define a shim. */
#if __ORBIS__
#include <sys/types.h>
static inline char *getenv(const char *name) { return NULL; }
static inline int setenv(const char *name, const char *value, int overwrite)
{ return 0; }
static pid_t fork() { return -1; }
#endif /* #if __ORBIS__ */

/* GCOV_PREFIX and GCOV_PREFIX_STRIP support */
/* Return the path prefix specified by GCOV_PREFIX environment variable.
 * If GCOV_PREFIX_STRIP is also specified, the strip level (integer value)
 * is returned via \c *PrefixStrip. The prefix length is stored in *PrefixLen.
 */
const char *lprofGetPathPrefix(int *PrefixStrip, size_t *PrefixLen);
/* Apply the path prefix specified in \c Prefix to path string in \c PathStr,
 * and store the result to buffer pointed to by \c Buffer. If \c PrefixStrip
 * is not zero, path prefixes are stripped from \c PathStr (the level of
 * stripping is specified by \c PrefixStrip) before \c Prefix is added.
 */
void lprofApplyPathPrefix(char *Dest, const char *PathStr, const char *Prefix,
                          size_t PrefixLen, int PrefixStrip);

/* Returns a pointer to the first occurrence of \c DIR_SEPARATOR char in
 * the string \c Path, or NULL if the char is not found. */
const char *lprofFindFirstDirSeparator(const char *Path);
/* Returns a pointer to the last occurrence of \c DIR_SEPARATOR char in
 * the string \c Path, or NULL if the char is not found. */
const char *lprofFindLastDirSeparator(const char *Path);

int lprofGetHostName(char *Name, int Len);

unsigned lprofBoolCmpXchg(void **Ptr, void *OldV, void *NewV);
void *lprofPtrFetchAdd(void **Mem, long ByteIncr);

/* Temporarily suspend SIGKILL. Return value of 1 means a restore is needed.
 * Other return values mean no restore is needed.
 */
int lprofSuspendSigKill();

/* Restore previously suspended SIGKILL. */
void lprofRestoreSigKill();

static inline size_t lprofRoundUpTo(size_t x, size_t boundary) {
  return (x + boundary - 1) & ~(boundary - 1);
}

static inline size_t lprofRoundDownTo(size_t x, size_t boundary) {
  return x & ~(boundary - 1);
}

int lprofReleaseMemoryPagesToOS(uintptr_t Begin, uintptr_t End);

#endif /* PROFILE_INSTRPROFILINGUTIL_H */
