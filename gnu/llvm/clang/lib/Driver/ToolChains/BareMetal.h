//===--- BareMetal.h - Bare Metal Tool and ToolChain ------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_BAREMETAL_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_BAREMETAL_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

#include <string>

namespace clang {
namespace driver {

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY BareMetal : public ToolChain {
public:
  BareMetal(const Driver &D, const llvm::Triple &Triple,
            const llvm::opt::ArgList &Args);
  ~BareMetal() override = default;

  static bool handlesTarget(const llvm::Triple &Triple);

  void findMultilibs(const Driver &D, const llvm::Triple &Triple,
                     const llvm::opt::ArgList &Args);

protected:
  Tool *buildLinker() const override;
  Tool *buildStaticLibTool() const override;

public:
  bool useIntegratedAs() const override { return true; }
  bool isBareMetal() const override { return true; }
  bool isCrossCompiling() const override { return true; }
  bool HasNativeLLVMSupport() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }

  StringRef getOSLibName() const override { return "baremetal"; }

  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }
  CXXStdlibType GetDefaultCXXStdlibType() const override {
    return ToolChain::CST_Libcxx;
  }

  const char *getDefaultLinker() const override { return "ld.lld"; }

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void
  addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                        llvm::opt::ArgStringList &CC1Args,
                        Action::OffloadKind DeviceOffloadKind) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;
  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;
  void AddLinkRuntimeLib(const llvm::opt::ArgList &Args,
                         llvm::opt::ArgStringList &CmdArgs) const;
  std::string computeSysRoot() const override;
  SanitizerMask getSupportedSanitizers() const override;

private:
  using OrderedMultilibs =
      llvm::iterator_range<llvm::SmallVector<Multilib>::const_reverse_iterator>;
  OrderedMultilibs getOrderedMultilibs() const;
};

} // namespace toolchains

namespace tools {
namespace baremetal {

class LLVM_LIBRARY_VISIBILITY StaticLibTool : public Tool {
public:
  StaticLibTool(const ToolChain &TC)
      : Tool("baremetal::StaticLibTool", "llvm-ar", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("baremetal::Linker", "ld.lld", TC) {}
  bool isLinkJob() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // namespace baremetal
} // namespace tools

} // namespace driver
} // namespace clang

#endif
