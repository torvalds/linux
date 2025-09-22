//===-- PlatformMacOSX.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformMacOSX.h"
#include "PlatformRemoteMacOSX.h"
#include "PlatformRemoteiOS.h"
#if defined(__APPLE__)
#include "PlatformAppleSimulator.h"
#include "PlatformDarwinKernel.h"
#include "PlatformRemoteAppleBridge.h"
#include "PlatformRemoteAppleTV.h"
#include "PlatformRemoteAppleWatch.h"
#include "PlatformRemoteAppleXR.h"
#endif
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include <sstream>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(PlatformMacOSX)

static uint32_t g_initialize_count = 0;

void PlatformMacOSX::Initialize() {
  PlatformDarwin::Initialize();
  PlatformRemoteiOS::Initialize();
  PlatformRemoteMacOSX::Initialize();
#if defined(__APPLE__)
  PlatformAppleSimulator::Initialize();
  PlatformDarwinKernel::Initialize();
  PlatformRemoteAppleTV::Initialize();
  PlatformRemoteAppleWatch::Initialize();
  PlatformRemoteAppleBridge::Initialize();
  PlatformRemoteAppleXR::Initialize();
#endif

  if (g_initialize_count++ == 0) {
#if defined(__APPLE__)
    PlatformSP default_platform_sp(new PlatformMacOSX());
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(PlatformMacOSX::GetPluginNameStatic(),
                                  PlatformMacOSX::GetDescriptionStatic(),
                                  PlatformMacOSX::CreateInstance);
  }
}

void PlatformMacOSX::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformMacOSX::CreateInstance);
    }
  }

#if defined(__APPLE__)
  PlatformRemoteAppleXR::Terminate();
  PlatformRemoteAppleBridge::Terminate();
  PlatformRemoteAppleWatch::Terminate();
  PlatformRemoteAppleTV::Terminate();
  PlatformDarwinKernel::Terminate();
  PlatformAppleSimulator::Terminate();
#endif
  PlatformRemoteMacOSX::Initialize();
  PlatformRemoteiOS::Terminate();
  PlatformDarwin::Terminate();
}

llvm::StringRef PlatformMacOSX::GetDescriptionStatic() {
  return "Local Mac OS X user platform plug-in.";
}

PlatformSP PlatformMacOSX::CreateInstance(bool force, const ArchSpec *arch) {
  // The only time we create an instance is when we are creating a remote
  // macosx platform which is handled by PlatformRemoteMacOSX.
  return PlatformSP();
}

/// Default Constructor
PlatformMacOSX::PlatformMacOSX() : PlatformDarwinDevice(true) {}

ConstString PlatformMacOSX::GetSDKDirectory(lldb_private::Target &target) {
  ModuleSP exe_module_sp(target.GetExecutableModule());
  if (!exe_module_sp)
    return {};

  ObjectFile *objfile = exe_module_sp->GetObjectFile();
  if (!objfile)
    return {};

  llvm::VersionTuple version = objfile->GetSDKVersion();
  if (version.empty())
    return {};

  // First try to find an SDK that matches the given SDK version.
  if (FileSpec fspec = HostInfo::GetXcodeContentsDirectory()) {
    StreamString sdk_path;
    sdk_path.Printf("%s/Developer/Platforms/MacOSX.platform/Developer/"
                    "SDKs/MacOSX%u.%u.sdk",
                    fspec.GetPath().c_str(), version.getMajor(),
                    *version.getMinor());
    if (FileSystem::Instance().Exists(fspec))
      return ConstString(sdk_path.GetString());
  }

  // Use the default SDK as a fallback.
  auto sdk_path_or_err =
      HostInfo::GetSDKRoot(HostInfo::SDKOptions{XcodeSDK::GetAnyMacOS()});
  if (!sdk_path_or_err) {
    Debugger::ReportError("Error while searching for Xcode SDK: " +
                          toString(sdk_path_or_err.takeError()));
    return {};
  }

  FileSpec fspec(*sdk_path_or_err);
  if (fspec) {
    if (FileSystem::Instance().Exists(fspec))
      return ConstString(fspec.GetPath());
  }

  return {};
}

