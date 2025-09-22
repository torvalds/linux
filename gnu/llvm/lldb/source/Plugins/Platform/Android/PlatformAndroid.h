//===-- PlatformAndroid.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_ANDROID_PLATFORMANDROID_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_ANDROID_PLATFORMANDROID_H

#include <memory>
#include <string>

#include "Plugins/Platform/Linux/PlatformLinux.h"

#include "AdbClient.h"

namespace lldb_private {
namespace platform_android {

class PlatformAndroid : public platform_linux::PlatformLinux {
public:
  PlatformAndroid(bool is_host);

  static void Initialize();

  static void Terminate();

  // lldb_private::PluginInterface functions
  static lldb::PlatformSP CreateInstance(bool force, const ArchSpec *arch);

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic(bool is_host) {
    return is_host ? Platform::GetHostPlatformName() : "remote-android";
  }

  static llvm::StringRef GetPluginDescriptionStatic(bool is_host);

  llvm::StringRef GetPluginName() override {
    return GetPluginNameStatic(IsHost());
  }

  // lldb_private::Platform functions

  Status ConnectRemote(Args &args) override;

  Status GetFile(const FileSpec &source, const FileSpec &destination) override;

  Status PutFile(const FileSpec &source, const FileSpec &destination,
                 uint32_t uid = UINT32_MAX, uint32_t gid = UINT32_MAX) override;

  uint32_t GetSdkVersion();

  bool GetRemoteOSVersion() override;

  Status DisconnectRemote() override;

  uint32_t GetDefaultMemoryCacheLineSize() override;

protected:
  const char *GetCacheHostname() override;

  Status DownloadModuleSlice(const FileSpec &src_file_spec,
                             const uint64_t src_offset, const uint64_t src_size,
                             const FileSpec &dst_file_spec) override;

  Status DownloadSymbolFile(const lldb::ModuleSP &module_sp,
                            const FileSpec &dst_file_spec) override;

  llvm::StringRef
  GetLibdlFunctionDeclarations(lldb_private::Process *process) override;

  typedef std::unique_ptr<AdbClient> AdbClientUP;
  virtual AdbClientUP GetAdbClient(Status &error);

  virtual llvm::StringRef GetPropertyPackageName();

  std::string GetRunAs();

private:
  AdbClient::SyncService *GetSyncService(Status &error);

  std::unique_ptr<AdbClient::SyncService> m_adb_sync_svc;
  std::string m_device_id;
  uint32_t m_sdk_version;
};

} // namespace platform_android
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_ANDROID_PLATFORMANDROID_H
