//===-- HostInfoLinux.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_linux_HostInfoLinux_h_
#define lldb_Host_linux_HostInfoLinux_h_

#include "lldb/Host/posix/HostInfoPosix.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"

#include <optional>
#include <string>

namespace lldb_private {

class HostInfoLinux : public HostInfoPosix {
  friend class HostInfoBase;

public:
  static void Initialize(SharedLibraryDirectoryHelper *helper = nullptr);
  static void Terminate();

  static llvm::VersionTuple GetOSVersion();
  static std::optional<std::string> GetOSBuildString();
  static llvm::StringRef GetDistributionId();
  static FileSpec GetProgramFileSpec();

protected:
  static bool ComputeSupportExeDirectory(FileSpec &file_spec);
  static bool ComputeSystemPluginsDirectory(FileSpec &file_spec);
  static bool ComputeUserPluginsDirectory(FileSpec &file_spec);
  static void ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                             ArchSpec &arch_64);
};
}

#endif
