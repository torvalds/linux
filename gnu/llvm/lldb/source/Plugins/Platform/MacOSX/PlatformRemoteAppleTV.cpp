//===-- PlatformRemoteAppleTV.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>
#include <vector>

#include "PlatformRemoteAppleTV.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

/// Default Constructor
PlatformRemoteAppleTV::PlatformRemoteAppleTV()
    : PlatformRemoteDarwinDevice () {}

// Static Variables
static uint32_t g_initialize_count = 0;

// Static Functions
void PlatformRemoteAppleTV::Initialize() {
  PlatformDarwin::Initialize();

  if (g_initialize_count++ == 0) {
    PluginManager::RegisterPlugin(PlatformRemoteAppleTV::GetPluginNameStatic(),
                                  PlatformRemoteAppleTV::GetDescriptionStatic(),
                                  PlatformRemoteAppleTV::CreateInstance);
  }
}

void PlatformRemoteAppleTV::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformRemoteAppleTV::CreateInstance);
    }
  }

  PlatformDarwin::Terminate();
}

PlatformSP PlatformRemoteAppleTV::CreateInstance(bool force,
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

    LLDB_LOGF(log, "PlatformRemoteAppleTV::%s(force=%s, arch={%s,%s})",
              __FUNCTION__, force ? "true" : "false", arch_name, triple_cstr);
  }

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    switch (arch->GetMachine()) {
    case llvm::Triple::arm:
    case llvm::Triple::aarch64:
    case llvm::Triple::thumb: {
      const llvm::Triple &triple = arch->GetTriple();
      llvm::Triple::VendorType vendor = triple.getVendor();
      switch (vendor) {
      case llvm::Triple::Apple:
        create = true;
        break;

#if defined(__APPLE__)
      // Only accept "unknown" for the vendor if the host is Apple and
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
        switch (triple.getOS()) {
        case llvm::Triple::TvOS: // This is the right triple value for Apple TV
                                 // debugging
          break;

        default:
          create = false;
          break;
        }
      }
    } break;
    default:
      break;
    }
  }

  if (create) {
    LLDB_LOGF(log, "PlatformRemoteAppleTV::%s() creating platform",
              __FUNCTION__);

    return lldb::PlatformSP(new PlatformRemoteAppleTV());
  }

  LLDB_LOGF(log, "PlatformRemoteAppleTV::%s() aborting creation of platform",
            __FUNCTION__);

  return lldb::PlatformSP();
}

llvm::StringRef PlatformRemoteAppleTV::GetDescriptionStatic() {
  return "Remote Apple TV platform plug-in.";
}

std::vector<ArchSpec> PlatformRemoteAppleTV::GetSupportedArchitectures(
    const ArchSpec &process_host_arch) {
  ArchSpec system_arch(GetSystemArchitecture());

  const ArchSpec::Core system_core = system_arch.GetCore();
  switch (system_core) {
  default:
  case ArchSpec::eCore_arm_arm64:
    return {ArchSpec("arm64-apple-tvos"), ArchSpec("armv7s-apple-tvos"),
            ArchSpec("armv7-apple-tvos"), ArchSpec("thumbv7s-apple-tvos"),
            ArchSpec("thumbv7-apple-tvos")};

  case ArchSpec::eCore_arm_armv7s:
    return {ArchSpec("armv7s-apple-tvos"), ArchSpec("armv7-apple-tvos"),
            ArchSpec("thumbv7s-apple-tvos"), ArchSpec("thumbv7-apple-tvos")};

  case ArchSpec::eCore_arm_armv7:
    return {ArchSpec("armv7-apple-tvos"), ArchSpec("thumbv7-apple-tvos")};
  }
}

llvm::StringRef PlatformRemoteAppleTV::GetDeviceSupportDirectoryName() {
  return "tvOS DeviceSupport";
}

llvm::StringRef PlatformRemoteAppleTV::GetPlatformName() {
  return "AppleTVOS.platform";
}
