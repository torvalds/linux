//===-- HostProcessPosix.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_HOSTPROCESSPOSIX_H
#define LLDB_HOST_POSIX_HOSTPROCESSPOSIX_H

#include "lldb/Host/HostNativeProcessBase.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class FileSpec;

class HostProcessPosix : public HostNativeProcessBase {
public:
  HostProcessPosix();
  HostProcessPosix(lldb::process_t process);
  ~HostProcessPosix() override;

  virtual Status Signal(int signo) const;
  static Status Signal(lldb::process_t process, int signo);

  Status Terminate() override;

  lldb::pid_t GetProcessId() const override;
  bool IsRunning() const override;

  llvm::Expected<HostThread>
  StartMonitoring(const Host::MonitorChildProcessCallback &callback) override;
};

} // namespace lldb_private

#endif // LLDB_HOST_POSIX_HOSTPROCESSPOSIX_H
