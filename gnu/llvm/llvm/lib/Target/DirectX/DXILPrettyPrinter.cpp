//===- DXILPrettyPrinter.cpp - DXIL Resource helper objects ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains a pass for pretty printing DXIL metadata into IR
/// comments when printing assembly output.
///
//===----------------------------------------------------------------------===//

#include "DXILResourceAnalysis.h"
#include "DirectX.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class DXILPrettyPrinter : public llvm::ModulePass {
  raw_ostream &OS; // raw_ostream to print to.

public:
  static char ID;
  DXILPrettyPrinter() : ModulePass(ID), OS(dbgs()) {
    initializeDXILPrettyPrinterPass(*PassRegistry::getPassRegistry());
  }

  explicit DXILPrettyPrinter(raw_ostream &O) : ModulePass(ID), OS(O) {
    initializeDXILPrettyPrinterPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "DXIL Metadata Pretty Printer";
  }

  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<DXILResourceWrapper>();
  }
};
} // namespace

char DXILPrettyPrinter::ID = 0;
INITIALIZE_PASS_BEGIN(DXILPrettyPrinter, "dxil-pretty-printer",
                      "DXIL Metadata Pretty Printer", true, true)
INITIALIZE_PASS_DEPENDENCY(DXILResourceWrapper)
INITIALIZE_PASS_END(DXILPrettyPrinter, "dxil-pretty-printer",
                    "DXIL Metadata Pretty Printer", true, true)

bool DXILPrettyPrinter::runOnModule(Module &M) {
  dxil::Resources &Res = getAnalysis<DXILResourceWrapper>().getDXILResource();
  Res.print(OS);
  return false;
}

ModulePass *llvm::createDXILPrettyPrinterPass(raw_ostream &OS) {
  return new DXILPrettyPrinter(OS);
}
