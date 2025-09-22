//===-- HostNativeProcess.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTNATIVEPROCESS_H
#define LLDB_HOST_HOSTNATIVEPROCESS_H

#if defined(_WIN32)
#include "lldb/Host/windows/HostProcessWindows.h"
namespace lldb_private {
typedef HostProcessWindows HostNativeProcess;
}
#else
#include "lldb/Host/posix/HostProcessPosix.h"
namespace lldb_private {
typedef HostProcessPosix HostNativeProcess;
}
#endif

#endif
