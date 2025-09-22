//===-- HostInfoMacOSX.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_MACOSX_HOSTINFOMACOSX_H
#define LLDB_HOST_MACOSX_HOSTINFOMACOSX_H

#include "lldb/Host/posix/HostInfoPosix.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/XcodeSDK.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"
#include <optional>

namespace lldb_private {

class ArchSpec;

class HostInfoMacOSX : public HostInfoPosix {
  friend class HostInfoBase;

public:
  static llvm::VersionTuple GetOSVersion();
  static llvm::VersionTuple GetMacCatalystVersion();
  static std::optional<std::string> GetOSBuildString();
  static FileSpec GetProgramFileSpec();
  static FileSpec GetXcodeContentsDirectory();
  static FileSpec GetXcodeDeveloperDirectory();

  /// Query xcrun to find an Xcode SDK directory.
  static llvm::Expected<llvm::StringRef> GetSDKRoot(SDKOptions options);
  static llvm::Expected<llvm::StringRef> FindSDKTool(XcodeSDK sdk,
                                                     llvm::StringRef tool);

  /// Shared cache utilities
  static SharedCacheImageInfo
  GetSharedCacheImageInfo(llvm::StringRef image_name);

protected:
  static bool ComputeSupportExeDirectory(FileSpec &file_spec);
  static void ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                             ArchSpec &arch_64);
  static bool ComputeHeaderDirectory(FileSpec &file_spec);
  static bool ComputeSystemPluginsDirectory(FileSpec &file_spec);
  static bool ComputeUserPluginsDirectory(FileSpec &file_spec);
};
}

#endif
