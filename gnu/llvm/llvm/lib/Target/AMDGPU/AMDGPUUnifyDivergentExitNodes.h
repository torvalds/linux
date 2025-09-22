//===- AMDGPUUnifyDivergentExitNodes.h ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a variant of the UnifyFunctionExitNodes pass. Rather than ensuring
// there is at most one ret and one unreachable instruction, it ensures there is
// at most one divergent exiting block.
//
// StructurizeCFG can't deal with multi-exit regions formed by branches to
// multiple return nodes. It is not desirable to structurize regions with
// uniform branches, so unifying those to the same return block as divergent
// branches inhibits use of scalar branching. It still can't deal with the case
// where one branch goes to return, and one unreachable. Replace unreachable in
// this case with a return.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUUNIFYDIVERGENTEXITNODES_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUUNIFYDIVERGENTEXITNODES_H

#include "AMDGPU.h"

namespace llvm {
class AMDGPUUnifyDivergentExitNodesPass
    : public PassInfoMixin<AMDGPUUnifyDivergentExitNodesPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUUNIFYDIVERGENTEXITNODES_H
