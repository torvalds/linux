//===-- PlatformWindows.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_WINDOWS_PLATFORMWINDOWS_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_WINDOWS_PLATFORMWINDOWS_H

#include "lldb/Target/RemoteAwarePlatform.h"

namespace lldb_private {

class PlatformWindows : public RemoteAwarePlatform {
public:
  PlatformWindows(bool is_host);

  static void Initialize();

  static void Terminate();

  // lldb_private::PluginInterface functions
  static lldb::PlatformSP CreateInstance(bool force,
                                         const lldb_private::ArchSpec *arch);

  static llvm::StringRef GetPluginNameStatic(bool is_host) {
    return is_host ? Platform::GetHostPlatformName() : "remote-windows";
  }

  static llvm::StringRef GetPluginDescriptionStatic(bool is_host);

  llvm::StringRef GetPluginName() override {
    return GetPluginNameStatic(IsHost());
  }

  // lldb_private::Platform functions
  llvm::StringRef GetDescription() override {
    return GetPluginDescriptionStatic(IsHost());
  }

  lldb_private::Status ConnectRemote(lldb_private::Args &args) override;

  lldb_private::Status DisconnectRemote() override;

  uint32_t DoLoadImage(lldb_private::Process *process,
                       const lldb_private::FileSpec &remote_file,
                       const std::vector<std::string> *paths,
                       lldb_private::Status &error,
                       lldb_private::FileSpec *loaded_path) override;

  lldb_private::Status UnloadImage(lldb_private::Process *process,
                                   uint32_t image_token) override;

  lldb::ProcessSP DebugProcess(lldb_private::ProcessLaunchInfo &launch_info,
                               lldb_private::Debugger &debugger,
                               lldb_private::Target &target,
                               lldb_private::Status &error) override;

  lldb::ProcessSP Attach(lldb_private::ProcessAttachInfo &attach_info,
                         lldb_private::Debugger &debugger,
                         lldb_private::Target *target,
                         lldb_private::Status &error) override;

  std::vector<ArchSpec>
  GetSupportedArchitectures(const ArchSpec &process_host_arch) override {
    return m_supported_architectures;
  }

  void GetStatus(lldb_private::Stream &strm) override;

  bool CanDebugProcess() override;

  // FIXME not sure what the _sigtramp equivalent would be on this platform
  void CalculateTrapHandlerSymbolNames() override {}

  ConstString GetFullNameForDylib(ConstString basename) override;

  size_t GetSoftwareBreakpointTrapOpcode(Target &target,
                                         BreakpointSite *bp_site) override;

  std::vector<ArchSpec> m_supported_architectures;

private:
  std::unique_ptr<lldb_private::UtilityFunction>
  MakeLoadImageUtilityFunction(lldb_private::ExecutionContext &context,
                               lldb_private::Status &status);

  lldb_private::Status EvaluateLoaderExpression(lldb_private::Process *process,
                                                const char *expression,
                                                lldb::ValueObjectSP &value);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_WINDOWS_PLATFORMWINDOWS_H
