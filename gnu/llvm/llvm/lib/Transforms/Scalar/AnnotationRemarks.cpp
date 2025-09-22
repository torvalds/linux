//===-- AnnotationRemarks.cpp - Generate remarks for annotated instrs. ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generate remarks for instructions marked with !annotation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/AnnotationRemarks.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Transforms/Utils/MemoryOpRemark.h"

using namespace llvm;
using namespace llvm::ore;

#define DEBUG_TYPE "annotation-remarks"
#define REMARK_PASS DEBUG_TYPE

static void tryEmitAutoInitRemark(ArrayRef<Instruction *> Instructions,
                                  OptimizationRemarkEmitter &ORE,
                                  const TargetLibraryInfo &TLI) {
  // For every auto-init annotation generate a separate remark.
  for (Instruction *I : Instructions) {
    if (!AutoInitRemark::canHandle(I))
      continue;

    Function &F = *I->getParent()->getParent();
    const DataLayout &DL = F.getDataLayout();
    AutoInitRemark Remark(ORE, REMARK_PASS, DL, TLI);
    Remark.visit(I);
  }
}

static void runImpl(Function &F, const TargetLibraryInfo &TLI) {
  if (!OptimizationRemarkEmitter::allowExtraAnalysis(F, REMARK_PASS))
    return;

  // Track all annotated instructions aggregated based on their debug location.
  DenseMap<MDNode *, SmallVector<Instruction *, 4>> DebugLoc2Annotated;

  OptimizationRemarkEmitter ORE(&F);
  // First, generate a summary of the annotated instructions.
  MapVector<StringRef, unsigned> Mapping;
  for (Instruction &I : instructions(F)) {
    if (!I.hasMetadata(LLVMContext::MD_annotation))
      continue;
    auto Iter = DebugLoc2Annotated.insert({I.getDebugLoc().getAsMDNode(), {}});
    Iter.first->second.push_back(&I);

    for (const MDOperand &Op :
         I.getMetadata(LLVMContext::MD_annotation)->operands()) {
      StringRef AnnotationStr =
          isa<MDString>(Op.get())
              ? cast<MDString>(Op.get())->getString()
              : cast<MDString>(cast<MDTuple>(Op.get())->getOperand(0).get())
                    ->getString();
      auto Iter = Mapping.insert({AnnotationStr, 0});
      Iter.first->second++;
    }
  }

  for (const auto &KV : Mapping)
    ORE.emit(OptimizationRemarkAnalysis(REMARK_PASS, "AnnotationSummary",
                                        F.getSubprogram(), &F.front())
             << "Annotated " << NV("count", KV.second) << " instructions with "
             << NV("type", KV.first));

  // For each debug location, look for all the instructions with annotations and
  // generate more detailed remarks to be displayed at that location.
  for (auto &KV : DebugLoc2Annotated) {
    // Don't generate remarks with no debug location.
    if (!KV.first)
      continue;

    tryEmitAutoInitRemark(KV.second, ORE, TLI);
  }
}

PreservedAnalyses AnnotationRemarksPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  runImpl(F, TLI);
  return PreservedAnalyses::all();
}
