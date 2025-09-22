//===--- Flang.h - Flang Tool and ToolChain Implementations ====-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_FLANG_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_FLANG_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Compiler.h"

namespace clang {
namespace driver {

namespace tools {

/// Flang compiler tool.
class LLVM_LIBRARY_VISIBILITY Flang : public Tool {
private:
  /// Extract fortran dialect options from the driver arguments and add them to
  /// the list of arguments for the generated command/job.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addFortranDialectOptions(const llvm::opt::ArgList &Args,
                                llvm::opt::ArgStringList &CmdArgs) const;

  /// Extract preprocessing options from the driver arguments and add them to
  /// the preprocessor command arguments.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addPreprocessingOptions(const llvm::opt::ArgList &Args,
                               llvm::opt::ArgStringList &CmdArgs) const;

  /// Extract PIC options from the driver arguments and add them to
  /// the command arguments.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addPicOptions(const llvm::opt::ArgList &Args,
                     llvm::opt::ArgStringList &CmdArgs) const;

  /// Extract target options from the driver arguments and add them to
  /// the command arguments.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addTargetOptions(const llvm::opt::ArgList &Args,
                        llvm::opt::ArgStringList &CmdArgs) const;

  /// Add specific options for AArch64 target.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void AddAArch64TargetArgs(const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CmdArgs) const;

  /// Add specific options for AMDGPU target.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void AddAMDGPUTargetArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const;

  /// Add specific options for RISC-V target.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void AddRISCVTargetArgs(const llvm::opt::ArgList &Args,
                          llvm::opt::ArgStringList &CmdArgs) const;

  /// Add specific options for X86_64 target.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void AddX86_64TargetArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const;

  /// Extract offload options from the driver arguments and add them to
  /// the command arguments.
  /// \param [in] C The current compilation for the driver invocation
  /// \param [in] Inputs The input infomration on the current file inputs
  /// \param [in] JA The job action
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addOffloadOptions(Compilation &C, const InputInfoList &Inputs,
                         const JobAction &JA, const llvm::opt::ArgList &Args,
                         llvm::opt::ArgStringList &CmdArgs) const;

  /// Extract options for code generation from the driver arguments and add them
  /// to the command arguments.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addCodegenOptions(const llvm::opt::ArgList &Args,
                         llvm::opt::ArgStringList &CmdArgs) const;

  /// Extract other compilation options from the driver arguments and add them
  /// to the command arguments.
  ///
  /// \param [in] Args The list of input driver arguments
  /// \param [out] CmdArgs The list of output command arguments
  void addOtherOptions(const llvm::opt::ArgList &Args,
                       llvm::opt::ArgStringList &CmdArgs) const;

public:
  Flang(const ToolChain &TC);
  ~Flang() override;

  bool hasGoodDiagnostics() const override { return true; }
  bool hasIntegratedAssembler() const override { return true; }
  bool hasIntegratedCPP() const override { return true; }
  bool canEmitIR() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace tools

} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_FLANG_H
