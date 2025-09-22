//===--- MSVC.h - MSVC ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H

#include "AMDGPU.h"
#include "Cuda.h"
#include "LazyDetector.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/WindowsDriver/MSVCPaths.h"

namespace clang {
namespace driver {
namespace tools {

/// Visual studio tools.
namespace visualstudio {
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("visualstudio::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace visualstudio

} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY MSVCToolChain : public ToolChain {
public:
  MSVCToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override;
  bool isPICDefault() const override;
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override;
  bool isPICDefaultForced() const override;

  /// Set CodeView as the default debug info format for non-MachO binary
  /// formats, and to DWARF otherwise. Users can use -gcodeview and -gdwarf to
  /// override the default.
  llvm::codegenoptions::DebugInfoFormat getDefaultDebugFormat() const override {
    return getTriple().isOSBinFormatCOFF() ? llvm::codegenoptions::DIF_CodeView
                                           : llvm::codegenoptions::DIF_DWARF;
  }

  /// Set the debugger tuning to "default", since we're definitely not tuning
  /// for GDB.
  llvm::DebuggerKind getDefaultDebuggerTuning() const override {
    return llvm::DebuggerKind::Default;
  }

  unsigned GetDefaultDwarfVersion() const override {
    return 4;
  }

  std::string getSubDirectoryPath(llvm::SubDirectoryType Type,
                                  llvm::StringRef SubdirParent = "") const;
  std::string getSubDirectoryPath(llvm::SubDirectoryType Type,
                                  llvm::Triple::ArchType TargetArch) const;

  bool getIsVS2017OrNewer() const {
    return VSLayout == llvm::ToolsetLayout::VS2017OrNewer;
  }

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  void AddCudaIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                          llvm::opt::ArgStringList &CC1Args) const override;

  void AddHIPIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                         llvm::opt::ArgStringList &CC1Args) const override;

  void AddHIPRuntimeLibArgs(const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CmdArgs) const override;

  bool getWindowsSDKLibraryPath(
      const llvm::opt::ArgList &Args, std::string &path) const;
  bool getUniversalCRTLibraryPath(const llvm::opt::ArgList &Args,
                                  std::string &path) const;
  bool useUniversalCRT() const;
  VersionTuple
  computeMSVCVersion(const Driver *D,
                     const llvm::opt::ArgList &Args) const override;

  std::string ComputeEffectiveClangTriple(const llvm::opt::ArgList &Args,
                                          types::ID InputType) const override;
  SanitizerMask getSupportedSanitizers() const override;

  void printVerboseInfo(raw_ostream &OS) const override;

  bool FoundMSVCInstall() const { return !VCToolChainPath.empty(); }

  void
  addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                        llvm::opt::ArgStringList &CC1Args,
                        Action::OffloadKind DeviceOffloadKind) const override;

protected:
  void AddSystemIncludeWithSubfolder(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &CC1Args,
                                     const std::string &folder,
                                     const Twine &subfolder1,
                                     const Twine &subfolder2 = "",
                                     const Twine &subfolder3 = "") const;

  Tool *buildLinker() const override;
  Tool *buildAssembler() const override;
private:
  std::optional<llvm::StringRef> WinSdkDir, WinSdkVersion, WinSysRoot;
  std::string VCToolChainPath;
  llvm::ToolsetLayout VSLayout = llvm::ToolsetLayout::OlderVS;
  LazyDetector<CudaInstallationDetector> CudaInstallation;
  LazyDetector<RocmInstallationDetector> RocmInstallation;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H
