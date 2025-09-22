//===-- PlatformMacOSX.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMMACOSX_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMMACOSX_H

#include "PlatformDarwinDevice.h"
#include "lldb/Target/Platform.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/XcodeSDK.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <vector>

namespace lldb_private {
class ArchSpec;
class FileSpec;
class FileSpecList;
class ModuleSpec;
class Process;
class Target;

class PlatformMacOSX : public PlatformDarwinDevice {
public:
  PlatformMacOSX();

  static lldb::PlatformSP CreateInstance(bool force, const ArchSpec *arch);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() {
    return Platform::GetHostPlatformName();
  }

  static llvm::StringRef GetDescriptionStatic();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  Status GetSharedModule(const ModuleSpec &module_spec, Process *process,
                         lldb::ModuleSP &module_sp,
                         const FileSpecList *module_search_paths_ptr,
                         llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                         bool *did_create_ptr) override;

  llvm::StringRef GetDescription() override { return GetDescriptionStatic(); }

  Status GetFile(const FileSpec &source, const FileSpec &destination) override {
    return PlatformDarwin::GetFile(source, destination);
  }

  std::vector<ArchSpec>
  GetSupportedArchitectures(const ArchSpec &process_host_arch) override;

  ConstString GetSDKDirectory(Target &target) override;

  void
  AddClangModuleCompilationOptions(Target *target,
                                   std::vector<std::string> &options) override {
    return PlatformDarwin::AddClangModuleCompilationOptionsForSDKType(
        target, options, XcodeSDK::Type::MacOSX);
  }

protected:
  llvm::StringRef GetDeviceSupportDirectoryName() override;
  llvm::StringRef GetPlatformName() override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMMACOSX_H
