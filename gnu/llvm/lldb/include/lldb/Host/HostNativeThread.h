//===-- HostNativeThread.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTNATIVETHREAD_H
#define LLDB_HOST_HOSTNATIVETHREAD_H

#include "HostNativeThreadForward.h"

#if defined(_WIN32)
#include "lldb/Host/windows/HostThreadWindows.h"
#elif defined(__APPLE__)
#include "lldb/Host/macosx/HostThreadMacOSX.h"
#else
#include "lldb/Host/posix/HostThreadPosix.h"
#endif

#endif
