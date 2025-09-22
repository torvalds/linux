//===-- PlatformOpenBSD.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformOpenBSD.h"
#include "lldb/Host/Config.h"

#include <cstdio>
#if LLDB_ENABLE_POSIX
#include <sys/utsname.h>
#endif

#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "Plugins/Process/OpenBSDKernel/ProcessOpenBSDKernel.h"

// Define these constants from OpenBSD mman.h for use when targeting remote
// openbsd systems even when host has different values.
#define MAP_PRIVATE 0x0002
#define MAP_ANON 0x1000

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_openbsd;

LLDB_PLUGIN_DEFINE(PlatformOpenBSD)

static uint32_t g_initialize_count = 0;


PlatformSP PlatformOpenBSD::CreateInstance(bool force, const ArchSpec *arch) {
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOG(log, "force = {0}, arch=({1}, {2})", force,
           arch ? arch->GetArchitectureName() : "<null>",
           arch ? arch->GetTriple().getTriple() : "<null>");

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getOS()) {
    case llvm::Triple::OpenBSD:
      create = true;
      break;

#if defined(__OpenBSD__)
    // Only accept "unknown" for the OS if the host is BSD and it "unknown"
    // wasn't specified (it was just returned because it was NOT specified)
    case llvm::Triple::OSType::UnknownOS:
      create = !arch->TripleOSWasSpecified();
      break;
#endif
    default:
      break;
    }
  }
  LLDB_LOG(log, "create = {0}", create);
  if (create) {
    return PlatformSP(new PlatformOpenBSD(false));
  }
  return PlatformSP();
}

llvm::StringRef PlatformOpenBSD::GetPluginDescriptionStatic(bool is_host) {
  if (is_host)
    return "Local OpenBSD user platform plug-in.";
  return "Remote OpenBSD user platform plug-in.";
}

void PlatformOpenBSD::Initialize() {
  PlatformPOSIX::Initialize();

  if (g_initialize_count++ == 0) {
#if defined(__OpenBSD__)
    PlatformSP default_platform_sp(new PlatformOpenBSD(true));
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(
        PlatformOpenBSD::GetPluginNameStatic(false),
        PlatformOpenBSD::GetPluginDescriptionStatic(false),
        PlatformOpenBSD::CreateInstance, nullptr);
	ProcessOpenBSDKernel::Initialize();
  }
}

void PlatformOpenBSD::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformOpenBSD::CreateInstance);
      ProcessOpenBSDKernel::Terminate();
    }
  }

  PlatformPOSIX::Terminate();
}

/// Default Constructor
PlatformOpenBSD::PlatformOpenBSD(bool is_host)
    : PlatformPOSIX(is_host) // This is the local host platform
{
  if (is_host) {
    m_supported_architectures.push_back(HostInfo::GetArchitecture());
  } else {
    m_supported_architectures =
        CreateArchList({llvm::Triple::x86_64, llvm::Triple::x86,
                        llvm::Triple::aarch64, llvm::Triple::arm},
                       llvm::Triple::OpenBSD);
  }
}

std::vector<ArchSpec>
PlatformOpenBSD::GetSupportedArchitectures(const ArchSpec &process_host_arch) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetSupportedArchitectures(process_host_arch);
  return m_supported_architectures;
}

void PlatformOpenBSD::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);

#if LLDB_ENABLE_POSIX
  // Display local kernel information only when we are running in host mode.
  // Otherwise, we would end up printing non-OpenBSD information (when running
  // on Mac OS for example).
  if (IsHost()) {
    struct utsname un;

    if (uname(&un))
      return;

    strm.Printf("    Kernel: %s\n", un.sysname);
    strm.Printf("   Release: %s\n", un.release);
    strm.Printf("   Version: %s\n", un.version);
  }
#endif
}

bool PlatformOpenBSD::CanDebugProcess() {
	if (IsHost()) {
		return true;
	} else {
		// If we're connected, we can debug.
		return IsConnected();
	}
}

void PlatformOpenBSD::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

MmapArgList PlatformOpenBSD::GetMmapArgumentList(const ArchSpec &arch,
                                                 addr_t addr, addr_t length,
                                                 unsigned prot, unsigned flags,
                                                 addr_t fd, addr_t offset) {
  uint64_t flags_platform = 0;

  if (flags & eMmapFlagsPrivate)
    flags_platform |= MAP_PRIVATE;
  if (flags & eMmapFlagsAnon)
    flags_platform |= MAP_ANON;

  MmapArgList args({addr, length, prot, flags_platform, fd, offset});
  return args;
}

FileSpec PlatformOpenBSD::LocateExecutable(const char *basename) {

  std::string check = std::string("/usr/bin/") + basename;
  if (access(check.c_str(), X_OK) == 0) {
    return FileSpec(check);
  }

  return FileSpec();
}
