//===--- Haiku.cpp - Haiku ToolChain Implementations ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Haiku.h"
#include "CommonArgs.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void haiku::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const Haiku &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple &Triple = ToolChain.getTriple();
  const bool Static = Args.hasArg(options::OPT_static);
  const bool Shared = Args.hasArg(options::OPT_shared);
  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  // Silence warning for "clang -pie foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_pie);

  // -rdynamic is a no-op with Haiku. Claim argument to avoid warning.
  Args.ClaimAllArgs(options::OPT_rdynamic);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  CmdArgs.push_back("--eh-frame-hdr");
  if (Static) {
    CmdArgs.push_back("-Bstatic");
  } else {
    if (Shared)
      CmdArgs.push_back("-shared");
    CmdArgs.push_back("--enable-new-dtags");
  }

  CmdArgs.push_back("-shared");

  if (!Shared)
    CmdArgs.push_back("--no-undefined");

  if (Triple.isRISCV64()) {
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
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtbeginS.o")));
    if (!Shared)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("start_dyn.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("init_term_dyn.o")));
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group,
                            options::OPT_s, options::OPT_t});
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  if (D.isUsingLTO()) {
    assert(!Inputs.empty() && "Must have at least one input.");
    // Find the first filename InputInfo object.
    auto Input = llvm::find_if(
        Inputs, [](const InputInfo &II) -> bool { return II.isFilename(); });
    if (Input == Inputs.end())
      // For a very rare case, all of the inputs to the linker are
      // InputArg. If that happens, just use the first InputInfo.
      Input = Inputs.begin();

    addLTOOptions(ToolChain, Args, CmdArgs, Output, *Input,
                  D.getLTOMode() == LTOK_Thin);
  }

  addLinkerCompressDebugSectionsOption(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    // Use the static OpenMP runtime with -static-openmp
    bool StaticOpenMP = Args.hasArg(options::OPT_static_openmp) && !Static;
    addOpenMPRuntime(C, CmdArgs, ToolChain, Args, StaticOpenMP);

    if (D.CCCIsCXX() && ToolChain.ShouldLinkCXXStdlib(Args))
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);

    // Silence warnings when linking C code with a C++ '-stdlib' argument.
    Args.ClaimAllArgs(options::OPT_stdlib_EQ);

    // Additional linker set-up and flags for Fortran. This is required in order
    // to generate executables. As Fortran runtime depends on the C runtime,
    // these dependencies need to be listed before the C runtime below (i.e.
    // AddRunTimeLibs).
    if (D.IsFlangMode()) {
      addFortranRuntimeLibraryPath(ToolChain, Args, CmdArgs);
      addFortranRuntimeLibs(ToolChain, Args, CmdArgs);
    }

    CmdArgs.push_back("-lgcc");

    CmdArgs.push_back("--push-state");
    CmdArgs.push_back("--as-needed");
    CmdArgs.push_back("-lgcc_s");
    CmdArgs.push_back("--no-as-needed");
    CmdArgs.push_back("--pop-state");

    CmdArgs.push_back("-lroot");

    CmdArgs.push_back("-lgcc");

    CmdArgs.push_back("--push-state");
    CmdArgs.push_back("--as-needed");
    CmdArgs.push_back("-lgcc_s");
    CmdArgs.push_back("--no-as-needed");
    CmdArgs.push_back("--pop-state");
  }

  // No need to do anything for pthreads. Claim argument to avoid warning.
  Args.claimAllArgs(options::OPT_pthread, options::OPT_pthreads);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtendS.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
  }

  ToolChain.addProfileRTLibs(Args, CmdArgs);

  const char *Exec = Args.MakeArgString(getToolChain().GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

/// Haiku - Haiku tool chain which can call as(1) and ld(1) directly.

Haiku::Haiku(const Driver &D, const llvm::Triple& Triple, const ArgList &Args)
  : Generic_ELF(D, Triple, Args) {

  GCCInstallation.init(Triple, Args);

  getFilePaths().push_back(concat(getDriver().SysRoot, "/boot/system/lib"));
  getFilePaths().push_back(concat(getDriver().SysRoot, "/boot/system/develop/lib"));

  if (GCCInstallation.isValid())
    getFilePaths().push_back(GCCInstallation.getInstallPath().str());
}

void Haiku::AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &CC1Args) const {
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

  // Add dirs specified via 'configure --with-c-include-dirs'.
  StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (!CIncludeDirs.empty()) {
    SmallVector<StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (StringRef dir : dirs) {
      StringRef Prefix =
        llvm::sys::path::is_absolute(dir) ? StringRef(D.SysRoot) : "";
      addExternCSystemInclude(DriverArgs, CC1Args, Prefix + dir);
    }
    return;
  }

  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/non-packaged/develop/headers"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/app"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/device"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/drivers"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/game"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/interface"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/kernel"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/locale"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/mail"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/media"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/midi"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/midi2"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/net"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/opengl"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/storage"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/support"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/translation"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/add-ons/graphics"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/add-ons/input_server"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/add-ons/mail_daemon"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/add-ons/registrar"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/add-ons/screen_saver"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/add-ons/tracker"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/be_apps/Deskbar"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/be_apps/NetPositive"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/os/be_apps/Tracker"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/3rdparty"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/bsd"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/glibc"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/gnu"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers/posix"));
  addSystemInclude(DriverArgs, CC1Args, concat(D.SysRoot,
                   "/boot/system/develop/headers"));
}

void Haiku::addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                  llvm::opt::ArgStringList &CC1Args) const {
  addSystemInclude(DriverArgs, CC1Args,
                   concat(getDriver().SysRoot, "/boot/system/develop/headers/c++/v1"));
}

Tool *Haiku::buildLinker() const { return new tools::haiku::Linker(*this); }

bool Haiku::HasNativeLLVMSupport() const { return true; }
