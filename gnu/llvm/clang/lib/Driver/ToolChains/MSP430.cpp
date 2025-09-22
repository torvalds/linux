//===--- MSP430.cpp - MSP430 Helpers for Tools ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MSP430.h"
#include "CommonArgs.h"
#include "Gnu.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Multilib.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static bool isSupportedMCU(const StringRef MCU) {
  return llvm::StringSwitch<bool>(MCU)
#define MSP430_MCU(NAME) .Case(NAME, true)
#include "clang/Basic/MSP430Target.def"
      .Default(false);
}

static StringRef getSupportedHWMult(const Arg *MCU) {
  if (!MCU)
    return "none";

  return llvm::StringSwitch<StringRef>(MCU->getValue())
#define MSP430_MCU_FEAT(NAME, HWMULT) .Case(NAME, HWMULT)
#include "clang/Basic/MSP430Target.def"
      .Default("none");
}

static StringRef getHWMultLib(const ArgList &Args) {
  StringRef HWMult = Args.getLastArgValue(options::OPT_mhwmult_EQ, "auto");
  if (HWMult == "auto") {
    HWMult = getSupportedHWMult(Args.getLastArg(options::OPT_mmcu_EQ));
  }

  return llvm::StringSwitch<StringRef>(HWMult)
      .Case("16bit", "-lmul_16")
      .Case("32bit", "-lmul_32")
      .Case("f5series", "-lmul_f5")
      .Default("-lmul_none");
}

void msp430::getMSP430TargetFeatures(const Driver &D, const ArgList &Args,
                                     std::vector<StringRef> &Features) {
  const Arg *MCU = Args.getLastArg(options::OPT_mmcu_EQ);
  if (MCU && !isSupportedMCU(MCU->getValue())) {
    D.Diag(diag::err_drv_clang_unsupported) << MCU->getValue();
    return;
  }

  const Arg *HWMultArg = Args.getLastArg(options::OPT_mhwmult_EQ);
  if (!MCU && !HWMultArg)
    return;

  StringRef HWMult = HWMultArg ? HWMultArg->getValue() : "auto";
  StringRef SupportedHWMult = getSupportedHWMult(MCU);

  if (HWMult == "auto") {
    // 'auto' - deduce hw multiplier support based on mcu name provided.
    // If no mcu name is provided, assume no hw multiplier is supported.
    if (!MCU)
      D.Diag(clang::diag::warn_drv_msp430_hwmult_no_device);
    HWMult = SupportedHWMult;
  }

  if (HWMult == "none") {
    // 'none' - disable hw multiplier.
    Features.push_back("-hwmult16");
    Features.push_back("-hwmult32");
    Features.push_back("-hwmultf5");
    return;
  }

  if (MCU && SupportedHWMult == "none")
    D.Diag(clang::diag::warn_drv_msp430_hwmult_unsupported) << HWMult;
  if (MCU && HWMult != SupportedHWMult)
    D.Diag(clang::diag::warn_drv_msp430_hwmult_mismatch)
        << SupportedHWMult << HWMult;

  if (HWMult == "16bit") {
    // '16bit' - for 16-bit only hw multiplier.
    Features.push_back("+hwmult16");
  } else if (HWMult == "32bit") {
    // '32bit' - for 16/32-bit hw multiplier.
    Features.push_back("+hwmult32");
  } else if (HWMult == "f5series") {
    // 'f5series' - for 16/32-bit hw multiplier supported by F5 series mcus.
    Features.push_back("+hwmultf5");
  } else {
    D.Diag(clang::diag::err_drv_unsupported_option_argument)
        << HWMultArg->getSpelling() << HWMult;
  }
}

/// MSP430 Toolchain
MSP430ToolChain::MSP430ToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {

  StringRef MultilibSuf = "";

  GCCInstallation.init(Triple, Args);
  if (GCCInstallation.isValid()) {
    MultilibSuf = GCCInstallation.getMultilib().gccSuffix();

    SmallString<128> GCCBinPath;
    llvm::sys::path::append(GCCBinPath,
                            GCCInstallation.getParentLibPath(), "..", "bin");
    addPathIfExists(D, GCCBinPath, getProgramPaths());

    SmallString<128> GCCRtPath;
    llvm::sys::path::append(GCCRtPath,
                            GCCInstallation.getInstallPath(), MultilibSuf);
    addPathIfExists(D, GCCRtPath, getFilePaths());
  }

  SmallString<128> SysRootDir(computeSysRoot());
  llvm::sys::path::append(SysRootDir, "msp430-elf", "lib", MultilibSuf);
  addPathIfExists(D, SysRootDir, getFilePaths());
}

std::string MSP430ToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;

  SmallString<128> Dir;
  if (GCCInstallation.isValid())
    llvm::sys::path::append(Dir, GCCInstallation.getParentLibPath(), "..");
  else
    llvm::sys::path::append(Dir, getDriver().Dir, "..");

  return std::string(Dir);
}

void MSP430ToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "msp430-elf", "include");
  addSystemInclude(DriverArgs, CC1Args, Dir.str());
}

