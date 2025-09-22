//===--- OHOS.h - OHOS ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_OHOS_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_OHOS_H

#include "Linux.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY OHOS : public Generic_ELF {
public:
  OHOS(const Driver &D, const llvm::Triple &Triple,
          const llvm::opt::ArgList &Args);

  bool HasNativeLLVMSupport() const override { return true; }

  bool IsMathErrnoDefault() const override { return false; }

  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }
  CXXStdlibType GetDefaultCXXStdlibType() const override {
    return ToolChain::CST_Libcxx;
  }
  // Not add -funwind-tables by default
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override { return true; }
  bool isPICDefaultForced() const override { return false; }
  UnwindLibType GetUnwindLibType(const llvm::opt::ArgList &Args) const override;
  UnwindLibType GetDefaultUnwindLibType() const override { return UNW_CompilerRT; }

  RuntimeLibType
  GetRuntimeLibType(const llvm::opt::ArgList &Args) const override;
  CXXStdlibType
  GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void
  AddClangCXXStdlibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &CC1Args) const override;
  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;

  std::string computeSysRoot() const override;
  std::string getDynamicLinker(const llvm::opt::ArgList &Args) const override;

  std::string
  getCompilerRT(const llvm::opt::ArgList &Args, StringRef Component,
                FileType Type = ToolChain::FT_Static) const override;

  const char *getDefaultLinker() const override {
    return "ld.lld";
  }

  Tool *buildLinker() const override {
    return new tools::gnutools::Linker(*this);
  }
  Tool *buildAssembler() const override {
    return new tools::gnutools::Assembler(*this);
  }

  path_list getRuntimePaths() const;

protected:
  std::string getMultiarchTriple(const llvm::Triple &T) const;
  std::string getMultiarchTriple(const Driver &D,
                                 const llvm::Triple &TargetTriple,
                                 StringRef SysRoot) const override;
  void addExtraOpts(llvm::opt::ArgStringList &CmdArgs) const override;
  SanitizerMask getSupportedSanitizers() const override;
  void addProfileRTLibs(const llvm::opt::ArgList &Args,
                             llvm::opt::ArgStringList &CmdArgs) const override;
  path_list getArchSpecificLibPaths() const override;

private:
  Multilib SelectedMultilib;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_OHOS_H