std::vector<ArchSpec>
PlatformMacOSX::GetSupportedArchitectures(const ArchSpec &process_host_arch) {
  std::vector<ArchSpec> result;
#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  // When cmdline lldb is run on iOS, watchOS, etc, it is still
  // using "PlatformMacOSX".
  llvm::Triple::OSType host_os = GetHostOSType();
  ARMGetSupportedArchitectures(result, host_os);

  if (host_os == llvm::Triple::MacOSX) {
    // We can't use x86GetSupportedArchitectures() because it uses
    // the system architecture for some of its return values and also
    // has a 32bits variant.
    result.push_back(ArchSpec("x86_64-apple-macosx"));
    result.push_back(ArchSpec("x86_64-apple-ios-macabi"));
    result.push_back(ArchSpec("arm64-apple-ios-macabi"));
    result.push_back(ArchSpec("arm64e-apple-ios-macabi"));

    // On Apple Silicon, the host platform is compatible with iOS triples to
    // support unmodified "iPhone and iPad Apps on Apple Silicon Macs". Because
    // the binaries are identical, we must rely on the host architecture to
    // tell them apart and mark the host platform as compatible or not.
    if (!process_host_arch ||
        process_host_arch.GetTriple().getOS() == llvm::Triple::MacOSX) {
      result.push_back(ArchSpec("arm64-apple-ios"));
      result.push_back(ArchSpec("arm64e-apple-ios"));
    }
  }
#else
  x86GetSupportedArchitectures(result);
  result.push_back(ArchSpec("x86_64-apple-ios-macabi"));
#endif
  return result;
}

lldb_private::Status PlatformMacOSX::GetSharedModule(
    const lldb_private::ModuleSpec &module_spec, Process *process,
    lldb::ModuleSP &module_sp,
    const lldb_private::FileSpecList *module_search_paths_ptr,
    llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules, bool *did_create_ptr) {
  Status error = GetSharedModuleWithLocalCache(module_spec, module_sp,
                                               module_search_paths_ptr,
                                               old_modules, did_create_ptr);

  if (module_sp) {
    if (module_spec.GetArchitecture().GetCore() ==
        ArchSpec::eCore_x86_64_x86_64h) {
      ObjectFile *objfile = module_sp->GetObjectFile();
      if (objfile == nullptr) {
        // We didn't find an x86_64h slice, fall back to a x86_64 slice
        ModuleSpec module_spec_x86_64(module_spec);
        module_spec_x86_64.GetArchitecture() = ArchSpec("x86_64-apple-macosx");
        lldb::ModuleSP x86_64_module_sp;
        llvm::SmallVector<lldb::ModuleSP, 1> old_x86_64_modules;
        bool did_create = false;
        Status x86_64_error = GetSharedModuleWithLocalCache(
            module_spec_x86_64, x86_64_module_sp, module_search_paths_ptr,
            &old_x86_64_modules, &did_create);
        if (x86_64_module_sp && x86_64_module_sp->GetObjectFile()) {
          module_sp = x86_64_module_sp;
          if (old_modules)
            old_modules->append(old_x86_64_modules.begin(),
                                old_x86_64_modules.end());
          if (did_create_ptr)
            *did_create_ptr = did_create;
          return x86_64_error;
        }
      }
    }
  }

  if (!module_sp) {
    error = FindBundleBinaryInExecSearchPaths(module_spec, process, module_sp,
                                              module_search_paths_ptr,
                                              old_modules, did_create_ptr);
  }
  return error;
}

llvm::StringRef PlatformMacOSX::GetDeviceSupportDirectoryName() {
  return "macOS DeviceSupport";
}

llvm::StringRef PlatformMacOSX::GetPlatformName() { return "MacOSX.platform"; }
