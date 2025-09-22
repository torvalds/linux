//===-- PlatformRemoteAppleTV.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEAPPLETV_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEAPPLETV_H

#include "PlatformRemoteDarwinDevice.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"

#include <string>
#include <vector>

namespace lldb_private {
class ArchSpec;

class PlatformRemoteAppleTV : public PlatformRemoteDarwinDevice {
public:
  PlatformRemoteAppleTV();

  // Class Functions
  static lldb::PlatformSP CreateInstance(bool force,
                                         const lldb_private::ArchSpec *arch);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "remote-tvos"; }

  static llvm::StringRef GetDescriptionStatic();

  // lldb_private::PluginInterface functions
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // lldb_private::Platform functions

  llvm::StringRef GetDescription() override { return GetDescriptionStatic(); }

  std::vector<lldb_private::ArchSpec> GetSupportedArchitectures(
      const lldb_private::ArchSpec &process_host_arch) override;

protected:
  llvm::StringRef GetDeviceSupportDirectoryName() override;
  llvm::StringRef GetPlatformName() override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEAPPLETV_H
