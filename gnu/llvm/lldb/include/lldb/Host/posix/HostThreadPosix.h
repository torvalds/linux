//===-- HostThreadPosix.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_HOSTTHREADPOSIX_H
#define LLDB_HOST_POSIX_HOSTTHREADPOSIX_H

#include "lldb/Host/HostNativeThreadBase.h"

namespace lldb_private {

class HostThreadPosix : public HostNativeThreadBase {
  HostThreadPosix(const HostThreadPosix &) = delete;
  const HostThreadPosix &operator=(const HostThreadPosix &) = delete;

public:
  HostThreadPosix();
  HostThreadPosix(lldb::thread_t thread);
  ~HostThreadPosix() override;

  Status Join(lldb::thread_result_t *result) override;
  Status Cancel() override;

  Status Detach();
};

} // namespace lldb_private

#endif // LLDB_HOST_POSIX_HOSTTHREADPOSIX_H
