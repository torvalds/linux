//===--- SPIRV.h - SPIR-V Tool Implementations ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_SPIRV_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_SPIRV_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace SPIRV {

void constructTranslateCommand(Compilation &C, const Tool &T,
                               const JobAction &JA, const InputInfo &Output,
                               const InputInfo &Input,
                               const llvm::opt::ArgStringList &Args);

class LLVM_LIBRARY_VISIBILITY Translator : public Tool {
public:
  Translator(const ToolChain &TC)
      : Tool("SPIR-V::Translator", "llvm-spirv", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool hasIntegratedAssembler() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("SPIRV::Linker", "spirv-link", TC) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // namespace SPIRV
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY SPIRVToolChain final : public ToolChain {
  mutable std::unique_ptr<Tool> Translator;

public:
  SPIRVToolChain(const Driver &D, const llvm::Triple &Triple,
                 const llvm::opt::ArgList &Args)
      : ToolChain(D, Triple, Args) {}

  bool useIntegratedAs() const override { return true; }

  bool IsIntegratedBackendDefault() const override { return false; }
  bool IsNonIntegratedBackendSupported() const override { return true; }
  bool IsMathErrnoDefault() const override { return false; }
  bool isCrossCompiling() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }

  clang::driver::Tool *SelectTool(const JobAction &JA) const override;

protected:
  clang::driver::Tool *getTool(Action::ActionClass AC) const override;
  Tool *buildLinker() const override;

private:
  clang::driver::Tool *getTranslator() const;
};

} // namespace toolchains
} // namespace driver
} // namespace clang
#endif
