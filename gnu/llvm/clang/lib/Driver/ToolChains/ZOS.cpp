//===--- ZOS.cpp - z/OS ToolChain Implementations ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZOS.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/WithColor.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace llvm;
using namespace llvm::opt;
using namespace llvm::sys;

ZOS::ZOS(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : ToolChain(D, Triple, Args) {}

ZOS::~ZOS() {}

void ZOS::addClangTargetOptions(const ArgList &DriverArgs,
                                ArgStringList &CC1Args,
                                Action::OffloadKind DeviceOffloadKind) const {
  // Pass "-faligned-alloc-unavailable" only when the user hasn't manually
  // enabled or disabled aligned allocations.
  if (!DriverArgs.hasArgNoClaim(options::OPT_faligned_allocation,
                                options::OPT_fno_aligned_allocation))
    CC1Args.push_back("-faligned-alloc-unavailable");

  // Pass "-fno-sized-deallocation" only when the user hasn't manually enabled
  // or disabled sized deallocations.
  if (!DriverArgs.hasArgNoClaim(options::OPT_fsized_deallocation,
                                options::OPT_fno_sized_deallocation))
    CC1Args.push_back("-fno-sized-deallocation");
}

void zos::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  // Specify assembler output file.
  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  // Specify assembler input file.
  // The system assembler on z/OS takes exactly one input file. The driver is
  // expected to invoke as(1) separately for each assembler source input file.
  if (Inputs.size() != 1)
    llvm_unreachable("Invalid number of input files.");
  const InputInfo &II = Inputs[0];
  assert((II.isFilename() || II.isNothing()) && "Invalid input.");
  if (II.isFilename())
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs));
}

static std::string getLEHLQ(const ArgList &Args) {
  if (Args.hasArg(options::OPT_mzos_hlq_le_EQ)) {
    Arg *LEHLQArg = Args.getLastArg(options::OPT_mzos_hlq_le_EQ);
    StringRef HLQ = LEHLQArg->getValue();
    if (!HLQ.empty())
      return HLQ.str();
  }
  return "CEE";
}

static std::string getClangHLQ(const ArgList &Args) {
  if (Args.hasArg(options::OPT_mzos_hlq_clang_EQ)) {
    Arg *ClangHLQArg = Args.getLastArg(options::OPT_mzos_hlq_clang_EQ);
    StringRef HLQ = ClangHLQArg->getValue();
    if (!HLQ.empty())
      return HLQ.str();
  }
  return getLEHLQ(Args);
}

static std::string getCSSHLQ(const ArgList &Args) {
  if (Args.hasArg(options::OPT_mzos_hlq_csslib_EQ)) {
    Arg *CsslibHLQArg = Args.getLastArg(options::OPT_mzos_hlq_csslib_EQ);
    StringRef HLQ = CsslibHLQArg->getValue();
    if (!HLQ.empty())
      return HLQ.str();
  }
  return "SYS1";
}

void zos::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                               const InputInfo &Output,
                               const InputInfoList &Inputs, const ArgList &Args,
                               const char *LinkingOutput) const {
  const ZOS &ToolChain = static_cast<const ZOS &>(getToolChain());
  ArgStringList CmdArgs;

  const bool IsSharedLib =
      Args.hasFlag(options::OPT_shared, options::OPT_static, false);

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  SmallString<128> LinkerOptions;
  LinkerOptions = "AMODE=";
  LinkerOptions += "64";
  LinkerOptions += ",LIST";
  LinkerOptions += ",DYNAM=DLL";
  LinkerOptions += ",MSGLEVEL=4";
  LinkerOptions += ",CASE=MIXED";
  LinkerOptions += ",REUS=RENT";

  CmdArgs.push_back("-b");
  CmdArgs.push_back(Args.MakeArgString(LinkerOptions));

  if (!IsSharedLib) {
    CmdArgs.push_back("-e");
    CmdArgs.push_back("CELQSTRT");

    CmdArgs.push_back("-O");
    CmdArgs.push_back("CELQSTRT");

    CmdArgs.push_back("-u");
    CmdArgs.push_back("CELQMAIN");
  }

  // Generate side file if -shared option is present.
  if (IsSharedLib) {
    StringRef OutputName = Output.getFilename();
    // Strip away the last file suffix in presence from output name and add
    // a new .x suffix.
    size_t Suffix = OutputName.find_last_of('.');
    const char *SideDeckName =
        Args.MakeArgString(OutputName.substr(0, Suffix) + ".x");
    CmdArgs.push_back("-x");
    CmdArgs.push_back(SideDeckName);
  } else {
    // We need to direct side file to /dev/null to suppress linker warning when
    // the object file contains exported symbols, and -shared or
    // -Wl,-x<sidedeck>.x is not specified.
    CmdArgs.push_back("-x");
    CmdArgs.push_back("/dev/null");
  }

  // Add archive library search paths.
  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  // Specify linker input file(s)
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  //  z/OS tool chain depends on LE data sets and the CSSLIB data set.
  //  These data sets can have different high level qualifiers (HLQs)
  //  as each installation can define them differently.

  std::string LEHLQ = getLEHLQ(Args);
  std::string CsslibHLQ = getCSSHLQ(Args);

  StringRef ld_env_var = StringRef(getenv("_LD_SYSLIB")).trim();
  if (ld_env_var.empty()) {
    CmdArgs.push_back("-S");
    CmdArgs.push_back(Args.MakeArgString("//'" + LEHLQ + ".SCEEBND2'"));
    CmdArgs.push_back("-S");
    CmdArgs.push_back(Args.MakeArgString("//'" + CsslibHLQ + ".CSSLIB'"));
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    ld_env_var = StringRef(getenv("_LD_SIDE_DECKS")).trim();
    if (ld_env_var.empty()) {
      CmdArgs.push_back(
          Args.MakeArgString("//'" + LEHLQ + ".SCEELIB(CELQS001)'"));
      CmdArgs.push_back(
          Args.MakeArgString("//'" + LEHLQ + ".SCEELIB(CELQS003)'"));
    } else {
      SmallVector<StringRef> ld_side_deck;
      ld_env_var.split(ld_side_deck, ":");
      for (StringRef ld_loc : ld_side_deck) {
        CmdArgs.push_back((ld_loc.str()).c_str());
      }
    }
  }
  // Link libc++ library
  if (ToolChain.ShouldLinkCXXStdlib(Args)) {
    ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
  }

  // Specify compiler-rt library path for linker
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    AddRunTimeLibs(ToolChain, ToolChain.getDriver(), CmdArgs, Args);

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs));
}

