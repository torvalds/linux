//===- AMDGPUPerfHintAnalysis.h ---- analysis of memory traffic -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Analyzes if a function potentially memory bound and if a kernel
/// kernel may benefit from limiting number of waves to reduce cache thrashing.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MDGPUPERFHINTANALYSIS_H
#define LLVM_LIB_TARGET_AMDGPU_MDGPUPERFHINTANALYSIS_H

#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/IR/ValueMap.h"

namespace llvm {

struct AMDGPUPerfHintAnalysis : public CallGraphSCCPass {
  static char ID;

public:
  AMDGPUPerfHintAnalysis() : CallGraphSCCPass(ID) {}

  bool runOnSCC(CallGraphSCC &SCC) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool isMemoryBound(const Function *F) const;

  bool needsWaveLimiter(const Function *F) const;

  struct FuncInfo {
    unsigned MemInstCost;
    unsigned InstCost;
    unsigned IAMInstCost; // Indirect access memory instruction count
    unsigned LSMInstCost; // Large stride memory instruction count
    bool HasDenseGlobalMemAcc; // Set if at least 1 basic block has relatively
                               // high global memory access
    FuncInfo()
        : MemInstCost(0), InstCost(0), IAMInstCost(0), LSMInstCost(0),
          HasDenseGlobalMemAcc(false) {}
  };

  typedef ValueMap<const Function*, FuncInfo> FuncInfoMap;

private:

  FuncInfoMap FIM;
};
} // namespace llvm
#endif // LLVM_LIB_TARGET_AMDGPU_MDGPUPERFHINTANALYSIS_H
