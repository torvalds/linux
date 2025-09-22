//===-- PlatformNetBSD.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformNetBSD.h"
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

// Define these constants from NetBSD mman.h for use when targeting remote
// netbsd systems even when host has different values.
#define MAP_PRIVATE 0x0002
#define MAP_ANON 0x1000

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_netbsd;

LLDB_PLUGIN_DEFINE(PlatformNetBSD)

static uint32_t g_initialize_count = 0;


PlatformSP PlatformNetBSD::CreateInstance(bool force, const ArchSpec *arch) {
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOG(log, "force = {0}, arch=({1}, {2})", force,
           arch ? arch->GetArchitectureName() : "<null>",
           arch ? arch->GetTriple().getTriple() : "<null>");

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getOS()) {
    case llvm::Triple::NetBSD:
      create = true;
      break;

    default:
      break;
    }
  }

  LLDB_LOG(log, "create = {0}", create);
  if (create) {
    return PlatformSP(new PlatformNetBSD(false));
  }
  return PlatformSP();
}

llvm::StringRef PlatformNetBSD::GetPluginDescriptionStatic(bool is_host) {
  if (is_host)
    return "Local NetBSD user platform plug-in.";
  return "Remote NetBSD user platform plug-in.";
}

void PlatformNetBSD::Initialize() {
  PlatformPOSIX::Initialize();

  if (g_initialize_count++ == 0) {
#if defined(__NetBSD__)
    PlatformSP default_platform_sp(new PlatformNetBSD(true));
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(
        PlatformNetBSD::GetPluginNameStatic(false),
        PlatformNetBSD::GetPluginDescriptionStatic(false),
        PlatformNetBSD::CreateInstance, nullptr);
  }
}

void PlatformNetBSD::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformNetBSD::CreateInstance);
    }
  }

  PlatformPOSIX::Terminate();
}

/// Default Constructor
PlatformNetBSD::PlatformNetBSD(bool is_host)
    : PlatformPOSIX(is_host) // This is the local host platform
{
  if (is_host) {
    ArchSpec hostArch = HostInfo::GetArchitecture(HostInfo::eArchKindDefault);
    m_supported_architectures.push_back(hostArch);
    if (hostArch.GetTriple().isArch64Bit()) {
      m_supported_architectures.push_back(
          HostInfo::GetArchitecture(HostInfo::eArchKind32));
    }
  } else {
    m_supported_architectures = CreateArchList(
        {llvm::Triple::x86_64, llvm::Triple::x86}, llvm::Triple::NetBSD);
  }
}

std::vector<ArchSpec>
PlatformNetBSD::GetSupportedArchitectures(const ArchSpec &process_host_arch) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetSupportedArchitectures(process_host_arch);
  return m_supported_architectures;
}

void PlatformNetBSD::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);

#if LLDB_ENABLE_POSIX
  // Display local kernel information only when we are running in host mode.
  // Otherwise, we would end up printing non-NetBSD information (when running
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

uint32_t
PlatformNetBSD::GetResumeCountForLaunchInfo(ProcessLaunchInfo &launch_info) {
  uint32_t resume_count = 0;

  // Always resume past the initial stop when we use eLaunchFlagDebug
  if (launch_info.GetFlags().Test(eLaunchFlagDebug)) {
    // Resume past the stop for the final exec into the true inferior.
    ++resume_count;
  }

  // If we're not launching a shell, we're done.
  const FileSpec &shell = launch_info.GetShell();
  if (!shell)
    return resume_count;

  std::string shell_string = shell.GetPath();
  // We're in a shell, so for sure we have to resume past the shell exec.
  ++resume_count;

  // Figure out what shell we're planning on using.
  const char *shell_name = strrchr(shell_string.c_str(), '/');
  if (shell_name == nullptr)
    shell_name = shell_string.c_str();
  else
    shell_name++;

  if (strcmp(shell_name, "csh") == 0 || strcmp(shell_name, "tcsh") == 0 ||
      strcmp(shell_name, "zsh") == 0 || strcmp(shell_name, "sh") == 0) {
    // These shells seem to re-exec themselves.  Add another resume.
    ++resume_count;
  }

  return resume_count;
}

bool PlatformNetBSD::CanDebugProcess() {
  if (IsHost()) {
    return true;
  } else {
    // If we're connected, we can debug.
    return IsConnected();
  }
}

