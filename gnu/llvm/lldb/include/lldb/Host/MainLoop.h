//===-- MainLoop.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_MAINLOOP_H
#define LLDB_HOST_MAINLOOP_H

#ifdef _WIN32
#include "lldb/Host/windows/MainLoopWindows.h"
namespace lldb_private {
using MainLoop = MainLoopWindows;
}
#else
#include "lldb/Host/posix/MainLoopPosix.h"
namespace lldb_private {
using MainLoop = MainLoopPosix;
}
#endif

#endif // LLDB_HOST_MAINLOOP_H
