//===-- HostInfoNetBSD.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_netbsd_HostInfoNetBSD_h_
#define lldb_Host_netbsd_HostInfoNetBSD_h_

#include "lldb/Host/posix/HostInfoPosix.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/VersionTuple.h"
#include <optional>

namespace lldb_private {

class HostInfoNetBSD : public HostInfoPosix {
public:
  static llvm::VersionTuple GetOSVersion();
  static std::optional<std::string> GetOSBuildString();
  static FileSpec GetProgramFileSpec();
};
}

#endif
