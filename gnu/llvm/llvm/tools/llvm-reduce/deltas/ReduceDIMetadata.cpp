//===- ReduceDIMetadata.cpp - Specialized Delta pass for DebugInfo --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements two functions used by the Generic Delta Debugging
// Algorithm, which are used to reduce DebugInfo metadata nodes.
//
//===----------------------------------------------------------------------===//

#include "ReduceDIMetadata.h"
#include "Delta.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/InstIterator.h"
#include <tuple>
#include <vector>

using namespace llvm;

using MDNodeList = SmallVector<MDNode *>;

void identifyUninterestingMDNodes(Oracle &O, MDNodeList &MDs) {
  SetVector<std::tuple<MDNode *, size_t, MDNode *>> Tuples;
  std::vector<MDNode *> ToLook;
  SetVector<MDNode *> Visited;

  // Start by looking at the attachments we collected
  for (const auto &NMD : MDs)
    if (NMD)
      ToLook.push_back(NMD);

  while (!ToLook.empty()) {
    MDNode *MD = ToLook.back();
    ToLook.pop_back();

    if (Visited.count(MD))
      continue;

    // Determine if the current MDNode is DebugInfo
    if (DINode *DIM = dyn_cast_or_null<DINode>(MD)) {
      // Scan operands and record attached tuples
      for (size_t I = 0; I < DIM->getNumOperands(); ++I)
        if (MDTuple *MDT = dyn_cast_or_null<MDTuple>(DIM->getOperand(I)))
          if (!Visited.count(MDT) && MDT->getNumOperands())
            Tuples.insert({DIM, I, MDT});
    }

    // Add all of the operands of the current node to the loop's todo list.
    for (Metadata *Op : MD->operands())
      if (MDNode *OMD = dyn_cast_or_null<MDNode>(Op))
        ToLook.push_back(OMD);

    Visited.insert(MD);
  }

  for (auto &T : Tuples) {
    auto [DbgNode, OpIdx, Tup] = T;
    // Remove the operands of the tuple that are not in the desired chunks.
    SmallVector<Metadata *, 16> TN;
    for (size_t I = 0; I < Tup->getNumOperands(); ++I) {
      // Ignore any operands that are not DebugInfo metadata nodes.
      if (Metadata *Op = Tup->getOperand(I).get()) {
        if (isa<DINode>(Op) || isa<DIGlobalVariableExpression>(Op))
          // Don't add uninteresting operands to the tuple.
          if (!O.shouldKeep())
            continue;
        TN.push_back(Op);
      }
    }
    if (TN.size() != Tup->getNumOperands())
      DbgNode->replaceOperandWith(OpIdx, DbgNode->get(DbgNode->getContext(), TN));
  }
}

static void extractDIMetadataFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();

  MDNodeList MDs;
  // Collect all !dbg metadata attachments.
  for (const auto &DC : Program.debug_compile_units())
    if (DC)
      MDs.push_back(DC);
  for (GlobalVariable &GV : Program.globals())
    GV.getMetadata(llvm::LLVMContext::MD_dbg, MDs);
  for (Function &F : Program.functions()) {
    F.getMetadata(llvm::LLVMContext::MD_dbg, MDs);
    for (Instruction &I : instructions(F))
      if (auto *DI = I.getMetadata(llvm::LLVMContext::MD_dbg))
        MDs.push_back(DI);
  }
  identifyUninterestingMDNodes(O, MDs);
}

void llvm::reduceDIMetadataDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractDIMetadataFromModule, "Reducing DIMetadata");
}
