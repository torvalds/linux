//===--- Mips.h - Mips ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MIPS_LINUX_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MIPS_LINUX_H

#include "Linux.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY MipsLLVMToolChain : public Linux {
protected:
  Tool *buildLinker() const override;

public:
  MipsLLVMToolChain(const Driver &D, const llvm::Triple &Triple,
                    const llvm::opt::ArgList &Args);

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;

  CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;

  void addLibCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;

  std::string
  getCompilerRT(const llvm::opt::ArgList &Args, StringRef Component,
                FileType Type = ToolChain::FT_Static) const override;

  std::string computeSysRoot() const override;

  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return GCCInstallation.isValid() ? RuntimeLibType::RLT_Libgcc
                                     : RuntimeLibType::RLT_CompilerRT;
  }

  const char *getDefaultLinker() const override {
    return "ld.lld";
  }

private:
  std::string LibSuffix;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MIPS_LINUX_H