ToolChain::RuntimeLibType ZOS::GetDefaultRuntimeLibType() const {
  return ToolChain::RLT_CompilerRT;
}

ToolChain::CXXStdlibType ZOS::GetDefaultCXXStdlibType() const {
  return ToolChain::CST_Libcxx;
}

void ZOS::AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                              llvm::opt::ArgStringList &CmdArgs) const {
  switch (GetCXXStdlibType(Args)) {
  case ToolChain::CST_Libstdcxx:
    llvm::report_fatal_error("linking libstdc++ is unimplemented on z/OS");
    break;
  case ToolChain::CST_Libcxx: {
    std::string ClangHLQ = getClangHLQ(Args);
    CmdArgs.push_back(
        Args.MakeArgString("//'" + ClangHLQ + ".SCEELIB(CRTDQCXE)'"));
    CmdArgs.push_back(
        Args.MakeArgString("//'" + ClangHLQ + ".SCEELIB(CRTDQCXS)'"));
    CmdArgs.push_back(
        Args.MakeArgString("//'" + ClangHLQ + ".SCEELIB(CRTDQCXP)'"));
    CmdArgs.push_back(
        Args.MakeArgString("//'" + ClangHLQ + ".SCEELIB(CRTDQCXA)'"));
    CmdArgs.push_back(
        Args.MakeArgString("//'" + ClangHLQ + ".SCEELIB(CRTDQXLA)'"));
    CmdArgs.push_back(
        Args.MakeArgString("//'" + ClangHLQ + ".SCEELIB(CRTDQUNW)'"));
  } break;
  }
}

auto ZOS::buildAssembler() const -> Tool * { return new zos::Assembler(*this); }

auto ZOS::buildLinker() const -> Tool * { return new zos::Linker(*this); }

void ZOS::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                    ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  const Driver &D = getDriver();

  // resolve ResourceDir
  std::string ResourceDir(D.ResourceDir);

  // zos_wrappers must take highest precedence

  // - <clang>/lib/clang/<ver>/include/zos_wrappers
  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(ResourceDir);
    path::append(P, "include", "zos_wrappers");
    addSystemInclude(DriverArgs, CC1Args, P.str());

    // - <clang>/lib/clang/<ver>/include
    SmallString<128> P2(ResourceDir);
    path::append(P2, "include");
    addSystemInclude(DriverArgs, CC1Args, P2.str());
  }

  // - /usr/include
  if (Arg *SysIncludeArg =
          DriverArgs.getLastArg(options::OPT_mzos_sys_include_EQ)) {
    StringRef SysInclude = SysIncludeArg->getValue();

    // fall back to the default include path
    if (!SysInclude.empty()) {

      // -mzos-sys-include opton can have colon separated
      // list of paths, so we need to parse the value.
      StringRef PathLE(SysInclude);
      size_t Colon = PathLE.find(':');
      if (Colon == StringRef::npos) {
        addSystemInclude(DriverArgs, CC1Args, PathLE.str());
        return;
      }

      while (Colon != StringRef::npos) {
        SmallString<128> P = PathLE.substr(0, Colon);
        addSystemInclude(DriverArgs, CC1Args, P.str());
        PathLE = PathLE.substr(Colon + 1);
        Colon = PathLE.find(':');
      }
      if (PathLE.size())
        addSystemInclude(DriverArgs, CC1Args, PathLE.str());

      return;
    }
  }

  addSystemInclude(DriverArgs, CC1Args, "/usr/include");
}

void ZOS::TryAddIncludeFromPath(llvm::SmallString<128> Path,
                                const llvm::opt::ArgList &DriverArgs,
                                llvm::opt::ArgStringList &CC1Args) const {
  if (!getVFS().exists(Path)) {
    if (DriverArgs.hasArg(options::OPT_v))
      WithColor::warning(errs(), "Clang")
          << "ignoring nonexistent directory \"" << Path << "\"\n";
    if (!DriverArgs.hasArg(options::OPT__HASH_HASH_HASH))
      return;
  }
  addSystemInclude(DriverArgs, CC1Args, Path);
}

void ZOS::AddClangCXXStdlibIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdincxx) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libcxx: {
    // <install>/bin/../include/c++/v1
    llvm::SmallString<128> InstallBin(getDriver().Dir);
    llvm::sys::path::append(InstallBin, "..", "include", "c++", "v1");
    TryAddIncludeFromPath(InstallBin, DriverArgs, CC1Args);
    break;
  }
  case ToolChain::CST_Libstdcxx:
    llvm::report_fatal_error(
        "picking up libstdc++ headers is unimplemented on z/OS");
    break;
  }
}
