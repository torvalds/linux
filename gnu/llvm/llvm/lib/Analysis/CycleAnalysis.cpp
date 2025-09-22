//===- CycleAnalysis.cpp - Compute CycleInfo for LLVM IR ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/ADT/GenericCycleImpl.h"
#include "llvm/IR/CFG.h" // for successors found by ADL in GenericCycleImpl.h
#include "llvm/InitializePasses.h"

using namespace llvm;

namespace llvm {
class Module;
} // namespace llvm

CycleInfo CycleAnalysis::run(Function &F, FunctionAnalysisManager &) {
  CycleInfo CI;
  CI.compute(F);
  return CI;
}

AnalysisKey CycleAnalysis::Key;

CycleInfoPrinterPass::CycleInfoPrinterPass(raw_ostream &OS) : OS(OS) {}

PreservedAnalyses CycleInfoPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  OS << "CycleInfo for function: " << F.getName() << "\n";
  AM.getResult<CycleAnalysis>(F).print(OS);

  return PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
//  CycleInfoWrapperPass Implementation
//===----------------------------------------------------------------------===//
//
// The implementation details of the wrapper pass that holds a CycleInfo
// suitable for use with the legacy pass manager.
//
//===----------------------------------------------------------------------===//

char CycleInfoWrapperPass::ID = 0;

CycleInfoWrapperPass::CycleInfoWrapperPass() : FunctionPass(ID) {
  initializeCycleInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(CycleInfoWrapperPass, "cycles", "Cycle Info Analysis",
                      true, true)
INITIALIZE_PASS_END(CycleInfoWrapperPass, "cycles", "Cycle Info Analysis", true,
                    true)

void CycleInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

bool CycleInfoWrapperPass::runOnFunction(Function &Func) {
  CI.clear();

  F = &Func;
  CI.compute(Func);
  return false;
}

void CycleInfoWrapperPass::print(raw_ostream &OS, const Module *) const {
  OS << "CycleInfo for function: " << F->getName() << "\n";
  CI.print(OS);
}

void CycleInfoWrapperPass::releaseMemory() {
  CI.clear();
  F = nullptr;
}
