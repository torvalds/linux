//===-- PlatformRemoteAppleWatch.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>
#include <vector>

#include "PlatformRemoteAppleWatch.h"

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

// Static Variables
static uint32_t g_initialize_count = 0;

// Static Functions
void PlatformRemoteAppleWatch::Initialize() {
  PlatformDarwin::Initialize();

  if (g_initialize_count++ == 0) {
    PluginManager::RegisterPlugin(
        PlatformRemoteAppleWatch::GetPluginNameStatic(),
        PlatformRemoteAppleWatch::GetDescriptionStatic(),
        PlatformRemoteAppleWatch::CreateInstance);
  }
}

void PlatformRemoteAppleWatch::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformRemoteAppleWatch::CreateInstance);
    }
  }

  PlatformDarwin::Terminate();
}

PlatformSP PlatformRemoteAppleWatch::CreateInstance(bool force,
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

    LLDB_LOGF(log, "PlatformRemoteAppleWatch::%s(force=%s, arch={%s,%s})",
              __FUNCTION__, force ? "true" : "false", arch_name, triple_cstr);
  }

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    switch (arch->GetMachine()) {
    case llvm::Triple::arm:
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_32:
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
        case llvm::Triple::WatchOS: // This is the right triple value for Apple
                                    // Watch debugging
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

#if defined(__APPLE__) &&                                                      \
    (defined(__arm__) || defined(__arm64__) || defined(__aarch64__))
  // If lldb is running on a watch, this isn't a RemoteWatch environment; it's
  // a local system environment.
  if (force == false) {
    create = false;
  }
#endif

  if (create) {
    LLDB_LOGF(log, "PlatformRemoteAppleWatch::%s() creating platform",
              __FUNCTION__);

    return lldb::PlatformSP(new PlatformRemoteAppleWatch());
  }

  LLDB_LOGF(log, "PlatformRemoteAppleWatch::%s() aborting creation of platform",
            __FUNCTION__);

  return lldb::PlatformSP();
}

llvm::StringRef PlatformRemoteAppleWatch::GetDescriptionStatic() {
  return "Remote Apple Watch platform plug-in.";
}

/// Default Constructor
PlatformRemoteAppleWatch::PlatformRemoteAppleWatch()
    : PlatformRemoteDarwinDevice() {}

std::vector<ArchSpec>
PlatformRemoteAppleWatch::GetSupportedArchitectures(const ArchSpec &host_info) {
  ArchSpec system_arch(GetSystemArchitecture());

  const ArchSpec::Core system_core = system_arch.GetCore();
  switch (system_core) {
  default:
  case ArchSpec::eCore_arm_arm64:
    return {
        ArchSpec("arm64-apple-watchos"),    ArchSpec("armv7k-apple-watchos"),
        ArchSpec("armv7s-apple-watchos"),   ArchSpec("armv7-apple-watchos"),
        ArchSpec("thumbv7k-apple-watchos"), ArchSpec("thumbv7-apple-watchos"),
        ArchSpec("thumbv7s-apple-watchos"), ArchSpec("arm64_32-apple-watchos")};

  case ArchSpec::eCore_arm_armv7k:
    return {
        ArchSpec("armv7k-apple-watchos"),  ArchSpec("armv7s-apple-watchos"),
        ArchSpec("armv7-apple-watchos"),   ArchSpec("thumbv7k-apple-watchos"),
        ArchSpec("thumbv7-apple-watchos"), ArchSpec("thumbv7s-apple-watchos"),
        ArchSpec("arm64_32-apple-watchos")};

  case ArchSpec::eCore_arm_armv7s:
    return {
        ArchSpec("armv7s-apple-watchos"),  ArchSpec("armv7k-apple-watchos"),
        ArchSpec("armv7-apple-watchos"),   ArchSpec("thumbv7k-apple-watchos"),
        ArchSpec("thumbv7-apple-watchos"), ArchSpec("thumbv7s-apple-watchos"),
        ArchSpec("arm64_32-apple-watchos")};

  case ArchSpec::eCore_arm_armv7:
    return {ArchSpec("armv7-apple-watchos"), ArchSpec("armv7k-apple-watchos"),
            ArchSpec("thumbv7k-apple-watchos"),
            ArchSpec("thumbv7-apple-watchos"),
            ArchSpec("arm64_32-apple-watchos")};
  }
}

llvm::StringRef PlatformRemoteAppleWatch::GetDeviceSupportDirectoryName() {
  return "watchOS DeviceSupport";
}

llvm::StringRef PlatformRemoteAppleWatch::GetPlatformName() {
  return "WatchOS.platform";
}
