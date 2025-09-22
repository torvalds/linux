//===-- PlatformAppleSimulator.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformAppleSimulator.h"

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/Support/Threading.h"

#include <mutex>
#include <thread>

using namespace lldb;
using namespace lldb_private;

#if !defined(__APPLE__)
#define UNSUPPORTED_ERROR ("Apple simulators aren't supported on this platform")
#endif

/// Default Constructor
PlatformAppleSimulator::PlatformAppleSimulator(
    const char *class_name, const char *description, ConstString plugin_name,
    llvm::Triple::OSType preferred_os,
    llvm::SmallVector<llvm::StringRef, 4> supported_triples,
    std::string sdk_name_primary, std::string sdk_name_secondary,
    lldb_private::XcodeSDK::Type sdk_type,
    CoreSimulatorSupport::DeviceType::ProductFamilyID kind)
    : PlatformDarwin(true), m_class_name(class_name),
      m_description(description), m_plugin_name(plugin_name), m_kind(kind),
      m_os_type(preferred_os), m_supported_triples(supported_triples),
      m_sdk_name_primary(std::move(sdk_name_primary)),
      m_sdk_name_secondary(std::move(sdk_name_secondary)),
      m_sdk_type(sdk_type) {}

/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
PlatformAppleSimulator::~PlatformAppleSimulator() = default;

lldb_private::Status PlatformAppleSimulator::LaunchProcess(
    lldb_private::ProcessLaunchInfo &launch_info) {
#if defined(__APPLE__)
  LoadCoreSimulator();
  CoreSimulatorSupport::Device device(GetSimulatorDevice());

  if (device.GetState() != CoreSimulatorSupport::Device::State::Booted) {
    Status boot_err;
    device.Boot(boot_err);
    if (boot_err.Fail())
      return boot_err;
  }

  auto spawned = device.Spawn(launch_info);

  if (spawned) {
    launch_info.SetProcessID(spawned.GetPID());
    return Status();
  } else
    return spawned.GetError();
#else
  Status err;
  err.SetErrorString(UNSUPPORTED_ERROR);
  return err;
#endif
}

void PlatformAppleSimulator::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);
  llvm::StringRef sdk = GetSDKFilepath();
  if (!sdk.empty())
    strm << "  SDK Path: \"" << sdk << "\"\n";
  else
    strm << "  SDK Path: error: unable to locate SDK\n";

#if defined(__APPLE__)
  // This will get called by subclasses, so just output status on the current
  // simulator
  PlatformAppleSimulator::LoadCoreSimulator();

  std::string developer_dir = HostInfo::GetXcodeDeveloperDirectory().GetPath();
  CoreSimulatorSupport::DeviceSet devices =
      CoreSimulatorSupport::DeviceSet::GetAvailableDevices(
          developer_dir.c_str());
  const size_t num_devices = devices.GetNumDevices();
  if (num_devices) {
    strm.Printf("Available devices:\n");
    for (size_t i = 0; i < num_devices; ++i) {
      CoreSimulatorSupport::Device device = devices.GetDeviceAtIndex(i);
      strm << "   " << device.GetUDID() << ": " << device.GetName() << "\n";
    }

    if (m_device.has_value() && m_device->operator bool()) {
      strm << "Current device: " << m_device->GetUDID() << ": "
           << m_device->GetName();
      if (m_device->GetState() == CoreSimulatorSupport::Device::State::Booted) {
        strm << " state = booted";
      }
      strm << "\nType \"platform connect <ARG>\" where <ARG> is a device "
              "UDID or a device name to disconnect and connect to a "
              "different device.\n";

    } else {
      strm << "No current device is selected, \"platform connect <ARG>\" "
              "where <ARG> is a device UDID or a device name to connect to "
              "a specific device.\n";
    }

  } else {
    strm << "No devices are available.\n";
  }
#else
  strm << UNSUPPORTED_ERROR;
#endif
}

