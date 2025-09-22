//===-- PlatformLinux.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformLinux.h"
#include "lldb/Host/Config.h"

#include <cstdio>
#if LLDB_ENABLE_POSIX
#include <sys/utsname.h>
#endif

#include "Utility/ARM64_DWARF_Registers.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

// Define these constants from Linux mman.h for use when targeting remote linux
// systems even when host has different values.
#define MAP_PRIVATE 2
#define MAP_ANON 0x20

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_linux;

LLDB_PLUGIN_DEFINE(PlatformLinux)

static uint32_t g_initialize_count = 0;


PlatformSP PlatformLinux::CreateInstance(bool force, const ArchSpec *arch) {
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOG(log, "force = {0}, arch=({1}, {2})", force,
           arch ? arch->GetArchitectureName() : "<null>",
           arch ? arch->GetTriple().getTriple() : "<null>");

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getOS()) {
    case llvm::Triple::Linux:
      create = true;
      break;

#if defined(__linux__)
    // Only accept "unknown" for the OS if the host is linux and it "unknown"
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
    return PlatformSP(new PlatformLinux(false));
  }
  return PlatformSP();
}

llvm::StringRef PlatformLinux::GetPluginDescriptionStatic(bool is_host) {
  if (is_host)
    return "Local Linux user platform plug-in.";
  return "Remote Linux user platform plug-in.";
}

void PlatformLinux::Initialize() {
  PlatformPOSIX::Initialize();

  if (g_initialize_count++ == 0) {
#if defined(__linux__) && !defined(__ANDROID__)
    PlatformSP default_platform_sp(new PlatformLinux(true));
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(
        PlatformLinux::GetPluginNameStatic(false),
        PlatformLinux::GetPluginDescriptionStatic(false),
        PlatformLinux::CreateInstance, nullptr);
  }
}

void PlatformLinux::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformLinux::CreateInstance);
    }
  }

  PlatformPOSIX::Terminate();
}

/// Default Constructor
PlatformLinux::PlatformLinux(bool is_host)
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
        {llvm::Triple::x86_64, llvm::Triple::x86, llvm::Triple::arm,
         llvm::Triple::aarch64, llvm::Triple::mips64, llvm::Triple::mips64,
         llvm::Triple::hexagon, llvm::Triple::mips, llvm::Triple::mips64el,
         llvm::Triple::mipsel, llvm::Triple::msp430, llvm::Triple::systemz},
        llvm::Triple::Linux);
  }
}

std::vector<ArchSpec>
PlatformLinux::GetSupportedArchitectures(const ArchSpec &process_host_arch) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetSupportedArchitectures(process_host_arch);
  return m_supported_architectures;
}

void PlatformLinux::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);

#if LLDB_ENABLE_POSIX
  // Display local kernel information only when we are running in host mode.
  // Otherwise, we would end up printing non-Linux information (when running on
  // Mac OS for example).
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
PlatformLinux::GetResumeCountForLaunchInfo(ProcessLaunchInfo &launch_info) {
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

bool PlatformLinux::CanDebugProcess() {
  if (IsHost()) {
    return true;
  } else {
    // If we're connected, we can debug.
    return IsConnected();
  }
}

void PlatformLinux::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
  m_trap_handlers.push_back(ConstString("__kernel_rt_sigreturn"));
  m_trap_handlers.push_back(ConstString("__restore_rt"));
}

static lldb::UnwindPlanSP GetAArch64TrapHandlerUnwindPlan(ConstString name) {
  UnwindPlanSP unwind_plan_sp;
  if (name != "__kernel_rt_sigreturn")
    return unwind_plan_sp;

  UnwindPlan::RowSP row = std::make_shared<UnwindPlan::Row>();
  row->SetOffset(0);

  // In the signal trampoline frame, sp points to an rt_sigframe[1], which is:
  //  - 128-byte siginfo struct
  //  - ucontext struct:
  //     - 8-byte long (uc_flags)
  //     - 8-byte pointer (uc_link)
  //     - 24-byte stack_t
  //     - 128-byte signal set
  //     - 8 bytes of padding because sigcontext has 16-byte alignment
  //     - sigcontext/mcontext_t
  // [1]
  // https://github.com/torvalds/linux/blob/master/arch/arm64/kernel/signal.c
  int32_t offset = 128 + 8 + 8 + 24 + 128 + 8;
  // Then sigcontext[2] is:
  // - 8 byte fault address
  // - 31 8 byte registers
  // - 8 byte sp
  // - 8 byte pc
  // [2]
  // https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/sigcontext.h

  // Skip fault address
  offset += 8;
  row->GetCFAValue().SetIsRegisterPlusOffset(arm64_dwarf::sp, offset);

  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x0, 0 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x1, 1 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x2, 2 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x3, 3 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x4, 4 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x5, 5 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x6, 6 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x7, 7 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x8, 8 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x9, 9 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x10, 10 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x11, 11 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x12, 12 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x13, 13 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x14, 14 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x15, 15 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x16, 16 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x17, 17 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x18, 18 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x19, 19 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x20, 20 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x21, 21 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x22, 22 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x23, 23 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x24, 24 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x25, 25 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x26, 26 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x27, 27 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x28, 28 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::fp, 29 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::x30, 30 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::sp, 31 * 8, false);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_dwarf::pc, 32 * 8, false);

  // The sigcontext may also contain floating point and SVE registers.
  // However this would require a dynamic unwind plan so they are not included
  // here.

  unwind_plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  unwind_plan_sp->AppendRow(row);
  unwind_plan_sp->SetSourceName("AArch64 Linux sigcontext");
  unwind_plan_sp->SetSourcedFromCompiler(eLazyBoolYes);
  // Because sp is the same throughout the function
  unwind_plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolYes);

  return unwind_plan_sp;
}

