//===-- PlatformRemoteDarwinDevice.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformRemoteDarwinDevice.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

PlatformRemoteDarwinDevice::SDKDirectoryInfo::SDKDirectoryInfo(
    const lldb_private::FileSpec &sdk_dir)
    : directory(sdk_dir), build(), user_cached(false) {
  llvm::StringRef dirname_str = sdk_dir.GetFilename().GetStringRef();
  llvm::StringRef build_str;
  std::tie(version, build_str) = ParseVersionBuildDir(dirname_str);
  build.SetString(build_str);
}

/// Default Constructor
PlatformRemoteDarwinDevice::PlatformRemoteDarwinDevice()
    : PlatformDarwinDevice(false) {} // This is a remote platform

/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
PlatformRemoteDarwinDevice::~PlatformRemoteDarwinDevice() = default;

void PlatformRemoteDarwinDevice::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);
  const char *sdk_directory = GetDeviceSupportDirectoryForOSVersion();
  if (sdk_directory)
    strm.Printf("  SDK Path: \"%s\"\n", sdk_directory);
  else
    strm.PutCString("  SDK Path: error: unable to locate SDK\n");

  const uint32_t num_sdk_infos = m_sdk_directory_infos.size();
  for (uint32_t i = 0; i < num_sdk_infos; ++i) {
    const SDKDirectoryInfo &sdk_dir_info = m_sdk_directory_infos[i];
    strm.Printf(" SDK Roots: [%2u] \"%s\"\n", i,
                sdk_dir_info.directory.GetPath().c_str());
  }
}

bool PlatformRemoteDarwinDevice::GetFileInSDK(const char *platform_file_path,
                                     uint32_t sdk_idx,
                                     lldb_private::FileSpec &local_file) {
  Log *log = GetLog(LLDBLog::Host);
  if (sdk_idx < m_sdk_directory_infos.size()) {
    std::string sdkroot_path =
        m_sdk_directory_infos[sdk_idx].directory.GetPath();
    local_file.Clear();

    if (!sdkroot_path.empty() && platform_file_path && platform_file_path[0]) {
      // We may need to interpose "/Symbols/" or "/Symbols.Internal/" between
      // the
      // SDK root directory and the file path.

      const char *paths_to_try[] = {"Symbols", "", "Symbols.Internal", nullptr};
      for (size_t i = 0; paths_to_try[i] != nullptr; i++) {
        local_file.SetFile(sdkroot_path, FileSpec::Style::native);
        if (paths_to_try[i][0] != '\0')
          local_file.AppendPathComponent(paths_to_try[i]);
        local_file.AppendPathComponent(platform_file_path);
        FileSystem::Instance().Resolve(local_file);
        if (FileSystem::Instance().Exists(local_file)) {
          LLDB_LOGF(log, "Found a copy of %s in the SDK dir %s/%s",
                    platform_file_path, sdkroot_path.c_str(), paths_to_try[i]);
          return true;
        }
        local_file.Clear();
      }
    }
  }
  return false;
}

