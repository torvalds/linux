//===-- HostProcess.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTPROCESS_H
#define LLDB_HOST_HOSTPROCESS_H

#include "lldb/Host/Host.h"
#include "lldb/lldb-types.h"

/// A class that represents a running process on the host machine.
///
/// HostProcess allows querying and manipulation of processes running on the
/// host machine.  It is not intended to be represent a process which is being
/// debugged, although the native debug engine of a platform may likely back
/// inferior processes by a HostProcess.
///
/// HostProcess is implemented using static polymorphism so that on any given
/// platform, an instance of HostProcess will always be able to bind
/// statically to the concrete Process implementation for that platform.  See
/// HostInfo for more details.
///

namespace lldb_private {

class HostNativeProcessBase;
class HostThread;

class HostProcess {
public:
  HostProcess();
  HostProcess(lldb::process_t process);
  ~HostProcess();

  Status Terminate();

  lldb::pid_t GetProcessId() const;
  bool IsRunning() const;

  llvm::Expected<HostThread>
  StartMonitoring(const Host::MonitorChildProcessCallback &callback);

  HostNativeProcessBase &GetNativeProcess();
  const HostNativeProcessBase &GetNativeProcess() const;

private:
  std::shared_ptr<HostNativeProcessBase> m_native_process;
};
}

#endif
