//===--- AMDGPU.h - AMDGPU ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_AMDGPU_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_AMDGPU_H

#include "Gnu.h"
#include "ROCm.h"
#include "clang/Basic/TargetID.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/TargetParser/TargetParser.h"

#include <map>

namespace clang {
namespace driver {

namespace tools {
namespace amdgpu {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("amdgpu::Linker", "ld.lld", TC) {}
  bool isLinkJob() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

void getAMDGPUTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                             const llvm::opt::ArgList &Args,
                             std::vector<StringRef> &Features);

} // end namespace amdgpu
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY AMDGPUToolChain : public Generic_ELF {
protected:
  const std::map<options::ID, const StringRef> OptionsDefault;

  Tool *buildLinker() const override;
  StringRef getOptionDefault(options::ID OptID) const {
    auto opt = OptionsDefault.find(OptID);
    assert(opt != OptionsDefault.end() && "No Default for Option");
    return opt->second;
  }

public:
  AMDGPUToolChain(const Driver &D, const llvm::Triple &Triple,
                  const llvm::opt::ArgList &Args);
  unsigned GetDefaultDwarfVersion() const override { return 5; }

  bool IsMathErrnoDefault() const override { return false; }
  bool isCrossCompiling() const override { return true; }
  bool isPICDefault() const override { return true; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return true; }
  bool SupportsProfiling() const override { return false; }

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;

  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind DeviceOffloadKind) const override;

  /// Return whether denormals should be flushed, and treated as 0 by default
  /// for the subtarget.
  static bool getDefaultDenormsAreZeroForTarget(llvm::AMDGPU::GPUKind GPUKind);

  llvm::DenormalMode getDefaultDenormalModeForType(
      const llvm::opt::ArgList &DriverArgs, const JobAction &JA,
      const llvm::fltSemantics *FPType = nullptr) const override;

  static bool isWave64(const llvm::opt::ArgList &DriverArgs,
                       llvm::AMDGPU::GPUKind Kind);
  /// Needed for using lto.
  bool HasNativeLLVMSupport() const override {
    return true;
  }

  /// Needed for translating LTO options.
  const char *getDefaultLinker() const override { return "ld.lld"; }

  /// Uses amdgpu-arch tool to get arch of the system GPU. Will return error
  /// if unable to find one.
  virtual Expected<SmallVector<std::string>>
  getSystemGPUArchs(const llvm::opt::ArgList &Args) const override;

protected:
  /// Check and diagnose invalid target ID specified by -mcpu.
  virtual void checkTargetID(const llvm::opt::ArgList &DriverArgs) const;

  /// The struct type returned by getParsedTargetID.
  struct ParsedTargetIDType {
    std::optional<std::string> OptionalTargetID;
    std::optional<std::string> OptionalGPUArch;
    std::optional<llvm::StringMap<bool>> OptionalFeatures;
  };

  /// Get target ID, GPU arch, and target ID features if the target ID is
  /// specified and valid.
  ParsedTargetIDType
  getParsedTargetID(const llvm::opt::ArgList &DriverArgs) const;

  /// Get GPU arch from -mcpu without checking.
  StringRef getGPUArch(const llvm::opt::ArgList &DriverArgs) const;

  /// Common warning options shared by AMDGPU HIP, OpenCL and OpenMP toolchains.
  /// Language specific warning options should go to derived classes.
  void addClangWarningOptions(llvm::opt::ArgStringList &CC1Args) const override;
};

class LLVM_LIBRARY_VISIBILITY ROCMToolChain : public AMDGPUToolChain {
public:
  ROCMToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);
  void
  addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                        llvm::opt::ArgStringList &CC1Args,
                        Action::OffloadKind DeviceOffloadKind) const override;

  // Returns a list of device library names shared by different languages
  llvm::SmallVector<std::string, 12>
  getCommonDeviceLibNames(const llvm::opt::ArgList &DriverArgs,
                          const std::string &GPUArch,
                          bool isOpenMP = false) const;
  SanitizerMask getSupportedSanitizers() const override {
    return SanitizerKind::Address;
  }
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_AMDGPU_H