lldb::UnwindPlanSP
PlatformLinux::GetTrapHandlerUnwindPlan(const llvm::Triple &triple,
                                        ConstString name) {
  if (triple.isAArch64())
    return GetAArch64TrapHandlerUnwindPlan(name);

  return {};
}

MmapArgList PlatformLinux::GetMmapArgumentList(const ArchSpec &arch,
                                               addr_t addr, addr_t length,
                                               unsigned prot, unsigned flags,
                                               addr_t fd, addr_t offset) {
  uint64_t flags_platform = 0;
  uint64_t map_anon = arch.IsMIPS() ? 0x800 : MAP_ANON;

  if (flags & eMmapFlagsPrivate)
    flags_platform |= MAP_PRIVATE;
  if (flags & eMmapFlagsAnon)
    flags_platform |= map_anon;

  MmapArgList args({addr, length, prot, flags_platform, fd, offset});
  return args;
}

CompilerType PlatformLinux::GetSiginfoType(const llvm::Triple &triple) {
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_type_system)
      m_type_system = std::make_shared<TypeSystemClang>("siginfo", triple);
  }
  TypeSystemClang *ast = m_type_system.get();

  bool si_errno_then_code = true;

  switch (triple.getArch()) {
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    // mips has si_code and si_errno swapped
    si_errno_then_code = false;
    break;
  default:
    break;
  }

  // generic types
  CompilerType int_type = ast->GetBasicType(eBasicTypeInt);
  CompilerType uint_type = ast->GetBasicType(eBasicTypeUnsignedInt);
  CompilerType short_type = ast->GetBasicType(eBasicTypeShort);
  CompilerType long_type = ast->GetBasicType(eBasicTypeLong);
  CompilerType voidp_type = ast->GetBasicType(eBasicTypeVoid).GetPointerType();

  // platform-specific types
  CompilerType &pid_type = int_type;
  CompilerType &uid_type = uint_type;
  CompilerType &clock_type = long_type;
  CompilerType &band_type = long_type;

  CompilerType sigval_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "__lldb_sigval_t",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(sigval_type);
  ast->AddFieldToRecordType(sigval_type, "sival_int", int_type,
                            lldb::eAccessPublic, 0);
  ast->AddFieldToRecordType(sigval_type, "sival_ptr", voidp_type,
                            lldb::eAccessPublic, 0);
  ast->CompleteTagDeclarationDefinition(sigval_type);

  CompilerType sigfault_bounds_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(sigfault_bounds_type);
  ast->AddFieldToRecordType(
      sigfault_bounds_type, "_addr_bnd",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_lower", voidp_type},
                                         {"_upper", voidp_type},
                                     }),
      lldb::eAccessPublic, 0);
  ast->AddFieldToRecordType(sigfault_bounds_type, "_pkey", uint_type,
                            lldb::eAccessPublic, 0);
  ast->CompleteTagDeclarationDefinition(sigfault_bounds_type);

  // siginfo_t
  CompilerType siginfo_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "__lldb_siginfo_t",
      llvm::to_underlying(clang::TagTypeKind::Struct), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(siginfo_type);
  ast->AddFieldToRecordType(siginfo_type, "si_signo", int_type,
                            lldb::eAccessPublic, 0);

  if (si_errno_then_code) {
    ast->AddFieldToRecordType(siginfo_type, "si_errno", int_type,
                              lldb::eAccessPublic, 0);
    ast->AddFieldToRecordType(siginfo_type, "si_code", int_type,
                              lldb::eAccessPublic, 0);
  } else {
    ast->AddFieldToRecordType(siginfo_type, "si_code", int_type,
                              lldb::eAccessPublic, 0);
    ast->AddFieldToRecordType(siginfo_type, "si_errno", int_type,
                              lldb::eAccessPublic, 0);
  }

  // the structure is padded on 64-bit arches to fix alignment
  if (triple.isArch64Bit())
    ast->AddFieldToRecordType(siginfo_type, "__pad0", int_type,
                              lldb::eAccessPublic, 0);

  // union used to hold the signal data
  CompilerType union_type = ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, "",
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeC);
  ast->StartTagDeclarationDefinition(union_type);

  ast->AddFieldToRecordType(
      union_type, "_kill",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"si_pid", pid_type},
                                         {"si_uid", uid_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_timer",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"si_tid", int_type},
                                         {"si_overrun", int_type},
                                         {"si_sigval", sigval_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_rt",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"si_pid", pid_type},
                                         {"si_uid", uid_type},
                                         {"si_sigval", sigval_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_sigchld",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"si_pid", pid_type},
                                         {"si_uid", uid_type},
                                         {"si_status", int_type},
                                         {"si_utime", clock_type},
                                         {"si_stime", clock_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_sigfault",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"si_addr", voidp_type},
                                         {"si_addr_lsb", short_type},
                                         {"_bounds", sigfault_bounds_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->AddFieldToRecordType(
      union_type, "_sigpoll",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"si_band", band_type},
                                         {"si_fd", int_type},
                                     }),
      lldb::eAccessPublic, 0);

  // NB: SIGSYS is not present on ia64 but we don't seem to support that
  ast->AddFieldToRecordType(
      union_type, "_sigsys",
      ast->CreateStructForIdentifier(llvm::StringRef(),
                                     {
                                         {"_call_addr", voidp_type},
                                         {"_syscall", int_type},
                                         {"_arch", uint_type},
                                     }),
      lldb::eAccessPublic, 0);

  ast->CompleteTagDeclarationDefinition(union_type);
  ast->AddFieldToRecordType(siginfo_type, "_sifields", union_type,
                            lldb::eAccessPublic, 0);

  ast->CompleteTagDeclarationDefinition(siginfo_type);
  return siginfo_type;
}