Status PlatformRemoteDarwinDevice::GetSymbolFile(const FileSpec &platform_file,
                                                 const UUID *uuid_ptr,
                                                 FileSpec &local_file) {
  Log *log = GetLog(LLDBLog::Host);
  Status error;
  char platform_file_path[PATH_MAX];
  if (platform_file.GetPath(platform_file_path, sizeof(platform_file_path))) {
    const char *os_version_dir = GetDeviceSupportDirectoryForOSVersion();
    if (os_version_dir) {
      std::string resolved_path =
          (llvm::Twine(os_version_dir) + "/" + platform_file_path).str();

      local_file.SetFile(resolved_path, FileSpec::Style::native);
      FileSystem::Instance().Resolve(local_file);
      if (FileSystem::Instance().Exists(local_file)) {
        if (log) {
          LLDB_LOGF(log, "Found a copy of %s in the DeviceSupport dir %s",
                    platform_file_path, os_version_dir);
        }
        return error;
      }

      resolved_path = (llvm::Twine(os_version_dir) + "/Symbols.Internal/" +
                       platform_file_path)
                          .str();

      local_file.SetFile(resolved_path, FileSpec::Style::native);
      FileSystem::Instance().Resolve(local_file);
      if (FileSystem::Instance().Exists(local_file)) {
        LLDB_LOGF(
            log,
            "Found a copy of %s in the DeviceSupport dir %s/Symbols.Internal",
            platform_file_path, os_version_dir);
        return error;
      }
      resolved_path =
          (llvm::Twine(os_version_dir) + "/Symbols/" + platform_file_path)
              .str();

      local_file.SetFile(resolved_path, FileSpec::Style::native);
      FileSystem::Instance().Resolve(local_file);
      if (FileSystem::Instance().Exists(local_file)) {
        LLDB_LOGF(log, "Found a copy of %s in the DeviceSupport dir %s/Symbols",
                  platform_file_path, os_version_dir);
        return error;
      }
    }
    local_file = platform_file;
    if (FileSystem::Instance().Exists(local_file))
      return error;

    error.SetErrorStringWithFormatv(
        "unable to locate a platform file for '{0}' in platform '{1}'",
        platform_file_path, GetPluginName());
  } else {
    error.SetErrorString("invalid platform file argument");
  }
  return error;
}

