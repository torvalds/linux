//===--- PS4CPU.h - PS4CPU ToolChain Implementations ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_PS4CPU_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_PS4CPU_H

#include "Gnu.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {

namespace PScpu {
// Functions/classes in this namespace support both PS4 and PS5.

void addProfileRTArgs(const ToolChain &TC, const llvm::opt::ArgList &Args,
                      llvm::opt::ArgStringList &CmdArgs);

void addSanitizerArgs(const ToolChain &TC, const llvm::opt::ArgList &Args,
                      llvm::opt::ArgStringList &CmdArgs);

class LLVM_LIBRARY_VISIBILITY Assembler final : public Tool {
public:
  Assembler(const ToolChain &TC) : Tool("PScpu::Assembler", "assembler", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // namespace PScpu

namespace PS4cpu {
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("PS4cpu::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // namespace PS4cpu

namespace PS5cpu {
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("PS5cpu::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // namespace PS5cpu

} // namespace tools

namespace toolchains {

// Common Toolchain base class for PS4 and PS5.
class LLVM_LIBRARY_VISIBILITY PS4PS5Base : public Generic_ELF {
public:
  PS4PS5Base(const Driver &D, const llvm::Triple &Triple,
             const llvm::opt::ArgList &Args, StringRef Platform,
             const char *EnvVar);

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  // No support for finding a C++ standard library yet.
  void addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args) const override {
  }
  void
  addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                           llvm::opt::ArgStringList &CC1Args) const override {}

  bool IsMathErrnoDefault() const override { return false; }
  bool IsObjCNonFragileABIDefault() const override { return true; }
  bool HasNativeLLVMSupport() const override { return true; }
  bool isPICDefault() const override { return true; }

  LangOptions::StackProtectorMode
  GetDefaultStackProtectorLevel(bool KernelOrKext) const override {
    return LangOptions::SSPStrong;
  }

  llvm::DebuggerKind getDefaultDebuggerTuning() const override {
    return llvm::DebuggerKind::SCE;
  }

  SanitizerMask getSupportedSanitizers() const override;

  void addClangTargetOptions(
      const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
      Action::OffloadKind DeviceOffloadingKind) const override;

  llvm::DenormalMode getDefaultDenormalModeForType(
      const llvm::opt::ArgList &DriverArgs, const JobAction &JA,
      const llvm::fltSemantics *FPType) const override {
    // DAZ and FTZ are on by default.
    return llvm::DenormalMode::getPreserveSign();
  }

  // Helper methods for PS4/PS5.
  virtual const char *getLinkerBaseName() const = 0;
  virtual std::string qualifyPSCmdName(StringRef CmdName) const = 0;
  virtual void addSanitizerArgs(const llvm::opt::ArgList &Args,
                                llvm::opt::ArgStringList &CmdArgs,
                                const char *Prefix,
                                const char *Suffix) const = 0;
  virtual const char *getProfileRTLibName() const = 0;

private:
  // We compute the SDK root dir in the ctor, and use it later.
  std::string SDKRootDir;
};

// PS4-specific Toolchain class.
class LLVM_LIBRARY_VISIBILITY PS4CPU : public PS4PS5Base {
public:
  PS4CPU(const Driver &D, const llvm::Triple &Triple,
         const llvm::opt::ArgList &Args);

  unsigned GetDefaultDwarfVersion() const override { return 4; }

  // PS4 toolchain uses legacy thin LTO API, which is not
  // capable of unit splitting.
  bool canSplitThinLTOUnit() const override { return false; }

  const char *getLinkerBaseName() const override { return "ld"; }
  std::string qualifyPSCmdName(StringRef CmdName) const override {
    return Twine("orbis-", CmdName).str();
  }
  void addSanitizerArgs(const llvm::opt::ArgList &Args,
                        llvm::opt::ArgStringList &CmdArgs, const char *Prefix,
                        const char *Suffix) const override;
  const char *getProfileRTLibName() const override {
    return "libclang_rt.profile-x86_64.a";
  }

protected:
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;
};

// PS5-specific Toolchain class.
class LLVM_LIBRARY_VISIBILITY PS5CPU : public PS4PS5Base {
public:
  PS5CPU(const Driver &D, const llvm::Triple &Triple,
         const llvm::opt::ArgList &Args);

  unsigned GetDefaultDwarfVersion() const override { return 5; }

  SanitizerMask getSupportedSanitizers() const override;

  const char *getLinkerBaseName() const override { return "lld"; }
  std::string qualifyPSCmdName(StringRef CmdName) const override {
    return Twine("prospero-", CmdName).str();
  }
  void addSanitizerArgs(const llvm::opt::ArgList &Args,
                        llvm::opt::ArgStringList &CmdArgs, const char *Prefix,
                        const char *Suffix) const override;
  const char *getProfileRTLibName() const override {
    return "libclang_rt.profile-x86_64_nosubmission.a";
  }

protected:
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_PS4CPU_H
