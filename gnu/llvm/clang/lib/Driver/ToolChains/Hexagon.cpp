//===--- Hexagon.cpp - Hexagon ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

// Default hvx-length for various versions.
static StringRef getDefaultHvxLength(StringRef HvxVer) {
  return llvm::StringSwitch<StringRef>(HvxVer)
      .Case("v60", "64b")
      .Case("v62", "64b")
      .Case("v65", "64b")
      .Default("128b");
}

static void handleHVXWarnings(const Driver &D, const ArgList &Args) {
  // Handle the unsupported values passed to mhvx-length.
  if (Arg *A = Args.getLastArg(options::OPT_mhexagon_hvx_length_EQ)) {
    StringRef Val = A->getValue();
    if (!Val.equals_insensitive("64b") && !Val.equals_insensitive("128b"))
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }
}

// Handle hvx target features explicitly.
static void handleHVXTargetFeatures(const Driver &D, const ArgList &Args,
                                    std::vector<StringRef> &Features,
                                    StringRef Cpu, bool &HasHVX) {
  // Handle HVX warnings.
  handleHVXWarnings(D, Args);

  auto makeFeature = [&Args](Twine T, bool Enable) -> StringRef {
    const std::string &S = T.str();
    StringRef Opt(S);
    Opt.consume_back("=");
    if (Opt.starts_with("mno-"))
      Opt = Opt.drop_front(4);
    else if (Opt.starts_with("m"))
      Opt = Opt.drop_front(1);
    return Args.MakeArgString(Twine(Enable ? "+" : "-") + Twine(Opt));
  };

  auto withMinus = [](StringRef S) -> std::string {
    return "-" + S.str();
  };

  // Drop tiny core suffix for HVX version.
  std::string HvxVer =
      (Cpu.back() == 'T' || Cpu.back() == 't' ? Cpu.drop_back(1) : Cpu).str();
  HasHVX = false;

  // Handle -mhvx, -mhvx=, -mno-hvx. If versioned and versionless flags
  // are both present, the last one wins.
  Arg *HvxEnablingArg =
      Args.getLastArg(options::OPT_mhexagon_hvx, options::OPT_mhexagon_hvx_EQ,
                      options::OPT_mno_hexagon_hvx);
  if (HvxEnablingArg) {
    if (HvxEnablingArg->getOption().matches(options::OPT_mno_hexagon_hvx))
      HvxEnablingArg = nullptr;
  }

  if (HvxEnablingArg) {
    // If -mhvx[=] was given, it takes precedence.
    if (Arg *A = Args.getLastArg(options::OPT_mhexagon_hvx,
                                 options::OPT_mhexagon_hvx_EQ)) {
      // If the version was given, set HvxVer. Otherwise HvxVer
      // will remain equal to the CPU version.
      if (A->getOption().matches(options::OPT_mhexagon_hvx_EQ))
        HvxVer = StringRef(A->getValue()).lower();
    }
    HasHVX = true;
    Features.push_back(makeFeature(Twine("hvx") + HvxVer, true));
  } else if (Arg *A = Args.getLastArg(options::OPT_mno_hexagon_hvx)) {
    // If there was an explicit -mno-hvx, add -hvx to target features.
    Features.push_back(makeFeature(A->getOption().getName(), false));
  }

  StringRef HvxLen = getDefaultHvxLength(HvxVer);

  // Handle -mhvx-length=.
  if (Arg *A = Args.getLastArg(options::OPT_mhexagon_hvx_length_EQ)) {
    // These flags are valid only if HVX in enabled.
    if (!HasHVX)
      D.Diag(diag::err_drv_needs_hvx) << withMinus(A->getOption().getName());
    else if (A->getOption().matches(options::OPT_mhexagon_hvx_length_EQ))
      HvxLen = A->getValue();
  }

  if (HasHVX) {
    StringRef L = makeFeature(Twine("hvx-length") + HvxLen.lower(), true);
    Features.push_back(L);
  }

  unsigned HvxVerNum;
  // getAsInteger returns 'true' on error.
  if (StringRef(HvxVer).drop_front(1).getAsInteger(10, HvxVerNum))
    HvxVerNum = 0;

  // Handle HVX floating point flags.
  auto checkFlagHvxVersion =
      [&](auto FlagOn, auto FlagOff,
          unsigned MinVerNum) -> std::optional<StringRef> {
    // Return an std::optional<StringRef>:
    // - std::nullopt indicates a verification failure, or that the flag was not
    //   present in Args.
    // - Otherwise the returned value is that name of the feature to add
    //   to Features.
    Arg *A = Args.getLastArg(FlagOn, FlagOff);
    if (!A)
      return std::nullopt;

    StringRef OptName = A->getOption().getName();
    if (A->getOption().matches(FlagOff))
      return makeFeature(OptName, false);

    if (!HasHVX) {
      D.Diag(diag::err_drv_needs_hvx) << withMinus(OptName);
      return std::nullopt;
    }
    if (HvxVerNum < MinVerNum) {
      D.Diag(diag::err_drv_needs_hvx_version)
          << withMinus(OptName) << ("v" + std::to_string(HvxVerNum));
      return std::nullopt;
    }
    return makeFeature(OptName, true);
  };

  if (auto F = checkFlagHvxVersion(options::OPT_mhexagon_hvx_qfloat,
                                   options::OPT_mno_hexagon_hvx_qfloat, 68)) {
    Features.push_back(*F);
  }
  if (auto F = checkFlagHvxVersion(options::OPT_mhexagon_hvx_ieee_fp,
                                   options::OPT_mno_hexagon_hvx_ieee_fp, 68)) {
    Features.push_back(*F);
  }
}