Status PlatformAppleSimulator::ConnectRemote(Args &args) {
#if defined(__APPLE__)
  Status error;
  if (args.GetArgumentCount() == 1) {
    if (m_device)
      DisconnectRemote();
    PlatformAppleSimulator::LoadCoreSimulator();
    const char *arg_cstr = args.GetArgumentAtIndex(0);
    if (arg_cstr) {
      std::string arg_str(arg_cstr);
      std::string developer_dir = HostInfo::GetXcodeDeveloperDirectory().GetPath();
      CoreSimulatorSupport::DeviceSet devices =
          CoreSimulatorSupport::DeviceSet::GetAvailableDevices(
              developer_dir.c_str());
      devices.ForEach(
          [this, &arg_str](const CoreSimulatorSupport::Device &device) -> bool {
            if (arg_str == device.GetUDID() || arg_str == device.GetName()) {
              m_device = device;
              return false; // Stop iterating
            } else {
              return true; // Keep iterating
            }
          });
      if (!m_device)
        error.SetErrorStringWithFormat(
            "no device with UDID or name '%s' was found", arg_cstr);
    }
  } else {
    error.SetErrorString("this command take a single UDID argument of the "
                         "device you want to connect to.");
  }
  return error;
#else
  Status err;
  err.SetErrorString(UNSUPPORTED_ERROR);
  return err;
#endif
}

Status PlatformAppleSimulator::DisconnectRemote() {
#if defined(__APPLE__)
  m_device.reset();
  return Status();
#else
  Status err;
  err.SetErrorString(UNSUPPORTED_ERROR);
  return err;
#endif
}

lldb::ProcessSP
PlatformAppleSimulator::DebugProcess(ProcessLaunchInfo &launch_info,
                                     Debugger &debugger, Target &target,
                                     Status &error) {
#if defined(__APPLE__)
  ProcessSP process_sp;
  // Make sure we stop at the entry point
  launch_info.GetFlags().Set(eLaunchFlagDebug);
  // We always launch the process we are going to debug in a separate process
  // group, since then we can handle ^C interrupts ourselves w/o having to
  // worry about the target getting them as well.
  launch_info.SetLaunchInSeparateProcessGroup(true);

  error = LaunchProcess(launch_info);
  if (error.Success()) {
    if (launch_info.GetProcessID() != LLDB_INVALID_PROCESS_ID) {
      ProcessAttachInfo attach_info(launch_info);
      process_sp = Attach(attach_info, debugger, &target, error);
      if (process_sp) {
        launch_info.SetHijackListener(attach_info.GetHijackListener());

        // Since we attached to the process, it will think it needs to detach
        // if the process object just goes away without an explicit call to
        // Process::Kill() or Process::Detach(), so let it know to kill the
        // process if this happens.
        process_sp->SetShouldDetach(false);

        // If we didn't have any file actions, the pseudo terminal might have
        // been used where the secondary side was given as the file to open for
        // stdin/out/err after we have already opened the primary so we can
        // read/write stdin/out/err.
        int pty_fd = launch_info.GetPTY().ReleasePrimaryFileDescriptor();
        if (pty_fd != PseudoTerminal::invalid_fd) {
          process_sp->SetSTDIOFileDescriptor(pty_fd);
        }
      }
    }
  }

  return process_sp;
#else
  return ProcessSP();
#endif
}

FileSpec PlatformAppleSimulator::GetCoreSimulatorPath() {
#if defined(__APPLE__)
  std::lock_guard<std::mutex> guard(m_core_sim_path_mutex);
  if (!m_core_simulator_framework_path.has_value()) {
    m_core_simulator_framework_path =
        FileSpec("/Library/Developer/PrivateFrameworks/CoreSimulator.framework/"
                 "CoreSimulator");
    FileSystem::Instance().Resolve(*m_core_simulator_framework_path);
  }
  return m_core_simulator_framework_path.value();
#else
  return FileSpec();
#endif
}

