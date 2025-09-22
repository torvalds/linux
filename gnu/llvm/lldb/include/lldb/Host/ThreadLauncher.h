//===-- ThreadLauncher.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_THREADLAUNCHER_H
#define LLDB_HOST_THREADLAUNCHER_H

#include "lldb/Host/HostThread.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace lldb_private {

class ThreadLauncher {
public:
  static llvm::Expected<HostThread>
  LaunchThread(llvm::StringRef name,
               std::function<lldb::thread_result_t()> thread_function,
               size_t min_stack_byte_size = 0); // Minimum stack size in bytes,
                                                // set stack size to zero for
                                                // default platform thread stack
                                                // size

  struct HostThreadCreateInfo {
    std::string thread_name;
    std::function<lldb::thread_result_t()> impl;

    HostThreadCreateInfo(std::string thread_name,
                         std::function<lldb::thread_result_t()> impl)
        : thread_name(std::move(thread_name)), impl(std::move(impl)) {}
  };
};
}

#endif
