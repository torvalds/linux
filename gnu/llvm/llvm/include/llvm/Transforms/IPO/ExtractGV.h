//===-- ExtractGV.h -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_EXTRACTGV_H
#define LLVM_TRANSFORMS_IPO_EXTRACTGV_H

#include "llvm/ADT/SetVector.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class GlobalValue;

class ExtractGVPass : public PassInfoMixin<ExtractGVPass> {
private:
  SetVector<GlobalValue *> Named;
  bool deleteStuff;
  bool keepConstInit;

public:
  ExtractGVPass(std::vector<GlobalValue *> &GVs, bool deleteS = true,
                bool keepConstInit = false);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXTRACTGV_H
