//===-- RISCVInsertReadWriteCSR.cpp - Insert Read/Write of RISC-V CSR -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file implements the machine function pass to insert read/write of CSR-s
// of the RISC-V instructions.
//
// Currently the pass implements:
// -Writing and saving frm before an RVV floating-point instruction with a
//  static rounding mode and restores the value after.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVBaseInfo.h"
#include "RISCV.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "riscv-insert-read-write-csr"
#define RISCV_INSERT_READ_WRITE_CSR_NAME "RISC-V Insert Read/Write CSR Pass"

static cl::opt<bool>
    DisableFRMInsertOpt("riscv-disable-frm-insert-opt", cl::init(false),
                        cl::Hidden,
                        cl::desc("Disable optimized frm insertion."));

namespace {

class RISCVInsertReadWriteCSR : public MachineFunctionPass {
  const TargetInstrInfo *TII;

public:
  static char ID;

  RISCVInsertReadWriteCSR() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return RISCV_INSERT_READ_WRITE_CSR_NAME;
  }

private:
  bool emitWriteRoundingMode(MachineBasicBlock &MBB);
  bool emitWriteRoundingModeOpt(MachineBasicBlock &MBB);
};

} // end anonymous namespace

char RISCVInsertReadWriteCSR::ID = 0;

INITIALIZE_PASS(RISCVInsertReadWriteCSR, DEBUG_TYPE,
                RISCV_INSERT_READ_WRITE_CSR_NAME, false, false)

// TODO: Use more accurate rounding mode at the start of MBB.
bool RISCVInsertReadWriteCSR::emitWriteRoundingModeOpt(MachineBasicBlock &MBB) {
  bool Changed = false;
  MachineInstr *LastFRMChanger = nullptr;
  unsigned CurrentRM = RISCVFPRndMode::DYN;
  Register SavedFRM;

  for (MachineInstr &MI : MBB) {
    if (MI.getOpcode() == RISCV::SwapFRMImm ||
        MI.getOpcode() == RISCV::WriteFRMImm) {
      CurrentRM = MI.getOperand(0).getImm();
      SavedFRM = Register();
      continue;
    }

    if (MI.getOpcode() == RISCV::WriteFRM) {
      CurrentRM = RISCVFPRndMode::DYN;
      SavedFRM = Register();
      continue;
    }

    if (MI.isCall() || MI.isInlineAsm() ||
        MI.readsRegister(RISCV::FRM, /*TRI=*/nullptr)) {
      // Restore FRM before unknown operations.
      if (SavedFRM.isValid())
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::WriteFRM))
            .addReg(SavedFRM);
      CurrentRM = RISCVFPRndMode::DYN;
      SavedFRM = Register();
      continue;
    }

    assert(!MI.modifiesRegister(RISCV::FRM, /*TRI=*/nullptr) &&
           "Expected that MI could not modify FRM.");

    int FRMIdx = RISCVII::getFRMOpNum(MI.getDesc());
    if (FRMIdx < 0)
      continue;
    unsigned InstrRM = MI.getOperand(FRMIdx).getImm();

    LastFRMChanger = &MI;

    // Make MI implicit use FRM.
    MI.addOperand(MachineOperand::CreateReg(RISCV::FRM, /*IsDef*/ false,
                                            /*IsImp*/ true));
    Changed = true;

    // Skip if MI uses same rounding mode as FRM.
    if (InstrRM == CurrentRM)
      continue;

    if (!SavedFRM.isValid()) {
      // Save current FRM value to SavedFRM.
      MachineRegisterInfo *MRI = &MBB.getParent()->getRegInfo();
      SavedFRM = MRI->createVirtualRegister(&RISCV::GPRRegClass);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::SwapFRMImm), SavedFRM)
          .addImm(InstrRM);
    } else {
      // Don't need to save current FRM when SavedFRM having value.
      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::WriteFRMImm))
          .addImm(InstrRM);
    }
    CurrentRM = InstrRM;
  }

  // Restore FRM if needed.
  if (SavedFRM.isValid()) {
    assert(LastFRMChanger && "Expected valid pointer.");
    MachineInstrBuilder MIB =
        BuildMI(*MBB.getParent(), {}, TII->get(RISCV::WriteFRM))
            .addReg(SavedFRM);
    MBB.insertAfter(LastFRMChanger, MIB);
  }

  return Changed;
}

// This function also swaps frm and restores it when encountering an RVV
// floating point instruction with a static rounding mode.
bool RISCVInsertReadWriteCSR::emitWriteRoundingMode(MachineBasicBlock &MBB) {
  bool Changed = false;
  for (MachineInstr &MI : MBB) {
    int FRMIdx = RISCVII::getFRMOpNum(MI.getDesc());
    if (FRMIdx < 0)
      continue;

    unsigned FRMImm = MI.getOperand(FRMIdx).getImm();

    // The value is a hint to this pass to not alter the frm value.
    if (FRMImm == RISCVFPRndMode::DYN)
      continue;

    Changed = true;

    // Save
    MachineRegisterInfo *MRI = &MBB.getParent()->getRegInfo();
    Register SavedFRM = MRI->createVirtualRegister(&RISCV::GPRRegClass);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::SwapFRMImm),
            SavedFRM)
        .addImm(FRMImm);
    MI.addOperand(MachineOperand::CreateReg(RISCV::FRM, /*IsDef*/ false,
                                            /*IsImp*/ true));
    // Restore
    MachineInstrBuilder MIB =
        BuildMI(*MBB.getParent(), {}, TII->get(RISCV::WriteFRM))
            .addReg(SavedFRM);
    MBB.insertAfter(MI, MIB);
  }
  return Changed;
}

bool RISCVInsertReadWriteCSR::runOnMachineFunction(MachineFunction &MF) {
  // Skip if the vector extension is not enabled.
  const RISCVSubtarget &ST = MF.getSubtarget<RISCVSubtarget>();
  if (!ST.hasVInstructions())
    return false;

  TII = ST.getInstrInfo();

  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    if (DisableFRMInsertOpt)
      Changed |= emitWriteRoundingMode(MBB);
    else
      Changed |= emitWriteRoundingModeOpt(MBB);
  }

  return Changed;
}

FunctionPass *llvm::createRISCVInsertReadWriteCSRPass() {
  return new RISCVInsertReadWriteCSR();
}
