//===------------- PPCEarlyReturn.cpp - Form Early Returns ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A pass that form early (predicated) returns. If-conversion handles some of
// this, but this pass picks up some remaining cases.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-early-ret"
STATISTIC(NumBCLR, "Number of early conditional returns");
STATISTIC(NumBLR,  "Number of early returns");

namespace {
  // PPCEarlyReturn pass - For simple functions without epilogue code, move
  // returns up, and create conditional returns, to avoid unnecessary
  // branch-to-blr sequences.
  struct PPCEarlyReturn : public MachineFunctionPass {
    static char ID;
    PPCEarlyReturn() : MachineFunctionPass(ID) {
      initializePPCEarlyReturnPass(*PassRegistry::getPassRegistry());
    }

    const TargetInstrInfo *TII;

protected:
    bool processBlock(MachineBasicBlock &ReturnMBB) {
      bool Changed = false;

      MachineBasicBlock::iterator I = ReturnMBB.begin();
      I = ReturnMBB.SkipPHIsLabelsAndDebug(I);

      // The block must be essentially empty except for the blr.
      if (I == ReturnMBB.end() ||
          (I->getOpcode() != PPC::BLR && I->getOpcode() != PPC::BLR8) ||
          I != ReturnMBB.getLastNonDebugInstr())
        return Changed;

      SmallVector<MachineBasicBlock*, 8> PredToRemove;
      for (MachineBasicBlock *Pred : ReturnMBB.predecessors()) {
        bool OtherReference = false, BlockChanged = false;

        if (Pred->empty())
          continue;

        for (MachineBasicBlock::iterator J = Pred->getLastNonDebugInstr();;) {
          if (J == Pred->end())
            break;

          if (J->getOpcode() == PPC::B) {
            if (J->getOperand(0).getMBB() == &ReturnMBB) {
              // This is an unconditional branch to the return. Replace the
              // branch with a blr.
              MachineInstr *MI = ReturnMBB.getParent()->CloneMachineInstr(&*I);
              Pred->insert(J, MI);

              MachineBasicBlock::iterator K = J--;
              K->eraseFromParent();
              BlockChanged = true;
              ++NumBLR;
              continue;
            }
          } else if (J->getOpcode() == PPC::BCC) {
            if (J->getOperand(2).getMBB() == &ReturnMBB) {
              // This is a conditional branch to the return. Replace the branch
              // with a bclr.
              MachineInstr *MI = ReturnMBB.getParent()->CloneMachineInstr(&*I);
              MI->setDesc(TII->get(PPC::BCCLR));
              MachineInstrBuilder(*ReturnMBB.getParent(), MI)
                  .add(J->getOperand(0))
                  .add(J->getOperand(1));
              Pred->insert(J, MI);

              MachineBasicBlock::iterator K = J--;
              K->eraseFromParent();
              BlockChanged = true;
              ++NumBCLR;
              continue;
            }
          } else if (J->getOpcode() == PPC::BC || J->getOpcode() == PPC::BCn) {
            if (J->getOperand(1).getMBB() == &ReturnMBB) {
              // This is a conditional branch to the return. Replace the branch
              // with a bclr.
              MachineInstr *MI = ReturnMBB.getParent()->CloneMachineInstr(&*I);
              MI->setDesc(
                  TII->get(J->getOpcode() == PPC::BC ? PPC::BCLR : PPC::BCLRn));
              MachineInstrBuilder(*ReturnMBB.getParent(), MI)
                  .add(J->getOperand(0));
              Pred->insert(J, MI);

              MachineBasicBlock::iterator K = J--;
              K->eraseFromParent();
              BlockChanged = true;
              ++NumBCLR;
              continue;
            }
          } else if (J->isBranch()) {
            if (J->isIndirectBranch()) {
              if (ReturnMBB.hasAddressTaken())
                OtherReference = true;
            } else
              for (unsigned i = 0; i < J->getNumOperands(); ++i)
                if (J->getOperand(i).isMBB() &&
                    J->getOperand(i).getMBB() == &ReturnMBB)
                  OtherReference = true;
          } else if (!J->isTerminator() && !J->isDebugInstr())
            break;

          if (J == Pred->begin())
            break;

          --J;
        }

        if (Pred->canFallThrough() && Pred->isLayoutSuccessor(&ReturnMBB))
          OtherReference = true;

        // Predecessors are stored in a vector and can't be removed here.
        if (!OtherReference && BlockChanged) {
          PredToRemove.push_back(Pred);
        }

        if (BlockChanged)
          Changed = true;
      }

      for (MachineBasicBlock *MBB : PredToRemove)
        MBB->removeSuccessor(&ReturnMBB, true);

      if (Changed && !ReturnMBB.hasAddressTaken()) {
        // We now might be able to merge this blr-only block into its
        // by-layout predecessor.
        if (ReturnMBB.pred_size() == 1) {
          MachineBasicBlock &PrevMBB = **ReturnMBB.pred_begin();
          if (PrevMBB.isLayoutSuccessor(&ReturnMBB) && PrevMBB.canFallThrough()) {
            // Move the blr into the preceding block.
            PrevMBB.splice(PrevMBB.end(), &ReturnMBB, I);
            PrevMBB.removeSuccessor(&ReturnMBB, true);
          }
        }

        if (ReturnMBB.pred_empty())
          ReturnMBB.eraseFromParent();
      }

      return Changed;
    }

public:
    bool runOnMachineFunction(MachineFunction &MF) override {
      if (skipFunction(MF.getFunction()))
        return false;

      TII = MF.getSubtarget().getInstrInfo();

      bool Changed = false;

      // If the function does not have at least two blocks, then there is
      // nothing to do.
      if (MF.size() < 2)
        return Changed;

      for (MachineBasicBlock &B : llvm::make_early_inc_range(MF))
        Changed |= processBlock(B);

      return Changed;
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS(PPCEarlyReturn, DEBUG_TYPE,
                "PowerPC Early-Return Creation", false, false)

char PPCEarlyReturn::ID = 0;
FunctionPass*
llvm::createPPCEarlyReturnPass() { return new PPCEarlyReturn(); }
