//===- MemDerefPrinter.cpp - Printer for isDereferenceablePointer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemDerefPrinter.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

PreservedAnalyses MemDerefPrinterPass::run(Function &F,
                                           FunctionAnalysisManager &AM) {
  OS << "Memory Dereferencibility of pointers in function '" << F.getName()
     << "'\n";

  SmallVector<Value *, 4> Deref;
  SmallPtrSet<Value *, 4> DerefAndAligned;

  const DataLayout &DL = F.getDataLayout();
  for (auto &I : instructions(F)) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      Value *PO = LI->getPointerOperand();
      if (isDereferenceablePointer(PO, LI->getType(), DL))
        Deref.push_back(PO);
      if (isDereferenceableAndAlignedPointer(PO, LI->getType(), LI->getAlign(),
                                             DL))
        DerefAndAligned.insert(PO);
    }
  }

  OS << "The following are dereferenceable:\n";
  for (Value *V : Deref) {
    OS << "  ";
    V->print(OS);
    if (DerefAndAligned.count(V))
      OS << "\t(aligned)";
    else
      OS << "\t(unaligned)";
    OS << "\n";
  }
  return PreservedAnalyses::all();
}
