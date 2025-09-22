//===-- windows/PosixApi.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_windows_PosixApi_h
#define liblldb_Host_windows_PosixApi_h

#include "lldb/Host/Config.h"
#include "llvm/Support/Compiler.h"
#if !defined(_WIN32)
#error "windows/PosixApi.h being #included on non Windows system!"
#endif

// va_start, va_end, etc macros.
#include <cstdarg>

// time_t, timespec, etc.
#include <ctime>

#ifndef PATH_MAX
#define PATH_MAX 32768
#endif

#define O_NOCTTY 0
#define O_NONBLOCK 0
#define SIGTRAP 5
#define SIGKILL 9
#define SIGSTOP 20

#ifndef S_IRUSR
#define S_IRUSR S_IREAD  /* read, user */
#define S_IWUSR S_IWRITE /* write, user */
#define S_IXUSR 0        /* execute, user */
#endif
#ifndef S_IRGRP
#define S_IRGRP 0 /* read, group */
#define S_IWGRP 0 /* write, group */
#define S_IXGRP 0 /* execute, group */
#endif
#ifndef S_IROTH
#define S_IROTH 0 /* read, others */
#define S_IWOTH 0 /* write, others */
#define S_IXOTH 0 /* execute, others */
#endif
#ifndef S_IRWXU
#define S_IRWXU 0
#endif
#ifndef S_IRWXG
#define S_IRWXG 0
#endif
#ifndef S_IRWXO
#define S_IRWXO 0
#endif

// pyconfig.h typedefs this.  We require python headers to be included before
// any LLDB headers, but there's no way to prevent python's pid_t definition
// from leaking, so this is the best option.
#ifndef NO_PID_T
#include <sys/types.h>
#endif

#ifdef _MSC_VER

// PRIxxx format macros for printf()
#include <cinttypes>

// open(), close(), creat(), etc.
#include <io.h>

typedef unsigned short mode_t;

// pyconfig.h typedefs this.  We require python headers to be included before
// any LLDB headers, but there's no way to prevent python's pid_t definition
// from leaking, so this is the best option.
#ifndef NO_PID_T
typedef uint32_t pid_t;
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#endif // _MSC_VER

// empty functions
inline int posix_openpt(int flag) { LLVM_BUILTIN_UNREACHABLE; }

inline int unlockpt(int fd) { LLVM_BUILTIN_UNREACHABLE; }
inline int grantpt(int fd) { LLVM_BUILTIN_UNREACHABLE; }
inline char *ptsname(int fd) { LLVM_BUILTIN_UNREACHABLE; }

inline pid_t fork(void) { LLVM_BUILTIN_UNREACHABLE; }
inline pid_t setsid(void) { LLVM_BUILTIN_UNREACHABLE; }

#endif
