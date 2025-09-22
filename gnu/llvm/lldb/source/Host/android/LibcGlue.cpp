//===-- LibcGlue.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This files adds functions missing from libc on earlier versions of Android

#include <android/api-level.h>

#include <sys/syscall.h>

#if __ANDROID_API__ < 21

#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lldb/Host/Time.h"

time_t timegm(struct tm *t) { return (time_t)timegm64(t); }

int posix_openpt(int flags) { return open("/dev/ptmx", flags); }

#endif