// Hexagon target features.
void hexagon::getHexagonTargetFeatures(const Driver &D,
                                       const llvm::Triple &Triple,
                                       const ArgList &Args,
                                       std::vector<StringRef> &Features) {
  handleTargetFeaturesGroup(D, Triple, Args, Features,
                            options::OPT_m_hexagon_Features_Group);

  bool UseLongCalls = false;
  if (Arg *A = Args.getLastArg(options::OPT_mlong_calls,
                               options::OPT_mno_long_calls)) {
    if (A->getOption().matches(options::OPT_mlong_calls))
      UseLongCalls = true;
  }

  Features.push_back(UseLongCalls ? "+long-calls" : "-long-calls");

  bool HasHVX = false;
  StringRef Cpu(toolchains::HexagonToolChain::GetTargetCPUVersion(Args));
  // 't' in Cpu denotes tiny-core micro-architecture. For now, the co-processors
  // have no dependency on micro-architecture.
  const bool TinyCore = Cpu.contains('t');

  if (TinyCore)
    Cpu = Cpu.take_front(Cpu.size() - 1);

  handleHVXTargetFeatures(D, Args, Features, Cpu, HasHVX);

  if (HexagonToolChain::isAutoHVXEnabled(Args) && !HasHVX)
    D.Diag(diag::warn_drv_needs_hvx) << "auto-vectorization";
}

// Hexagon tools start.
void hexagon::Assembler::RenderExtraToolArgs(const JobAction &JA,
                                             ArgStringList &CmdArgs) const {
}

void hexagon::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                      const InputInfo &Output,
                                      const InputInfoList &Inputs,
                                      const ArgList &Args,
                                      const char *LinkingOutput) const {
  claimNoWarnArgs(Args);

  auto &HTC = static_cast<const toolchains::HexagonToolChain&>(getToolChain());
  const Driver &D = HTC.getDriver();
  ArgStringList CmdArgs;

  CmdArgs.push_back("--arch=hexagon");

  RenderExtraToolArgs(JA, CmdArgs);

  const char *AsName = "llvm-mc";
  CmdArgs.push_back("-filetype=obj");
  CmdArgs.push_back(Args.MakeArgString(
      "-mcpu=hexagon" +
      toolchains::HexagonToolChain::GetTargetCPUVersion(Args)));

  addSanitizerRuntimes(HTC, Args, CmdArgs);

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    CmdArgs.push_back("-fsyntax-only");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mhexagon_hvx_ieee_fp,
                               options::OPT_mno_hexagon_hvx_ieee_fp)) {
    if (A->getOption().matches(options::OPT_mhexagon_hvx_ieee_fp))
      CmdArgs.push_back("-mhvx-ieee-fp");
  }

  if (auto G = toolchains::HexagonToolChain::getSmallDataThreshold(Args)) {
    CmdArgs.push_back(Args.MakeArgString("-gpsize=" + Twine(*G)));
  }

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  // Only pass -x if gcc will understand it; otherwise hope gcc
  // understands the suffix correctly. The main use case this would go
  // wrong in is for linker inputs if they happened to have an odd
  // suffix; really the only way to get this to happen is a command
  // like '-x foobar a.c' which will treat a.c like a linker input.
  //
  // FIXME: For the linker case specifically, can we safely convert
  // inputs into '-Wl,' options?
  for (const auto &II : Inputs) {
    // Don't try to pass LLVM or AST inputs to a generic gcc.
    if (types::isLLVMIR(II.getType()))
      D.Diag(clang::diag::err_drv_no_linker_llvm_support)
          << HTC.getTripleString();
    else if (II.getType() == types::TY_AST)
      D.Diag(clang::diag::err_drv_no_ast_support)
          << HTC.getTripleString();
    else if (II.getType() == types::TY_ModuleFile)
      D.Diag(diag::err_drv_no_module_support)
          << HTC.getTripleString();

    if (II.isFilename())
      CmdArgs.push_back(II.getFilename());
    else
      // Don't render as input, we need gcc to do the translations.
      // FIXME: What is this?
      II.getInputArg().render(Args, CmdArgs);
  }

  auto *Exec = Args.MakeArgString(HTC.GetProgramPath(AsName));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

