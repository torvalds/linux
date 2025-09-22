//===-- PlatformRemoteMacOSX.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <vector>

#include "PlatformRemoteMacOSX.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

/// Default Constructor
PlatformRemoteMacOSX::PlatformRemoteMacOSX() : PlatformRemoteDarwinDevice() {}

// Static Variables
static uint32_t g_initialize_count = 0;

// Static Functions
void PlatformRemoteMacOSX::Initialize() {
  PlatformDarwin::Initialize();

  if (g_initialize_count++ == 0) {
    PluginManager::RegisterPlugin(PlatformRemoteMacOSX::GetPluginNameStatic(),
                                  PlatformRemoteMacOSX::GetDescriptionStatic(),
                                  PlatformRemoteMacOSX::CreateInstance);
  }
}

void PlatformRemoteMacOSX::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformRemoteMacOSX::CreateInstance);
    }
  }

  PlatformDarwin::Terminate();
}

PlatformSP PlatformRemoteMacOSX::CreateInstance(bool force,
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

    LLDB_LOGF(log, "PlatformRemoteMacOSX::%s(force=%s, arch={%s,%s})",
              __FUNCTION__, force ? "true" : "false", arch_name, triple_cstr);
  }

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getVendor()) {
    case llvm::Triple::Apple:
      create = true;
      break;

#if defined(__APPLE__)
    // Only accept "unknown" for vendor if the host is Apple and it "unknown"
    // wasn't specified (it was just returned because it was NOT specified)
    case llvm::Triple::UnknownVendor:
      create = !arch->TripleVendorWasSpecified();
      break;
#endif
    default:
      break;
    }

    if (create) {
      switch (triple.getOS()) {
      case llvm::Triple::Darwin: // Deprecated, but still support Darwin for
                                 // historical reasons
      case llvm::Triple::MacOSX:
        break;
#if defined(__APPLE__)
      // Only accept "vendor" for vendor if the host is Apple and it "unknown"
      // wasn't specified (it was just returned because it was NOT specified)
      case llvm::Triple::UnknownOS:
        create = !arch->TripleOSWasSpecified();
        break;
#endif
      default:
        create = false;
        break;
      }
    }
  }

  if (create) {
    LLDB_LOGF(log, "PlatformRemoteMacOSX::%s() creating platform",
              __FUNCTION__);
    return std::make_shared<PlatformRemoteMacOSX>();
  }

  LLDB_LOGF(log, "PlatformRemoteMacOSX::%s() aborting creation of platform",
            __FUNCTION__);

  return PlatformSP();
}

std::vector<ArchSpec>
PlatformRemoteMacOSX::GetSupportedArchitectures(const ArchSpec &host_info) {
  // macOS for ARM64 support both native and translated x86_64 processes
  std::vector<ArchSpec> result;
  ARMGetSupportedArchitectures(result, llvm::Triple::MacOSX);

  // We can't use x86GetSupportedArchitectures() because it uses
  // the system architecture for some of its return values and also
  // has a 32bits variant.
  result.push_back(ArchSpec("x86_64-apple-macosx"));
  result.push_back(ArchSpec("x86_64-apple-ios-macabi"));
  result.push_back(ArchSpec("arm64-apple-ios"));
  result.push_back(ArchSpec("arm64e-apple-ios"));
  return result;
}

llvm::StringRef PlatformRemoteMacOSX::GetDescriptionStatic() {
  return "Remote Mac OS X user platform plug-in.";
}

llvm::StringRef PlatformRemoteMacOSX::GetDeviceSupportDirectoryName() {
  return "macOS DeviceSupport";
}

llvm::StringRef PlatformRemoteMacOSX::GetPlatformName() {
  return "MacOSX.platform";
}
