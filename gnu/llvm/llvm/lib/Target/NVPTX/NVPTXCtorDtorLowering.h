//===-- NVPTXCtorDtorLowering.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXCTORDTORLOWERING_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXCTORDTORLOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class PassRegistry;

extern char &NVPTXCtorDtorLoweringLegacyPassID;
extern void initializeNVPTXCtorDtorLoweringLegacyPass(PassRegistry &);

/// Lower llvm.global_ctors and llvm.global_dtors to special kernels.
class NVPTXCtorDtorLoweringPass
    : public PassInfoMixin<NVPTXCtorDtorLoweringPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_NVPTX_NVPTXCTORDTORLOWERING_H
