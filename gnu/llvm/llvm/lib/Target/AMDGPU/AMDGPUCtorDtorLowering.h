//===-- AMDGPUCtorDtorLowering.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUCTORDTORLOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUCTORDTORLOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;

/// Lower llvm.global_ctors and llvm.global_dtors to special kernels.
class AMDGPUCtorDtorLoweringPass
    : public PassInfoMixin<AMDGPUCtorDtorLoweringPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUCTORDTORLOWERING_H
