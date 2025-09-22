//===--- DragonFly.cpp - DragonFly ToolChain Implementations ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DragonFly.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void dragonfly::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                        const InputInfo &Output,
                                        const InputInfoList &Inputs,
                                        const ArgList &Args,
                                        const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const DragonFly &>(getToolChain());
  ArgStringList CmdArgs;

  claimNoWarnArgs(Args);

  // When building 32-bit code on DragonFly/pc64, we have to explicitly
  // instruct as in the base system to assemble 32-bit code.
  if (ToolChain.getArch() == llvm::Triple::x86)
    CmdArgs.push_back("--32");

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(ToolChain.GetProgramPath("as"));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

void dragonfly::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const DragonFly &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool Static = Args.hasArg(options::OPT_static);
  const bool Shared = Args.hasArg(options::OPT_shared);
  const bool Profiling = Args.hasArg(options::OPT_pg);
  const bool Pie = Args.hasArg(options::OPT_pie);
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  CmdArgs.push_back("--eh-frame-hdr");
  if (Static) {
    CmdArgs.push_back("-Bstatic");
  } else {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    if (Shared)
      CmdArgs.push_back("-shared");
    else if (!Args.hasArg(options::OPT_r)) {
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back("/usr/libexec/ld-elf.so.2");
    }
    CmdArgs.push_back("--hash-style=gnu");
    CmdArgs.push_back("--enable-new-dtags");
  }

  // When building 32-bit code on DragonFly/pc64, we have to explicitly
  // instruct ld in the base system to link 32-bit code.
  if (Arch == llvm::Triple::x86) {
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf_i386");
  }

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    const char *crt1 = nullptr;
    const char *crtbegin = nullptr;
    if (!Shared) {
      if (Profiling)
        crt1 = "gcrt1.o";
      else {
        if (Pie)
          crt1 = "Scrt1.o";
        else
          crt1 = "crt1.o";
      }
    }

    if (Shared || Pie)
      crtbegin = "crtbeginS.o";
    else
      crtbegin = "crtbegin.o";

    if (crt1)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt1)));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group,
                            options::OPT_s, options::OPT_t});
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    if (!Static) {
      CmdArgs.push_back("-rpath");
      CmdArgs.push_back("/usr/lib/gcc80");
    }

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

    if (Args.hasArg(options::OPT_pthread))
      CmdArgs.push_back("-lpthread");

    if (!Args.hasArg(options::OPT_nolibc))
      CmdArgs.push_back("-lc");

    if (Static || Args.hasArg(options::OPT_static_libgcc)) {
        CmdArgs.push_back("-lgcc");
        CmdArgs.push_back("-lgcc_eh");
    } else {
      if (Args.hasArg(options::OPT_shared_libgcc)) {
          CmdArgs.push_back("-lgcc_pic");
          if (!Shared)
            CmdArgs.push_back("-lgcc");
      } else {
          CmdArgs.push_back("-lgcc");
          CmdArgs.push_back("--as-needed");
          CmdArgs.push_back("-lgcc_pic");
          CmdArgs.push_back("--no-as-needed");
      }
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    const char *crtend = nullptr;
    if (Shared || Pie)
      crtend ="crtendS.o";
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

/// DragonFly - DragonFly tool chain which can call as(1) and ld(1) directly.

DragonFly::DragonFly(const Driver &D, const llvm::Triple &Triple,
                     const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // Path mangling to find libexec
  getProgramPaths().push_back(getDriver().Dir);

  getFilePaths().push_back(getDriver().Dir + "/../lib");
  getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib"));
  getFilePaths().push_back(concat(getDriver().SysRoot, "/usr/lib/gcc80"));
}

void DragonFly::AddClangSystemIncludeArgs(
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

  addExternCSystemInclude(DriverArgs, CC1Args,
                          concat(D.SysRoot, "/usr/include"));
}

void DragonFly::addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                         llvm::opt::ArgStringList &CC1Args) const {
  addLibStdCXXIncludePaths(concat(getDriver().SysRoot, "/usr/include/c++/8.0"), "", "",
                           DriverArgs, CC1Args);
}

Tool *DragonFly::buildAssembler() const {
  return new tools::dragonfly::Assembler(*this);
}

Tool *DragonFly::buildLinker() const {
  return new tools::dragonfly::Linker(*this);
}
