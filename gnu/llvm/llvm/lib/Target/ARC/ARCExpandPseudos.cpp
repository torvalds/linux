//===- ARCExpandPseudosPass - ARC expand pseudo loads -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass expands stores with large offsets into an appropriate sequence.
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "ARCInstrInfo.h"
#include "ARCRegisterInfo.h"
#include "ARCSubtarget.h"
#include "MCTargetDesc/ARCInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "arc-expand-pseudos"

namespace {

class ARCExpandPseudos : public MachineFunctionPass {
public:
  static char ID;
  ARCExpandPseudos() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override { return "ARC Expand Pseudos"; }

private:
  void expandStore(MachineFunction &, MachineBasicBlock::iterator);
  void expandCTLZ(MachineFunction &, MachineBasicBlock::iterator);
  void expandCTTZ(MachineFunction &, MachineBasicBlock::iterator);

  const ARCInstrInfo *TII;
};

char ARCExpandPseudos::ID = 0;

} // end anonymous namespace

static unsigned getMappedOp(unsigned PseudoOp) {
  switch (PseudoOp) {
  case ARC::ST_FAR:
    return ARC::ST_rs9;
  case ARC::STH_FAR:
    return ARC::STH_rs9;
  case ARC::STB_FAR:
    return ARC::STB_rs9;
  default:
    llvm_unreachable("Unhandled pseudo op.");
  }
}

void ARCExpandPseudos::expandStore(MachineFunction &MF,
                                   MachineBasicBlock::iterator SII) {
  MachineInstr &SI = *SII;
  Register AddrReg = MF.getRegInfo().createVirtualRegister(&ARC::GPR32RegClass);
  Register AddOpc =
      isUInt<6>(SI.getOperand(2).getImm()) ? ARC::ADD_rru6 : ARC::ADD_rrlimm;
  BuildMI(*SI.getParent(), SI, SI.getDebugLoc(), TII->get(AddOpc), AddrReg)
      .addReg(SI.getOperand(1).getReg())
      .addImm(SI.getOperand(2).getImm());
  BuildMI(*SI.getParent(), SI, SI.getDebugLoc(),
          TII->get(getMappedOp(SI.getOpcode())))
      .addReg(SI.getOperand(0).getReg())
      .addReg(AddrReg)
      .addImm(0);
  SI.eraseFromParent();
}

void ARCExpandPseudos::expandCTLZ(MachineFunction &MF,
                                  MachineBasicBlock::iterator MII) {
  // Expand:
  //	%R2<def> = CTLZ %R0, %STATUS<imp-def>
  // To:
  //	%R2<def> = FLS_f_rr %R0, %STATUS<imp-def>
  //	%R2<def,tied1> = MOV_cc_ru6 %R2<tied0>, 32, pred:1, %STATUS<imp-use>
  //	%R2<def,tied1> = RSUB_cc_rru6 %R2<tied0>, 31, pred:2, %STATUS<imp-use>
  MachineInstr &MI = *MII;
  const MachineOperand &Dest = MI.getOperand(0);
  const MachineOperand &Src = MI.getOperand(1);
  Register Ra = MF.getRegInfo().createVirtualRegister(&ARC::GPR32RegClass);
  Register Rb = MF.getRegInfo().createVirtualRegister(&ARC::GPR32RegClass);

  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(ARC::FLS_f_rr), Ra)
      .add(Src);
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(ARC::MOV_cc_ru6), Rb)
      .addImm(32)
      .addImm(ARCCC::EQ)
      .addReg(Ra);
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(ARC::RSUB_cc_rru6))
      .add(Dest)
      .addImm(31)
      .addImm(ARCCC::NE)
      .addReg(Rb);

  MI.eraseFromParent();
}

void ARCExpandPseudos::expandCTTZ(MachineFunction &MF,
                                  MachineBasicBlock::iterator MII) {
  // Expand:
  //	%R0<def> = CTTZ %R0<kill>, %STATUS<imp-def>
  // To:
  //	%R0<def> = FFS_f_rr %R0<kill>, %STATUS<imp-def>
  //	%R0<def,tied1> = MOVcc_ru6 %R0<tied0>, 32, pred:1, %STATUS<imp-use>
  MachineInstr &MI = *MII;
  const MachineOperand &Dest = MI.getOperand(0);
  const MachineOperand &Src = MI.getOperand(1);
  Register R = MF.getRegInfo().createVirtualRegister(&ARC::GPR32RegClass);

  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(ARC::FFS_f_rr), R)
      .add(Src);
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(ARC::MOV_cc_ru6))
      .add(Dest)
      .addImm(32)
      .addImm(ARCCC::EQ)
      .addReg(R);

  MI.eraseFromParent();
}

bool ARCExpandPseudos::runOnMachineFunction(MachineFunction &MF) {
  const ARCSubtarget *STI = &MF.getSubtarget<ARCSubtarget>();
  TII = STI->getInstrInfo();
  bool Expanded = false;
  for (auto &MBB : MF) {
    MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
    while (MBBI != E) {
      MachineBasicBlock::iterator NMBBI = std::next(MBBI);
      switch (MBBI->getOpcode()) {
      case ARC::ST_FAR:
      case ARC::STH_FAR:
      case ARC::STB_FAR:
        expandStore(MF, MBBI);
        Expanded = true;
        break;
      case ARC::CTLZ:
        expandCTLZ(MF, MBBI);
        Expanded = true;
        break;
      case ARC::CTTZ:
        expandCTTZ(MF, MBBI);
        Expanded = true;
        break;
      default:
        break;
      }
      MBBI = NMBBI;
    }
  }
  return Expanded;
}

FunctionPass *llvm::createARCExpandPseudosPass() {
  return new ARCExpandPseudos();
}
