//===-- MonitoringProcessLauncher.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_MONITORINGPROCESSLAUNCHER_H
#define LLDB_HOST_MONITORINGPROCESSLAUNCHER_H

#include <memory>
#include "lldb/Host/ProcessLauncher.h"

namespace lldb_private {

class MonitoringProcessLauncher : public ProcessLauncher {
public:
  explicit MonitoringProcessLauncher(
      std::unique_ptr<ProcessLauncher> delegate_launcher);

  /// Launch the process specified in launch_info. The monitoring callback in
  /// launch_info must be set, and it will be called when the process
  /// terminates.
  HostProcess LaunchProcess(const ProcessLaunchInfo &launch_info,
                            Status &error) override;

private:
  std::unique_ptr<ProcessLauncher> m_delegate_launcher;
};

} // namespace lldb_private

#endif // LLDB_HOST_MONITORINGPROCESSLAUNCHER_H