void MSP430ToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                            ArgStringList &CC1Args,
                                            Action::OffloadKind) const {
  CC1Args.push_back("-nostdsysteminc");

  const auto *MCUArg = DriverArgs.getLastArg(options::OPT_mmcu_EQ);
  if (!MCUArg)
    return;

  const StringRef MCU = MCUArg->getValue();
  if (MCU.starts_with("msp430i")) {
    // 'i' should be in lower case as it's defined in TI MSP430-GCC headers
    CC1Args.push_back(DriverArgs.MakeArgString(
        "-D__MSP430i" + MCU.drop_front(7).upper() + "__"));
  } else {
    CC1Args.push_back(DriverArgs.MakeArgString("-D__" + MCU.upper() + "__"));
  }
}

Tool *MSP430ToolChain::buildLinker() const {
  return new tools::msp430::Linker(*this);
}

void msp430::Linker::AddStartFiles(bool UseExceptions, const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  const ToolChain &ToolChain = getToolChain();

  CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
  const char *crtbegin = UseExceptions ? "crtbegin.o" : "crtbegin_no_eh.o";
  CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
}

void msp430::Linker::AddDefaultLibs(const llvm::opt::ArgList &Args,
                                    llvm::opt::ArgStringList &CmdArgs) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();

  CmdArgs.push_back("--start-group");
  CmdArgs.push_back(Args.MakeArgString(getHWMultLib(Args)));
  CmdArgs.push_back("-lc");
  AddRunTimeLibs(ToolChain, D, CmdArgs, Args);
  CmdArgs.push_back("-lcrt");

  if (Args.hasArg(options::OPT_msim)) {
    CmdArgs.push_back("-lsim");

    // msp430-sim.ld relies on __crt0_call_exit being implicitly .refsym-ed
    // in main() by msp430-gcc.
    // This workaround should work seamlessly unless the compilation unit that
    // contains main() is compiled by clang and then passed to
    // gcc compiler driver for linkage.
    CmdArgs.push_back("--undefined=__crt0_call_exit");
  } else
    CmdArgs.push_back("-lnosys");

  CmdArgs.push_back("--end-group");
  AddRunTimeLibs(ToolChain, D, CmdArgs, Args);
}

void msp430::Linker::AddEndFiles(bool UseExceptions, const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();

  const char *crtend = UseExceptions ? "crtend.o" : "crtend_no_eh.o";
  CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtend)));
  AddRunTimeLibs(ToolChain, D, CmdArgs, Args);
}

static void AddSspArgs(const ArgList &Args, ArgStringList &CmdArgs) {
  Arg *SspFlag = Args.getLastArg(
      options::OPT_fno_stack_protector, options::OPT_fstack_protector,
      options::OPT_fstack_protector_all, options::OPT_fstack_protector_strong);

  if (SspFlag &&
      !SspFlag->getOption().matches(options::OPT_fno_stack_protector)) {
    CmdArgs.push_back("-lssp_nonshared");
    CmdArgs.push_back("-lssp");
  }
}

static void AddImplicitLinkerScript(const std::string SysRoot,
                                    const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_T))
    return;

  if (Args.hasArg(options::OPT_msim)) {
    CmdArgs.push_back("-Tmsp430-sim.ld");
    return;
  }

  const Arg *MCUArg = Args.getLastArg(options::OPT_mmcu_EQ);
  if (!MCUArg)
    return;

  SmallString<128> MCULinkerScriptPath(SysRoot);
  llvm::sys::path::append(MCULinkerScriptPath, "include");
  // -L because <mcu>.ld INCLUDEs <mcu>_symbols.ld
  CmdArgs.push_back(Args.MakeArgString("-L" + MCULinkerScriptPath));
  CmdArgs.push_back(
      Args.MakeArgString("-T" + StringRef(MCUArg->getValue()) + ".ld"));
}

void msp430::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();
  std::string Linker = ToolChain.GetProgramPath(getShortName());
  ArgStringList CmdArgs;
  bool UseExceptions = Args.hasFlag(options::OPT_fexceptions,
                                    options::OPT_fno_exceptions, false);
  bool UseStartAndEndFiles = !Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                                          options::OPT_nostartfiles);

  if (Args.hasArg(options::OPT_mrelax))
    CmdArgs.push_back("--relax");
  if (!Args.hasArg(options::OPT_r, options::OPT_g_Group))
    CmdArgs.push_back("--gc-sections");

  Args.addAllArgs(CmdArgs, {
                               options::OPT_n,
                               options::OPT_s,
                               options::OPT_t,
                               options::OPT_u,
                           });

  if (UseStartAndEndFiles)
    AddStartFiles(UseExceptions, Args, CmdArgs);

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                   options::OPT_nodefaultlibs)) {
    AddSspArgs(Args, CmdArgs);
    AddRunTimeLibs(ToolChain, D, CmdArgs, Args);
    if (!Args.hasArg(options::OPT_nolibc)) {
      AddDefaultLibs(Args, CmdArgs);
      AddImplicitLinkerScript(D.SysRoot, Args, CmdArgs);
    }
  }

  if (UseStartAndEndFiles)
    AddEndFiles(UseExceptions, Args, CmdArgs);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  Args.AddAllArgs(CmdArgs, options::OPT_T);

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Args.MakeArgString(Linker),
      CmdArgs, Inputs, Output));
}
