//===-- PPCCTRLoops.cpp - Verify CTR loops -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass verifies that all bdnz/bdz instructions are dominated by a loop
// mtctr before any other instructions that might clobber the ctr register.
//
//===----------------------------------------------------------------------===//

// CTR loops are produced by the HardwareLoops pass and this pass is simply a
// verification that no invalid CTR loops are produced. As such, it isn't
// something that needs to be run (or even defined) for Release builds so the
// entire file is guarded by NDEBUG.
#ifndef NDEBUG
#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "PPC.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GenericDomTreeConstruction.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-ctrloops-verify"

namespace {

  struct PPCCTRLoopsVerify : public MachineFunctionPass {
  public:
    static char ID;

    PPCCTRLoopsVerify() : MachineFunctionPass(ID) {
      initializePPCCTRLoopsVerifyPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    MachineDominatorTree *MDT;
  };

  char PPCCTRLoopsVerify::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS_BEGIN(PPCCTRLoopsVerify, "ppc-ctr-loops-verify",
                      "PowerPC CTR Loops Verify", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(PPCCTRLoopsVerify, "ppc-ctr-loops-verify",
                    "PowerPC CTR Loops Verify", false, false)

FunctionPass *llvm::createPPCCTRLoopsVerify() {
  return new PPCCTRLoopsVerify();
}

static bool clobbersCTR(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg()) {
      if (MO.isDef() && (MO.getReg() == PPC::CTR || MO.getReg() == PPC::CTR8))
        return true;
    } else if (MO.isRegMask()) {
      if (MO.clobbersPhysReg(PPC::CTR) || MO.clobbersPhysReg(PPC::CTR8))
        return true;
    }
  }

  return false;
}

static bool verifyCTRBranch(MachineBasicBlock *MBB,
                            MachineBasicBlock::iterator I) {
  MachineBasicBlock::iterator BI = I;
  SmallSet<MachineBasicBlock *, 16>   Visited;
  SmallVector<MachineBasicBlock *, 8> Preds;
  bool CheckPreds;

  if (I == MBB->begin()) {
    Visited.insert(MBB);
    goto queue_preds;
  } else
    --I;

check_block:
  Visited.insert(MBB);
  if (I == MBB->end())
    goto queue_preds;

  CheckPreds = true;
  for (MachineBasicBlock::iterator IE = MBB->begin();; --I) {
    unsigned Opc = I->getOpcode();
    if (Opc == PPC::MTCTRloop || Opc == PPC::MTCTR8loop) {
      CheckPreds = false;
      break;
    }

    if (I != BI && clobbersCTR(*I)) {
      LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " (" << MBB->getFullName()
                        << ") instruction " << *I
                        << " clobbers CTR, invalidating "
                        << printMBBReference(*BI->getParent()) << " ("
                        << BI->getParent()->getFullName() << ") instruction "
                        << *BI << "\n");
      return false;
    }

    if (I == IE)
      break;
  }

  if (!CheckPreds && Preds.empty())
    return true;

  if (CheckPreds) {
queue_preds:
    if (MachineFunction::iterator(MBB) == MBB->getParent()->begin()) {
      LLVM_DEBUG(dbgs() << "Unable to find a MTCTR instruction for "
                        << printMBBReference(*BI->getParent()) << " ("
                        << BI->getParent()->getFullName() << ") instruction "
                        << *BI << "\n");
      return false;
    }

    append_range(Preds, MBB->predecessors());
  }

  do {
    MBB = Preds.pop_back_val();
    if (!Visited.count(MBB)) {
      I = MBB->getLastNonDebugInstr();
      goto check_block;
    }
  } while (!Preds.empty());

  return true;
}

bool PPCCTRLoopsVerify::runOnMachineFunction(MachineFunction &MF) {
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();

  // Verify that all bdnz/bdz instructions are dominated by a loop mtctr before
  // any other instructions that might clobber the ctr register.
  for (MachineBasicBlock &MBB : MF) {
    if (!MDT->isReachableFromEntry(&MBB))
      continue;

    for (MachineBasicBlock::iterator MII = MBB.getFirstTerminator(),
      MIIE = MBB.end(); MII != MIIE; ++MII) {
      unsigned Opc = MII->getOpcode();
      if (Opc == PPC::BDNZ8 || Opc == PPC::BDNZ ||
          Opc == PPC::BDZ8  || Opc == PPC::BDZ)
        if (!verifyCTRBranch(&MBB, MII))
          llvm_unreachable("Invalid PPC CTR loop!");
    }
  }

  return false;
}
#endif // NDEBUG
