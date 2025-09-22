/*===- InstrProfilingUtil.c - Support library for PGO instrumentation -----===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#include "WindowsMMap.h"
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef COMPILER_RT_HAS_UNAME
#include <sys/utsname.h>
#endif

#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <signal.h>
#include <sys/prctl.h>
#endif

#if defined(__Fuchsia__)
#include <zircon/process.h>
#include <zircon/syscalls.h>
#endif

#if defined(__FreeBSD__)
#include <signal.h>
#include <sys/procctl.h>
#endif

#include "InstrProfiling.h"
#include "InstrProfilingUtil.h"

COMPILER_RT_VISIBILITY unsigned lprofDirMode = 0755;

COMPILER_RT_VISIBILITY
void __llvm_profile_recursive_mkdir(char *path) {
  int i;
  int start = 1;

#if defined(__ANDROID__) && defined(__ANDROID_API__) &&                        \
    defined(__ANDROID_API_FUTURE__) &&                                         \
    __ANDROID_API__ == __ANDROID_API_FUTURE__
  // Avoid spammy selinux denial messages in Android by not attempting to
  // create directories in GCOV_PREFIX.  These denials occur when creating (or
  // even attempting to stat()) top-level directories like "/data".
  //
  // Do so by ignoring ${GCOV_PREFIX} when invoking mkdir().
  const char *gcov_prefix = getenv("GCOV_PREFIX");
  if (gcov_prefix != NULL) {
    const int gcov_prefix_len = strlen(gcov_prefix);
    if (strncmp(path, gcov_prefix, gcov_prefix_len) == 0)
      start = gcov_prefix_len;
  }
#endif

  for (i = start; path[i] != '\0'; ++i) {
    char save = path[i];
    if (!IS_DIR_SEPARATOR(path[i]))
      continue;
    path[i] = '\0';
#ifdef _WIN32
    _mkdir(path);
#else
    /* Some of these will fail, ignore it. */
    mkdir(path, __llvm_profile_get_dir_mode());
#endif
    path[i] = save;
  }
}

COMPILER_RT_VISIBILITY
void __llvm_profile_set_dir_mode(unsigned Mode) { lprofDirMode = Mode; }

COMPILER_RT_VISIBILITY
unsigned __llvm_profile_get_dir_mode(void) { return lprofDirMode; }

#if COMPILER_RT_HAS_ATOMICS != 1
COMPILER_RT_VISIBILITY
uint32_t lprofBoolCmpXchg(void **Ptr, void *OldV, void *NewV) {
  void *R = *Ptr;
  if (R == OldV) {
    *Ptr = NewV;
    return 1;
  }
  return 0;
}
COMPILER_RT_VISIBILITY
void *lprofPtrFetchAdd(void **Mem, long ByteIncr) {
  void *Old = *Mem;
  *((char **)Mem) += ByteIncr;
  return Old;
}

#endif

#ifdef _WIN32
COMPILER_RT_VISIBILITY int lprofGetHostName(char *Name, int Len) {
  WCHAR Buffer[COMPILER_RT_MAX_HOSTLEN];
  DWORD BufferSize = sizeof(Buffer);
  BOOL Result =
      GetComputerNameExW(ComputerNameDnsFullyQualified, Buffer, &BufferSize);
  if (!Result)
    return -1;
  if (WideCharToMultiByte(CP_UTF8, 0, Buffer, -1, Name, Len, NULL, NULL) == 0)
    return -1;
  return 0;
}
#elif defined(COMPILER_RT_HAS_UNAME)
COMPILER_RT_VISIBILITY int lprofGetHostName(char *Name, int Len) {
  struct utsname N;
  int R = uname(&N);
  if (R >= 0) {
    strncpy(Name, N.nodename, Len);
    return 0;
  }
  return R;
}
#endif

