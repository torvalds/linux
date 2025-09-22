//===-- AMDGPUGlobalISelDivergenceLowering.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// GlobalISel pass that selects divergent i1 phis as lane mask phis.
/// Lane mask merging uses same algorithm as SDAG in SILowerI1Copies.
/// Handles all cases of temporal divergence.
/// For divergent non-phi i1 and uniform i1 uses outside of the cycle this pass
/// currently depends on LCSSA to insert phis with one incoming.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "SILowerI1Copies.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineUniformityAnalysis.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "amdgpu-global-isel-divergence-lowering"

using namespace llvm;

namespace {

class AMDGPUGlobalISelDivergenceLowering : public MachineFunctionPass {
public:
  static char ID;

public:
  AMDGPUGlobalISelDivergenceLowering() : MachineFunctionPass(ID) {
    initializeAMDGPUGlobalISelDivergenceLoweringPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "AMDGPU GlobalISel divergence lowering";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineUniformityAnalysisPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

class DivergenceLoweringHelper : public PhiLoweringHelper {
public:
  DivergenceLoweringHelper(MachineFunction *MF, MachineDominatorTree *DT,
                           MachinePostDominatorTree *PDT,
                           MachineUniformityInfo *MUI);

private:
  MachineUniformityInfo *MUI = nullptr;
  MachineIRBuilder B;
  Register buildRegCopyToLaneMask(Register Reg);

public:
  void markAsLaneMask(Register DstReg) const override;
  void getCandidatesForLowering(
      SmallVectorImpl<MachineInstr *> &Vreg1Phis) const override;
  void collectIncomingValuesFromPhi(
      const MachineInstr *MI,
      SmallVectorImpl<Incoming> &Incomings) const override;
  void replaceDstReg(Register NewReg, Register OldReg,
                     MachineBasicBlock *MBB) override;
  void buildMergeLaneMasks(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I, const DebugLoc &DL,
                           Register DstReg, Register PrevReg,
                           Register CurReg) override;
  void constrainAsLaneMask(Incoming &In) override;
};

DivergenceLoweringHelper::DivergenceLoweringHelper(
    MachineFunction *MF, MachineDominatorTree *DT,
    MachinePostDominatorTree *PDT, MachineUniformityInfo *MUI)
    : PhiLoweringHelper(MF, DT, PDT), MUI(MUI), B(*MF) {}

// _(s1) -> SReg_32/64(s1)
void DivergenceLoweringHelper::markAsLaneMask(Register DstReg) const {
  assert(MRI->getType(DstReg) == LLT::scalar(1));

  if (MRI->getRegClassOrNull(DstReg)) {
    if (MRI->constrainRegClass(DstReg, ST->getBoolRC()))
      return;
    llvm_unreachable("Failed to constrain register class");
  }

  MRI->setRegClass(DstReg, ST->getBoolRC());
}

void DivergenceLoweringHelper::getCandidatesForLowering(
    SmallVectorImpl<MachineInstr *> &Vreg1Phis) const {
  LLT S1 = LLT::scalar(1);

  // Add divergent i1 phis to the list
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB.phis()) {
      Register Dst = MI.getOperand(0).getReg();
      if (MRI->getType(Dst) == S1 && MUI->isDivergent(Dst))
        Vreg1Phis.push_back(&MI);
    }
  }
}

void DivergenceLoweringHelper::collectIncomingValuesFromPhi(
    const MachineInstr *MI, SmallVectorImpl<Incoming> &Incomings) const {
  for (unsigned i = 1; i < MI->getNumOperands(); i += 2) {
    Incomings.emplace_back(MI->getOperand(i).getReg(),
                           MI->getOperand(i + 1).getMBB(), Register());
  }
}

void DivergenceLoweringHelper::replaceDstReg(Register NewReg, Register OldReg,
                                             MachineBasicBlock *MBB) {
  BuildMI(*MBB, MBB->getFirstNonPHI(), {}, TII->get(AMDGPU::COPY), OldReg)
      .addReg(NewReg);
}

// Copy Reg to new lane mask register, insert a copy after instruction that
// defines Reg while skipping phis if needed.
Register DivergenceLoweringHelper::buildRegCopyToLaneMask(Register Reg) {
  Register LaneMask = createLaneMaskReg(MRI, LaneMaskRegAttrs);
  MachineInstr *Instr = MRI->getVRegDef(Reg);
  MachineBasicBlock *MBB = Instr->getParent();
  B.setInsertPt(*MBB, MBB->SkipPHIsAndLabels(std::next(Instr->getIterator())));
  B.buildCopy(LaneMask, Reg);
  return LaneMask;
}

// bb.previous
//   %PrevReg = ...
//
// bb.current
//   %CurReg = ...
//
//   %DstReg - not defined
//
// -> (wave32 example, new registers have sreg_32 reg class and S1 LLT)
//
// bb.previous
//   %PrevReg = ...
//   %PrevRegCopy:sreg_32(s1) = COPY %PrevReg
//
// bb.current
//   %CurReg = ...
//   %CurRegCopy:sreg_32(s1) = COPY %CurReg
//   ...
//   %PrevMaskedReg:sreg_32(s1) = ANDN2 %PrevRegCopy, ExecReg - active lanes 0
//   %CurMaskedReg:sreg_32(s1)  = AND %ExecReg, CurRegCopy - inactive lanes to 0
//   %DstReg:sreg_32(s1)        = OR %PrevMaskedReg, CurMaskedReg
//
// DstReg = for active lanes rewrite bit in PrevReg with bit from CurReg
void DivergenceLoweringHelper::buildMergeLaneMasks(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, const DebugLoc &DL,
    Register DstReg, Register PrevReg, Register CurReg) {
  // DstReg = (PrevReg & !EXEC) | (CurReg & EXEC)
  // TODO: check if inputs are constants or results of a compare.

  Register PrevRegCopy = buildRegCopyToLaneMask(PrevReg);
  Register CurRegCopy = buildRegCopyToLaneMask(CurReg);
  Register PrevMaskedReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
  Register CurMaskedReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);

