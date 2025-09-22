//===--- ZOS.h - z/OS ToolChain Implementations -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ZOS_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ZOS_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {

/// Directly call system default assembler and linker.
namespace zos {

class LLVM_LIBRARY_VISIBILITY Assembler final : public Tool {
public:
  Assembler(const ToolChain &TC) : Tool("zos::Assembler", "assembler", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("zos::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace zos
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY ZOS : public ToolChain {
public:
  ZOS(const Driver &D, const llvm::Triple &Triple,
      const llvm::opt::ArgList &Args);
  ~ZOS() override;

  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }

  void TryAddIncludeFromPath(llvm::SmallString<128> Path,
                             const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args) const;
  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;

  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  unsigned GetDefaultDwarfVersion() const override { return 4; }
  CXXStdlibType GetDefaultCXXStdlibType() const override;

  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;

  RuntimeLibType GetDefaultRuntimeLibType() const override;

  void addClangTargetOptions(
      const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
      Action::OffloadKind DeviceOffloadingKind) const override;

  const char *getDefaultLinker() const override { return "/bin/ld"; }

protected:
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ZOS_H
