//===--- NetBSD.cpp - NetBSD ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NetBSD.h"
#include "Arch/ARM.h"
#include "Arch/Mips.h"
#include "Arch/Sparc.h"
#include "CommonArgs.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void netbsd::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const NetBSD &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple &Triple = ToolChain.getTriple();
  ArgStringList CmdArgs;

  claimNoWarnArgs(Args);

  // GNU as needs different flags for creating the correct output format
  // on architectures with different ABIs or optional feature sets.
  switch (ToolChain.getArch()) {
  case llvm::Triple::x86:
    CmdArgs.push_back("--32");
    break;
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    StringRef MArch, MCPU;
    arm::getARMArchCPUFromArgs(Args, MArch, MCPU, /*FromAs*/ true);
    std::string Arch = arm::getARMTargetCPU(MCPU, MArch, Triple);
    CmdArgs.push_back(Args.MakeArgString("-mcpu=" + Arch));
    break;
  }

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el: {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);

    CmdArgs.push_back("-march");
    CmdArgs.push_back(CPUName.data());

    CmdArgs.push_back("-mabi");
    CmdArgs.push_back(mips::getGnuCompatibleMipsABIName(ABIName).data());

    if (Triple.isLittleEndian())
      CmdArgs.push_back("-EL");
    else
      CmdArgs.push_back("-EB");

    AddAssemblerKPIC(ToolChain, Args, CmdArgs);
    break;
  }

  case llvm::Triple::sparc: {
    CmdArgs.push_back("-32");
    std::string CPU = getCPUName(D, Args, Triple);
    CmdArgs.push_back(sparc::getSparcAsmModeForCPU(CPU, Triple));
    AddAssemblerKPIC(ToolChain, Args, CmdArgs);
    break;
  }

  case llvm::Triple::sparcv9: {
    CmdArgs.push_back("-64");
    std::string CPU = getCPUName(D, Args, Triple);
    CmdArgs.push_back(sparc::getSparcAsmModeForCPU(CPU, Triple));
    AddAssemblerKPIC(ToolChain, Args, CmdArgs);
    break;
  }

  default:
    break;
  }

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString((ToolChain.GetProgramPath("as")));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

