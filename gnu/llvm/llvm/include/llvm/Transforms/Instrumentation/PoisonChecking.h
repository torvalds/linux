//===- PoisonChecking.h - ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_POISONCHECKING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_POISONCHECKING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct PoisonCheckingPass : public PassInfoMixin<PoisonCheckingPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_POISONCHECKING_H
