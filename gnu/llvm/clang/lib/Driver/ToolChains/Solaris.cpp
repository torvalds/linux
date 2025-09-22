//===--- Solaris.cpp - Solaris ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Solaris.h"
#include "CommonArgs.h"
#include "Gnu.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void solaris::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                      const InputInfo &Output,
                                      const InputInfoList &Inputs,
                                      const ArgList &Args,
                                      const char *LinkingOutput) const {
  // Just call the Gnu version, which enforces gas on Solaris.
  gnutools::Assembler::ConstructJob(C, JA, Output, Inputs, Args, LinkingOutput);
}

bool solaris::isLinkerGnuLd(const ToolChain &TC, const ArgList &Args) {
  // Only used if targetting Solaris.
  const Arg *A = Args.getLastArg(options::OPT_fuse_ld_EQ);
  StringRef UseLinker = A ? A->getValue() : CLANG_DEFAULT_LINKER;
  return UseLinker == "bfd" || UseLinker == "gld";
}

static bool getPIE(const ArgList &Args, const ToolChain &TC) {
  if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_static) ||
      Args.hasArg(options::OPT_r))
    return false;

  return Args.hasFlag(options::OPT_pie, options::OPT_no_pie,
                      TC.isPIEDefault(Args));
}

// FIXME: Need to handle CLANG_DEFAULT_LINKER here?
std::string solaris::Linker::getLinkerPath(const ArgList &Args) const {
  const ToolChain &ToolChain = getToolChain();
  if (const Arg *A = Args.getLastArg(options::OPT_fuse_ld_EQ)) {
    StringRef UseLinker = A->getValue();
    if (!UseLinker.empty()) {
      if (llvm::sys::path::is_absolute(UseLinker) &&
          llvm::sys::fs::can_execute(UseLinker))
        return std::string(UseLinker);

      // Accept 'bfd' and 'gld' as aliases for the GNU linker.
      if (UseLinker == "bfd" || UseLinker == "gld")
        // FIXME: Could also use /usr/bin/gld here.
        return "/usr/gnu/bin/ld";

      // Accept 'ld' as alias for the default linker
      if (UseLinker != "ld")
        ToolChain.getDriver().Diag(diag::err_drv_invalid_linker_name)
            << A->getAsString(Args);
    }
  }

  // getDefaultLinker() always returns an absolute path.
  return ToolChain.getDefaultLinker();
}

