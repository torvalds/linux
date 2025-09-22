//===- llvm/CodeGen/ShadowStackGCLowering.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SHADOWSTACKGCLOWERING_H
#define LLVM_CODEGEN_SHADOWSTACKGCLOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class ShadowStackGCLoweringPass
    : public PassInfoMixin<ShadowStackGCLoweringPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SHADOWSTACKGCLOWERING_H
