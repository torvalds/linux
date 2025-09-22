//===-- PlatformDarwinDevice.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWINDEVICE_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWINDEVICE_H

#include "PlatformDarwin.h"

#include "llvm/ADT/StringRef.h"

#include <string>

namespace lldb_private {

/// Abstract Darwin platform with a potential device support directory.
class PlatformDarwinDevice : public PlatformDarwin {
public:
  using PlatformDarwin::PlatformDarwin;
  ~PlatformDarwinDevice() override;

protected:
  virtual Status GetSharedModuleWithLocalCache(
      const ModuleSpec &module_spec, lldb::ModuleSP &module_sp,
      const FileSpecList *module_search_paths_ptr,
      llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules, bool *did_create_ptr);

  struct SDKDirectoryInfo {
    SDKDirectoryInfo(const FileSpec &sdk_dir_spec);
    FileSpec directory;
    ConstString build;
    llvm::VersionTuple version;
    bool user_cached;
  };

  typedef std::vector<SDKDirectoryInfo> SDKDirectoryInfoCollection;

  bool UpdateSDKDirectoryInfosIfNeeded();

  const SDKDirectoryInfo *GetSDKDirectoryForLatestOSVersion();
  const SDKDirectoryInfo *GetSDKDirectoryForCurrentOSVersion();

  static FileSystem::EnumerateDirectoryResult
  GetContainedFilesIntoVectorOfStringsCallback(void *baton,
                                               llvm::sys::fs::file_type ft,
                                               llvm::StringRef path);

  const char *GetDeviceSupportDirectory();
  const char *GetDeviceSupportDirectoryForOSVersion();

  virtual llvm::StringRef GetPlatformName() = 0;
  virtual llvm::StringRef GetDeviceSupportDirectoryName() = 0;

  std::mutex m_sdk_dir_mutex;
  SDKDirectoryInfoCollection m_sdk_directory_infos;

private:
  std::string m_device_support_directory;
  std::string m_device_support_directory_for_os_version;
};
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWINDEVICE_H
