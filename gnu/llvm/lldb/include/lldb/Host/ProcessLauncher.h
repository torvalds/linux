//===-- ProcessLauncher.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_PROCESSLAUNCHER_H
#define LLDB_HOST_PROCESSLAUNCHER_H

namespace lldb_private {

class ProcessLaunchInfo;
class Status;
class HostProcess;

class ProcessLauncher {
public:
  virtual ~ProcessLauncher() = default;
  virtual HostProcess LaunchProcess(const ProcessLaunchInfo &launch_info,
                                    Status &error) = 0;
};
}

#endif
