//===-- PlatformNetBSD.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_NETBSD_PLATFORMNETBSD_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_NETBSD_PLATFORMNETBSD_H

#include "Plugins/Platform/POSIX/PlatformPOSIX.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

namespace lldb_private {
namespace platform_netbsd {

class PlatformNetBSD : public PlatformPOSIX {
public:
  PlatformNetBSD(bool is_host);

  static void Initialize();

  static void Terminate();

  // lldb_private::PluginInterface functions
  static lldb::PlatformSP CreateInstance(bool force, const ArchSpec *arch);

  static llvm::StringRef GetPluginNameStatic(bool is_host) {
    return is_host ? Platform::GetHostPlatformName() : "remote-netbsd";
  }

  static llvm::StringRef GetPluginDescriptionStatic(bool is_host);

  llvm::StringRef GetPluginName() override {
    return GetPluginNameStatic(IsHost());
  }

  // lldb_private::Platform functions
  llvm::StringRef GetDescription() override {
    return GetPluginDescriptionStatic(IsHost());
  }

  void GetStatus(Stream &strm) override;

  std::vector<ArchSpec>
  GetSupportedArchitectures(const ArchSpec &process_host_arch) override;

  uint32_t GetResumeCountForLaunchInfo(ProcessLaunchInfo &launch_info) override;

  bool CanDebugProcess() override;

  void CalculateTrapHandlerSymbolNames() override;

  MmapArgList GetMmapArgumentList(const ArchSpec &arch, lldb::addr_t addr,
                                  lldb::addr_t length, unsigned prot,
                                  unsigned flags, lldb::addr_t fd,
                                  lldb::addr_t offset) override;

  CompilerType GetSiginfoType(const llvm::Triple &triple) override;

  std::vector<ArchSpec> m_supported_architectures;

private:
  std::mutex m_mutex;
  std::shared_ptr<TypeSystemClang> m_type_system;
};

} // namespace platform_netbsd
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_NETBSD_PLATFORMNETBSD_H