void netbsd::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const NetBSD &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple &Triple = ToolChain.getTriple();
  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool Static = Args.hasArg(options::OPT_static);
  const bool Shared = Args.hasArg(options::OPT_shared);
  const bool Pie = Args.hasArg(options::OPT_pie);
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  CmdArgs.push_back("--eh-frame-hdr");
  if (Static) {
    CmdArgs.push_back("-Bstatic");
    if (Pie) {
      Args.AddAllArgs(CmdArgs, options::OPT_pie);
      CmdArgs.push_back("--no-dynamic-linker");
    }
  } else {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    if (Shared) {
      CmdArgs.push_back("-shared");
    } else if (!Args.hasArg(options::OPT_r)) {
      Args.AddAllArgs(CmdArgs, options::OPT_pie);
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back("/libexec/ld.elf_so");
    }
  }

  // Many NetBSD architectures support more than one ABI.
  // Determine the correct emulation for ld.
  switch (Arch) {
  case llvm::Triple::x86:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf_i386");
    break;
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    CmdArgs.push_back("-m");
    switch (Triple.getEnvironment()) {
    case llvm::Triple::EABI:
    case llvm::Triple::GNUEABI:
      CmdArgs.push_back("armelf_nbsd_eabi");
      break;
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
      CmdArgs.push_back("armelf_nbsd_eabihf");
      break;
    default:
      CmdArgs.push_back("armelf_nbsd");
      break;
    }
    break;
  case llvm::Triple::armeb:
  case llvm::Triple::thumbeb:
    arm::appendBE8LinkFlag(Args, CmdArgs, ToolChain.getEffectiveTriple());
    CmdArgs.push_back("-m");
    switch (Triple.getEnvironment()) {
    case llvm::Triple::EABI:
    case llvm::Triple::GNUEABI:
      CmdArgs.push_back("armelfb_nbsd_eabi");
      break;
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
      CmdArgs.push_back("armelfb_nbsd_eabihf");
      break;
    default:
      CmdArgs.push_back("armelfb_nbsd");
      break;
    }
    break;
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    if (mips::hasMipsAbiArg(Args, "32")) {
      CmdArgs.push_back("-m");
      if (Arch == llvm::Triple::mips64)
        CmdArgs.push_back("elf32btsmip");
      else
        CmdArgs.push_back("elf32ltsmip");
    } else if (mips::hasMipsAbiArg(Args, "64")) {
      CmdArgs.push_back("-m");
      if (Arch == llvm::Triple::mips64)
        CmdArgs.push_back("elf64btsmip");
      else
        CmdArgs.push_back("elf64ltsmip");
    }
    break;
  case llvm::Triple::ppc:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf32ppc_nbsd");
    break;

  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf64ppc");
    break;

  case llvm::Triple::riscv32:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf32lriscv");
    break;

  case llvm::Triple::riscv64:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf64lriscv");
    break;

  case llvm::Triple::sparc:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf32_sparc");
    break;

  case llvm::Triple::sparcv9:
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf64_sparc");
    break;

  default:
    break;
  }

  if (Triple.isRISCV()) {
    CmdArgs.push_back("-X");
    if (Args.hasArg(options::OPT_mno_relax))
      CmdArgs.push_back("--no-relax");
  }

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    const char *crt0 = nullptr;
    const char *crtbegin = nullptr;
    if (!Shared)
      crt0 = "crt0.o";

    if (Shared || Pie)
      crtbegin = "crtbeginS.o";
    else
      crtbegin = "crtbegin.o";

    if (crt0)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt0)));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group,
                            options::OPT_s, options::OPT_t});
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  bool NeedsSanitizerDeps = addSanitizerRuntimes(ToolChain, Args, CmdArgs);
  bool NeedsXRayDeps = addXRayRuntime(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  const SanitizerArgs &SanArgs = ToolChain.getSanitizerArgs(Args);
  if (SanArgs.needsSharedRt()) {
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back(Args.MakeArgString(ToolChain.getCompilerRTPath()));
  }

  bool useLibgcc = true;
  switch (ToolChain.getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::sparc:
  case llvm::Triple::sparcv9:
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    useLibgcc = false;
    break;
  default:
    break;
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    // Use the static OpenMP runtime with -static-openmp
    bool StaticOpenMP = Args.hasArg(options::OPT_static_openmp) && !Static;
    addOpenMPRuntime(C, CmdArgs, ToolChain, Args, StaticOpenMP);

    if (D.CCCIsCXX()) {
      if (ToolChain.ShouldLinkCXXStdlib(Args))
        ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
      CmdArgs.push_back("-lm");
    }

    // Silence warnings when linking C code with a C++ '-stdlib' argument.
    Args.ClaimAllArgs(options::OPT_stdlib_EQ);

    // Additional linker set-up and flags for Fortran. This is required in order
    // to generate executables. As Fortran runtime depends on the C runtime,
    // these dependencies need to be listed before the C runtime below (i.e.
    // AddRunTimeLibs).
    if (D.IsFlangMode()) {
      addFortranRuntimeLibraryPath(ToolChain, Args, CmdArgs);
      addFortranRuntimeLibs(ToolChain, Args, CmdArgs);
      CmdArgs.push_back("-lm");
    }

    if (NeedsSanitizerDeps)
      linkSanitizerRuntimeDeps(ToolChain, Args, CmdArgs);
    if (NeedsXRayDeps)
      linkXRayRuntimeDeps(ToolChain, Args, CmdArgs);
    if (Args.hasArg(options::OPT_pthread))
      CmdArgs.push_back("-lpthread");
    CmdArgs.push_back("-lc");

    if (useLibgcc) {
      if (Static) {
        // libgcc_eh depends on libc, so resolve as much as possible,
        // pull in any new requirements from libc and then get the rest
        // of libgcc.
        CmdArgs.push_back("-lgcc_eh");
        CmdArgs.push_back("-lc");
        CmdArgs.push_back("-lgcc");
      } else {
        CmdArgs.push_back("-lgcc");
        CmdArgs.push_back("--as-needed");
        CmdArgs.push_back("-lgcc_s");
        CmdArgs.push_back("--no-as-needed");
      }
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    const char *crtend = nullptr;
    if (Shared || Pie)
      crtend = "crtendS.o";
    else
      crtend = "crtend.o";

    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtend)));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
  }

  ToolChain.addProfileRTLibs(Args, CmdArgs);

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

/// NetBSD - NetBSD tool chain which can call as(1) and ld(1) directly.

NetBSD::NetBSD(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  if (!Args.hasArg(options::OPT_nostdlib)) {
    // When targeting a 32-bit platform, try the special directory used on
    // 64-bit hosts, and only fall back to the main library directory if that
    // doesn't work.
    // FIXME: It'd be nicer to test if this directory exists, but I'm not sure
    // what all logic is needed to emulate the '=' prefix here.
    switch (Triple.getArch()) {
    case llvm::Triple::x86:
      getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/i386"));
      break;
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
      switch (Triple.getEnvironment()) {
      case llvm::Triple::EABI:
      case llvm::Triple::GNUEABI:
        getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/eabi"));
        break;
      case llvm::Triple::EABIHF:
      case llvm::Triple::GNUEABIHF:
        getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/eabihf"));
        break;
      default:
        getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/oabi"));
        break;
      }
      break;
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
      if (tools::mips::hasMipsAbiArg(Args, "o32"))
        getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/o32"));
      else if (tools::mips::hasMipsAbiArg(Args, "64"))
        getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/64"));
      break;
    case llvm::Triple::ppc:
      getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/powerpc"));
      break;
    case llvm::Triple::sparc:
      getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/sparc"));
      break;
    default:
      break;
    }

    getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib"));
  }
}

