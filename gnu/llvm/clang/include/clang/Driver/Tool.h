//===--- Tool.h - Compilation Tools -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_TOOL_H
#define LLVM_CLANG_DRIVER_TOOL_H

#include "clang/Basic/LLVM.h"

namespace llvm {
namespace opt {
  class ArgList;
}
}

namespace clang {
namespace driver {

  class Compilation;
  class InputInfo;
  class Job;
  class JobAction;
  class ToolChain;

  typedef SmallVector<InputInfo, 4> InputInfoList;

/// Tool - Information on a specific compilation tool.
class Tool {
  /// The tool name (for debugging).
  const char *Name;

  /// The human readable name for the tool, for use in diagnostics.
  const char *ShortName;

  /// The tool chain this tool is a part of.
  const ToolChain &TheToolChain;

public:
  Tool(const char *Name, const char *ShortName, const ToolChain &TC);

public:
  virtual ~Tool();

  const char *getName() const { return Name; }

  const char *getShortName() const { return ShortName; }

  const ToolChain &getToolChain() const { return TheToolChain; }

  virtual bool hasIntegratedAssembler() const { return false; }
  virtual bool hasIntegratedBackend() const { return true; }
  virtual bool canEmitIR() const { return false; }
  virtual bool hasIntegratedCPP() const = 0;
  virtual bool isLinkJob() const { return false; }
  virtual bool isDsymutilJob() const { return false; }

  /// Does this tool have "good" standardized diagnostics, or should the
  /// driver add an additional "command failed" diagnostic on failures.
  virtual bool hasGoodDiagnostics() const { return false; }

  /// ConstructJob - Construct jobs to perform the action \p JA,
  /// writing to \p Output and with \p Inputs, and add the jobs to
  /// \p C.
  ///
  /// \param TCArgs - The argument list for this toolchain, with any
  /// tool chain specific translations applied.
  /// \param LinkingOutput - If this output will eventually feed the
  /// linker, then this is the final output name of the linked image.
  virtual void ConstructJob(Compilation &C, const JobAction &JA,
                            const InputInfo &Output,
                            const InputInfoList &Inputs,
                            const llvm::opt::ArgList &TCArgs,
                            const char *LinkingOutput) const = 0;
  /// Construct jobs to perform the action \p JA, writing to the \p Outputs and
  /// with \p Inputs, and add the jobs to \p C. The default implementation
  /// assumes a single output and is expected to be overloaded for the tools
  /// that support multiple inputs.
  ///
  /// \param TCArgs The argument list for this toolchain, with any
  /// tool chain specific translations applied.
  /// \param LinkingOutput If this output will eventually feed the
  /// linker, then this is the final output name of the linked image.
  virtual void ConstructJobMultipleOutputs(Compilation &C, const JobAction &JA,
                                           const InputInfoList &Outputs,
                                           const InputInfoList &Inputs,
                                           const llvm::opt::ArgList &TCArgs,
                                           const char *LinkingOutput) const;
};

} // end namespace driver
} // end namespace clang

#endif