void PlatformAppleSimulator::LoadCoreSimulator() {
#if defined(__APPLE__)
  static llvm::once_flag g_load_core_sim_flag;
  llvm::call_once(g_load_core_sim_flag, [this] {
    const std::string core_sim_path(GetCoreSimulatorPath().GetPath());
    if (core_sim_path.size())
      dlopen(core_sim_path.c_str(), RTLD_LAZY);
  });
#endif
}

#if defined(__APPLE__)
CoreSimulatorSupport::Device PlatformAppleSimulator::GetSimulatorDevice() {
  if (!m_device.has_value()) {
    const CoreSimulatorSupport::DeviceType::ProductFamilyID dev_id = m_kind;
    std::string developer_dir =
        HostInfo::GetXcodeDeveloperDirectory().GetPath();
    m_device = CoreSimulatorSupport::DeviceSet::GetAvailableDevices(
                   developer_dir.c_str())
                   .GetFanciest(dev_id);
  }

  if (m_device.has_value())
    return m_device.value();
  else
    return CoreSimulatorSupport::Device();
}
#endif

std::vector<ArchSpec> PlatformAppleSimulator::GetSupportedArchitectures(
    const ArchSpec &process_host_arch) {
  std::vector<ArchSpec> result(m_supported_triples.size());
  llvm::transform(m_supported_triples, result.begin(),
                  [](llvm::StringRef triple) { return ArchSpec(triple); });
  return result;
}

static llvm::StringRef GetXcodeSDKDir(std::string preferred,
                                      std::string secondary) {
  llvm::StringRef sdk;
  auto get_sdk = [&](std::string sdk) -> llvm::StringRef {
    auto sdk_path_or_err =
        HostInfo::GetSDKRoot(HostInfo::SDKOptions{XcodeSDK(std::move(sdk))});
    if (!sdk_path_or_err) {
      Debugger::ReportError("Error while searching for Xcode SDK: " +
                            toString(sdk_path_or_err.takeError()));
      return {};
    }
    return *sdk_path_or_err;
  };

  sdk = get_sdk(preferred);
  if (sdk.empty())
    sdk = get_sdk(secondary);
  return sdk;
}

llvm::StringRef PlatformAppleSimulator::GetSDKFilepath() {
  if (!m_have_searched_for_sdk) {
    m_sdk = GetXcodeSDKDir(m_sdk_name_primary, m_sdk_name_secondary);
    m_have_searched_for_sdk = true;
  }
  return m_sdk;
}

PlatformSP PlatformAppleSimulator::CreateInstance(
    const char *class_name, const char *description, ConstString plugin_name,
    llvm::SmallVector<llvm::Triple::ArchType, 4> supported_arch,
    llvm::Triple::OSType preferred_os,
    llvm::SmallVector<llvm::Triple::OSType, 4> supported_os,
    llvm::SmallVector<llvm::StringRef, 4> supported_triples,
    std::string sdk_name_primary, std::string sdk_name_secondary,
    lldb_private::XcodeSDK::Type sdk_type,
    CoreSimulatorSupport::DeviceType::ProductFamilyID kind, bool force,
    const ArchSpec *arch) {
  Log *log = GetLog(LLDBLog::Platform);
  if (log) {
    const char *arch_name;
    if (arch && arch->GetArchitectureName())
      arch_name = arch->GetArchitectureName();
    else
      arch_name = "<null>";

    const char *triple_cstr =
        arch ? arch->GetTriple().getTriple().c_str() : "<null>";

    LLDB_LOGF(log, "%s::%s(force=%s, arch={%s,%s})", class_name, __FUNCTION__,
              force ? "true" : "false", arch_name, triple_cstr);
  }

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    if (llvm::is_contained(supported_arch, arch->GetMachine())) {
      const llvm::Triple &triple = arch->GetTriple();
      switch (triple.getVendor()) {
      case llvm::Triple::Apple:
        create = true;
        break;

#if defined(__APPLE__)
      // Only accept "unknown" for the vendor if the host is Apple and if
      // "unknown" wasn't specified (it was just returned because it was NOT
      // specified)
      case llvm::Triple::UnknownVendor:
        create = !arch->TripleVendorWasSpecified();
        break;
#endif
      default:
        break;
      }

      if (create) {
        if (llvm::is_contained(supported_os, triple.getOS()))
          create = true;
#if defined(__APPLE__)
        // Only accept "unknown" for the OS if the host is Apple and it
        // "unknown" wasn't specified (it was just returned because it was NOT
        // specified)
        else if (triple.getOS() == llvm::Triple::UnknownOS)
          create = !arch->TripleOSWasSpecified();
#endif
        else
          create = false;
      }
    }
  }
  if (create) {
    LLDB_LOGF(log, "%s::%s() creating platform", class_name, __FUNCTION__);

    return PlatformSP(new PlatformAppleSimulator(
        class_name, description, plugin_name, preferred_os, supported_triples,
        sdk_name_primary, sdk_name_secondary, sdk_type, kind));
  }

  LLDB_LOGF(log, "%s::%s() aborting creation of platform", class_name,
            __FUNCTION__);

  return PlatformSP();
}