COMPILER_RT_VISIBILITY int lprofLockFd(int fd) {
#ifdef COMPILER_RT_HAS_FCNTL_LCK
  struct flock s_flock;

  s_flock.l_whence = SEEK_SET;
  s_flock.l_start = 0;
  s_flock.l_len = 0; /* Until EOF.  */
  s_flock.l_pid = getpid();
  s_flock.l_type = F_WRLCK;

  while (fcntl(fd, F_SETLKW, &s_flock) == -1) {
    if (errno != EINTR) {
      if (errno == ENOLCK) {
        return -1;
      }
      break;
    }
  }
  return 0;
#else
  flock(fd, LOCK_EX);
  return 0;
#endif
}

COMPILER_RT_VISIBILITY int lprofUnlockFd(int fd) {
#ifdef COMPILER_RT_HAS_FCNTL_LCK
  struct flock s_flock;

  s_flock.l_whence = SEEK_SET;
  s_flock.l_start = 0;
  s_flock.l_len = 0; /* Until EOF.  */
  s_flock.l_pid = getpid();
  s_flock.l_type = F_UNLCK;

  while (fcntl(fd, F_SETLKW, &s_flock) == -1) {
    if (errno != EINTR) {
      if (errno == ENOLCK) {
        return -1;
      }
      break;
    }
  }
  return 0;
#else
  flock(fd, LOCK_UN);
  return 0;
#endif
}

COMPILER_RT_VISIBILITY int lprofLockFileHandle(FILE *F) {
  int fd;
#if defined(_WIN32)
  fd = _fileno(F);
#else
  fd = fileno(F);
#endif
  return lprofLockFd(fd);
}

COMPILER_RT_VISIBILITY int lprofUnlockFileHandle(FILE *F) {
  int fd;
#if defined(_WIN32)
  fd = _fileno(F);
#else
  fd = fileno(F);
#endif
  return lprofUnlockFd(fd);
}

COMPILER_RT_VISIBILITY FILE *lprofOpenFileEx(const char *ProfileName) {
  FILE *f;
  int fd;
#ifdef COMPILER_RT_HAS_FCNTL_LCK
  fd = open(ProfileName, O_RDWR | O_CREAT, 0666);
  if (fd < 0)
    return NULL;

  if (lprofLockFd(fd) != 0)
    PROF_WARN("Data may be corrupted during profile merging : %s\n",
              "Fail to obtain file lock due to system limit.");

  f = fdopen(fd, "r+b");
#elif defined(_WIN32)
  // FIXME: Use the wide variants to handle Unicode filenames.
  HANDLE h = CreateFileA(ProfileName, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE)
    return NULL;

  fd = _open_osfhandle((intptr_t)h, 0);
  if (fd == -1) {
    CloseHandle(h);
    return NULL;
  }

  if (lprofLockFd(fd) != 0)
    PROF_WARN("Data may be corrupted during profile merging : %s\n",
              "Fail to obtain file lock due to system limit.");

  f = _fdopen(fd, "r+b");
  if (f == 0) {
    CloseHandle(h);
    return NULL;
  }
#else
  /* Worst case no locking applied.  */
  PROF_WARN("Concurrent file access is not supported : %s\n",
            "lack file locking");
  fd = open(ProfileName, O_RDWR | O_CREAT, 0666);
  if (fd < 0)
    return NULL;
  f = fdopen(fd, "r+b");
#endif

  return f;
}

COMPILER_RT_VISIBILITY const char *lprofGetPathPrefix(int *PrefixStrip,
                                                      size_t *PrefixLen) {
  const char *Prefix = getenv("GCOV_PREFIX");
  const char *PrefixStripStr = getenv("GCOV_PREFIX_STRIP");

  *PrefixLen = 0;
  *PrefixStrip = 0;
  if (Prefix == NULL || Prefix[0] == '\0')
    return NULL;

  if (PrefixStripStr) {
    *PrefixStrip = atoi(PrefixStripStr);

    /* Negative GCOV_PREFIX_STRIP values are ignored */
    if (*PrefixStrip < 0)
      *PrefixStrip = 0;
  } else {
    *PrefixStrip = 0;
  }
  *PrefixLen = strlen(Prefix);

  return Prefix;
}

