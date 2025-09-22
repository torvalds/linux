//===- MachineUniformityAnalysis.h ---------------------------*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Machine IR instance of the generic uniformity analysis
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEUNIFORMITYANALYSIS_H
#define LLVM_CODEGEN_MACHINEUNIFORMITYANALYSIS_H

#include "llvm/ADT/GenericUniformityInfo.h"
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineSSAContext.h"

namespace llvm {

extern template class GenericUniformityInfo<MachineSSAContext>;
using MachineUniformityInfo = GenericUniformityInfo<MachineSSAContext>;

/// \brief Compute uniformity information for a Machine IR function.
///
/// If \p HasBranchDivergence is false, produces a dummy result which assumes
/// everything is uniform.
MachineUniformityInfo computeMachineUniformityInfo(
    MachineFunction &F, const MachineCycleInfo &cycleInfo,
    const MachineDominatorTree &domTree, bool HasBranchDivergence);

/// Legacy analysis pass which computes a \ref MachineUniformityInfo.
class MachineUniformityAnalysisPass : public MachineFunctionPass {
  MachineUniformityInfo UI;

public:
  static char ID;

  MachineUniformityAnalysisPass();

  MachineUniformityInfo &getUniformityInfo() { return UI; }
  const MachineUniformityInfo &getUniformityInfo() const { return UI; }

  bool runOnMachineFunction(MachineFunction &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  // TODO: verify analysis
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEUNIFORMITYANALYSIS_H
