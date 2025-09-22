//===-- sanitizer_common_nolibc.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains stubs for libc function to facilitate optional use of
// libc in no-libcdep sources.
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"
#include "sanitizer_platform.h"

namespace __sanitizer {

// The Windows implementations of these functions use the win32 API directly,
// bypassing libc.
#if !SANITIZER_WINDOWS
#if SANITIZER_LINUX
void LogMessageOnPrintf(const char *str) {}
#endif
void WriteToSyslog(const char *buffer) {}
void Abort() { internal__exit(1); }
bool CreateDir(const char *pathname) { return false; }
#endif // !SANITIZER_WINDOWS

#if !SANITIZER_WINDOWS && !SANITIZER_APPLE
void ListOfModules::init() {}
void InitializePlatformCommonFlags(CommonFlags *cf) {}
#endif

}  // namespace __sanitizer