void hexagon::Linker::RenderExtraToolArgs(const JobAction &JA,
                                          ArgStringList &CmdArgs) const {
}

static void
constructHexagonLinkArgs(Compilation &C, const JobAction &JA,
                         const toolchains::HexagonToolChain &HTC,
                         const InputInfo &Output, const InputInfoList &Inputs,
                         const ArgList &Args, ArgStringList &CmdArgs,
                         const char *LinkingOutput) {

  const Driver &D = HTC.getDriver();

  //----------------------------------------------------------------------------
  //
  //----------------------------------------------------------------------------
  bool IsStatic = Args.hasArg(options::OPT_static);
  bool IsShared = Args.hasArg(options::OPT_shared);
  bool IsPIE = Args.hasArg(options::OPT_pie);
  bool IncStdLib = !Args.hasArg(options::OPT_nostdlib);
  bool IncStartFiles = !Args.hasArg(options::OPT_nostartfiles);
  bool IncDefLibs = !Args.hasArg(options::OPT_nodefaultlibs);
  bool UseG0 = false;
  bool UseLLD = false;
  const char *Exec = Args.MakeArgString(HTC.GetLinkerPath(&UseLLD));
  UseLLD = UseLLD || llvm::sys::path::filename(Exec).ends_with("ld.lld") ||
           llvm::sys::path::stem(Exec).ends_with("ld.lld");
  bool UseShared = IsShared && !IsStatic;
  StringRef CpuVer = toolchains::HexagonToolChain::GetTargetCPUVersion(Args);

  bool NeedsSanitizerDeps = addSanitizerRuntimes(HTC, Args, CmdArgs);
  bool NeedsXRayDeps = addXRayRuntime(HTC, Args, CmdArgs);

  //----------------------------------------------------------------------------
  // Silence warnings for various options
  //----------------------------------------------------------------------------
  Args.ClaimAllArgs(options::OPT_g_Group);
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  Args.ClaimAllArgs(options::OPT_w); // Other warning options are already
                                     // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_static_libgcc);

  //----------------------------------------------------------------------------
  //
  //----------------------------------------------------------------------------
  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("-s");

  if (Args.hasArg(options::OPT_r))
    CmdArgs.push_back("-r");

  for (const auto &Opt : HTC.ExtraOpts)
    CmdArgs.push_back(Opt.c_str());

  if (!UseLLD) {
    CmdArgs.push_back("-march=hexagon");
    CmdArgs.push_back(Args.MakeArgString("-mcpu=hexagon" + CpuVer));
  }

  if (IsShared) {
    CmdArgs.push_back("-shared");
    // The following should be the default, but doing as hexagon-gcc does.
    CmdArgs.push_back("-call_shared");
  }

  if (IsStatic)
    CmdArgs.push_back("-static");

  if (IsPIE && !IsShared)
    CmdArgs.push_back("-pie");

  if (auto G = toolchains::HexagonToolChain::getSmallDataThreshold(Args)) {
    CmdArgs.push_back(Args.MakeArgString("-G" + Twine(*G)));
    UseG0 = *G == 0;
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (HTC.getTriple().isMusl()) {
    if (!Args.hasArg(options::OPT_shared, options::OPT_static))
      CmdArgs.push_back("-dynamic-linker=/lib/ld-musl-hexagon.so.1");

    if (!Args.hasArg(options::OPT_shared, options::OPT_nostartfiles,
                     options::OPT_nostdlib))
      CmdArgs.push_back(Args.MakeArgString(D.SysRoot + "/usr/lib/crt1.o"));
    else if (Args.hasArg(options::OPT_shared) &&
             !Args.hasArg(options::OPT_nostartfiles, options::OPT_nostdlib))
      CmdArgs.push_back(Args.MakeArgString(D.SysRoot + "/usr/lib/crti.o"));

    CmdArgs.push_back(
        Args.MakeArgString(StringRef("-L") + D.SysRoot + "/usr/lib"));
    Args.addAllArgs(CmdArgs, {options::OPT_T_Group, options::OPT_s,
                              options::OPT_t, options::OPT_u_Group});
    AddLinkerInputs(HTC, Inputs, Args, CmdArgs, JA);

    ToolChain::UnwindLibType UNW = HTC.GetUnwindLibType(Args);

    if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
      if (NeedsSanitizerDeps) {
        linkSanitizerRuntimeDeps(HTC, Args, CmdArgs);

        if (UNW != ToolChain::UNW_None)
          CmdArgs.push_back("-lunwind");
      }
      if (NeedsXRayDeps)
        linkXRayRuntimeDeps(HTC, Args, CmdArgs);

      if (!Args.hasArg(options::OPT_nolibc))
        CmdArgs.push_back("-lc");
      CmdArgs.push_back("-lclang_rt.builtins-hexagon");
    }
    if (D.CCCIsCXX()) {
      if (HTC.ShouldLinkCXXStdlib(Args))
        HTC.AddCXXStdlibLibArgs(Args, CmdArgs);
    }
    const ToolChain::path_list &LibPaths = HTC.getFilePaths();
    for (const auto &LibPath : LibPaths)
      CmdArgs.push_back(Args.MakeArgString(StringRef("-L") + LibPath));
    Args.ClaimAllArgs(options::OPT_L);
    return;
  }

  //----------------------------------------------------------------------------
  // moslib
  //----------------------------------------------------------------------------
  std::vector<std::string> OsLibs;
  bool HasStandalone = false;
  for (const Arg *A : Args.filtered(options::OPT_moslib_EQ)) {
    A->claim();
    OsLibs.emplace_back(A->getValue());
    HasStandalone = HasStandalone || (OsLibs.back() == "standalone");
  }
  if (OsLibs.empty()) {
    OsLibs.push_back("standalone");
    HasStandalone = true;
  }

  //----------------------------------------------------------------------------
  // Start Files
  //----------------------------------------------------------------------------
  const std::string MCpuSuffix = "/" + CpuVer.str();
  const std::string MCpuG0Suffix = MCpuSuffix + "/G0";
  const std::string RootDir =
      HTC.getHexagonTargetDir(D.Dir, D.PrefixDirs) + "/";
  const std::string StartSubDir =
      "hexagon/lib" + (UseG0 ? MCpuG0Suffix : MCpuSuffix);

  auto Find = [&HTC] (const std::string &RootDir, const std::string &SubDir,
                      const char *Name) -> std::string {
    std::string RelName = SubDir + Name;
    std::string P = HTC.GetFilePath(RelName.c_str());
    if (llvm::sys::fs::exists(P))
      return P;
    return RootDir + RelName;
  };

  if (IncStdLib && IncStartFiles) {
    if (!IsShared) {
      if (HasStandalone) {
        std::string Crt0SA = Find(RootDir, StartSubDir, "/crt0_standalone.o");
        CmdArgs.push_back(Args.MakeArgString(Crt0SA));
      }
      std::string Crt0 = Find(RootDir, StartSubDir, "/crt0.o");
      CmdArgs.push_back(Args.MakeArgString(Crt0));
    }
    std::string Init = UseShared
          ? Find(RootDir, StartSubDir + "/pic", "/initS.o")
          : Find(RootDir, StartSubDir, "/init.o");
    CmdArgs.push_back(Args.MakeArgString(Init));
  }

  //----------------------------------------------------------------------------
  // Library Search Paths
  //----------------------------------------------------------------------------
  const ToolChain::path_list &LibPaths = HTC.getFilePaths();
  for (const auto &LibPath : LibPaths)
    CmdArgs.push_back(Args.MakeArgString(StringRef("-L") + LibPath));
  Args.ClaimAllArgs(options::OPT_L);

  //----------------------------------------------------------------------------
  //
  //----------------------------------------------------------------------------
  Args.addAllArgs(CmdArgs, {options::OPT_T_Group, options::OPT_s,
                            options::OPT_t, options::OPT_u_Group});

  AddLinkerInputs(HTC, Inputs, Args, CmdArgs, JA);

  //----------------------------------------------------------------------------
  // Libraries
  //----------------------------------------------------------------------------
  if (IncStdLib && IncDefLibs) {
    if (D.CCCIsCXX()) {
      if (HTC.ShouldLinkCXXStdlib(Args))
        HTC.AddCXXStdlibLibArgs(Args, CmdArgs);
      CmdArgs.push_back("-lm");
    }

    CmdArgs.push_back("--start-group");

    if (!IsShared) {
      for (StringRef Lib : OsLibs)
        CmdArgs.push_back(Args.MakeArgString("-l" + Lib));
      if (!Args.hasArg(options::OPT_nolibc))
        CmdArgs.push_back("-lc");
    }
    CmdArgs.push_back("-lgcc");

    CmdArgs.push_back("--end-group");
  }

  //----------------------------------------------------------------------------
  // End files
  //----------------------------------------------------------------------------
  if (IncStdLib && IncStartFiles) {
    std::string Fini = UseShared
          ? Find(RootDir, StartSubDir + "/pic", "/finiS.o")
          : Find(RootDir, StartSubDir, "/fini.o");
    CmdArgs.push_back(Args.MakeArgString(Fini));
  }
}

