//===-- ARMOptimizeBarriersPass - two DMBs without a memory access in between,
//removed one -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===------------------------------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMInstrInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "double barriers"

STATISTIC(NumDMBsRemoved, "Number of DMBs removed");

namespace {
class ARMOptimizeBarriersPass : public MachineFunctionPass {
public:
  static char ID;
  ARMOptimizeBarriersPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &Fn) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override { return "optimise barriers pass"; }
};
char ARMOptimizeBarriersPass::ID = 0;
}

// Returns whether the instruction can safely move past a DMB instruction
// The current implementation allows this iif MI does not have any possible
// memory access
static bool CanMovePastDMB(const MachineInstr *MI) {
  return !(MI->mayLoad() ||
          MI->mayStore() ||
          MI->hasUnmodeledSideEffects() ||
          MI->isCall() ||
          MI->isReturn());
}

bool ARMOptimizeBarriersPass::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  // Vector to store the DMBs we will remove after the first iteration
  std::vector<MachineInstr *> ToRemove;
  // DMBType is the Imm value of the first operand. It determines whether it's a
  // DMB ish, dmb sy, dmb osh, etc
  int64_t DMBType = -1;

  // Find a dmb. If we can move it until the next dmb, tag the second one for
  // removal
  for (auto &MBB : MF) {
    // Will be true when we have seen a DMB, and not seen any instruction since
    // that cannot move past a DMB
    bool IsRemovableNextDMB = false;
    for (auto &MI : MBB) {
      if (MI.getOpcode() == ARM::DMB) {
        if (IsRemovableNextDMB) {
          // If the Imm of this DMB is the same as that of the last DMB, we can
          // tag this second DMB for removal
          if (MI.getOperand(0).getImm() == DMBType) {
            ToRemove.push_back(&MI);
          } else {
            // If it has a different DMBType, we cannot remove it, but will scan
            // for the next DMB, recording this DMB's type as last seen DMB type
            DMBType = MI.getOperand(0).getImm();
          }
        } else {
          // After we see a DMB, a next one is removable
          IsRemovableNextDMB = true;
          DMBType = MI.getOperand(0).getImm();
        }
      } else if (!CanMovePastDMB(&MI)) {
        // If we find an instruction unable to pass past a DMB, a next DMB is
        // not removable
        IsRemovableNextDMB = false;
      }
    }
  }
  bool Changed = false;
  // Remove the tagged DMB
  for (auto *MI : ToRemove) {
    MI->eraseFromParent();
    ++NumDMBsRemoved;
    Changed = true;
  }

  return Changed;
}

/// createARMOptimizeBarriersPass - Returns an instance of the remove double
/// barriers
/// pass.
FunctionPass *llvm::createARMOptimizeBarriersPass() {
  return new ARMOptimizeBarriersPass();
}
