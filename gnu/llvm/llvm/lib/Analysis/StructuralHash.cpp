//===- StructuralHash.cpp - Function Hash Printing ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the StructuralHashPrinterPass which is used to show
// the structural hash of all functions in a module and the module itself.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/StructuralHash.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/StructuralHash.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

PreservedAnalyses StructuralHashPrinterPass::run(Module &M,
                                                 ModuleAnalysisManager &MAM) {
  OS << "Module Hash: "
     << Twine::utohexstr(StructuralHash(M, EnableDetailedStructuralHash))
     << "\n";
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    OS << "Function " << F.getName() << " Hash: "
       << Twine::utohexstr(StructuralHash(F, EnableDetailedStructuralHash))
       << "\n";
  }
  return PreservedAnalyses::all();
}
