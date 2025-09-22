//===-- MipsLinux.cpp - Mips ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MipsLinux.h"
#include "Arch/Mips.h"
#include "CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// Mips Toolchain
MipsLLVMToolChain::MipsLLVMToolChain(const Driver &D,
                                     const llvm::Triple &Triple,
                                     const ArgList &Args)
    : Linux(D, Triple, Args) {
  // Select the correct multilib according to the given arguments.
  DetectedMultilibs Result;
  findMIPSMultilibs(D, Triple, "", Args, Result);
  Multilibs = Result.Multilibs;
  SelectedMultilibs = Result.SelectedMultilibs;

  // Find out the library suffix based on the ABI.
  LibSuffix = tools::mips::getMipsABILibSuffix(Args, Triple);
  getFilePaths().clear();
  getFilePaths().push_back(computeSysRoot() + "/usr/lib" + LibSuffix);
}

void MipsLLVMToolChain::AddClangSystemIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  const Driver &D = getDriver();

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  const auto &Callback = Multilibs.includeDirsCallback();
  if (Callback) {
    for (const auto &Path : Callback(SelectedMultilibs.back()))
      addExternCSystemIncludeIfExists(DriverArgs, CC1Args, D.Dir + Path);
  }
}

Tool *MipsLLVMToolChain::buildLinker() const {
  return new tools::gnutools::Linker(*this);
}

std::string MipsLLVMToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot + SelectedMultilibs.back().osSuffix();

  const std::string InstalledDir(getDriver().Dir);
  std::string SysRootPath =
      InstalledDir + "/../sysroot" + SelectedMultilibs.back().osSuffix();
  if (llvm::sys::fs::exists(SysRootPath))
    return SysRootPath;

  return std::string();
}

ToolChain::CXXStdlibType
MipsLLVMToolChain::GetCXXStdlibType(const ArgList &Args) const {
  Arg *A = Args.getLastArg(options::OPT_stdlib_EQ);
  if (A) {
    StringRef Value = A->getValue();
    if (Value != "libc++")
      getDriver().Diag(clang::diag::err_drv_invalid_stdlib_name)
          << A->getAsString(Args);
  }

  return ToolChain::CST_Libcxx;
}

void MipsLLVMToolChain::addLibCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  if (const auto &Callback = Multilibs.includeDirsCallback()) {
    for (std::string Path : Callback(SelectedMultilibs.back())) {
      Path = getDriver().Dir + Path + "/c++/v1";
      if (llvm::sys::fs::exists(Path)) {
        addSystemInclude(DriverArgs, CC1Args, Path);
        return;
      }
    }
  }
}

void MipsLLVMToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                            ArgStringList &CmdArgs) const {
  assert((GetCXXStdlibType(Args) == ToolChain::CST_Libcxx) &&
         "Only -lc++ (aka libxx) is supported in this toolchain.");

  CmdArgs.push_back("-lc++");
  if (Args.hasArg(options::OPT_fexperimental_library))
    CmdArgs.push_back("-lc++experimental");
  CmdArgs.push_back("-lc++abi");
  CmdArgs.push_back("-lunwind");
}

std::string MipsLLVMToolChain::getCompilerRT(const ArgList &Args,
                                             StringRef Component,
                                             FileType Type) const {
  SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, SelectedMultilibs.back().osSuffix(), "lib" + LibSuffix,
                          getOS());
  const char *Suffix;
  switch (Type) {
  case ToolChain::FT_Object:
    Suffix = ".o";
    break;
  case ToolChain::FT_Static:
    Suffix = ".a";
    break;
  case ToolChain::FT_Shared:
    Suffix = ".so";
    break;
  }
  llvm::sys::path::append(
      Path, Twine("libclang_rt." + Component + "-" + "mips" + Suffix));
  return std::string(Path);
}
