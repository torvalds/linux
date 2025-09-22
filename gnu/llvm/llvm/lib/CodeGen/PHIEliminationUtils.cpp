//===-- PHIEliminationUtils.cpp - Helper functions for PHI elimination ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PHIEliminationUtils.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

// findCopyInsertPoint - Find a safe place in MBB to insert a copy from SrcReg
// when following the CFG edge to SuccMBB. This needs to be after any def of
// SrcReg, but before any subsequent point where control flow might jump out of
// the basic block.
MachineBasicBlock::iterator
llvm::findPHICopyInsertPoint(MachineBasicBlock* MBB, MachineBasicBlock* SuccMBB,
                             unsigned SrcReg) {
  // Handle the trivial case trivially.
  if (MBB->empty())
    return MBB->begin();

  // Usually, we just want to insert the copy before the first terminator
  // instruction. However, for the edge going to a landing pad, we must insert
  // the copy before the call/invoke instruction. Similarly for an INLINEASM_BR
  // going to an indirect target. This is similar to SplitKit.cpp's
  // computeLastInsertPoint, and similarly assumes that there cannot be multiple
  // instructions that are Calls with EHPad successors or INLINEASM_BR in a
  // block.
  bool EHPadSuccessor = SuccMBB->isEHPad();
  if (!EHPadSuccessor && !SuccMBB->isInlineAsmBrIndirectTarget())
    return MBB->getFirstTerminator();

  // Discover any defs in this basic block.
  SmallPtrSet<MachineInstr *, 8> DefsInMBB;
  MachineRegisterInfo& MRI = MBB->getParent()->getRegInfo();
  for (MachineInstr &RI : MRI.def_instructions(SrcReg))
    if (RI.getParent() == MBB)
      DefsInMBB.insert(&RI);

  MachineBasicBlock::iterator InsertPoint = MBB->begin();
  // Insert the copy at the _latest_ point of:
  // 1. Immediately AFTER the last def
  // 2. Immediately BEFORE a call/inlineasm_br.
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    if (DefsInMBB.contains(&*I)) {
      InsertPoint = std::next(I.getReverse());
      break;
    }
    if ((EHPadSuccessor && I->isCall()) ||
        I->getOpcode() == TargetOpcode::INLINEASM_BR) {
      InsertPoint = I.getReverse();
      break;
    }
  }

  // Make sure the copy goes after any phi nodes but before
  // any debug nodes.
  return MBB->SkipPHIsAndLabels(InsertPoint);
}
