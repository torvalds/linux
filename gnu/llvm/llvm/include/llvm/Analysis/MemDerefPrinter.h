//===- MemDerefPrinter.h - Printer for isDereferenceablePointer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMDEREFPRINTER_H
#define LLVM_ANALYSIS_MEMDEREFPRINTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class MemDerefPrinterPass : public PassInfoMixin<MemDerefPrinterPass> {
  raw_ostream &OS;

public:
  MemDerefPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};
} // namespace llvm

#endif // LLVM_ANALYSIS_MEMDEREFPRINTER_H