Status PlatformAppleSimulator::GetSymbolFile(const FileSpec &platform_file,
                                             const UUID *uuid_ptr,
                                             FileSpec &local_file) {
  Status error;
  char platform_file_path[PATH_MAX];
  if (platform_file.GetPath(platform_file_path, sizeof(platform_file_path))) {
    char resolved_path[PATH_MAX];

    llvm::StringRef sdk = GetSDKFilepath();
    if (!sdk.empty()) {
      ::snprintf(resolved_path, sizeof(resolved_path), "%s/%s",
                 sdk.str().c_str(), platform_file_path);

      // First try in the SDK and see if the file is in there
      local_file.SetFile(resolved_path, FileSpec::Style::native);
      FileSystem::Instance().Resolve(local_file);
      if (FileSystem::Instance().Exists(local_file))
        return error;

      // Else fall back to the actual path itself
      local_file.SetFile(platform_file_path, FileSpec::Style::native);
      FileSystem::Instance().Resolve(local_file);
      if (FileSystem::Instance().Exists(local_file))
        return error;
    }
    error.SetErrorStringWithFormatv(
        "unable to locate a platform file for '{0}' in platform '{1}'",
        platform_file_path, GetPluginName());
  } else {
    error.SetErrorString("invalid platform file argument");
  }
  return error;
}

Status PlatformAppleSimulator::GetSharedModule(
    const ModuleSpec &module_spec, Process *process, ModuleSP &module_sp,
    const FileSpecList *module_search_paths_ptr,
    llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules, bool *did_create_ptr) {
  // For iOS/tvOS/watchOS, the SDK files are all cached locally on the
  // host system. So first we ask for the file in the cached SDK, then
  // we attempt to get a shared module for the right architecture with
  // the right UUID.
  Status error;
  ModuleSpec platform_module_spec(module_spec);
  const FileSpec &platform_file = module_spec.GetFileSpec();
  error = GetSymbolFile(platform_file, module_spec.GetUUIDPtr(),
                        platform_module_spec.GetFileSpec());
  if (error.Success()) {
    error = ResolveExecutable(platform_module_spec, module_sp,
                              module_search_paths_ptr);
  } else {
    const bool always_create = false;
    error = ModuleList::GetSharedModule(module_spec, module_sp,
                                        module_search_paths_ptr, old_modules,
                                        did_create_ptr, always_create);
  }
  if (module_sp)
    module_sp->SetPlatformFileSpec(platform_file);

  return error;
}

uint32_t PlatformAppleSimulator::FindProcesses(
    const ProcessInstanceInfoMatch &match_info,
    ProcessInstanceInfoList &process_infos) {
  ProcessInstanceInfoList all_osx_process_infos;
  // First we get all OSX processes
  const uint32_t n = Host::FindProcesses(match_info, all_osx_process_infos);

  // Now we filter them down to only the matching triples.
  for (uint32_t i = 0; i < n; ++i) {
    const ProcessInstanceInfo &proc_info = all_osx_process_infos[i];
    const llvm::Triple &triple = proc_info.GetArchitecture().GetTriple();
    if (triple.getOS() == m_os_type &&
        triple.getEnvironment() == llvm::Triple::Simulator) {
      process_infos.push_back(proc_info);
    }
  }
  return process_infos.size();
}

