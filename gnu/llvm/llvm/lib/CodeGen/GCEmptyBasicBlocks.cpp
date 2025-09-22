//===-- GCEmptyBasicBlocks.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of empty blocks garbage collection
/// pass.
///
//===----------------------------------------------------------------------===//
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "gc-empty-basic-blocks"

STATISTIC(NumEmptyBlocksRemoved, "Number of empty blocks removed");

class GCEmptyBasicBlocks : public MachineFunctionPass {
public:
  static char ID;

  GCEmptyBasicBlocks() : MachineFunctionPass(ID) {
    initializeGCEmptyBasicBlocksPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Remove Empty Basic Blocks.";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

bool GCEmptyBasicBlocks::runOnMachineFunction(MachineFunction &MF) {
  if (MF.size() < 2)
    return false;
  MachineJumpTableInfo *JTI = MF.getJumpTableInfo();
  int NumRemoved = 0;

  // Iterate over all blocks except the last one. We can't remove the last block
  // since it has no fallthrough block to rewire its predecessors to.
  for (MachineFunction::iterator MBB = MF.begin(),
                                 LastMBB = MachineFunction::iterator(MF.back()),
                                 NextMBB;
       MBB != LastMBB; MBB = NextMBB) {
    NextMBB = std::next(MBB);
    // TODO If a block is an eh pad, or it has address taken, we don't remove
    // it. Removing such blocks is possible, but it probably requires a more
    // complex logic.
    if (MBB->isEHPad() || MBB->hasAddressTaken())
      continue;
    // Skip blocks with real code.
    bool HasAnyRealCode = llvm::any_of(*MBB, [](const MachineInstr &MI) {
      return !MI.isPosition() && !MI.isImplicitDef() && !MI.isKill() &&
             !MI.isDebugInstr();
    });
    if (HasAnyRealCode)
      continue;

    LLVM_DEBUG(dbgs() << "Removing basic block " << MBB->getName()
                      << " in function " << MF.getName() << ":\n"
                      << *MBB << "\n");
    SmallVector<MachineBasicBlock *, 8> Preds(MBB->predecessors());
    // Rewire the predecessors of this block to use the next block.
    for (auto &Pred : Preds)
      Pred->ReplaceUsesOfBlockWith(&*MBB, &*NextMBB);
    // Update the jump tables.
    if (JTI)
      JTI->ReplaceMBBInJumpTables(&*MBB, &*NextMBB);
    // Remove this block from predecessors of all its successors.
    while (!MBB->succ_empty())
      MBB->removeSuccessor(MBB->succ_end() - 1);
    // Finally, remove the block from the function.
    MBB->eraseFromParent();
    ++NumRemoved;
  }
  NumEmptyBlocksRemoved += NumRemoved;
  return NumRemoved != 0;
}

char GCEmptyBasicBlocks::ID = 0;
INITIALIZE_PASS(GCEmptyBasicBlocks, "gc-empty-basic-blocks",
                "Removes empty basic blocks and redirects their uses to their "
                "fallthrough blocks.",
                false, false)

MachineFunctionPass *llvm::createGCEmptyBasicBlocksPass() {
  return new GCEmptyBasicBlocks();
}