COMPILER_RT_VISIBILITY void
lprofApplyPathPrefix(char *Dest, const char *PathStr, const char *Prefix,
                     size_t PrefixLen, int PrefixStrip) {

  const char *Ptr;
  int Level;
  const char *StrippedPathStr = PathStr;

  for (Level = 0, Ptr = PathStr + 1; Level < PrefixStrip; ++Ptr) {
    if (*Ptr == '\0')
      break;

    if (!IS_DIR_SEPARATOR(*Ptr))
      continue;

    StrippedPathStr = Ptr;
    ++Level;
  }

  memcpy(Dest, Prefix, PrefixLen);

  if (!IS_DIR_SEPARATOR(Prefix[PrefixLen - 1]))
    Dest[PrefixLen++] = DIR_SEPARATOR;

  memcpy(Dest + PrefixLen, StrippedPathStr, strlen(StrippedPathStr) + 1);
}

COMPILER_RT_VISIBILITY const char *
lprofFindFirstDirSeparator(const char *Path) {
  const char *Sep = strchr(Path, DIR_SEPARATOR);
#if defined(DIR_SEPARATOR_2)
  const char *Sep2 = strchr(Path, DIR_SEPARATOR_2);
  if (Sep2 && (!Sep || Sep2 < Sep))
    Sep = Sep2;
#endif
  return Sep;
}

COMPILER_RT_VISIBILITY const char *lprofFindLastDirSeparator(const char *Path) {
  const char *Sep = strrchr(Path, DIR_SEPARATOR);
#if defined(DIR_SEPARATOR_2)
  const char *Sep2 = strrchr(Path, DIR_SEPARATOR_2);
  if (Sep2 && (!Sep || Sep2 > Sep))
    Sep = Sep2;
#endif
  return Sep;
}

COMPILER_RT_VISIBILITY int lprofSuspendSigKill(void) {
#if defined(__linux__)
  int PDeachSig = 0;
  /* Temporarily suspend getting SIGKILL upon exit of the parent process. */
  if (prctl(PR_GET_PDEATHSIG, &PDeachSig) == 0 && PDeachSig == SIGKILL)
    prctl(PR_SET_PDEATHSIG, 0);
  return (PDeachSig == SIGKILL);
#elif defined(__FreeBSD__)
  int PDeachSig = 0, PDisableSig = 0;
  if (procctl(P_PID, 0, PROC_PDEATHSIG_STATUS, &PDeachSig) == 0 &&
      PDeachSig == SIGKILL)
    procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &PDisableSig);
  return (PDeachSig == SIGKILL);
#else
  return 0;
#endif
}

COMPILER_RT_VISIBILITY void lprofRestoreSigKill(void) {
#if defined(__linux__)
  prctl(PR_SET_PDEATHSIG, SIGKILL);
#elif defined(__FreeBSD__)
  int PEnableSig = SIGKILL;
  procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &PEnableSig);
#endif
}

COMPILER_RT_VISIBILITY int lprofReleaseMemoryPagesToOS(uintptr_t Begin,
                                                       uintptr_t End) {
#if defined(__ve__)
  // VE doesn't support madvise.
  return 0;
#else
  size_t PageSize = getpagesize();
  uintptr_t BeginAligned = lprofRoundUpTo((uintptr_t)Begin, PageSize);
  uintptr_t EndAligned = lprofRoundDownTo((uintptr_t)End, PageSize);
  if (BeginAligned < EndAligned) {
#if defined(__Fuchsia__)
    return _zx_vmar_op_range(_zx_vmar_root_self(), ZX_VMAR_OP_DECOMMIT,
                             (zx_vaddr_t)BeginAligned,
                             EndAligned - BeginAligned, NULL, 0);
#else
    return madvise((void *)BeginAligned, EndAligned - BeginAligned,
                   MADV_DONTNEED);
#endif
  }
  return 0;
#endif
}