void solaris::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const Solaris &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool IsPIE = getPIE(Args, ToolChain);
  const bool LinkerIsGnuLd = isLinkerGnuLd(ToolChain, Args);
  ArgStringList CmdArgs;

  // Demangle C++ names in errors.  GNU ld already defaults to --demangle.
  if (!LinkerIsGnuLd)
    CmdArgs.push_back("-C");

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_shared,
                   options::OPT_r)) {
    CmdArgs.push_back("-e");
    CmdArgs.push_back("_start");
  }

  if (IsPIE) {
    if (LinkerIsGnuLd) {
      CmdArgs.push_back("-pie");
    } else {
      CmdArgs.push_back("-z");
      CmdArgs.push_back("type=pie");
    }
  }

  if (Args.hasArg(options::OPT_static)) {
    CmdArgs.push_back("-Bstatic");
    CmdArgs.push_back("-dn");
  } else {
    if (!Args.hasArg(options::OPT_r) && Args.hasArg(options::OPT_shared))
      CmdArgs.push_back("-shared");

    // libpthread has been folded into libc since Solaris 10, no need to do
    // anything for pthreads. Claim argument to avoid warning.
    Args.ClaimAllArgs(options::OPT_pthread);
    Args.ClaimAllArgs(options::OPT_pthreads);
  }

  if (LinkerIsGnuLd) {
    // Set the correct linker emulation for 32- and 64-bit Solaris.
    switch (Arch) {
    case llvm::Triple::x86:
      CmdArgs.push_back("-m");
      CmdArgs.push_back("elf_i386_sol2");
      break;
    case llvm::Triple::x86_64:
      CmdArgs.push_back("-m");
      CmdArgs.push_back("elf_x86_64_sol2");
      break;
    case llvm::Triple::sparc:
      CmdArgs.push_back("-m");
      CmdArgs.push_back("elf32_sparc_sol2");
      break;
    case llvm::Triple::sparcv9:
      CmdArgs.push_back("-m");
      CmdArgs.push_back("elf64_sparc_sol2");
      break;
    default:
      break;
    }

    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");

    CmdArgs.push_back("--eh-frame-hdr");
  } else {
    // -rdynamic is a no-op with Solaris ld.  Claim argument to avoid warning.
    Args.ClaimAllArgs(options::OPT_rdynamic);
  }

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    if (!Args.hasArg(options::OPT_shared))
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt1.o")));

    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));

    const Arg *Std = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi);
    bool HaveAnsi = false;
    const LangStandard *LangStd = nullptr;
    if (Std) {
      HaveAnsi = Std->getOption().matches(options::OPT_ansi);
      if (!HaveAnsi)
        LangStd = LangStandard::getLangStandardForName(Std->getValue());
    }

    const char *values_X = "values-Xa.o";
    // Use values-Xc.o for -ansi, -std=c*, -std=iso9899:199409.
    if (HaveAnsi || (LangStd && !LangStd->isGNUMode()))
      values_X = "values-Xc.o";
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(values_X)));

    const char *values_xpg = "values-xpg6.o";
    // Use values-xpg4.o for -std=c90, -std=gnu90, -std=iso9899:199409.
    if (LangStd && LangStd->getLanguage() == Language::C && !LangStd->isC99())
      values_xpg = "values-xpg4.o";
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(values_xpg)));

    const char *crtbegin = nullptr;
    if (Args.hasArg(options::OPT_shared) || IsPIE)
      crtbegin = "crtbeginS.o";
    else
      crtbegin = "crtbegin.o";
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
    // Add crtfastmath.o if available and fast math is enabled.
    ToolChain.addFastMathRuntimeIfAvailable(Args, CmdArgs);
  }

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group});

  bool NeedsSanitizerDeps = addSanitizerRuntimes(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    // Use the static OpenMP runtime with -static-openmp
    bool StaticOpenMP = Args.hasArg(options::OPT_static_openmp) &&
                        !Args.hasArg(options::OPT_static);
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
    // these dependencies need to be listed before the C runtime below.
    if (D.IsFlangMode()) {
      addFortranRuntimeLibraryPath(getToolChain(), Args, CmdArgs);
      addFortranRuntimeLibs(getToolChain(), Args, CmdArgs);
      CmdArgs.push_back("-lm");
    }
    if (Args.hasArg(options::OPT_fstack_protector) ||
        Args.hasArg(options::OPT_fstack_protector_strong) ||
        Args.hasArg(options::OPT_fstack_protector_all)) {
      // Explicitly link ssp libraries, not folded into Solaris libc.
      CmdArgs.push_back("-lssp_nonshared");
      CmdArgs.push_back("-lssp");
    }
    // LLVM support for atomics on 32-bit SPARC V8+ is incomplete, so
    // forcibly link with libatomic as a workaround.
    if (Arch == llvm::Triple::sparc) {
      addAsNeededOption(ToolChain, Args, CmdArgs, true);
      CmdArgs.push_back("-latomic");
      addAsNeededOption(ToolChain, Args, CmdArgs, false);
    }
    addAsNeededOption(ToolChain, Args, CmdArgs, true);
    CmdArgs.push_back("-lgcc_s");
    addAsNeededOption(ToolChain, Args, CmdArgs, false);
    CmdArgs.push_back("-lc");
    if (!Args.hasArg(options::OPT_shared)) {
      CmdArgs.push_back("-lgcc");
    }
    const SanitizerArgs &SA = ToolChain.getSanitizerArgs(Args);
    if (NeedsSanitizerDeps) {
      linkSanitizerRuntimeDeps(ToolChain, Args, CmdArgs);

      // Work around Solaris/amd64 ld bug when calling __tls_get_addr directly.
      // However, ld -z relax=transtls is available since Solaris 11.2, but not
      // in Illumos.
      if (Arch == llvm::Triple::x86_64 &&
          (SA.needsAsanRt() || SA.needsStatsRt() ||
           (SA.needsUbsanRt() && !SA.requiresMinimalRuntime())) &&
          !LinkerIsGnuLd) {
        CmdArgs.push_back("-z");
        CmdArgs.push_back("relax=transtls");
      }
    }
    // Avoid AsanInitInternal cycle, Issue #64126.
    if (ToolChain.getTriple().isX86() && SA.needsSharedRt() &&
        SA.needsAsanRt()) {
      CmdArgs.push_back("-z");
      CmdArgs.push_back("now");
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    const char *crtend = nullptr;
    if (Args.hasArg(options::OPT_shared) || IsPIE)
      crtend = "crtendS.o";
    else
      crtend = "crtend.o";
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtend)));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
  }

  ToolChain.addProfileRTLibs(Args, CmdArgs);

  const char *Exec = Args.MakeArgString(getLinkerPath(Args));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

