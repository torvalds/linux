//===- CostModel.h - --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_COSTMODEL_H
#define LLVM_ANALYSIS_COSTMODEL_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Printer pass for cost modeling results.
class CostModelPrinterPass : public PassInfoMixin<CostModelPrinterPass> {
  raw_ostream &OS;

public:
  explicit CostModelPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};
} // end namespace llvm

#endif // LLVM_ANALYSIS_COSTMODEL_H
