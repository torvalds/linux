//===-- CSKYRegisterInfo.h - CSKY Register Information Impl ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the CSKY implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "CSKYRegisterInfo.h"
#include "CSKY.h"
#include "CSKYSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/MCContext.h"

#define GET_REGINFO_TARGET_DESC
#include "CSKYGenRegisterInfo.inc"

using namespace llvm;

CSKYRegisterInfo::CSKYRegisterInfo()
    : CSKYGenRegisterInfo(CSKY::R15, 0, 0, 0) {}

const uint32_t *
CSKYRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID Id) const {
  const CSKYSubtarget &STI = MF.getSubtarget<CSKYSubtarget>();
  if (STI.hasFPUv2DoubleFloat() || STI.hasFPUv3DoubleFloat())
    return CSR_GPR_FPR64_RegMask;
  if (STI.hasFPUv2SingleFloat() || STI.hasFPUv3SingleFloat())
    return CSR_GPR_FPR32_RegMask;
  return CSR_I32_RegMask;
}

Register CSKYRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? CSKY::R8 : CSKY::R14;
}

BitVector CSKYRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  const CSKYFrameLowering *TFI = getFrameLowering(MF);
  const CSKYSubtarget &STI = MF.getSubtarget<CSKYSubtarget>();
  BitVector Reserved(getNumRegs());

  // Reserve the base register if we need to allocate
  // variable-sized objects at runtime.
  if (TFI->hasBP(MF))
    markSuperRegs(Reserved, CSKY::R7); // bp

  if (TFI->hasFP(MF))
    markSuperRegs(Reserved, CSKY::R8); // fp

  if (!STI.hasE2()) {
    for (unsigned i = 0; i < 6; i++)
      markSuperRegs(Reserved, CSKY::R8 + i); // R8 - R13
  }

  markSuperRegs(Reserved, CSKY::R14); // sp
  markSuperRegs(Reserved, CSKY::R15); // lr

  if (!STI.hasHighRegisters()) {
    for (unsigned i = 0; i < 10; i++)
      markSuperRegs(Reserved, CSKY::R16 + i); // R16 - R25
  }

  markSuperRegs(Reserved, CSKY::R26);
  markSuperRegs(Reserved, CSKY::R27);
  markSuperRegs(Reserved, CSKY::R28); // gp
  markSuperRegs(Reserved, CSKY::R29);
  markSuperRegs(Reserved, CSKY::R30);
  markSuperRegs(Reserved, CSKY::R31); // tp

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

const uint32_t *CSKYRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

const MCPhysReg *
CSKYRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const CSKYSubtarget &STI = MF->getSubtarget<CSKYSubtarget>();
  if (MF->getFunction().hasFnAttribute("interrupt")) {
    if (STI.hasFPUv3DoubleFloat())
      return CSR_GPR_FPR64v3_ISR_SaveList;
    if (STI.hasFPUv3SingleFloat())
      return CSR_GPR_FPR32v3_ISR_SaveList;
    if (STI.hasFPUv2DoubleFloat())
      return CSR_GPR_FPR64_ISR_SaveList;
    if (STI.hasFPUv2SingleFloat())
      return CSR_GPR_FPR32_ISR_SaveList;
    return CSR_GPR_ISR_SaveList;
  }

  if (STI.hasFPUv2DoubleFloat() || STI.hasFPUv3DoubleFloat())
    return CSR_GPR_FPR64_SaveList;
  if (STI.hasFPUv2SingleFloat() || STI.hasFPUv3SingleFloat())
    return CSR_GPR_FPR32_SaveList;
  return CSR_I32_SaveList;
}

