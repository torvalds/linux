//===-- HostThreadMacOSX.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_MACOSX_HOSTTHREADMACOSX_H
#define LLDB_HOST_MACOSX_HOSTTHREADMACOSX_H

#include "lldb/Host/posix/HostThreadPosix.h"

namespace lldb_private {

class HostThreadMacOSX : public HostThreadPosix {
  friend class ThreadLauncher;

public:
  using HostThreadPosix::HostThreadPosix;

protected:
  static lldb::thread_result_t ThreadCreateTrampoline(lldb::thread_arg_t arg);
};
}

#endif
