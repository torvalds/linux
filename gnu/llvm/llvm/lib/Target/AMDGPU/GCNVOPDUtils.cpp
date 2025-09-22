//===- GCNVOPDUtils.cpp - GCN VOPD Utils  ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the AMDGPU DAG scheduling
/// mutation to pair VOPD instructions back to back. It also contains
//  subroutines useful in the creation of VOPD instructions
//
//===----------------------------------------------------------------------===//

#include "GCNVOPDUtils.h"
#include "AMDGPUSubtarget.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

#define DEBUG_TYPE "gcn-vopd-utils"

bool llvm::checkVOPDRegConstraints(const SIInstrInfo &TII,
                                   const MachineInstr &FirstMI,
                                   const MachineInstr &SecondMI) {
  namespace VOPD = AMDGPU::VOPD;

  const MachineFunction *MF = FirstMI.getMF();
  const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = dyn_cast<SIRegisterInfo>(ST.getRegisterInfo());
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  // Literals also count against scalar bus limit
  SmallVector<const MachineOperand *> UniqueLiterals;
  auto addLiteral = [&](const MachineOperand &Op) {
    for (auto &Literal : UniqueLiterals) {
      if (Literal->isIdenticalTo(Op))
        return;
    }
    UniqueLiterals.push_back(&Op);
  };
  SmallVector<Register> UniqueScalarRegs;
  assert([&]() -> bool {
    for (auto MII = MachineBasicBlock::const_iterator(&FirstMI);
         MII != FirstMI.getParent()->instr_end(); ++MII) {
      if (&*MII == &SecondMI)
        return true;
    }
    return false;
  }() && "Expected FirstMI to precede SecondMI");
  // Cannot pair dependent instructions
  for (const auto &Use : SecondMI.uses())
    if (Use.isReg() && FirstMI.modifiesRegister(Use.getReg(), TRI))
      return false;

  auto getVRegIdx = [&](unsigned OpcodeIdx, unsigned OperandIdx) {
    const MachineInstr &MI = (OpcodeIdx == VOPD::X) ? FirstMI : SecondMI;
    const MachineOperand &Operand = MI.getOperand(OperandIdx);
    if (Operand.isReg() && TRI->isVectorRegister(MRI, Operand.getReg()))
      return Operand.getReg();
    return Register();
  };

  auto InstInfo =
      AMDGPU::getVOPDInstInfo(FirstMI.getDesc(), SecondMI.getDesc());

  for (auto CompIdx : VOPD::COMPONENTS) {
    const MachineInstr &MI = (CompIdx == VOPD::X) ? FirstMI : SecondMI;

    const MachineOperand &Src0 = MI.getOperand(VOPD::Component::SRC0);
    if (Src0.isReg()) {
      if (!TRI->isVectorRegister(MRI, Src0.getReg())) {
        if (!is_contained(UniqueScalarRegs, Src0.getReg()))
          UniqueScalarRegs.push_back(Src0.getReg());
      }
    } else {
      if (!TII.isInlineConstant(MI, VOPD::Component::SRC0))
        addLiteral(Src0);
    }

    if (InstInfo[CompIdx].hasMandatoryLiteral()) {
      auto CompOprIdx = InstInfo[CompIdx].getMandatoryLiteralCompOperandIndex();
      addLiteral(MI.getOperand(CompOprIdx));
    }
    if (MI.getDesc().hasImplicitUseOfPhysReg(AMDGPU::VCC))
      UniqueScalarRegs.push_back(AMDGPU::VCC_LO);
  }

  if (UniqueLiterals.size() > 1)
    return false;
  if ((UniqueLiterals.size() + UniqueScalarRegs.size()) > 2)
    return false;

  // On GFX12 if both OpX and OpY are V_MOV_B32 then OPY uses SRC2 source-cache.
  bool SkipSrc = ST.getGeneration() >= AMDGPUSubtarget::GFX12 &&
                 FirstMI.getOpcode() == AMDGPU::V_MOV_B32_e32 &&
                 SecondMI.getOpcode() == AMDGPU::V_MOV_B32_e32;

  if (InstInfo.hasInvalidOperand(getVRegIdx, SkipSrc))
    return false;

  LLVM_DEBUG(dbgs() << "VOPD Reg Constraints Passed\n\tX: " << FirstMI
                    << "\n\tY: " << SecondMI << "\n");
  return true;
}

