//===-- UnreachableBlockElim.cpp - Remove unreachable blocks for codegen --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is an extremely simple version of the SimplifyCFG pass.  Its sole
// job is to delete LLVM basic blocks that are not reachable from the entry
// node.  To do this, it performs a simple depth first traversal of the CFG,
// then deletes any unvisited nodes.
//
// Note that this pass is really a hack.  In particular, the instruction
// selectors for various targets should just not generate code for unreachable
// blocks.  Until LLVM has a more systematic way of defining instruction
// selectors, however, we cannot really expect them to handle additional
// complexity.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

namespace {
class UnreachableBlockElimLegacyPass : public FunctionPass {
  bool runOnFunction(Function &F) override {
    return llvm::EliminateUnreachableBlocks(F);
  }

public:
  static char ID; // Pass identification, replacement for typeid
  UnreachableBlockElimLegacyPass() : FunctionPass(ID) {
    initializeUnreachableBlockElimLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<DominatorTreeWrapperPass>();
  }
};
}
char UnreachableBlockElimLegacyPass::ID = 0;
INITIALIZE_PASS(UnreachableBlockElimLegacyPass, "unreachableblockelim",
                "Remove unreachable blocks from the CFG", false, false)

FunctionPass *llvm::createUnreachableBlockEliminationPass() {
  return new UnreachableBlockElimLegacyPass();
}

PreservedAnalyses UnreachableBlockElimPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  bool Changed = llvm::EliminateUnreachableBlocks(F);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

namespace {
  class UnreachableMachineBlockElim : public MachineFunctionPass {
    bool runOnMachineFunction(MachineFunction &F) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;

  public:
    static char ID; // Pass identification, replacement for typeid
    UnreachableMachineBlockElim() : MachineFunctionPass(ID) {}
  };
}
char UnreachableMachineBlockElim::ID = 0;

INITIALIZE_PASS(UnreachableMachineBlockElim, "unreachable-mbb-elimination",
  "Remove unreachable machine basic blocks", false, false)

char &llvm::UnreachableMachineBlockElimID = UnreachableMachineBlockElim::ID;

void UnreachableMachineBlockElim::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool UnreachableMachineBlockElim::runOnMachineFunction(MachineFunction &F) {
  df_iterator_default_set<MachineBasicBlock*> Reachable;
  bool ModifiedPHI = false;

  MachineDominatorTreeWrapperPass *MDTWrapper =
      getAnalysisIfAvailable<MachineDominatorTreeWrapperPass>();
  MachineDominatorTree *MDT = MDTWrapper ? &MDTWrapper->getDomTree() : nullptr;
  MachineLoopInfoWrapperPass *MLIWrapper =
      getAnalysisIfAvailable<MachineLoopInfoWrapperPass>();
  MachineLoopInfo *MLI = MLIWrapper ? &MLIWrapper->getLI() : nullptr;

  // Mark all reachable blocks.
  for (MachineBasicBlock *BB : depth_first_ext(&F, Reachable))
    (void)BB/* Mark all reachable blocks */;

  // Loop over all dead blocks, remembering them and deleting all instructions
  // in them.
  std::vector<MachineBasicBlock*> DeadBlocks;
  for (MachineBasicBlock &BB : F) {
    // Test for deadness.
    if (!Reachable.count(&BB)) {
      DeadBlocks.push_back(&BB);

      // Update dominator and loop info.
      if (MLI) MLI->removeBlock(&BB);
      if (MDT && MDT->getNode(&BB)) MDT->eraseNode(&BB);

      while (!BB.succ_empty()) {
        MachineBasicBlock* succ = *BB.succ_begin();

        for (MachineInstr &Phi : succ->phis()) {
          for (unsigned i = Phi.getNumOperands() - 1; i >= 2; i -= 2) {
            if (Phi.getOperand(i).isMBB() &&
                Phi.getOperand(i).getMBB() == &BB) {
              Phi.removeOperand(i);
              Phi.removeOperand(i - 1);
            }
          }
        }

        BB.removeSuccessor(BB.succ_begin());
      }
    }
  }

  // Actually remove the blocks now.
  for (MachineBasicBlock *BB : DeadBlocks) {
    // Remove any call site information for calls in the block.
    for (auto &I : BB->instrs())
      if (I.shouldUpdateCallSiteInfo())
        BB->getParent()->eraseCallSiteInfo(&I);

    BB->eraseFromParent();
  }

  // Cleanup PHI nodes.
  for (MachineBasicBlock &BB : F) {
    // Prune unneeded PHI entries.
    SmallPtrSet<MachineBasicBlock*, 8> preds(BB.pred_begin(),
                                             BB.pred_end());
    for (MachineInstr &Phi : make_early_inc_range(BB.phis())) {
      for (unsigned i = Phi.getNumOperands() - 1; i >= 2; i -= 2) {
        if (!preds.count(Phi.getOperand(i).getMBB())) {
          Phi.removeOperand(i);
          Phi.removeOperand(i - 1);
          ModifiedPHI = true;
        }
      }

      if (Phi.getNumOperands() == 3) {
        const MachineOperand &Input = Phi.getOperand(1);
        const MachineOperand &Output = Phi.getOperand(0);
        Register InputReg = Input.getReg();
        Register OutputReg = Output.getReg();
        assert(Output.getSubReg() == 0 && "Cannot have output subregister");
        ModifiedPHI = true;

        if (InputReg != OutputReg) {
          MachineRegisterInfo &MRI = F.getRegInfo();
          unsigned InputSub = Input.getSubReg();
          if (InputSub == 0 &&
              MRI.constrainRegClass(InputReg, MRI.getRegClass(OutputReg)) &&
              !Input.isUndef()) {
            MRI.replaceRegWith(OutputReg, InputReg);
          } else {
            // The input register to the PHI has a subregister or it can't be
            // constrained to the proper register class or it is undef:
            // insert a COPY instead of simply replacing the output
            // with the input.
            const TargetInstrInfo *TII = F.getSubtarget().getInstrInfo();
            BuildMI(BB, BB.getFirstNonPHI(), Phi.getDebugLoc(),
                    TII->get(TargetOpcode::COPY), OutputReg)
                .addReg(InputReg, getRegState(Input), InputSub);
          }
          Phi.eraseFromParent();
        }
      }
    }
  }

  F.RenumberBlocks();

  return (!DeadBlocks.empty() || ModifiedPHI);
}
