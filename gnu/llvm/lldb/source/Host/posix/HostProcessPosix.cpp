//===-- HostProcessPosix.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Host.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/posix/HostProcessPosix.h"

#include "llvm/ADT/STLExtras.h"

#include <climits>
#include <csignal>
#include <unistd.h>

using namespace lldb_private;

static const int kInvalidPosixProcess = 0;

HostProcessPosix::HostProcessPosix()
    : HostNativeProcessBase(kInvalidPosixProcess) {}

HostProcessPosix::HostProcessPosix(lldb::process_t process)
    : HostNativeProcessBase(process) {}

HostProcessPosix::~HostProcessPosix() = default;

Status HostProcessPosix::Signal(int signo) const {
  if (m_process == kInvalidPosixProcess) {
    Status error;
    error.SetErrorString("HostProcessPosix refers to an invalid process");
    return error;
  }

  return HostProcessPosix::Signal(m_process, signo);
}

Status HostProcessPosix::Signal(lldb::process_t process, int signo) {
  Status error;

  if (-1 == ::kill(process, signo))
    error.SetErrorToErrno();

  return error;
}

Status HostProcessPosix::Terminate() { return Signal(SIGKILL); }

lldb::pid_t HostProcessPosix::GetProcessId() const { return m_process; }

bool HostProcessPosix::IsRunning() const {
  if (m_process == kInvalidPosixProcess)
    return false;

  // Send this process the null signal.  If it succeeds the process is running.
  Status error = Signal(0);
  return error.Success();
}

llvm::Expected<HostThread> HostProcessPosix::StartMonitoring(
    const Host::MonitorChildProcessCallback &callback) {
  return Host::StartMonitoringChildProcess(callback, m_process);
}
