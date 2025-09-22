//===--- Hexagon.h - Hexagon ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HEXAGON_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HEXAGON_H

#include "Linux.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace hexagon {
// For Hexagon, we do not need to instantiate tools for PreProcess, PreCompile
// and Compile.
// We simply use "clang -cc1" for those actions.
class LLVM_LIBRARY_VISIBILITY Assembler final : public Tool {
public:
  Assembler(const ToolChain &TC)
      : Tool("hexagon::Assembler", "hexagon-as", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void RenderExtraToolArgs(const JobAction &JA,
                           llvm::opt::ArgStringList &CmdArgs) const;
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("hexagon::Linker", "hexagon-ld", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  virtual void RenderExtraToolArgs(const JobAction &JA,
                                   llvm::opt::ArgStringList &CmdArgs) const;
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

void getHexagonTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                              const llvm::opt::ArgList &Args,
                              std::vector<StringRef> &Features);

} // end namespace hexagon.
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY HexagonToolChain : public Linux {
protected:
  GCCVersion GCCLibAndIncVersion;
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;

  unsigned getOptimizationLevel(const llvm::opt::ArgList &DriverArgs) const;

public:
  HexagonToolChain(const Driver &D, const llvm::Triple &Triple,
                   const llvm::opt::ArgList &Args);
  ~HexagonToolChain() override;

  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind DeviceOffloadKind) const override;
  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addLibStdCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  void addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args) const override;

  const char *getDefaultLinker() const override {
    return getTriple().isMusl() ? "ld.lld" : "hexagon-link";
  }

  CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;

  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;

  StringRef GetGCCLibAndIncVersion() const { return GCCLibAndIncVersion.Text; }

  std::string getHexagonTargetDir(
      const std::string &InstalledDir,
      const SmallVectorImpl<std::string> &PrefixDirs) const;
  void getHexagonLibraryPaths(const llvm::opt::ArgList &Args,
      ToolChain::path_list &LibPaths) const;

  std::string getCompilerRTPath() const override;

  static bool isAutoHVXEnabled(const llvm::opt::ArgList &Args);
  static StringRef GetDefaultCPU();
  static StringRef GetTargetCPUVersion(const llvm::opt::ArgList &Args);

  static std::optional<unsigned>
  getSmallDataThreshold(const llvm::opt::ArgList &Args);
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HEXAGON_H