void hexagon::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  auto &HTC = static_cast<const toolchains::HexagonToolChain&>(getToolChain());

  ArgStringList CmdArgs;
  constructHexagonLinkArgs(C, JA, HTC, Output, Inputs, Args, CmdArgs,
                           LinkingOutput);

  const char *Exec = Args.MakeArgString(HTC.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}
// Hexagon tools end.

/// Hexagon Toolchain

std::string HexagonToolChain::getHexagonTargetDir(
      const std::string &InstalledDir,
      const SmallVectorImpl<std::string> &PrefixDirs) const {
  std::string InstallRelDir;
  const Driver &D = getDriver();

  // Locate the rest of the toolchain ...
  for (auto &I : PrefixDirs)
    if (D.getVFS().exists(I))
      return I;

  if (getVFS().exists(InstallRelDir = InstalledDir + "/../target"))
    return InstallRelDir;

  return InstalledDir;
}

std::optional<unsigned>
HexagonToolChain::getSmallDataThreshold(const ArgList &Args) {
  StringRef Gn = "";
  if (Arg *A = Args.getLastArg(options::OPT_G)) {
    Gn = A->getValue();
  } else if (Args.getLastArg(options::OPT_shared, options::OPT_fpic,
                             options::OPT_fPIC)) {
    Gn = "0";
  }

  unsigned G;
  if (!Gn.getAsInteger(10, G))
    return G;

  return std::nullopt;
}

