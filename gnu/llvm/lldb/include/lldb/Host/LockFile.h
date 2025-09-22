//===-- LockFile.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_LOCKFILE_H
#define LLDB_HOST_LOCKFILE_H

#if defined(_WIN32)
#include "lldb/Host/windows/LockFileWindows.h"
namespace lldb_private {
typedef LockFileWindows LockFile;
}
#else
#include "lldb/Host/posix/LockFilePosix.h"
namespace lldb_private {
typedef LockFilePosix LockFile;
}
#endif

#endif // LLDB_HOST_LOCKFILE_H
