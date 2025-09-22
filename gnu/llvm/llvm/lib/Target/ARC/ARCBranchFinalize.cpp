//===- ARCBranchFinalize.cpp - ARC conditional branches ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass takes existing conditional branches and expands them into longer
// range conditional branches.
//===----------------------------------------------------------------------===//

#include "ARCInstrInfo.h"
#include "ARCTargetMachine.h"
#include "MCTargetDesc/ARCInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include <vector>

#define DEBUG_TYPE "arc-branch-finalize"

using namespace llvm;

namespace llvm {

void initializeARCBranchFinalizePass(PassRegistry &Registry);
FunctionPass *createARCBranchFinalizePass();

} // end namespace llvm

namespace {

class ARCBranchFinalize : public MachineFunctionPass {
public:
  static char ID;

  ARCBranchFinalize() : MachineFunctionPass(ID) {
    initializeARCBranchFinalizePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "ARC Branch Finalization Pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void replaceWithBRcc(MachineInstr *MI) const;
  void replaceWithCmpBcc(MachineInstr *MI) const;

private:
  const ARCInstrInfo *TII{nullptr};
};

char ARCBranchFinalize::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(ARCBranchFinalize, "arc-branch-finalize",
                      "ARC finalize branches", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(ARCBranchFinalize, "arc-branch-finalize",
                    "ARC finalize branches", false, false)

// BRcc has 6 supported condition codes, which differ from the 16
// condition codes supported in the predicated instructions:
// EQ -- 000
// NE -- 001
// LT -- 010
// GE -- 011
// LO -- 100
// HS -- 101
static unsigned getCCForBRcc(unsigned CC) {
  switch (CC) {
  case ARCCC::EQ:
    return 0;
  case ARCCC::NE:
    return 1;
  case ARCCC::LT:
    return 2;
  case ARCCC::GE:
    return 3;
  case ARCCC::LO:
    return 4;
  case ARCCC::HS:
    return 5;
  default:
    return -1U;
  }
}

static bool isBRccPseudo(MachineInstr *MI) {
  return !(MI->getOpcode() != ARC::BRcc_rr_p &&
           MI->getOpcode() != ARC::BRcc_ru6_p);
}

static unsigned getBRccForPseudo(MachineInstr *MI) {
  assert(isBRccPseudo(MI) && "Can't get BRcc for wrong instruction.");
  if (MI->getOpcode() == ARC::BRcc_rr_p)
    return ARC::BRcc_rr;
  return ARC::BRcc_ru6;
}

static unsigned getCmpForPseudo(MachineInstr *MI) {
  assert(isBRccPseudo(MI) && "Can't get BRcc for wrong instruction.");
  if (MI->getOpcode() == ARC::BRcc_rr_p)
    return ARC::CMP_rr;
  return ARC::CMP_ru6;
}

void ARCBranchFinalize::replaceWithBRcc(MachineInstr *MI) const {
  LLVM_DEBUG(dbgs() << "Replacing pseudo branch with BRcc\n");
  unsigned CC = getCCForBRcc(MI->getOperand(3).getImm());
  if (CC != -1U) {
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
            TII->get(getBRccForPseudo(MI)))
        .addMBB(MI->getOperand(0).getMBB())
        .addReg(MI->getOperand(1).getReg())
        .add(MI->getOperand(2))
        .addImm(getCCForBRcc(MI->getOperand(3).getImm()));
    MI->eraseFromParent();
  } else {
    replaceWithCmpBcc(MI);
  }
}

void ARCBranchFinalize::replaceWithCmpBcc(MachineInstr *MI) const {
  LLVM_DEBUG(dbgs() << "Branch: " << *MI << "\n");
  LLVM_DEBUG(dbgs() << "Replacing pseudo branch with Cmp + Bcc\n");
  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
          TII->get(getCmpForPseudo(MI)))
      .addReg(MI->getOperand(1).getReg())
      .add(MI->getOperand(2));
  BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), TII->get(ARC::Bcc))
      .addMBB(MI->getOperand(0).getMBB())
      .addImm(MI->getOperand(3).getImm());
  MI->eraseFromParent();
}

bool ARCBranchFinalize::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "Running ARC Branch Finalize on " << MF.getName()
                    << "\n");
  std::vector<MachineInstr *> Branches;
  bool Changed = false;
  unsigned MaxSize = 0;
  TII = MF.getSubtarget<ARCSubtarget>().getInstrInfo();
  std::map<MachineBasicBlock *, unsigned> BlockToPCMap;
  std::vector<std::pair<MachineInstr *, unsigned>> BranchToPCList;
  unsigned PC = 0;

  for (auto &MBB : MF) {
    BlockToPCMap.insert(std::make_pair(&MBB, PC));
    for (auto &MI : MBB) {
      unsigned Size = TII->getInstSizeInBytes(MI);
      if (Size > 8 || Size == 0) {
        LLVM_DEBUG(dbgs() << "Unknown (or size 0) size for: " << MI << "\n");
      } else {
        MaxSize += Size;
      }
      if (MI.isBranch()) {
        Branches.push_back(&MI);
        BranchToPCList.emplace_back(&MI, PC);
      }
      PC += Size;
    }
  }
  for (auto P : BranchToPCList) {
    if (isBRccPseudo(P.first))
      isInt<9>(MaxSize) ? replaceWithBRcc(P.first) : replaceWithCmpBcc(P.first);
  }

  LLVM_DEBUG(dbgs() << "Estimated function size for " << MF.getName() << ": "
                    << MaxSize << "\n");

  return Changed;
}

FunctionPass *llvm::createARCBranchFinalizePass() {
  return new ARCBranchFinalize();
}
