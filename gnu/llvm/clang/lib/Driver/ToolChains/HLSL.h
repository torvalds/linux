//===--- HLSL.h - HLSL ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HLSL_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HLSL_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {

namespace tools {

namespace hlsl {
class LLVM_LIBRARY_VISIBILITY Validator : public Tool {
public:
  Validator(const ToolChain &TC) : Tool("hlsl::Validator", "dxv", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // namespace hlsl
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY HLSLToolChain : public ToolChain {
public:
  HLSLToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);
  Tool *getTool(Action::ActionClass AC) const override;

  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;
  static std::optional<std::string> parseTargetProfile(StringRef TargetProfile);
  bool requiresValidation(llvm::opt::DerivedArgList &Args) const;

  // Set default DWARF version to 4 for DXIL uses version 4.
  unsigned GetDefaultDwarfVersion() const override { return 4; }

private:
  mutable std::unique_ptr<tools::hlsl::Validator> Validator;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HLSL_H