Tool *NetBSD::buildAssembler() const {
  return new tools::netbsd::Assembler(*this);
}

Tool *NetBSD::buildLinker() const { return new tools::netbsd::Linker(*this); }

ToolChain::CXXStdlibType NetBSD::GetDefaultCXXStdlibType() const {
  switch (getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::sparc:
  case llvm::Triple::sparcv9:
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return ToolChain::CST_Libcxx;
  default:
    break;
  }
  return ToolChain::CST_Libstdcxx;
}

void NetBSD::AddClangSystemIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> Dir(D.ResourceDir);
    llvm::sys::path::append(Dir, "include");
    addSystemInclude(DriverArgs, CC1Args, Dir.str());
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Check for configure-time C include directories.
  StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (CIncludeDirs != "") {
    SmallVector<StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (StringRef dir : dirs) {
      StringRef Prefix =
          llvm::sys::path::is_absolute(dir) ? StringRef(D.SysRoot) : "";
      addExternCSystemInclude(DriverArgs, CC1Args, Prefix + dir);
    }
    return;
  }

  addExternCSystemInclude(DriverArgs, CC1Args,
                          concat(D.SysRoot, "/usr/include"));
}

void NetBSD::addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                   llvm::opt::ArgStringList &CC1Args) const {
  const std::string Candidates[] = {
    // directory relative to build tree
    concat(getDriver().Dir, "/../include/c++/v1"),
    // system install with full upstream path
    concat(getDriver().SysRoot, "/usr/include/c++/v1"),
    // system install from src
    concat(getDriver().SysRoot, "/usr/include/c++"),
  };

  for (const auto &IncludePath : Candidates) {
    if (!getVFS().exists(IncludePath + "/__config"))
      continue;

    // Use the first candidate that looks valid.
    addSystemInclude(DriverArgs, CC1Args, IncludePath);
    return;
  }
}

void NetBSD::addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &CC1Args) const {
  addLibStdCXXIncludePaths(concat(getDriver().SysRoot, "/usr/include/g++"), "", "",
                           DriverArgs, CC1Args);
}

llvm::ExceptionHandling NetBSD::GetExceptionModel(const ArgList &Args) const {
  // NetBSD uses Dwarf exceptions on ARM.
  llvm::Triple::ArchType TArch = getTriple().getArch();
  if (TArch == llvm::Triple::arm || TArch == llvm::Triple::armeb ||
      TArch == llvm::Triple::thumb || TArch == llvm::Triple::thumbeb)
    return llvm::ExceptionHandling::DwarfCFI;
  return llvm::ExceptionHandling::None;
}

SanitizerMask NetBSD::getSupportedSanitizers() const {
  const bool IsX86 = getTriple().getArch() == llvm::Triple::x86;
  const bool IsX86_64 = getTriple().getArch() == llvm::Triple::x86_64;
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  if (IsX86 || IsX86_64) {
    Res |= SanitizerKind::Address;
    Res |= SanitizerKind::PointerCompare;
    Res |= SanitizerKind::PointerSubtract;
    Res |= SanitizerKind::Leak;
    Res |= SanitizerKind::SafeStack;
    Res |= SanitizerKind::Scudo;
    Res |= SanitizerKind::Vptr;
  }
  if (IsX86_64) {
    Res |= SanitizerKind::DataFlow;
    Res |= SanitizerKind::Fuzzer;
    Res |= SanitizerKind::FuzzerNoLink;
    Res |= SanitizerKind::HWAddress;
    Res |= SanitizerKind::KernelAddress;
    Res |= SanitizerKind::KernelHWAddress;
    Res |= SanitizerKind::KernelMemory;
    Res |= SanitizerKind::Memory;
    Res |= SanitizerKind::Thread;
  }
  return Res;
}

void NetBSD::addClangTargetOptions(const ArgList &DriverArgs,
                                   ArgStringList &CC1Args,
                                   Action::OffloadKind) const {
  const SanitizerArgs &SanArgs = getSanitizerArgs(DriverArgs);
  if (SanArgs.hasAnySanitizer())
    CC1Args.push_back("-D_REENTRANT");

  VersionTuple OsVersion = getTriple().getOSVersion();
  bool UseInitArrayDefault =
      OsVersion >= VersionTuple(9) || OsVersion.getMajor() == 0 ||
      getTriple().getArch() == llvm::Triple::aarch64 ||
      getTriple().getArch() == llvm::Triple::aarch64_be ||
      getTriple().getArch() == llvm::Triple::arm ||
      getTriple().getArch() == llvm::Triple::armeb ||
      getTriple().getArch() == llvm::Triple::riscv32 ||
      getTriple().getArch() == llvm::Triple::riscv64;

  if (!DriverArgs.hasFlag(options::OPT_fuse_init_array,
                          options::OPT_fno_use_init_array, UseInitArrayDefault))
    CC1Args.push_back("-fno-use-init-array");
}