/// Check if the instr pair, FirstMI and SecondMI, should be scheduled
/// together. Given SecondMI, when FirstMI is unspecified, then check if
/// SecondMI may be part of a fused pair at all.
static bool shouldScheduleVOPDAdjacent(const TargetInstrInfo &TII,
                                       const TargetSubtargetInfo &TSI,
                                       const MachineInstr *FirstMI,
                                       const MachineInstr &SecondMI) {
  const SIInstrInfo &STII = static_cast<const SIInstrInfo &>(TII);
  unsigned Opc2 = SecondMI.getOpcode();
  auto SecondCanBeVOPD = AMDGPU::getCanBeVOPD(Opc2);

  // One instruction case
  if (!FirstMI)
    return SecondCanBeVOPD.Y;

  unsigned Opc = FirstMI->getOpcode();
  auto FirstCanBeVOPD = AMDGPU::getCanBeVOPD(Opc);

  if (!((FirstCanBeVOPD.X && SecondCanBeVOPD.Y) ||
        (FirstCanBeVOPD.Y && SecondCanBeVOPD.X)))
    return false;

  return checkVOPDRegConstraints(STII, *FirstMI, SecondMI);
}

namespace {
/// Adapts design from MacroFusion
/// Puts valid candidate instructions back-to-back so they can easily
/// be turned into VOPD instructions
/// Greedily pairs instruction candidates. O(n^2) algorithm.
struct VOPDPairingMutation : ScheduleDAGMutation {
  MacroFusionPredTy shouldScheduleAdjacent; // NOLINT: function pointer

  VOPDPairingMutation(
      MacroFusionPredTy shouldScheduleAdjacent) // NOLINT: function pointer
      : shouldScheduleAdjacent(shouldScheduleAdjacent) {}

  void apply(ScheduleDAGInstrs *DAG) override {
    const TargetInstrInfo &TII = *DAG->TII;
    const GCNSubtarget &ST = DAG->MF.getSubtarget<GCNSubtarget>();
    if (!AMDGPU::hasVOPD(ST) || !ST.isWave32()) {
      LLVM_DEBUG(dbgs() << "Target does not support VOPDPairingMutation\n");
      return;
    }

    std::vector<SUnit>::iterator ISUI, JSUI;
    for (ISUI = DAG->SUnits.begin(); ISUI != DAG->SUnits.end(); ++ISUI) {
      const MachineInstr *IMI = ISUI->getInstr();
      if (!shouldScheduleAdjacent(TII, ST, nullptr, *IMI))
        continue;
      if (!hasLessThanNumFused(*ISUI, 2))
        continue;

      for (JSUI = ISUI + 1; JSUI != DAG->SUnits.end(); ++JSUI) {
        if (JSUI->isBoundaryNode())
          continue;
        const MachineInstr *JMI = JSUI->getInstr();
        if (!hasLessThanNumFused(*JSUI, 2) ||
            !shouldScheduleAdjacent(TII, ST, IMI, *JMI))
          continue;
        if (fuseInstructionPair(*DAG, *ISUI, *JSUI))
          break;
      }
    }
    LLVM_DEBUG(dbgs() << "Completed VOPDPairingMutation\n");
  }
};
} // namespace

std::unique_ptr<ScheduleDAGMutation> llvm::createVOPDPairingMutation() {
  return std::make_unique<VOPDPairingMutation>(shouldScheduleVOPDAdjacent);
}
