//===-- PlatformRemoteAppleXR.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEAPPLEXR_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEAPPLEXR_H

#include "PlatformRemoteDarwinDevice.h"

namespace lldb_private {
class PlatformRemoteAppleXR : public PlatformRemoteDarwinDevice {
public:
  PlatformRemoteAppleXR();

  static lldb::PlatformSP CreateInstance(bool force,
                                         const lldb_private::ArchSpec *arch);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic();

  static llvm::StringRef GetDescriptionStatic();

  llvm::StringRef GetDescription() override { return GetDescriptionStatic(); }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  std::vector<lldb_private::ArchSpec> GetSupportedArchitectures(
      const lldb_private::ArchSpec &process_host_arch) override;

protected:
  llvm::StringRef GetDeviceSupportDirectoryName() override;
  llvm::StringRef GetPlatformName() override;
};
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEAPPLEXR_H
