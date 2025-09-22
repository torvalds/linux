//===-- PlatformRemoteDarwinDevice.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEDARWINDEVICE_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEDARWINDEVICE_H

#include "PlatformDarwinDevice.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/XcodeSDK.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/VersionTuple.h"

#include <mutex>
#include <string>
#include <vector>

namespace lldb_private {
class FileSpecList;
class ModuleSpec;
class Process;
class Stream;
class Target;
class UUID;

class PlatformRemoteDarwinDevice : public PlatformDarwinDevice {
public:
  PlatformRemoteDarwinDevice();

  ~PlatformRemoteDarwinDevice() override;

  // Platform functions
  void GetStatus(Stream &strm) override;

  virtual Status GetSymbolFile(const FileSpec &platform_file,
                               const UUID *uuid_ptr, FileSpec &local_file);

  Status GetSharedModule(const ModuleSpec &module_spec, Process *process,
                         lldb::ModuleSP &module_sp,
                         const FileSpecList *module_search_paths_ptr,
                         llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                         bool *did_create_ptr) override;

  void
  AddClangModuleCompilationOptions(Target *target,
                                   std::vector<std::string> &options) override {
    return PlatformDarwin::AddClangModuleCompilationOptionsForSDKType(
        target, options, XcodeSDK::Type::iPhoneOS);
  }

protected:
  std::string m_build_update;
  uint32_t m_last_module_sdk_idx = UINT32_MAX;
  uint32_t m_connected_module_sdk_idx = UINT32_MAX;

  bool GetFileInSDK(const char *platform_file_path, uint32_t sdk_idx,
                    FileSpec &local_file);

  uint32_t GetConnectedSDKIndex();

  // Get index of SDK in SDKDirectoryInfoCollection by its pointer and return
  // UINT32_MAX if that SDK not found.
  uint32_t GetSDKIndexBySDKDirectoryInfo(const SDKDirectoryInfo *sdk_info);

private:
  PlatformRemoteDarwinDevice(const PlatformRemoteDarwinDevice &) = delete;
  const PlatformRemoteDarwinDevice &
  operator=(const PlatformRemoteDarwinDevice &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMREMOTEDARWINDEVICE_H
