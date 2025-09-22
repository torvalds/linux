//===--- PS4CPU.cpp - PS4CPU ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PS4CPU.h"
#include "CommonArgs.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <cstdlib> // ::getenv

using namespace clang::driver;
using namespace clang;
using namespace llvm::opt;

// Helper to paste bits of an option together and return a saved string.
static const char *makeArgString(const ArgList &Args, const char *Prefix,
                                 const char *Base, const char *Suffix) {
  // Basically "Prefix + Base + Suffix" all converted to Twine then saved.
  return Args.MakeArgString(Twine(StringRef(Prefix), Base) + Suffix);
}

void tools::PScpu::addProfileRTArgs(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  assert(TC.getTriple().isPS());
  auto &PSTC = static_cast<const toolchains::PS4PS5Base &>(TC);

  if ((Args.hasFlag(options::OPT_fprofile_arcs, options::OPT_fno_profile_arcs,
                    false) ||
       Args.hasFlag(options::OPT_fprofile_generate,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_generate_EQ,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_instr_generate,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_instr_generate_EQ,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fcs_profile_generate,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasFlag(options::OPT_fcs_profile_generate_EQ,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasArg(options::OPT_fcreate_profile) ||
       Args.hasArg(options::OPT_coverage)))
    CmdArgs.push_back(makeArgString(
        Args, "--dependent-lib=", PSTC.getProfileRTLibName(), ""));
}

void tools::PScpu::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  auto &TC = static_cast<const toolchains::PS4PS5Base &>(getToolChain());
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  std::string AsName = TC.qualifyPSCmdName("as");
  const char *Exec = Args.MakeArgString(TC.GetProgramPath(AsName.c_str()));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileUTF8(),
                                         Exec, CmdArgs, Inputs, Output));
}

void tools::PScpu::addSanitizerArgs(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  assert(TC.getTriple().isPS());
  auto &PSTC = static_cast<const toolchains::PS4PS5Base &>(TC);
  PSTC.addSanitizerArgs(Args, CmdArgs, "--dependent-lib=lib", ".a");
}

void toolchains::PS4CPU::addSanitizerArgs(const ArgList &Args,
                                          ArgStringList &CmdArgs,
                                          const char *Prefix,
                                          const char *Suffix) const {
  auto arg = [&](const char *Name) -> const char * {
    return makeArgString(Args, Prefix, Name, Suffix);
  };
  const SanitizerArgs &SanArgs = getSanitizerArgs(Args);
  if (SanArgs.needsUbsanRt())
    CmdArgs.push_back(arg("SceDbgUBSanitizer_stub_weak"));
  if (SanArgs.needsAsanRt())
    CmdArgs.push_back(arg("SceDbgAddressSanitizer_stub_weak"));
}

void toolchains::PS5CPU::addSanitizerArgs(const ArgList &Args,
                                          ArgStringList &CmdArgs,
                                          const char *Prefix,
                                          const char *Suffix) const {
  auto arg = [&](const char *Name) -> const char * {
    return makeArgString(Args, Prefix, Name, Suffix);
  };
  const SanitizerArgs &SanArgs = getSanitizerArgs(Args);
  if (SanArgs.needsUbsanRt())
    CmdArgs.push_back(arg("SceUBSanitizer_nosubmission_stub_weak"));
  if (SanArgs.needsAsanRt())
    CmdArgs.push_back(arg("SceAddressSanitizer_nosubmission_stub_weak"));
  if (SanArgs.needsTsanRt())
    CmdArgs.push_back(arg("SceThreadSanitizer_nosubmission_stub_weak"));
}

void tools::PS4cpu::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
                                         const char *LinkingOutput) const {
  auto &TC = static_cast<const toolchains::PS4PS5Base &>(getToolChain());
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  if (Args.hasArg(options::OPT_pie))
    CmdArgs.push_back("-pie");

  if (Args.hasArg(options::OPT_rdynamic))
    CmdArgs.push_back("-export-dynamic");
  if (Args.hasArg(options::OPT_shared))
    CmdArgs.push_back("--shared");

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  const bool UseLTO = D.isUsingLTO();
  const bool UseJMC =
      Args.hasFlag(options::OPT_fjmc, options::OPT_fno_jmc, false);

  const char *LTOArgs = "";
  auto AddCodeGenFlag = [&](Twine Flag) {
    LTOArgs = Args.MakeArgString(Twine(LTOArgs) + " " + Flag);
  };

  if (UseLTO) {
    // We default to creating the arange section, but LTO does not. Enable it
    // here.
    AddCodeGenFlag("-generate-arange-section");

    // This tells LTO to perform JustMyCode instrumentation.
    if (UseJMC)
      AddCodeGenFlag("-enable-jmc-instrument");

    if (Arg *A = Args.getLastArg(options::OPT_fcrash_diagnostics_dir))
      AddCodeGenFlag(Twine("-crash-diagnostics-dir=") + A->getValue());

    StringRef Parallelism = getLTOParallelism(Args, D);
    if (!Parallelism.empty())
      AddCodeGenFlag(Twine("-threads=") + Parallelism);

    const char *Prefix = nullptr;
    if (D.getLTOMode() == LTOK_Thin)
      Prefix = "-lto-thin-debug-options=";
    else if (D.getLTOMode() == LTOK_Full)
      Prefix = "-lto-debug-options=";
    else
      llvm_unreachable("new LTO mode?");

    CmdArgs.push_back(Args.MakeArgString(Twine(Prefix) + LTOArgs));
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    TC.addSanitizerArgs(Args, CmdArgs, "-l", "");

  if (D.isUsingLTO() && Args.hasArg(options::OPT_funified_lto)) {
    if (D.getLTOMode() == LTOK_Thin)
      CmdArgs.push_back("--lto=thin");
    else if (D.getLTOMode() == LTOK_Full)
      CmdArgs.push_back("--lto=full");
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group,
                            options::OPT_s, options::OPT_t});

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (Args.hasArg(options::OPT_pthread)) {
    CmdArgs.push_back("-lpthread");
  }

  if (UseJMC) {
    CmdArgs.push_back("--whole-archive");
    CmdArgs.push_back("-lSceDbgJmc");
    CmdArgs.push_back("--no-whole-archive");
  }

  if (Args.hasArg(options::OPT_fuse_ld_EQ)) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << "-fuse-ld" << TC.getTriple().str();
  }

  std::string LdName = TC.qualifyPSCmdName(TC.getLinkerBaseName());
  const char *Exec = Args.MakeArgString(TC.GetProgramPath(LdName.c_str()));

  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileUTF8(),
                                         Exec, CmdArgs, Inputs, Output));
}

void tools::PS5cpu::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
                                         const char *LinkingOutput) const {
  auto &TC = static_cast<const toolchains::PS4PS5Base &>(getToolChain());
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  if (Args.hasArg(options::OPT_pie))
    CmdArgs.push_back("-pie");

  if (Args.hasArg(options::OPT_rdynamic))
    CmdArgs.push_back("-export-dynamic");
  if (Args.hasArg(options::OPT_shared))
    CmdArgs.push_back("--shared");

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  const bool UseLTO = D.isUsingLTO();
  const bool UseJMC =
      Args.hasFlag(options::OPT_fjmc, options::OPT_fno_jmc, false);

  auto AddCodeGenFlag = [&](Twine Flag) {
    CmdArgs.push_back(Args.MakeArgString(Twine("-plugin-opt=") + Flag));
  };

  if (UseLTO) {
    // We default to creating the arange section, but LTO does not. Enable it
    // here.
    AddCodeGenFlag("-generate-arange-section");

    // This tells LTO to perform JustMyCode instrumentation.
    if (UseJMC)
      AddCodeGenFlag("-enable-jmc-instrument");

    if (Arg *A = Args.getLastArg(options::OPT_fcrash_diagnostics_dir))
      AddCodeGenFlag(Twine("-crash-diagnostics-dir=") + A->getValue());

    StringRef Parallelism = getLTOParallelism(Args, D);
    if (!Parallelism.empty())
      CmdArgs.push_back(Args.MakeArgString(Twine("-plugin-opt=jobs=") + Parallelism));
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    TC.addSanitizerArgs(Args, CmdArgs, "-l", "");

  if (D.isUsingLTO() && Args.hasArg(options::OPT_funified_lto)) {
    if (D.getLTOMode() == LTOK_Thin)
      CmdArgs.push_back("--lto=thin");
    else if (D.getLTOMode() == LTOK_Full)
      CmdArgs.push_back("--lto=full");
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group,
                            options::OPT_s, options::OPT_t});

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (Args.hasArg(options::OPT_pthread)) {
    CmdArgs.push_back("-lpthread");
  }

  if (UseJMC) {
    CmdArgs.push_back("--whole-archive");
    CmdArgs.push_back("-lSceJmc_nosubmission");
    CmdArgs.push_back("--no-whole-archive");
  }

  if (Args.hasArg(options::OPT_fuse_ld_EQ)) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << "-fuse-ld" << TC.getTriple().str();
  }

  std::string LdName = TC.qualifyPSCmdName(TC.getLinkerBaseName());
  const char *Exec = Args.MakeArgString(TC.GetProgramPath(LdName.c_str()));

  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileUTF8(),
                                         Exec, CmdArgs, Inputs, Output));
}

toolchains::PS4PS5Base::PS4PS5Base(const Driver &D, const llvm::Triple &Triple,
                                   const ArgList &Args, StringRef Platform,
                                   const char *EnvVar)
    : Generic_ELF(D, Triple, Args) {
  if (Args.hasArg(clang::driver::options::OPT_static))
    D.Diag(clang::diag::err_drv_unsupported_opt_for_target)
        << "-static" << Platform;

  // Determine where to find the PS4/PS5 libraries.
  // If -isysroot was passed, use that as the SDK base path.
  // If not, we use the EnvVar if it exists; otherwise use the driver's
  // installation path, which should be <SDK_DIR>/host_tools/bin.
  SmallString<80> Whence;
  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    SDKRootDir = A->getValue();
    if (!llvm::sys::fs::exists(SDKRootDir))
      D.Diag(clang::diag::warn_missing_sysroot) << SDKRootDir;
    Whence = A->getSpelling();
  } else if (const char *EnvValue = getenv(EnvVar)) {
    SDKRootDir = EnvValue;
    Whence = { "environment variable '", EnvVar, "'" };
  } else {
    SDKRootDir = D.Dir + "/../../";
    Whence = "compiler's location";
  }

  SmallString<512> SDKIncludeDir(SDKRootDir);
  llvm::sys::path::append(SDKIncludeDir, "target/include");
  if (!Args.hasArg(options::OPT_nostdinc) &&
      !Args.hasArg(options::OPT_nostdlibinc) &&
      !Args.hasArg(options::OPT_isysroot) &&
      !Args.hasArg(options::OPT__sysroot_EQ) &&
      !llvm::sys::fs::exists(SDKIncludeDir)) {
    D.Diag(clang::diag::warn_drv_unable_to_find_directory_expected)
        << Twine(Platform, " system headers").str() << SDKIncludeDir << Whence;
  }

  SmallString<512> SDKLibDir(SDKRootDir);
  llvm::sys::path::append(SDKLibDir, "target/lib");
  if (!Args.hasArg(options::OPT_nostdlib) &&
      !Args.hasArg(options::OPT_nodefaultlibs) &&
      !Args.hasArg(options::OPT__sysroot_EQ) && !Args.hasArg(options::OPT_E) &&
      !Args.hasArg(options::OPT_c) && !Args.hasArg(options::OPT_S) &&
      !Args.hasArg(options::OPT_emit_ast) &&
      !llvm::sys::fs::exists(SDKLibDir)) {
    D.Diag(clang::diag::warn_drv_unable_to_find_directory_expected)
        << Twine(Platform, " system libraries").str() << SDKLibDir << Whence;
    return;
  }
  getFilePaths().push_back(std::string(SDKLibDir));
}

void toolchains::PS4PS5Base::AddClangSystemIncludeArgs(
    const ArgList &DriverArgs,
    ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> Dir(D.ResourceDir);
    llvm::sys::path::append(Dir, "include");
    addSystemInclude(DriverArgs, CC1Args, Dir.str());
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  addExternCSystemInclude(DriverArgs, CC1Args,
                          SDKRootDir + "/target/include");
  addExternCSystemInclude(DriverArgs, CC1Args,
                          SDKRootDir + "/target/include_common");
}

Tool *toolchains::PS4CPU::buildAssembler() const {
  return new tools::PScpu::Assembler(*this);
}

Tool *toolchains::PS4CPU::buildLinker() const {
  return new tools::PS4cpu::Linker(*this);
}

Tool *toolchains::PS5CPU::buildAssembler() const {
  // PS5 does not support an external assembler.
  getDriver().Diag(clang::diag::err_no_external_assembler);
  return nullptr;
}

Tool *toolchains::PS5CPU::buildLinker() const {
  return new tools::PS5cpu::Linker(*this);
}

SanitizerMask toolchains::PS4PS5Base::getSupportedSanitizers() const {
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  Res |= SanitizerKind::Address;
  Res |= SanitizerKind::PointerCompare;
  Res |= SanitizerKind::PointerSubtract;
  Res |= SanitizerKind::Vptr;
  return Res;
}

SanitizerMask toolchains::PS5CPU::getSupportedSanitizers() const {
  SanitizerMask Res = PS4PS5Base::getSupportedSanitizers();
  Res |= SanitizerKind::Thread;
  return Res;
}

void toolchains::PS4PS5Base::addClangTargetOptions(
    const ArgList &DriverArgs, ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  // PS4/PS5 do not use init arrays.
  if (DriverArgs.hasArg(options::OPT_fuse_init_array)) {
    Arg *A = DriverArgs.getLastArg(options::OPT_fuse_init_array);
    getDriver().Diag(clang::diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(DriverArgs) << getTriple().str();
  }

  CC1Args.push_back("-fno-use-init-array");

  // Default to `hidden` visibility for PS5.
  if (getTriple().isPS5() &&
      !DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat))
    CC1Args.push_back("-fvisibility=hidden");

  // Default to -fvisibility-global-new-delete=source for PS5.
  if (getTriple().isPS5() &&
      !DriverArgs.hasArg(options::OPT_fvisibility_global_new_delete_EQ,
                         options::OPT_fvisibility_global_new_delete_hidden))
    CC1Args.push_back("-fvisibility-global-new-delete=source");

  const Arg *A =
      DriverArgs.getLastArg(options::OPT_fvisibility_from_dllstorageclass,
                            options::OPT_fno_visibility_from_dllstorageclass);
  if (!A ||
      A->getOption().matches(options::OPT_fvisibility_from_dllstorageclass)) {
    CC1Args.push_back("-fvisibility-from-dllstorageclass");

    if (DriverArgs.hasArg(options::OPT_fvisibility_dllexport_EQ))
      DriverArgs.AddLastArg(CC1Args, options::OPT_fvisibility_dllexport_EQ);
    else
      CC1Args.push_back("-fvisibility-dllexport=protected");

    // For PS4 we override the visibilty of globals definitions without
    // dllimport or  dllexport annotations.
    if (DriverArgs.hasArg(options::OPT_fvisibility_nodllstorageclass_EQ))
      DriverArgs.AddLastArg(CC1Args,
                            options::OPT_fvisibility_nodllstorageclass_EQ);
    else if (getTriple().isPS4())
      CC1Args.push_back("-fvisibility-nodllstorageclass=hidden");
    else
      CC1Args.push_back("-fvisibility-nodllstorageclass=keep");

    if (DriverArgs.hasArg(options::OPT_fvisibility_externs_dllimport_EQ))
      DriverArgs.AddLastArg(CC1Args,
                            options::OPT_fvisibility_externs_dllimport_EQ);
    else
      CC1Args.push_back("-fvisibility-externs-dllimport=default");

    // For PS4 we override the visibilty of external globals without
    // dllimport or  dllexport annotations.
    if (DriverArgs.hasArg(
            options::OPT_fvisibility_externs_nodllstorageclass_EQ))
      DriverArgs.AddLastArg(
          CC1Args, options::OPT_fvisibility_externs_nodllstorageclass_EQ);
    else if (getTriple().isPS4())
      CC1Args.push_back("-fvisibility-externs-nodllstorageclass=default");
    else
      CC1Args.push_back("-fvisibility-externs-nodllstorageclass=keep");
  }
}

// PS4 toolchain.
toolchains::PS4CPU::PS4CPU(const Driver &D, const llvm::Triple &Triple,
                           const llvm::opt::ArgList &Args)
    : PS4PS5Base(D, Triple, Args, "PS4", "SCE_ORBIS_SDK_DIR") {}

// PS5 toolchain.
toolchains::PS5CPU::PS5CPU(const Driver &D, const llvm::Triple &Triple,
                           const llvm::opt::ArgList &Args)
    : PS4PS5Base(D, Triple, Args, "PS5", "SCE_PROSPERO_SDK_DIR") {}
