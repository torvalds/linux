//===-- HostThreadMacOSX.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/macosx/HostThreadMacOSX.h"
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>

using namespace lldb_private;

lldb::thread_result_t
HostThreadMacOSX::ThreadCreateTrampoline(lldb::thread_arg_t arg) {
  @autoreleasepool {
    return HostThreadPosix::ThreadCreateTrampoline(arg);
  }
}