void PlatformNetBSD::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

MmapArgList PlatformNetBSD::GetMmapArgumentList(const ArchSpec &arch,
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

CompilerType PlatformNetBSD::GetSiginfoType(const llvm::Triple &triple) {
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_type_system)
      m_type_system = std::make_shared<TypeSystemClang>("siginfo", triple);
  }
  TypeSystemClang *ast = m_type_system.get();

  // generic types
  CompilerType int_type = ast->GetBasicType(eBasicTypeInt);
  CompilerType uint_type = ast->GetBasicType(eBasicTypeUnsignedInt);
  CompilerType long_type = ast->GetBasicType(eBasicTypeLong);
  CompilerType long_long_type = ast->GetBasicType(eBasicTypeLongLong);
  CompilerType voidp_type = ast->GetBasicType(eBasicTypeVoid).GetPointerType();

  // platform-specific types
  CompilerType &pid_type = int_type;
  CompilerType &uid_type = uint_type;
  CompilerType &clock_type = uint_type;
  CompilerType &lwpid_type = int_type;

  CompilerType sigval_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "__lldb_sigval_t",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(sigval_type);
  ast->AddFieldToRecordType(sigval_type, "sival_int", int_type,
                            lldb::eAccessPublic, 0);
  ast->AddFieldToRecordType(sigval_type, "sival_ptr", voidp_type,
                            lldb::eAccessPublic, 0);
  ast->CompleteTagDeclarationDefinition(sigval_type);

  CompilerType ptrace_option_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(ptrace_option_type);
  ast->AddFieldToRecordType(ptrace_option_type, "_pe_other_pid", pid_type,
                            lldb::eAccessPublic, 0);
  ast->AddFieldToRecordType(ptrace_option_type, "_pe_lwp", lwpid_type,
                            lldb::eAccessPublic, 0);
  ast->CompleteTagDeclarationDefinition(ptrace_option_type);

  // siginfo_t
  CompilerType siginfo_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "__lldb_siginfo_t",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(siginfo_type);

  // struct _ksiginfo
  CompilerType ksiginfo_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "",
      llvm::to_underlying(clang::TagTypeKind::Struct), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(ksiginfo_type);
  ast->AddFieldToRecordType(ksiginfo_type, "_signo", int_type,
                            lldb::eAccessPublic, 0);
  ast->AddFieldToRecordType(ksiginfo_type, "_code", int_type,
                            lldb::eAccessPublic, 0);
  ast->AddFieldToRecordType(ksiginfo_type, "_errno", int_type,
                            lldb::eAccessPublic, 0);

  // the structure is padded on 64-bit arches to fix alignment
  if (triple.isArch64Bit())
    ast->AddFieldToRecordType(ksiginfo_type, "__pad0", int_type,
                              lldb::eAccessPublic, 0);

  // union used to hold the signal data
  CompilerType union_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(union_type);

  ast->AddFieldToRecordType(
      union_type, "_rt",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_pid", pid_type},
                                         {"_uid", uid_type},
                                         {"_value", sigval_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_child",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_pid", pid_type},
                                         {"_uid", uid_type},
                                         {"_status", int_type},
                                         {"_utime", clock_type},
                                         {"_stime", clock_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_fault",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_addr", voidp_type},
                                         {"_trap", int_type},
                                         {"_trap2", int_type},
                                         {"_trap3", int_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_poll",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_band", long_type},
                                         {"_fd", int_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(union_type, "_syscall",
                            ast->CreateStructForIdentifier(
                                llvm::StringRef(),
                                {
                                    {"_sysnum", int_type},
                                    {"_retval", int_type.GetArrayType(2)},
                                    {"_error", int_type},
                                    {"_args", long_long_type.GetArrayType(8)},
                                }),
                            lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_ptrace_state",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_pe_report_event", int_type},
                                         {"_option", ptrace_option_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->CompleteTagDeclarationDefinition(union_type);
  ast->AddFieldToRecordType(ksiginfo_type, "_reason", union_type,
                            lldb::eAccessPublic, 0);

  ast->CompleteTagDeclarationDefinition(ksiginfo_type);
  ast->AddFieldToRecordType(siginfo_type, "_info", ksiginfo_type,
                            lldb::eAccessPublic, 0);

  ast->CompleteTagDeclarationDefinition(siginfo_type);
  return siginfo_type;
}
