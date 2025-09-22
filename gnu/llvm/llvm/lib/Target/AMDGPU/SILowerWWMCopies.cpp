//===-- SILowerWWMCopies.cpp - Lower Copies after regalloc ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Lowering the WWM_COPY instructions for various register classes.
/// AMDGPU target generates WWM_COPY instruction to differentiate WWM
/// copy from COPY. This pass generates the necessary exec mask manipulation
/// instructions to replicate 'Whole Wave Mode' and lowers WWM_COPY back to
/// COPY.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "si-lower-wwm-copies"

namespace {

class SILowerWWMCopies : public MachineFunctionPass {
public:
  static char ID;

  SILowerWWMCopies() : MachineFunctionPass(ID) {
    initializeSILowerWWMCopiesPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Lower WWM Copies"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool isSCCLiveAtMI(const MachineInstr &MI);
  void addToWWMSpills(MachineFunction &MF, Register Reg);

  LiveIntervals *LIS;
  SlotIndexes *Indexes;
  VirtRegMap *VRM;
  const SIRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  SIMachineFunctionInfo *MFI;
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SILowerWWMCopies, DEBUG_TYPE, "SI Lower WWM Copies",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_END(SILowerWWMCopies, DEBUG_TYPE, "SI Lower WWM Copies", false,
                    false)

char SILowerWWMCopies::ID = 0;

char &llvm::SILowerWWMCopiesID = SILowerWWMCopies::ID;

bool SILowerWWMCopies::isSCCLiveAtMI(const MachineInstr &MI) {
  // We can't determine the liveness info if LIS isn't available. Early return
  // in that case and always assume SCC is live.
  if (!LIS)
    return true;

  LiveRange &LR =
      LIS->getRegUnit(*MCRegUnitIterator(MCRegister::from(AMDGPU::SCC), TRI));
  SlotIndex Idx = LIS->getInstructionIndex(MI);
  return LR.liveAt(Idx);
}

// If \p Reg is assigned with a physical VGPR, add the latter into wwm-spills
// for preserving its entire lanes at function prolog/epilog.
void SILowerWWMCopies::addToWWMSpills(MachineFunction &MF, Register Reg) {
  if (Reg.isPhysical())
    return;

  Register PhysReg = VRM->getPhys(Reg);
  assert(PhysReg != VirtRegMap::NO_PHYS_REG &&
         "should have allocated a physical register");

  MFI->allocateWWMSpill(MF, PhysReg);
}

bool SILowerWWMCopies::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();

  MFI = MF.getInfo<SIMachineFunctionInfo>();
  auto *LISWrapper = getAnalysisIfAvailable<LiveIntervalsWrapperPass>();
  LIS = LISWrapper ? &LISWrapper->getLIS() : nullptr;
  auto *SIWrapper = getAnalysisIfAvailable<SlotIndexesWrapperPass>();
  Indexes = SIWrapper ? &SIWrapper->getSI() : nullptr;
  VRM = getAnalysisIfAvailable<VirtRegMap>();
  TRI = ST.getRegisterInfo();
  MRI = &MF.getRegInfo();

  if (!MFI->hasVRegFlags())
    return false;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != AMDGPU::WWM_COPY)
        continue;

      // TODO: Club adjacent WWM ops between same exec save/restore
      assert(TII->isVGPRCopy(MI));

      // For WWM vector copies, manipulate the exec mask around the copy
      // instruction.
      const DebugLoc &DL = MI.getDebugLoc();
      MachineBasicBlock::iterator InsertPt = MI.getIterator();
      Register RegForExecCopy = MFI->getSGPRForEXECCopy();
      TII->insertScratchExecCopy(MF, MBB, InsertPt, DL, RegForExecCopy,
                                 isSCCLiveAtMI(MI), Indexes);
      TII->restoreExec(MF, MBB, ++InsertPt, DL, RegForExecCopy, Indexes);
      addToWWMSpills(MF, MI.getOperand(0).getReg());
      LLVM_DEBUG(dbgs() << "WWM copy manipulation for " << MI);

      // Lower WWM_COPY back to COPY
      MI.setDesc(TII->get(AMDGPU::COPY));
      Changed |= true;
    }
  }

  return Changed;
}
