//===- StripGCRelocates.cpp - Remove gc.relocates inserted by RewriteStatePoints===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a little utility pass that removes the gc.relocates inserted by
// RewriteStatepointsForGC. Note that the generated IR is incorrect,
// but this is useful as a single pass in itself, for analysis of IR, without
// the GC.relocates. The statepoint and gc.result intrinsics would still be
// present.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/StripGCRelocates.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Statepoint.h"

using namespace llvm;

static bool stripGCRelocates(Function &F) {
  // Nothing to do for declarations.
  if (F.isDeclaration())
    return false;
  SmallVector<GCRelocateInst *, 20> GCRelocates;
  // TODO: We currently do not handle gc.relocates that are in landing pads,
  // i.e. not bound to a single statepoint token.
  for (Instruction &I : instructions(F)) {
    if (auto *GCR = dyn_cast<GCRelocateInst>(&I))
      if (isa<GCStatepointInst>(GCR->getOperand(0)))
        GCRelocates.push_back(GCR);
  }
  // All gc.relocates are bound to a single statepoint token. The order of
  // visiting gc.relocates for deletion does not matter.
  for (GCRelocateInst *GCRel : GCRelocates) {
    Value *OrigPtr = GCRel->getDerivedPtr();
    Value *ReplaceGCRel = OrigPtr;

    // All gc_relocates are i8 addrspace(1)* typed, we need a bitcast from i8
    // addrspace(1)* to the type of the OrigPtr, if the are not the same.
    if (GCRel->getType() != OrigPtr->getType())
      ReplaceGCRel = new BitCastInst(OrigPtr, GCRel->getType(), "cast", GCRel->getIterator());

    // Replace all uses of gc.relocate and delete the gc.relocate
    // There maybe unncessary bitcasts back to the OrigPtr type, an instcombine
    // pass would clear this up.
    GCRel->replaceAllUsesWith(ReplaceGCRel);
    GCRel->eraseFromParent();
  }
  return !GCRelocates.empty();
}

PreservedAnalyses StripGCRelocates::run(Function &F,
                                        FunctionAnalysisManager &AM) {
  if (!stripGCRelocates(F))
    return PreservedAnalyses::all();

  // Removing gc.relocate preserves the CFG, but most other analysis probably
  // need to re-run.
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
