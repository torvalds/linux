//===- zOSSupport.h - Common z/OS Include File ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines z/OS implementations for common functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ZOSSUPPORT_H
#define LLVM_SUPPORT_ZOSSUPPORT_H

#ifdef __MVS__
#include <sys/resource.h>
#include <sys/wait.h>

// z/OS Unix System Services does not have strsignal() support, so the
// strsignal() function is implemented here.
inline char *strsignal(int sig) {
  static char msg[256];
  sprintf(msg, "%d", sig);
  return msg;
}

// z/OS Unix System Services does not have wait4() support, so the wait4
// function is implemented here.
inline pid_t wait4(pid_t pid, int *wstatus, int options,
                   struct rusage *rusage) {
  pid_t Result = waitpid(pid, wstatus, options);
  int GetrusageRC = getrusage(RUSAGE_CHILDREN, rusage);
  assert(!GetrusageRC && "Must have valid measure of the resources!");
  return Result;
}

// z/OS Unix System Services does not have strnlen() support, so the strnlen()
// function is implemented here.
inline std::size_t strnlen(const char *S, std::size_t MaxLen) {
  const char *PtrToNullChar =
      static_cast<const char *>(std::memchr(S, '\0', MaxLen));
  return PtrToNullChar ? PtrToNullChar - S : MaxLen;
}

#endif
#endif
