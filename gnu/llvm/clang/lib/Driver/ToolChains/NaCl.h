//===--- NaCl.h - Native Client ToolChain Implementations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_NACL_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_NACL_H

#include "Gnu.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace nacltools {
class LLVM_LIBRARY_VISIBILITY AssemblerARM : public gnutools::Assembler {
public:
  AssemblerARM(const ToolChain &TC) : gnutools::Assembler(TC) {}

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("NaCl::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace nacltools
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY NaClToolChain : public Generic_ELF {
public:
  NaClToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addLibCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;

  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;

  bool IsIntegratedAssemblerDefault() const override {
    return getTriple().getArch() == llvm::Triple::mipsel;
  }

  // Get the path to the file containing NaCl's ARM macros.
  // It lives in NaClToolChain because the ARMAssembler tool needs a
  // const char * that it can pass around,
  const char *GetNaClArmMacrosPath() const { return NaClArmMacrosPath.c_str(); }

  std::string ComputeEffectiveClangTriple(const llvm::opt::ArgList &Args,
                                          types::ID InputType) const override;

protected:
  Tool *buildLinker() const override;
  Tool *buildAssembler() const override;

private:
  std::string NaClArmMacrosPath;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_NACL_H
