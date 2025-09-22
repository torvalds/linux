//===-- R600ClauseMergePass - Merge consecutive CF_ALU -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600EmitClauseMarker pass emits CFAlu instruction in a conservative manner.
/// This pass is merging consecutive CFAlus where applicable.
/// It needs to be called after IfCvt for best results.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600.h"
#include "R600Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "r600mergeclause"

namespace {

static bool isCFAlu(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case R600::CF_ALU:
  case R600::CF_ALU_PUSH_BEFORE:
    return true;
  default:
    return false;
  }
}

class R600ClauseMergePass : public MachineFunctionPass {

private:
  const R600InstrInfo *TII;

  unsigned getCFAluSize(const MachineInstr &MI) const;
  bool isCFAluEnabled(const MachineInstr &MI) const;

  /// IfCvt pass can generate "disabled" ALU clause marker that need to be
  /// removed and their content affected to the previous alu clause.
  /// This function parse instructions after CFAlu until it find a disabled
  /// CFAlu and merge the content, or an enabled CFAlu.
  void cleanPotentialDisabledCFAlu(MachineInstr &CFAlu) const;

  /// Check whether LatrCFAlu can be merged into RootCFAlu and do it if
  /// it is the case.
  bool mergeIfPossible(MachineInstr &RootCFAlu,
                       const MachineInstr &LatrCFAlu) const;

public:
  static char ID;

  R600ClauseMergePass() : MachineFunctionPass(ID) { }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override;
};

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(R600ClauseMergePass, DEBUG_TYPE,
                      "R600 Clause Merge", false, false)
INITIALIZE_PASS_END(R600ClauseMergePass, DEBUG_TYPE,
                    "R600 Clause Merge", false, false)

char R600ClauseMergePass::ID = 0;

char &llvm::R600ClauseMergePassID = R600ClauseMergePass::ID;

unsigned R600ClauseMergePass::getCFAluSize(const MachineInstr &MI) const {
  assert(isCFAlu(MI));
  return MI
      .getOperand(TII->getOperandIdx(MI.getOpcode(), R600::OpName::COUNT))
      .getImm();
}

bool R600ClauseMergePass::isCFAluEnabled(const MachineInstr &MI) const {
  assert(isCFAlu(MI));
  return MI
      .getOperand(TII->getOperandIdx(MI.getOpcode(), R600::OpName::Enabled))
      .getImm();
}

void R600ClauseMergePass::cleanPotentialDisabledCFAlu(
    MachineInstr &CFAlu) const {
  int CntIdx = TII->getOperandIdx(R600::CF_ALU, R600::OpName::COUNT);
  MachineBasicBlock::iterator I = CFAlu, E = CFAlu.getParent()->end();
  I++;
  do {
    while (I != E && !isCFAlu(*I))
      I++;
    if (I == E)
      return;
    MachineInstr &MI = *I++;
    if (isCFAluEnabled(MI))
      break;
    CFAlu.getOperand(CntIdx).setImm(getCFAluSize(CFAlu) + getCFAluSize(MI));
    MI.eraseFromParent();
  } while (I != E);
}

bool R600ClauseMergePass::mergeIfPossible(MachineInstr &RootCFAlu,
                                          const MachineInstr &LatrCFAlu) const {
  assert(isCFAlu(RootCFAlu) && isCFAlu(LatrCFAlu));
  int CntIdx = TII->getOperandIdx(R600::CF_ALU, R600::OpName::COUNT);
  unsigned RootInstCount = getCFAluSize(RootCFAlu),
      LaterInstCount = getCFAluSize(LatrCFAlu);
  unsigned CumuledInsts = RootInstCount + LaterInstCount;
  if (CumuledInsts >= TII->getMaxAlusPerClause()) {
    LLVM_DEBUG(dbgs() << "Excess inst counts\n");
    return false;
  }
  if (RootCFAlu.getOpcode() == R600::CF_ALU_PUSH_BEFORE)
    return false;
  // Is KCache Bank 0 compatible ?
  int Mode0Idx =
      TII->getOperandIdx(R600::CF_ALU, R600::OpName::KCACHE_MODE0);
  int KBank0Idx =
      TII->getOperandIdx(R600::CF_ALU, R600::OpName::KCACHE_BANK0);
  int KBank0LineIdx =
      TII->getOperandIdx(R600::CF_ALU, R600::OpName::KCACHE_ADDR0);
  if (LatrCFAlu.getOperand(Mode0Idx).getImm() &&
      RootCFAlu.getOperand(Mode0Idx).getImm() &&
      (LatrCFAlu.getOperand(KBank0Idx).getImm() !=
           RootCFAlu.getOperand(KBank0Idx).getImm() ||
       LatrCFAlu.getOperand(KBank0LineIdx).getImm() !=
           RootCFAlu.getOperand(KBank0LineIdx).getImm())) {
    LLVM_DEBUG(dbgs() << "Wrong KC0\n");
    return false;
  }
  // Is KCache Bank 1 compatible ?
  int Mode1Idx =
      TII->getOperandIdx(R600::CF_ALU, R600::OpName::KCACHE_MODE1);
  int KBank1Idx =
      TII->getOperandIdx(R600::CF_ALU, R600::OpName::KCACHE_BANK1);
  int KBank1LineIdx =
      TII->getOperandIdx(R600::CF_ALU, R600::OpName::KCACHE_ADDR1);
  if (LatrCFAlu.getOperand(Mode1Idx).getImm() &&
      RootCFAlu.getOperand(Mode1Idx).getImm() &&
      (LatrCFAlu.getOperand(KBank1Idx).getImm() !=
           RootCFAlu.getOperand(KBank1Idx).getImm() ||
       LatrCFAlu.getOperand(KBank1LineIdx).getImm() !=
           RootCFAlu.getOperand(KBank1LineIdx).getImm())) {
    LLVM_DEBUG(dbgs() << "Wrong KC0\n");
    return false;
  }
  if (LatrCFAlu.getOperand(Mode0Idx).getImm()) {
    RootCFAlu.getOperand(Mode0Idx).setImm(
        LatrCFAlu.getOperand(Mode0Idx).getImm());
    RootCFAlu.getOperand(KBank0Idx).setImm(
        LatrCFAlu.getOperand(KBank0Idx).getImm());
    RootCFAlu.getOperand(KBank0LineIdx)
        .setImm(LatrCFAlu.getOperand(KBank0LineIdx).getImm());
  }
  if (LatrCFAlu.getOperand(Mode1Idx).getImm()) {
    RootCFAlu.getOperand(Mode1Idx).setImm(
        LatrCFAlu.getOperand(Mode1Idx).getImm());
    RootCFAlu.getOperand(KBank1Idx).setImm(
        LatrCFAlu.getOperand(KBank1Idx).getImm());
    RootCFAlu.getOperand(KBank1LineIdx)
        .setImm(LatrCFAlu.getOperand(KBank1LineIdx).getImm());
  }
  RootCFAlu.getOperand(CntIdx).setImm(CumuledInsts);
  RootCFAlu.setDesc(TII->get(LatrCFAlu.getOpcode()));
  return true;
}

bool R600ClauseMergePass::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const R600Subtarget &ST = MF.getSubtarget<R600Subtarget>();
  TII = ST.getInstrInfo();

  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator I = MBB.begin(),  E = MBB.end();
    MachineBasicBlock::iterator LatestCFAlu = E;
    while (I != E) {
      MachineInstr &MI = *I++;
      if ((!TII->canBeConsideredALU(MI) && !isCFAlu(MI)) ||
          TII->mustBeLastInClause(MI.getOpcode()))
        LatestCFAlu = E;
      if (!isCFAlu(MI))
        continue;
      cleanPotentialDisabledCFAlu(MI);

      if (LatestCFAlu != E && mergeIfPossible(*LatestCFAlu, MI)) {
        MI.eraseFromParent();
      } else {
        assert(MI.getOperand(8).getImm() && "CF ALU instruction disabled");
        LatestCFAlu = MI;
      }
    }
  }
  return false;
}

StringRef R600ClauseMergePass::getPassName() const {
  return "R600 Merge Clause Markers Pass";
}

llvm::FunctionPass *llvm::createR600ClauseMergePass() {
  return new R600ClauseMergePass();
}
