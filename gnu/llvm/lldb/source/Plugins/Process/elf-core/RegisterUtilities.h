//===-- RegisterUtilities.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERUTILITIES_H
#define LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERUTILITIES_H

#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#include "lldb/Utility/DataExtractor.h"
#include "llvm/BinaryFormat/ELF.h"

namespace lldb_private {
/// Core files PT_NOTE segment descriptor types

namespace NETBSD {
enum { NT_PROCINFO = 1, NT_AUXV = 2 };

/* Size in bytes */
enum { NT_PROCINFO_SIZE = 160 };

/* Size in bytes */
enum {
  NT_PROCINFO_CPI_VERSION_SIZE = 4,
  NT_PROCINFO_CPI_CPISIZE_SIZE = 4,
  NT_PROCINFO_CPI_SIGNO_SIZE = 4,
  NT_PROCINFO_CPI_SIGCODE_SIZE = 4,
  NT_PROCINFO_CPI_SIGPEND_SIZE = 16,
  NT_PROCINFO_CPI_SIGMASK_SIZE = 16,
  NT_PROCINFO_CPI_SIGIGNORE_SIZE = 16,
  NT_PROCINFO_CPI_SIGCATCH_SIZE = 16,
  NT_PROCINFO_CPI_PID_SIZE = 4,
  NT_PROCINFO_CPI_PPID_SIZE = 4,
  NT_PROCINFO_CPI_PGRP_SIZE = 4,
  NT_PROCINFO_CPI_SID_SIZE = 4,
  NT_PROCINFO_CPI_RUID_SIZE = 4,
  NT_PROCINFO_CPI_EUID_SIZE = 4,
  NT_PROCINFO_CPI_SVUID_SIZE = 4,
  NT_PROCINFO_CPI_RGID_SIZE = 4,
  NT_PROCINFO_CPI_EGID_SIZE = 4,
  NT_PROCINFO_CPI_SVGID_SIZE = 4,
  NT_PROCINFO_CPI_NLWPS_SIZE = 4,
  NT_PROCINFO_CPI_NAME_SIZE = 32,
  NT_PROCINFO_CPI_SIGLWP_SIZE = 4,
};

namespace AARCH64 {
enum { NT_REGS = 32, NT_FPREGS = 34 };
}

namespace AMD64 {
enum { NT_REGS = 33, NT_FPREGS = 35 };
}

namespace I386 {
enum { NT_REGS = 33, NT_FPREGS = 35 };
}

} // namespace NETBSD

namespace OPENBSD {
enum {
  NT_PROCINFO = 10,
  NT_AUXV = 11,
  NT_REGS = 20,
  NT_FPREGS = 21,
  NT_PACMASK = 24,
};
}

struct CoreNote {
  ELFNote info;
  DataExtractor data;
};

// A structure describing how to find a register set in a core file from a given
// OS.
struct RegsetDesc {
  // OS to which this entry applies to. Must not be UnknownOS.
  llvm::Triple::OSType OS;

  // Architecture to which this entry applies to. Can be UnknownArch, in which
  // case it applies to all architectures of a given OS.
  llvm::Triple::ArchType Arch;

  // The note type under which the register set can be found.
  uint32_t Note;
};

// Returns the register set in Notes which corresponds to the specified Triple
// according to the list of register set descriptions in RegsetDescs. The list
// is scanned linearly, so you can use a more specific entry (e.g. linux-i386)
// to override a more general entry (e.g. general linux), as long as you place
// it earlier in the list. If a register set is not found, it returns an empty
// DataExtractor.
DataExtractor getRegset(llvm::ArrayRef<CoreNote> Notes,
                        const llvm::Triple &Triple,
                        llvm::ArrayRef<RegsetDesc> RegsetDescs);

constexpr RegsetDesc FPR_Desc[] = {
    // FreeBSD/i386 core NT_FPREGSET is x87 FSAVE result but the XSAVE dump
    // starts with FXSAVE struct, so use that instead if available.
    {llvm::Triple::FreeBSD, llvm::Triple::x86, llvm::ELF::NT_X86_XSTATE},
    {llvm::Triple::FreeBSD, llvm::Triple::UnknownArch, llvm::ELF::NT_FPREGSET},
    // In a i386 core file NT_FPREGSET is present, but it's not the result
    // of the FXSAVE instruction like in 64 bit files.
    // The result from FXSAVE is in NT_PRXFPREG for i386 core files
    {llvm::Triple::Linux, llvm::Triple::x86, llvm::ELF::NT_PRXFPREG},
    {llvm::Triple::Linux, llvm::Triple::UnknownArch, llvm::ELF::NT_FPREGSET},
    {llvm::Triple::NetBSD, llvm::Triple::aarch64, NETBSD::AARCH64::NT_FPREGS},
    {llvm::Triple::NetBSD, llvm::Triple::x86, NETBSD::I386::NT_FPREGS},
    {llvm::Triple::NetBSD, llvm::Triple::x86_64, NETBSD::AMD64::NT_FPREGS},
    {llvm::Triple::OpenBSD, llvm::Triple::UnknownArch, OPENBSD::NT_FPREGS},
};

constexpr RegsetDesc AARCH64_SVE_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64, llvm::ELF::NT_ARM_SVE},
};

constexpr RegsetDesc AARCH64_SSVE_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64, llvm::ELF::NT_ARM_SSVE},
};

constexpr RegsetDesc AARCH64_ZA_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64, llvm::ELF::NT_ARM_ZA},
};

constexpr RegsetDesc AARCH64_ZT_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64, llvm::ELF::NT_ARM_ZT},
};

constexpr RegsetDesc AARCH64_PAC_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64, llvm::ELF::NT_ARM_PAC_MASK},
    {llvm::Triple::OpenBSD, llvm::Triple::aarch64, OPENBSD::NT_PACMASK},
};

constexpr RegsetDesc AARCH64_TLS_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64, llvm::ELF::NT_ARM_TLS},
};

constexpr RegsetDesc AARCH64_MTE_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::aarch64,
     llvm::ELF::NT_ARM_TAGGED_ADDR_CTRL},
};

constexpr RegsetDesc PPC_VMX_Desc[] = {
    {llvm::Triple::FreeBSD, llvm::Triple::UnknownArch, llvm::ELF::NT_PPC_VMX},
    {llvm::Triple::Linux, llvm::Triple::UnknownArch, llvm::ELF::NT_PPC_VMX},
};

constexpr RegsetDesc PPC_VSX_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::UnknownArch, llvm::ELF::NT_PPC_VSX},
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERUTILITIES_H