static StringRef getSolarisLibSuffix(const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  case llvm::Triple::x86:
  case llvm::Triple::sparc:
  default:
    break;
  case llvm::Triple::x86_64:
    return "/amd64";
  case llvm::Triple::sparcv9:
    return "/sparcv9";
  }
  return "";
}

/// Solaris - Solaris tool chain which can call as(1) and ld(1) directly.

Solaris::Solaris(const Driver &D, const llvm::Triple &Triple,
                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {

  GCCInstallation.init(Triple, Args);

  StringRef LibSuffix = getSolarisLibSuffix(Triple);
  path_list &Paths = getFilePaths();
  if (GCCInstallation.isValid()) {
    // On Solaris gcc uses both an architecture-specific path with triple in it
    // as well as a more generic lib path (+arch suffix).
    addPathIfExists(D,
                    GCCInstallation.getInstallPath() +
                        GCCInstallation.getMultilib().gccSuffix(),
                    Paths);
    addPathIfExists(D, GCCInstallation.getParentLibPath() + LibSuffix, Paths);
  }

  // If we are currently running Clang inside of the requested system root,
  // add its parent library path to those searched.
  if (StringRef(D.Dir).starts_with(D.SysRoot))
    addPathIfExists(D, D.Dir + "/../lib", Paths);

  addPathIfExists(D, D.SysRoot + "/usr/lib" + LibSuffix, Paths);
}

SanitizerMask Solaris::getSupportedSanitizers() const {
  const bool IsX86 = getTriple().getArch() == llvm::Triple::x86;
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  // FIXME: Omit X86_64 until 64-bit support is figured out.
  if (IsX86) {
    Res |= SanitizerKind::Address;
    Res |= SanitizerKind::PointerCompare;
    Res |= SanitizerKind::PointerSubtract;
  }
  Res |= SanitizerKind::SafeStack;
  Res |= SanitizerKind::Vptr;
  return Res;
}

const char *Solaris::getDefaultLinker() const {
  // FIXME: Only handle Solaris ld and GNU ld here.
  return llvm::StringSwitch<const char *>(CLANG_DEFAULT_LINKER)
      .Cases("bfd", "gld", "/usr/gnu/bin/ld")
      .Default("/usr/bin/ld");
}

Tool *Solaris::buildAssembler() const {
  return new tools::solaris::Assembler(*this);
}

Tool *Solaris::buildLinker() const { return new tools::solaris::Linker(*this); }

void Solaris::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nostdlibinc))
    addSystemInclude(DriverArgs, CC1Args, D.SysRoot + "/usr/local/include");

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
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
          llvm::sys::path::is_absolute(dir) ? "" : StringRef(D.SysRoot);
      addExternCSystemInclude(DriverArgs, CC1Args, Prefix + dir);
    }
    return;
  }

  // Add include directories specific to the selected multilib set and multilib.
  if (GCCInstallation.isValid()) {
    const MultilibSet::IncludeDirsFunc &Callback =
        Multilibs.includeDirsCallback();
    if (Callback) {
      for (const auto &Path : Callback(GCCInstallation.getMultilib()))
        addExternCSystemIncludeIfExists(
            DriverArgs, CC1Args, GCCInstallation.getInstallPath() + Path);
    }
  }

  addExternCSystemInclude(DriverArgs, CC1Args, D.SysRoot + "/usr/include");
}

void Solaris::addLibStdCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  // We need a detected GCC installation on Solaris (similar to Linux)
  // to provide libstdc++'s headers.
  if (!GCCInstallation.isValid())
    return;

  // By default, look for the C++ headers in an include directory adjacent to
  // the lib directory of the GCC installation.
  // On Solaris this usually looks like /usr/gcc/X.Y/include/c++/X.Y.Z
  StringRef LibDir = GCCInstallation.getParentLibPath();
  StringRef TripleStr = GCCInstallation.getTriple().str();
  const Multilib &Multilib = GCCInstallation.getMultilib();
  const GCCVersion &Version = GCCInstallation.getVersion();

  // The primary search for libstdc++ supports multiarch variants.
  addLibStdCXXIncludePaths(LibDir.str() + "/../include/c++/" + Version.Text,
                           TripleStr, Multilib.includeSuffix(), DriverArgs,
                           CC1Args);
}
