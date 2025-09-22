//===- OptimizePHIs.cpp - Optimize machine instruction PHIs ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass optimizes machine instruction PHIs to take advantage of
// opportunities created during DAG legalization.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "opt-phis"

STATISTIC(NumPHICycles, "Number of PHI cycles replaced");
STATISTIC(NumDeadPHICycles, "Number of dead PHI cycles");

namespace {

  class OptimizePHIs : public MachineFunctionPass {
    MachineRegisterInfo *MRI = nullptr;
    const TargetInstrInfo *TII = nullptr;

  public:
    static char ID; // Pass identification

    OptimizePHIs() : MachineFunctionPass(ID) {
      initializeOptimizePHIsPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &Fn) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

  private:
    using InstrSet = SmallPtrSet<MachineInstr *, 16>;
    using InstrSetIterator = SmallPtrSetIterator<MachineInstr *>;

    bool IsSingleValuePHICycle(MachineInstr *MI, unsigned &SingleValReg,
                               InstrSet &PHIsInCycle);
    bool IsDeadPHICycle(MachineInstr *MI, InstrSet &PHIsInCycle);
    bool OptimizeBB(MachineBasicBlock &MBB);
  };

} // end anonymous namespace

char OptimizePHIs::ID = 0;

char &llvm::OptimizePHIsID = OptimizePHIs::ID;

INITIALIZE_PASS(OptimizePHIs, DEBUG_TYPE,
                "Optimize machine instruction PHIs", false, false)

bool OptimizePHIs::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  MRI = &Fn.getRegInfo();
  TII = Fn.getSubtarget().getInstrInfo();

  // Find dead PHI cycles and PHI cycles that can be replaced by a single
  // value.  InstCombine does these optimizations, but DAG legalization may
  // introduce new opportunities, e.g., when i64 values are split up for
  // 32-bit targets.
  bool Changed = false;
  for (MachineBasicBlock &MBB : Fn)
    Changed |= OptimizeBB(MBB);

  return Changed;
}

/// IsSingleValuePHICycle - Check if MI is a PHI where all the source operands
/// are copies of SingleValReg, possibly via copies through other PHIs. If
/// SingleValReg is zero on entry, it is set to the register with the single
/// non-copy value. PHIsInCycle is a set used to keep track of the PHIs that
/// have been scanned. PHIs may be grouped by cycle, several cycles or chains.
bool OptimizePHIs::IsSingleValuePHICycle(MachineInstr *MI,
                                         unsigned &SingleValReg,
                                         InstrSet &PHIsInCycle) {
  assert(MI->isPHI() && "IsSingleValuePHICycle expects a PHI instruction");
  Register DstReg = MI->getOperand(0).getReg();

  // See if we already saw this register.
  if (!PHIsInCycle.insert(MI).second)
    return true;

  // Don't scan crazily complex things.
  if (PHIsInCycle.size() == 16)
    return false;

  // Scan the PHI operands.
  for (unsigned i = 1; i != MI->getNumOperands(); i += 2) {
    Register SrcReg = MI->getOperand(i).getReg();
    if (SrcReg == DstReg)
      continue;
    MachineInstr *SrcMI = MRI->getVRegDef(SrcReg);

    // Skip over register-to-register moves.
    if (SrcMI && SrcMI->isCopy() && !SrcMI->getOperand(0).getSubReg() &&
        !SrcMI->getOperand(1).getSubReg() &&
        SrcMI->getOperand(1).getReg().isVirtual()) {
      SrcReg = SrcMI->getOperand(1).getReg();
      SrcMI = MRI->getVRegDef(SrcReg);
    }
    if (!SrcMI)
      return false;

    if (SrcMI->isPHI()) {
      if (!IsSingleValuePHICycle(SrcMI, SingleValReg, PHIsInCycle))
        return false;
    } else {
      // Fail if there is more than one non-phi/non-move register.
      if (SingleValReg != 0 && SingleValReg != SrcReg)
        return false;
      SingleValReg = SrcReg;
    }
  }
  return true;
}

/// IsDeadPHICycle - Check if the register defined by a PHI is only used by
/// other PHIs in a cycle.
bool OptimizePHIs::IsDeadPHICycle(MachineInstr *MI, InstrSet &PHIsInCycle) {
  assert(MI->isPHI() && "IsDeadPHICycle expects a PHI instruction");
  Register DstReg = MI->getOperand(0).getReg();
  assert(DstReg.isVirtual() && "PHI destination is not a virtual register");

  // See if we already saw this register.
  if (!PHIsInCycle.insert(MI).second)
    return true;

  // Don't scan crazily complex things.
  if (PHIsInCycle.size() == 16)
    return false;

  for (MachineInstr &UseMI : MRI->use_nodbg_instructions(DstReg)) {
    if (!UseMI.isPHI() || !IsDeadPHICycle(&UseMI, PHIsInCycle))
      return false;
  }

  return true;
}

/// OptimizeBB - Remove dead PHI cycles and PHI cycles that can be replaced by
/// a single value.
bool OptimizePHIs::OptimizeBB(MachineBasicBlock &MBB) {
  bool Changed = false;
  for (MachineBasicBlock::iterator
         MII = MBB.begin(), E = MBB.end(); MII != E; ) {
    MachineInstr *MI = &*MII++;
    if (!MI->isPHI())
      break;

    // Check for single-value PHI cycles.
    unsigned SingleValReg = 0;
    InstrSet PHIsInCycle;
    if (IsSingleValuePHICycle(MI, SingleValReg, PHIsInCycle) &&
        SingleValReg != 0) {
      Register OldReg = MI->getOperand(0).getReg();
      if (!MRI->constrainRegClass(SingleValReg, MRI->getRegClass(OldReg)))
        continue;

      MRI->replaceRegWith(OldReg, SingleValReg);
      MI->eraseFromParent();

      // The kill flags on OldReg and SingleValReg may no longer be correct.
      MRI->clearKillFlags(SingleValReg);

      ++NumPHICycles;
      Changed = true;
      continue;
    }

    // Check for dead PHI cycles.
    PHIsInCycle.clear();
    if (IsDeadPHICycle(MI, PHIsInCycle)) {
      for (MachineInstr *PhiMI : PHIsInCycle) {
        if (MII == PhiMI)
          ++MII;
        PhiMI->eraseFromParent();
      }
      ++NumDeadPHICycles;
      Changed = true;
    }
  }
  return Changed;
}
