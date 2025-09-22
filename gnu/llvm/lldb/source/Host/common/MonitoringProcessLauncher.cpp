//===-- MonitoringProcessLauncher.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/MonitoringProcessLauncher.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Support/FileSystem.h"

using namespace lldb;
using namespace lldb_private;

MonitoringProcessLauncher::MonitoringProcessLauncher(
    std::unique_ptr<ProcessLauncher> delegate_launcher)
    : m_delegate_launcher(std::move(delegate_launcher)) {}

HostProcess
MonitoringProcessLauncher::LaunchProcess(const ProcessLaunchInfo &launch_info,
                                         Status &error) {
  ProcessLaunchInfo resolved_info(launch_info);

  error.Clear();

  FileSystem &fs = FileSystem::Instance();
  FileSpec exe_spec(resolved_info.GetExecutableFile());

  if (!fs.Exists(exe_spec))
    FileSystem::Instance().Resolve(exe_spec);

  if (!fs.Exists(exe_spec))
    FileSystem::Instance().ResolveExecutableLocation(exe_spec);

  if (!fs.Exists(exe_spec)) {
    error.SetErrorStringWithFormatv("executable doesn't exist: '{0}'",
                                    exe_spec);
    return HostProcess();
  }

  resolved_info.SetExecutableFile(exe_spec, false);
  assert(!resolved_info.GetFlags().Test(eLaunchFlagLaunchInTTY));

  HostProcess process =
      m_delegate_launcher->LaunchProcess(resolved_info, error);

  if (process.GetProcessId() != LLDB_INVALID_PROCESS_ID) {
    Log *log = GetLog(LLDBLog::Process);

    assert(launch_info.GetMonitorProcessCallback());
    llvm::Expected<HostThread> maybe_thread =
        process.StartMonitoring(launch_info.GetMonitorProcessCallback());
    if (!maybe_thread)
      error.SetErrorStringWithFormatv("failed to launch host thread: {}",
                                      llvm::toString(maybe_thread.takeError()));
    if (log)
      log->PutCString("started monitoring child process.");
  } else {
    // Invalid process ID, something didn't go well
    if (error.Success())
      error.SetErrorString("process launch failed for unknown reasons");
  }
  return process;
}