std::string HexagonToolChain::getCompilerRTPath() const {
  SmallString<128> Dir(getDriver().SysRoot);
  llvm::sys::path::append(Dir, "usr", "lib");
  if (!SelectedMultilibs.empty()) {
    Dir += SelectedMultilibs.back().gccSuffix();
  }
  return std::string(Dir);
}

void HexagonToolChain::getHexagonLibraryPaths(const ArgList &Args,
      ToolChain::path_list &LibPaths) const {
  const Driver &D = getDriver();

  //----------------------------------------------------------------------------
  // -L Args
  //----------------------------------------------------------------------------
  for (Arg *A : Args.filtered(options::OPT_L))
    llvm::append_range(LibPaths, A->getValues());

  //----------------------------------------------------------------------------
  // Other standard paths
  //----------------------------------------------------------------------------
  std::vector<std::string> RootDirs;
  std::copy(D.PrefixDirs.begin(), D.PrefixDirs.end(),
            std::back_inserter(RootDirs));

  std::string TargetDir = getHexagonTargetDir(D.Dir, D.PrefixDirs);
  if (!llvm::is_contained(RootDirs, TargetDir))
    RootDirs.push_back(TargetDir);

  bool HasPIC = Args.hasArg(options::OPT_fpic, options::OPT_fPIC);
  // Assume G0 with -shared.
  bool HasG0 = Args.hasArg(options::OPT_shared);
  if (auto G = getSmallDataThreshold(Args))
    HasG0 = *G == 0;

  const std::string CpuVer = GetTargetCPUVersion(Args).str();
  for (auto &Dir : RootDirs) {
    std::string LibDir = Dir + "/hexagon/lib";
    std::string LibDirCpu = LibDir + '/' + CpuVer;
    if (HasG0) {
      if (HasPIC)
        LibPaths.push_back(LibDirCpu + "/G0/pic");
      LibPaths.push_back(LibDirCpu + "/G0");
    }
    LibPaths.push_back(LibDirCpu);
    LibPaths.push_back(LibDir);
  }
}