  B.setInsertPt(MBB, I);
  B.buildInstr(AndN2Op, {PrevMaskedReg}, {PrevRegCopy, ExecReg});
  B.buildInstr(AndOp, {CurMaskedReg}, {ExecReg, CurRegCopy});
  B.buildInstr(OrOp, {DstReg}, {PrevMaskedReg, CurMaskedReg});
}

// GlobalISel has to constrain S1 incoming taken as-is with lane mask register
// class. Insert a copy of Incoming.Reg to new lane mask inside Incoming.Block,
// Incoming.Reg becomes that new lane mask.
void DivergenceLoweringHelper::constrainAsLaneMask(Incoming &In) {
  B.setInsertPt(*In.Block, In.Block->getFirstTerminator());

  auto Copy = B.buildCopy(LLT::scalar(1), In.Reg);
  MRI->setRegClass(Copy.getReg(0), ST->getBoolRC());
  In.Reg = Copy.getReg(0);
}

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(AMDGPUGlobalISelDivergenceLowering, DEBUG_TYPE,
                      "AMDGPU GlobalISel divergence lowering", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineUniformityAnalysisPass)
INITIALIZE_PASS_END(AMDGPUGlobalISelDivergenceLowering, DEBUG_TYPE,
                    "AMDGPU GlobalISel divergence lowering", false, false)

char AMDGPUGlobalISelDivergenceLowering::ID = 0;

char &llvm::AMDGPUGlobalISelDivergenceLoweringID =
    AMDGPUGlobalISelDivergenceLowering::ID;

FunctionPass *llvm::createAMDGPUGlobalISelDivergenceLoweringPass() {
  return new AMDGPUGlobalISelDivergenceLowering();
}

bool AMDGPUGlobalISelDivergenceLowering::runOnMachineFunction(
    MachineFunction &MF) {
  MachineDominatorTree &DT =
      getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MachinePostDominatorTree &PDT =
      getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  MachineUniformityInfo &MUI =
      getAnalysis<MachineUniformityAnalysisPass>().getUniformityInfo();

  DivergenceLoweringHelper Helper(&MF, &DT, &PDT, &MUI);

  return Helper.lowerPhis();
}
