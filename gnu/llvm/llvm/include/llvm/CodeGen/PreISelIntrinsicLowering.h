//===- PreISelIntrinsicLowering.h - Pre-ISel intrinsic lowering pass ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR lowering for the llvm.load.relative and llvm.objc.*
// intrinsics.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_PREISELINTRINSICLOWERING_H
#define LLVM_CODEGEN_PREISELINTRINSICLOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class TargetMachine;

struct PreISelIntrinsicLoweringPass
    : PassInfoMixin<PreISelIntrinsicLoweringPass> {
  const TargetMachine &TM;

  PreISelIntrinsicLoweringPass(const TargetMachine &TM) : TM(TM) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PREISELINTRINSICLOWERING_H