/// Whether to skip creating a simulator platform.
static bool shouldSkipSimulatorPlatform(bool force, const ArchSpec *arch) {
  // If the arch is known not to specify a simulator environment, skip creating
  // the simulator platform (we can create it later if there's a matching arch).
  // This avoids very slow xcrun queries for non-simulator archs (the slowness
  // is due to xcrun not caching negative queries.
  return !force && arch && arch->IsValid() &&
         !arch->TripleEnvironmentWasSpecified();
}

static const char *g_ios_plugin_name = "ios-simulator";
static const char *g_ios_description = "iPhone simulator platform plug-in.";

/// IPhone Simulator Plugin.
struct PlatformiOSSimulator {
  static void Initialize() {
    PluginManager::RegisterPlugin(g_ios_plugin_name, g_ios_description,
                                  PlatformiOSSimulator::CreateInstance);
  }

  static void Terminate() {
    PluginManager::UnregisterPlugin(PlatformiOSSimulator::CreateInstance);
  }

  static PlatformSP CreateInstance(bool force, const ArchSpec *arch) {
    if (shouldSkipSimulatorPlatform(force, arch))
      return nullptr;

    return PlatformAppleSimulator::CreateInstance(
        "PlatformiOSSimulator", g_ios_description,
        ConstString(g_ios_plugin_name),
        {llvm::Triple::aarch64, llvm::Triple::x86_64, llvm::Triple::x86},
        llvm::Triple::IOS,
        {// Deprecated, but still support Darwin for historical reasons.
         llvm::Triple::Darwin, llvm::Triple::MacOSX,
         // IOS is not used for simulator triples, but accept it just in
         // case.
         llvm::Triple::IOS},
        {
#ifdef __APPLE__
#if __arm64__
          "arm64e-apple-ios-simulator", "arm64-apple-ios-simulator",
              "x86_64-apple-ios-simulator", "x86_64h-apple-ios-simulator",
#else
          "x86_64h-apple-ios-simulator", "x86_64-apple-ios-simulator",
              "i386-apple-ios-simulator",
#endif
#endif
        },
        "iPhoneSimulator.Internal.sdk", "iPhoneSimulator.sdk",
        XcodeSDK::Type::iPhoneSimulator,
        CoreSimulatorSupport::DeviceType::ProductFamilyID::iPhone, force, arch);
  }
};

static const char *g_tvos_plugin_name = "tvos-simulator";
static const char *g_tvos_description = "tvOS simulator platform plug-in.";

/// Apple TV Simulator Plugin.
struct PlatformAppleTVSimulator {
  static void Initialize() {
    PluginManager::RegisterPlugin(g_tvos_plugin_name, g_tvos_description,
                                  PlatformAppleTVSimulator::CreateInstance);
  }

  static void Terminate() {
    PluginManager::UnregisterPlugin(PlatformAppleTVSimulator::CreateInstance);
  }

  static PlatformSP CreateInstance(bool force, const ArchSpec *arch) {
    if (shouldSkipSimulatorPlatform(force, arch))
      return nullptr;
    return PlatformAppleSimulator::CreateInstance(
        "PlatformAppleTVSimulator", g_tvos_description,
        ConstString(g_tvos_plugin_name),
        {llvm::Triple::aarch64, llvm::Triple::x86_64}, llvm::Triple::TvOS,
        {llvm::Triple::TvOS},
        {
#ifdef __APPLE__
#if __arm64__
          "arm64e-apple-tvos-simulator", "arm64-apple-tvos-simulator",
              "x86_64h-apple-tvos-simulator", "x86_64-apple-tvos-simulator",
#else
          "x86_64h-apple-tvos-simulator", "x86_64-apple-tvos-simulator",
#endif
#endif
        },
        "AppleTVSimulator.Internal.sdk", "AppleTVSimulator.sdk",
        XcodeSDK::Type::AppleTVSimulator,
        CoreSimulatorSupport::DeviceType::ProductFamilyID::appleTV, force,
        arch);
  }
};


static const char *g_watchos_plugin_name = "watchos-simulator";
static const char *g_watchos_description =
    "Apple Watch simulator platform plug-in.";

/// Apple Watch Simulator Plugin.
struct PlatformAppleWatchSimulator {
  static void Initialize() {
    PluginManager::RegisterPlugin(g_watchos_plugin_name, g_watchos_description,
                                  PlatformAppleWatchSimulator::CreateInstance);
  }

  static void Terminate() {
    PluginManager::UnregisterPlugin(
        PlatformAppleWatchSimulator::CreateInstance);
  }

  static PlatformSP CreateInstance(bool force, const ArchSpec *arch) {
    if (shouldSkipSimulatorPlatform(force, arch))
      return nullptr;
    return PlatformAppleSimulator::CreateInstance(
        "PlatformAppleWatchSimulator", g_watchos_description,
        ConstString(g_watchos_plugin_name),
        {llvm::Triple::aarch64, llvm::Triple::x86_64, llvm::Triple::x86},
        llvm::Triple::WatchOS, {llvm::Triple::WatchOS},
        {
#ifdef __APPLE__
#if __arm64__
          "arm64e-apple-watchos-simulator", "arm64-apple-watchos-simulator",
#else
          "x86_64-apple-watchos-simulator", "x86_64h-apple-watchos-simulator",
              "i386-apple-watchos-simulator",
#endif
#endif
        },
        "WatchSimulator.Internal.sdk", "WatchSimulator.sdk",
        XcodeSDK::Type::WatchSimulator,
        CoreSimulatorSupport::DeviceType::ProductFamilyID::appleWatch, force,
        arch);
  }
};

static const char *g_xros_plugin_name = "xros-simulator";
static const char *g_xros_description = "XROS simulator platform plug-in.";

/// XRSimulator Plugin.
struct PlatformXRSimulator {
  static void Initialize() {
    PluginManager::RegisterPlugin(g_xros_plugin_name, g_xros_description,
                                  PlatformXRSimulator::CreateInstance);
  }

  static void Terminate() {
    PluginManager::UnregisterPlugin(PlatformXRSimulator::CreateInstance);
  }

  static PlatformSP CreateInstance(bool force, const ArchSpec *arch) {
    return PlatformAppleSimulator::CreateInstance(
        "PlatformXRSimulator", g_xros_description,
        ConstString(g_xros_plugin_name),
        {llvm::Triple::aarch64, llvm::Triple::x86_64, llvm::Triple::x86},
        llvm::Triple::XROS, {llvm::Triple::XROS},
        {
#ifdef __APPLE__
#if __arm64__
          "arm64e-apple-xros-simulator", "arm64-apple-xros-simulator",
#else
          "x86_64-apple-xros-simulator", "x86_64h-apple-xros-simulator",
#endif
#endif
        },
        "XRSimulator.Internal.sdk", "XRSimulator.sdk",
        XcodeSDK::Type::XRSimulator,
        CoreSimulatorSupport::DeviceType::ProductFamilyID::appleXR, force,
        arch);
  }
};

static unsigned g_initialize_count = 0;

// Static Functions
void PlatformAppleSimulator::Initialize() {
  if (g_initialize_count++ == 0) {
    PlatformDarwin::Initialize();
    PlatformiOSSimulator::Initialize();
    PlatformAppleTVSimulator::Initialize();
    PlatformAppleWatchSimulator::Initialize();
    PlatformXRSimulator::Initialize();
  }
}

void PlatformAppleSimulator::Terminate() {
  if (g_initialize_count > 0)
    if (--g_initialize_count == 0) {
      PlatformXRSimulator::Terminate();
      PlatformAppleWatchSimulator::Terminate();
      PlatformAppleTVSimulator::Terminate();
      PlatformiOSSimulator::Terminate();
      PlatformDarwin::Terminate();
    }
}