static bool IsLegalOffset(const CSKYInstrInfo *TII, MachineInstr *MI,
                          int &Offset) {
  const MCInstrDesc &Desc = MI->getDesc();
  unsigned AddrMode = (Desc.TSFlags & CSKYII::AddrModeMask);
  unsigned i = 0;
  for (; !MI->getOperand(i).isFI(); ++i) {
    assert(i + 1 < MI->getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  if (MI->getOpcode() == CSKY::ADDI32) {
    if (!isUInt<12>(std::abs(Offset) - 1))
      return false;
    if (Offset < 0) {
      MI->setDesc(TII->get(CSKY::SUBI32));
      Offset = -Offset;
    }

    return true;
  }

  if (MI->getOpcode() == CSKY::ADDI16XZ)
    return false;

  if (Offset < 0)
    return false;

  unsigned NumBits = 0;
  unsigned Scale = 1;
  switch (AddrMode) {
  case CSKYII::AddrMode32B:
    Scale = 1;
    NumBits = 12;
    break;
  case CSKYII::AddrMode32H:
    Scale = 2;
    NumBits = 12;
    break;
  case CSKYII::AddrMode32WD:
    Scale = 4;
    NumBits = 12;
    break;
  case CSKYII::AddrMode16B:
    Scale = 1;
    NumBits = 5;
    break;
  case CSKYII::AddrMode16H:
    Scale = 2;
    NumBits = 5;
    break;
  case CSKYII::AddrMode16W:
    Scale = 4;
    NumBits = 5;
    break;
  case CSKYII::AddrMode32SDF:
    Scale = 4;
    NumBits = 8;
    break;
  default:
    llvm_unreachable("Unsupported addressing mode!");
  }

  // Cannot encode offset.
  if ((Offset & (Scale - 1)) != 0)
    return false;

  unsigned Mask = (1 << NumBits) - 1;
  if ((unsigned)Offset <= Mask * Scale)
    return true;

  // Offset out of range.
  return false;
}

bool CSKYRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr *MI = &*II;
  MachineBasicBlock &MBB = *MI->getParent();
  MachineFunction &MF = *MI->getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const CSKYInstrInfo *TII = MF.getSubtarget<CSKYSubtarget>().getInstrInfo();
  DebugLoc DL = MI->getDebugLoc();
  const CSKYSubtarget &STI = MF.getSubtarget<CSKYSubtarget>();

  switch (MI->getOpcode()) {
  default:
    break;
  case CSKY::RESTORE_CARRY: {
    Register NewReg = STI.hasE2()
                          ? MRI.createVirtualRegister(&CSKY::GPRRegClass)
                          : MRI.createVirtualRegister(&CSKY::mGPRRegClass);

    auto *Temp = BuildMI(MBB, II, DL, TII->get(CSKY::LD32W), NewReg)
                     .add(MI->getOperand(1))
                     .add(MI->getOperand(2))
                     .getInstr();

    BuildMI(MBB, II, DL, TII->get(STI.hasE2() ? CSKY::BTSTI32 : CSKY::BTSTI16),
            MI->getOperand(0).getReg())
        .addReg(NewReg, getKillRegState(true))
        .addImm(0);

    MI = Temp;

    MBB.erase(II);
    break;
  }
  case CSKY::SPILL_CARRY: {
    Register NewReg;
    if (STI.hasE2()) {
      NewReg = MRI.createVirtualRegister(&CSKY::GPRRegClass);
      BuildMI(MBB, II, DL, TII->get(CSKY::MVC32), NewReg)
          .add(MI->getOperand(0));
    } else {
      NewReg = MRI.createVirtualRegister(&CSKY::mGPRRegClass);
      BuildMI(MBB, II, DL, TII->get(CSKY::MOVI16), NewReg).addImm(0);
      BuildMI(MBB, II, DL, TII->get(CSKY::ADDC16))
          .addReg(NewReg, RegState::Define)
          .addReg(MI->getOperand(0).getReg(), RegState::Define)
          .addReg(NewReg, getKillRegState(true))
          .addReg(NewReg, getKillRegState(true))
          .addReg(MI->getOperand(0).getReg());

      BuildMI(MBB, II, DL, TII->get(CSKY::BTSTI16), MI->getOperand(0).getReg())
          .addReg(NewReg)
          .addImm(0);
    }

    MI = BuildMI(MBB, II, DL, TII->get(CSKY::ST32W))
             .addReg(NewReg, getKillRegState(true))
             .add(MI->getOperand(1))
             .add(MI->getOperand(2))
             .getInstr();

    MBB.erase(II);

    break;
  }
  }

  int FrameIndex = MI->getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  int Offset = getFrameLowering(MF)
                   ->getFrameIndexReference(MF, FrameIndex, FrameReg)
                   .getFixed() +
               MI->getOperand(FIOperandNum + 1).getImm();

  if (!isInt<32>(Offset))
    report_fatal_error(
        "Frame offsets outside of the signed 32-bit range not supported");

  bool FrameRegIsKill = false;
  MachineBasicBlock::iterator NewII(MI);
  if (!IsLegalOffset(TII, MI, Offset)) {
    assert(isInt<32>(Offset) && "Int32 expected");
    // The offset won't fit in an immediate, so use a scratch register instead
    // Modify Offset and FrameReg appropriately
    Register ScratchReg = TII->movImm(MBB, NewII, DL, Offset);
    BuildMI(MBB, NewII, DL,
            TII->get(STI.hasE2() ? CSKY::ADDU32 : CSKY::ADDU16XZ), ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addReg(FrameReg);

    Offset = 0;
    FrameReg = ScratchReg;
    FrameRegIsKill = true;
  }

  if (Offset == 0 &&
      (MI->getOpcode() == CSKY::ADDI32 || MI->getOpcode() == CSKY::ADDI16XZ)) {
    MI->setDesc(TII->get(TargetOpcode::COPY));
    MI->getOperand(FIOperandNum)
        .ChangeToRegister(FrameReg, false, false, FrameRegIsKill);
    MI->removeOperand(FIOperandNum + 1);
  } else {
    MI->getOperand(FIOperandNum)
        .ChangeToRegister(FrameReg, false, false, FrameRegIsKill);
    MI->getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  }
  return false;
}
