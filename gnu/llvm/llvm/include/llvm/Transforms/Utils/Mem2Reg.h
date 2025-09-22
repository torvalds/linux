//===- Mem2Reg.h - The -mem2reg pass, a wrapper around the Utils lib ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is a simple pass wrapper around the PromoteMemToReg function call
// exposed by the Utils library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MEM2REG_H
#define LLVM_TRANSFORMS_UTILS_MEM2REG_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

class PromotePass : public PassInfoMixin<PromotePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MEM2REG_H
