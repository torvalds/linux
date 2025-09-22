//===-- Time.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Include system time headers, adding missing functions as necessary

#ifndef LLDB_HOST_TIME_H
#define LLDB_HOST_TIME_H

#ifdef __ANDROID__
#include <android/api-level.h>
#endif

#if defined(__ANDROID_API__) && __ANDROID_API__ < 21
#include <time64.h>
extern time_t timegm(struct tm *t);
#else
#include <ctime>
#endif

#endif // LLDB_HOST_TIME_H
