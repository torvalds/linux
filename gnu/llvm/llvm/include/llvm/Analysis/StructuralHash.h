//=- StructuralHash.h - Structural Hash Printing --*- C++ -*-----------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_STRUCTURALHASH_H
#define LLVM_ANALYSIS_STRUCTURALHASH_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Printer pass for  StructuralHashes
class StructuralHashPrinterPass
    : public PassInfoMixin<StructuralHashPrinterPass> {
  raw_ostream &OS;
  bool EnableDetailedStructuralHash;

public:
  explicit StructuralHashPrinterPass(raw_ostream &OS, bool Detailed)
      : OS(OS), EnableDetailedStructuralHash(Detailed) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_STRUCTURALHASH_H
