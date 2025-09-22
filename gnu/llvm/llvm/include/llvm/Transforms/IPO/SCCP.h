//===- SCCP.h - Sparse Conditional Constant Propagation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements  interprocedural sparse conditional constant
// propagation and merging.
//
// Specifically, this:
//   * Assumes values are constant unless proven otherwise
//   * Assumes BasicBlocks are dead unless proven otherwise
//   * Proves values to be constant, and replaces them with constants
//   * Proves conditional branches to be unconditional
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SCCP_H
#define LLVM_TRANSFORMS_IPO_SCCP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// A set of parameters to control various transforms performed by IPSCCP pass.
/// Each of the boolean parameters can be set to:
///   true - enabling the transformation.
///   false - disabling the transformation.
/// Intended use is to create a default object, modify parameters with
/// additional setters and then pass it to IPSCCP.
struct IPSCCPOptions {
  bool AllowFuncSpec;

  IPSCCPOptions(bool AllowFuncSpec = true) : AllowFuncSpec(AllowFuncSpec) {}

  /// Enables or disables Specialization of Functions.
  IPSCCPOptions &setFuncSpec(bool FuncSpec) {
    AllowFuncSpec = FuncSpec;
    return *this;
  }
};

/// Pass to perform interprocedural constant propagation.
class IPSCCPPass : public PassInfoMixin<IPSCCPPass> {
  IPSCCPOptions Options;

public:
  IPSCCPPass() = default;

  IPSCCPPass(IPSCCPOptions Options) : Options(Options) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  bool isFuncSpecEnabled() const { return Options.AllowFuncSpec; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_SCCP_H