HexagonToolChain::HexagonToolChain(const Driver &D, const llvm::Triple &Triple,
                                   const llvm::opt::ArgList &Args)
    : Linux(D, Triple, Args) {
  const std::string TargetDir = getHexagonTargetDir(D.Dir, D.PrefixDirs);

  // Note: Generic_GCC::Generic_GCC adds InstalledDir and getDriver().Dir to
  // program paths
  const std::string BinDir(TargetDir + "/bin");
  if (D.getVFS().exists(BinDir))
    getProgramPaths().push_back(BinDir);

  ToolChain::path_list &LibPaths = getFilePaths();

  // Remove paths added by Linux toolchain. Currently Hexagon_TC really targets
  // 'elf' OS type, so the Linux paths are not appropriate. When we actually
  // support 'linux' we'll need to fix this up
  LibPaths.clear();
  getHexagonLibraryPaths(Args, LibPaths);
}

HexagonToolChain::~HexagonToolChain() {}

void HexagonToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                           ArgStringList &CmdArgs) const {
  CXXStdlibType Type = GetCXXStdlibType(Args);
  ToolChain::UnwindLibType UNW = GetUnwindLibType(Args);
  if (UNW != ToolChain::UNW_None && UNW != ToolChain::UNW_CompilerRT) {
    const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ);
    if (A) {
      getDriver().Diag(diag::err_drv_unsupported_unwind_for_platform)
          << A->getValue() << getTriple().normalize();
      return;
    }
  }

  switch (Type) {
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    if (Args.hasArg(options::OPT_fexperimental_library))
      CmdArgs.push_back("-lc++experimental");
    CmdArgs.push_back("-lc++abi");
    if (UNW != ToolChain::UNW_None)
      CmdArgs.push_back("-lunwind");
    break;

  case ToolChain::CST_Libstdcxx:
    CmdArgs.push_back("-lstdc++");
    break;
  }
}

Tool *HexagonToolChain::buildAssembler() const {
  return new tools::hexagon::Assembler(*this);
}

Tool *HexagonToolChain::buildLinker() const {
  return new tools::hexagon::Linker(*this);
}

unsigned HexagonToolChain::getOptimizationLevel(
    const llvm::opt::ArgList &DriverArgs) const {
  // Copied in large part from lib/Frontend/CompilerInvocation.cpp.
  Arg *A = DriverArgs.getLastArg(options::OPT_O_Group);
  if (!A)
    return 0;

  if (A->getOption().matches(options::OPT_O0))
    return 0;
  if (A->getOption().matches(options::OPT_Ofast) ||
      A->getOption().matches(options::OPT_O4))
    return 3;
  assert(A->getNumValues() != 0);
  StringRef S(A->getValue());
  if (S == "s" || S == "z" || S.empty())
    return 2;
  if (S == "g")
    return 1;

  unsigned OptLevel;
  if (S.getAsInteger(10, OptLevel))
    return 0;
  return OptLevel;
}

void HexagonToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                             ArgStringList &CC1Args,
                                             Action::OffloadKind) const {

  bool UseInitArrayDefault = getTriple().isMusl();

  if (!DriverArgs.hasFlag(options::OPT_fuse_init_array,
                          options::OPT_fno_use_init_array,
                          UseInitArrayDefault))
    CC1Args.push_back("-fno-use-init-array");

  if (DriverArgs.hasArg(options::OPT_ffixed_r19)) {
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+reserved-r19");
  }
  if (isAutoHVXEnabled(DriverArgs)) {
    CC1Args.push_back("-mllvm");
    CC1Args.push_back("-hexagon-autohvx");
  }
}

void HexagonToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                 ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  const bool IsELF = !getTriple().isMusl() && !getTriple().isOSLinux();
  const bool IsLinuxMusl = getTriple().isMusl() && getTriple().isOSLinux();

  const Driver &D = getDriver();
  SmallString<128> ResourceDirInclude(D.ResourceDir);
  if (!IsELF) {
    llvm::sys::path::append(ResourceDirInclude, "include");
    if (!DriverArgs.hasArg(options::OPT_nobuiltininc) &&
        (!IsLinuxMusl || DriverArgs.hasArg(options::OPT_nostdlibinc)))
      addSystemInclude(DriverArgs, CC1Args, ResourceDirInclude);
  }
  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  const bool HasSysRoot = !D.SysRoot.empty();
  if (HasSysRoot) {
    SmallString<128> P(D.SysRoot);
    if (IsLinuxMusl)
      llvm::sys::path::append(P, "usr/include");
    else
      llvm::sys::path::append(P, "include");

    addExternCSystemInclude(DriverArgs, CC1Args, P.str());
    // LOCAL_INCLUDE_DIR
    addSystemInclude(DriverArgs, CC1Args, P + "/usr/local/include");
    // TOOL_INCLUDE_DIR
    AddMultilibIncludeArgs(DriverArgs, CC1Args);
  }

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc) && IsLinuxMusl)
    addSystemInclude(DriverArgs, CC1Args, ResourceDirInclude);

  if (HasSysRoot)
    return;
  std::string TargetDir = getHexagonTargetDir(D.Dir, D.PrefixDirs);
  addExternCSystemInclude(DriverArgs, CC1Args, TargetDir + "/hexagon/include");
}

void HexagonToolChain::addLibCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  if (!D.SysRoot.empty() && getTriple().isMusl())
    addLibStdCXXIncludePaths(D.SysRoot + "/usr/include/c++/v1", "", "",
                             DriverArgs, CC1Args);
  else if (getTriple().isMusl())
    addLibStdCXXIncludePaths("/usr/include/c++/v1", "", "", DriverArgs,
                             CC1Args);
  else {
    std::string TargetDir = getHexagonTargetDir(D.Dir, D.PrefixDirs);
    addLibStdCXXIncludePaths(TargetDir + "/hexagon/include/c++/v1", "", "",
                             DriverArgs, CC1Args);
  }
}
void HexagonToolChain::addLibStdCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  std::string TargetDir = getHexagonTargetDir(D.Dir, D.PrefixDirs);
  addLibStdCXXIncludePaths(TargetDir + "/hexagon/include/c++", "", "",
                           DriverArgs, CC1Args);
}

ToolChain::CXXStdlibType
HexagonToolChain::GetCXXStdlibType(const ArgList &Args) const {
  Arg *A = Args.getLastArg(options::OPT_stdlib_EQ);
  if (!A) {
    if (getTriple().isMusl())
      return ToolChain::CST_Libcxx;
    else
      return ToolChain::CST_Libstdcxx;
  }
  StringRef Value = A->getValue();
  if (Value != "libstdc++" && Value != "libc++")
    getDriver().Diag(diag::err_drv_invalid_stdlib_name) << A->getAsString(Args);

  if (Value == "libstdc++")
    return ToolChain::CST_Libstdcxx;
  else if (Value == "libc++")
    return ToolChain::CST_Libcxx;
  else
    return ToolChain::CST_Libstdcxx;
}

bool HexagonToolChain::isAutoHVXEnabled(const llvm::opt::ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_fvectorize,
                               options::OPT_fno_vectorize))
    return A->getOption().matches(options::OPT_fvectorize);
  return false;
}

//
// Returns the default CPU for Hexagon. This is the default compilation target
// if no Hexagon processor is selected at the command-line.
//
StringRef HexagonToolChain::GetDefaultCPU() {
  return "hexagonv60";
}

StringRef HexagonToolChain::GetTargetCPUVersion(const ArgList &Args) {
  Arg *CpuArg = nullptr;
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
    CpuArg = A;

  StringRef CPU = CpuArg ? CpuArg->getValue() : GetDefaultCPU();
  CPU.consume_front("hexagon");
  return CPU;
}
