//===-- AArch64BranchTargets.cpp -- Harden code using v8.5-A BTI extension -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass inserts BTI instructions at the start of every function and basic
// block which could be indirectly called. The hardware will (when enabled)
// trap when an indirect branch or call instruction targets an instruction
// which is not a valid BTI instruction. This is intended to guard against
// control-flow hijacking attacks. Note that this does not do anything for RET
// instructions, as they can be more precisely protected by return address
// signing.
//
//===----------------------------------------------------------------------===//

#include "AArch64MachineFunctionInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-branch-targets"
#define AARCH64_BRANCH_TARGETS_NAME "AArch64 Branch Targets"

namespace {
class AArch64BranchTargets : public MachineFunctionPass {
public:
  static char ID;
  AArch64BranchTargets() : MachineFunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return AARCH64_BRANCH_TARGETS_NAME; }

private:
  void addBTI(MachineBasicBlock &MBB, bool CouldCall, bool CouldJump,
              bool NeedsWinCFI);
};
} // end anonymous namespace

char AArch64BranchTargets::ID = 0;

INITIALIZE_PASS(AArch64BranchTargets, "aarch64-branch-targets",
                AARCH64_BRANCH_TARGETS_NAME, false, false)

void AArch64BranchTargets::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

FunctionPass *llvm::createAArch64BranchTargetsPass() {
  return new AArch64BranchTargets();
}

bool AArch64BranchTargets::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getInfo<AArch64FunctionInfo>()->branchTargetEnforcement())
    return false;

  LLVM_DEBUG(
      dbgs() << "********** AArch64 Branch Targets  **********\n"
             << "********** Function: " << MF.getName() << '\n');

  // LLVM does not consider basic blocks which are the targets of jump tables
  // to be address-taken (the address can't escape anywhere else), but they are
  // used for indirect branches, so need BTI instructions.
  SmallPtrSet<MachineBasicBlock *, 8> JumpTableTargets;
  if (auto *JTI = MF.getJumpTableInfo())
    for (auto &JTE : JTI->getJumpTables())
      for (auto *MBB : JTE.MBBs)
        JumpTableTargets.insert(MBB);

  bool MadeChange = false;
  bool HasWinCFI = MF.hasWinCFI();
  for (MachineBasicBlock &MBB : MF) {
    bool CouldCall = false, CouldJump = false;
    // Even in cases where a function has internal linkage and is only called
    // directly in its translation unit, it can still be called indirectly if
    // the linker decides to add a thunk to it for whatever reason (say, for
    // example, if it is finally placed far from its call site and a BL is not
    // long-range enough). PLT entries and tail-calls use BR, but when they are
    // are in guarded pages should all use x16 or x17 to hold the called
    // address, so we don't need to set CouldJump here. BR instructions in
    // non-guarded pages (which might be non-BTI-aware code) are allowed to
    // branch to a "BTI c" using any register.
    if (&MBB == &*MF.begin())
      CouldCall = true;

    // If the block itself is address-taken, it could be indirectly branched
    // to, but not called.
    if (MBB.hasAddressTaken() || JumpTableTargets.count(&MBB))
      CouldJump = true;

    if (CouldCall || CouldJump) {
      addBTI(MBB, CouldCall, CouldJump, HasWinCFI);
      MadeChange = true;
    }
  }

  return MadeChange;
}

void AArch64BranchTargets::addBTI(MachineBasicBlock &MBB, bool CouldCall,
                                  bool CouldJump, bool HasWinCFI) {
  LLVM_DEBUG(dbgs() << "Adding BTI " << (CouldJump ? "j" : "")
                    << (CouldCall ? "c" : "") << " to " << MBB.getName()
                    << "\n");

  const AArch64InstrInfo *TII = static_cast<const AArch64InstrInfo *>(
      MBB.getParent()->getSubtarget().getInstrInfo());

  unsigned HintNum = 32;
  if (CouldCall)
    HintNum |= 2;
  if (CouldJump)
    HintNum |= 4;
  assert(HintNum != 32 && "No target kinds!");

  auto MBBI = MBB.begin();

  // Skip the meta instructions, those will be removed anyway.
  for (; MBBI != MBB.end() &&
         (MBBI->isMetaInstruction() || MBBI->getOpcode() == AArch64::EMITBKEY);
       ++MBBI)
    ;

  // SCTLR_EL1.BT[01] is set to 0 by default which means
  // PACI[AB]SP are implicitly BTI C so no BTI C instruction is needed there.
  if (MBBI != MBB.end() && HintNum == 34 &&
      (MBBI->getOpcode() == AArch64::PACIASP ||
       MBBI->getOpcode() == AArch64::PACIBSP))
    return;

  if (HasWinCFI && MBBI->getFlag(MachineInstr::FrameSetup)) {
    BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
            TII->get(AArch64::SEH_Nop));
  }
  BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
          TII->get(AArch64::HINT))
      .addImm(HintNum);
}
