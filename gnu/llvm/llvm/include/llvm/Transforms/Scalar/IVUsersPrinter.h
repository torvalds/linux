//===- IVUsersPrinter.h - Induction Variable Users Printing -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_IVUSERSPRINTER_H
#define LLVM_TRANSFORMS_SCALAR_IVUSERSPRINTER_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class Loop;
class raw_ostream;

/// Printer pass for the \c IVUsers for a loop.
class IVUsersPrinterPass : public PassInfoMixin<IVUsersPrinterPass> {
  raw_ostream &OS;

public:
  explicit IVUsersPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
  static bool isRequired() { return true; }
};
}

#endif
