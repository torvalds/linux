//===-- Pipe.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_PIPE_H
#define LLDB_HOST_PIPE_H

#if defined(_WIN32)
#include "lldb/Host/windows/PipeWindows.h"
namespace lldb_private {
typedef PipeWindows Pipe;
}
#else
#include "lldb/Host/posix/PipePosix.h"
namespace lldb_private {
typedef PipePosix Pipe;
}
#endif

#endif // LLDB_HOST_PIPE_H
