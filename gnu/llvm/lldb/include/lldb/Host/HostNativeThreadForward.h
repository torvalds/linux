//===-- HostNativeThreadForward.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTNATIVETHREADFORWARD_H
#define LLDB_HOST_HOSTNATIVETHREADFORWARD_H

namespace lldb_private {
#if defined(_WIN32)
class HostThreadWindows;
typedef HostThreadWindows HostNativeThread;
#elif defined(__APPLE__)
class HostThreadMacOSX;
typedef HostThreadMacOSX HostNativeThread;
#else
class HostThreadPosix;
typedef HostThreadPosix HostNativeThread;
#endif
}

#endif