Status PlatformRemoteDarwinDevice::GetSharedModule(
    const ModuleSpec &module_spec, Process *process, ModuleSP &module_sp,
    const FileSpecList *module_search_paths_ptr,
    llvm::SmallVectorImpl<ModuleSP> *old_modules, bool *did_create_ptr) {
  // For iOS, the SDK files are all cached locally on the host system. So first
  // we ask for the file in the cached SDK, then we attempt to get a shared
  // module for the right architecture with the right UUID.
  const FileSpec &platform_file = module_spec.GetFileSpec();
  Log *log = GetLog(LLDBLog::Host);

  Status error;
  char platform_file_path[PATH_MAX];

  if (platform_file.GetPath(platform_file_path, sizeof(platform_file_path))) {
    ModuleSpec platform_module_spec(module_spec);

    UpdateSDKDirectoryInfosIfNeeded();

    const uint32_t num_sdk_infos = m_sdk_directory_infos.size();

    // If we are connected we migth be able to correctly deduce the SDK
    // directory using the OS build.
    const uint32_t connected_sdk_idx = GetConnectedSDKIndex();
    if (connected_sdk_idx < num_sdk_infos) {
      LLDB_LOGV(log, "Searching for {0} in sdk path {1}", platform_file,
                m_sdk_directory_infos[connected_sdk_idx].directory);
      if (GetFileInSDK(platform_file_path, connected_sdk_idx,
                       platform_module_spec.GetFileSpec())) {
        module_sp.reset();
        error = ResolveExecutable(platform_module_spec, module_sp, nullptr);
        if (module_sp) {
          m_last_module_sdk_idx = connected_sdk_idx;
          error.Clear();
          return error;
        }
      }
    }

    // Try the last SDK index if it is set as most files from an SDK will tend
    // to be valid in that same SDK.
    if (m_last_module_sdk_idx < num_sdk_infos) {
      LLDB_LOGV(log, "Searching for {0} in sdk path {1}", platform_file,
                m_sdk_directory_infos[m_last_module_sdk_idx].directory);
      if (GetFileInSDK(platform_file_path, m_last_module_sdk_idx,
                       platform_module_spec.GetFileSpec())) {
        module_sp.reset();
        error = ResolveExecutable(platform_module_spec, module_sp, nullptr);
        if (module_sp) {
          error.Clear();
          return error;
        }
      }
    }

    // First try for an exact match of major, minor and update: If a particalar
    // SDK version was specified via --version or --build, look for a match on
    // disk.
    const SDKDirectoryInfo *current_sdk_info =
        GetSDKDirectoryForCurrentOSVersion();
    const uint32_t current_sdk_idx =
        GetSDKIndexBySDKDirectoryInfo(current_sdk_info);
    if (current_sdk_idx < num_sdk_infos &&
        current_sdk_idx != m_last_module_sdk_idx) {
      LLDB_LOGV(log, "Searching for {0} in sdk path {1}", platform_file,
                m_sdk_directory_infos[current_sdk_idx].directory);
      if (GetFileInSDK(platform_file_path, current_sdk_idx,
                       platform_module_spec.GetFileSpec())) {
        module_sp.reset();
        error = ResolveExecutable(platform_module_spec, module_sp, nullptr);
        if (module_sp) {
          m_last_module_sdk_idx = current_sdk_idx;
          error.Clear();
          return error;
        }
      }
    }

    // Second try all SDKs that were found.
    for (uint32_t sdk_idx = 0; sdk_idx < num_sdk_infos; ++sdk_idx) {
      if (m_last_module_sdk_idx == sdk_idx) {
        // Skip the last module SDK index if we already searched it above
        continue;
      }
      LLDB_LOGV(log, "Searching for {0} in sdk path {1}", platform_file,
                m_sdk_directory_infos[sdk_idx].directory);
      if (GetFileInSDK(platform_file_path, sdk_idx,
                       platform_module_spec.GetFileSpec())) {
        // printf ("sdk[%u]: '%s'\n", sdk_idx, local_file.GetPath().c_str());

        error = ResolveExecutable(platform_module_spec, module_sp, nullptr);
        if (module_sp) {
          // Remember the index of the last SDK that we found a file in in case
          // the wrong SDK was selected.
          m_last_module_sdk_idx = sdk_idx;
          error.Clear();
          return error;
        }
      }
    }
  }
  // Not the module we are looking for... Nothing to see here...
  module_sp.reset();

  // This may not be an SDK-related module.  Try whether we can bring in the
  // thing to our local cache.
  error = GetSharedModuleWithLocalCache(module_spec, module_sp,
                                        module_search_paths_ptr, old_modules,
                                        did_create_ptr);
  if (error.Success())
    return error;

  // See if the file is present in any of the module_search_paths_ptr
  // directories.
  if (!module_sp)
    error = PlatformDarwin::FindBundleBinaryInExecSearchPaths(
        module_spec, process, module_sp, module_search_paths_ptr, old_modules,
        did_create_ptr);

  if (error.Success())
    return error;

  const bool always_create = false;
  error = ModuleList::GetSharedModule(module_spec, module_sp,
                                      module_search_paths_ptr, old_modules,
                                      did_create_ptr, always_create);

  if (module_sp)
    module_sp->SetPlatformFileSpec(platform_file);

  return error;
}

uint32_t PlatformRemoteDarwinDevice::GetConnectedSDKIndex() {
  if (IsConnected()) {
    if (m_connected_module_sdk_idx == UINT32_MAX) {
      if (std::optional<std::string> build = GetRemoteOSBuildString()) {
        const uint32_t num_sdk_infos = m_sdk_directory_infos.size();
        for (uint32_t i = 0; i < num_sdk_infos; ++i) {
          const SDKDirectoryInfo &sdk_dir_info = m_sdk_directory_infos[i];
          if (strstr(sdk_dir_info.directory.GetFilename().AsCString(""),
                     build->c_str())) {
            m_connected_module_sdk_idx = i;
          }
        }
      }
    }
  } else {
    m_connected_module_sdk_idx = UINT32_MAX;
  }
  return m_connected_module_sdk_idx;
}

uint32_t PlatformRemoteDarwinDevice::GetSDKIndexBySDKDirectoryInfo(
    const SDKDirectoryInfo *sdk_info) {
  if (sdk_info == nullptr) {
    return UINT32_MAX;
  }

  return sdk_info - &m_sdk_directory_infos[0];
}
