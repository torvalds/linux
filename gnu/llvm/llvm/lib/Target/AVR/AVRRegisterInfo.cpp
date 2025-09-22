//===-- AVRRegisterInfo.cpp - AVR Register Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AVR implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "AVRRegisterInfo.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Function.h"

#include "AVR.h"
#include "AVRInstrInfo.h"
#include "AVRMachineFunctionInfo.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#define GET_REGINFO_TARGET_DESC
#include "AVRGenRegisterInfo.inc"

namespace llvm {

AVRRegisterInfo::AVRRegisterInfo() : AVRGenRegisterInfo(0) {}

const uint16_t *
AVRRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const AVRMachineFunctionInfo *AFI = MF->getInfo<AVRMachineFunctionInfo>();
  const AVRSubtarget &STI = MF->getSubtarget<AVRSubtarget>();
  if (STI.hasTinyEncoding())
    return AFI->isInterruptOrSignalHandler() ? CSR_InterruptsTiny_SaveList
                                             : CSR_NormalTiny_SaveList;
  else
    return AFI->isInterruptOrSignalHandler() ? CSR_Interrupts_SaveList
                                             : CSR_Normal_SaveList;
}

const uint32_t *
AVRRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  return STI.hasTinyEncoding() ? CSR_NormalTiny_RegMask : CSR_Normal_RegMask;
}

BitVector AVRRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // Reserve the intermediate result registers r1 and r2
  // The result of instructions like 'mul' is always stored here.
  // R0/R1/R1R0 are always reserved on both avr and avrtiny.
  Reserved.set(AVR::R0);
  Reserved.set(AVR::R1);
  Reserved.set(AVR::R1R0);

  // Reserve the stack pointer.
  Reserved.set(AVR::SPL);
  Reserved.set(AVR::SPH);
  Reserved.set(AVR::SP);

  // Reserve R2~R17 only on avrtiny.
  if (MF.getSubtarget<AVRSubtarget>().hasTinyEncoding()) {
    // Reserve 8-bit registers R2~R15, Rtmp(R16) and Zero(R17).
    for (unsigned Reg = AVR::R2; Reg <= AVR::R17; Reg++)
      Reserved.set(Reg);
    // Reserve 16-bit registers R3R2~R18R17.
    for (unsigned Reg = AVR::R3R2; Reg <= AVR::R18R17; Reg++)
      Reserved.set(Reg);
  }

  // We tenatively reserve the frame pointer register r29:r28 because the
  // function may require one, but we cannot tell until register allocation
  // is complete, which can be too late.
  //
  // Instead we just unconditionally reserve the Y register.
  //
  // TODO: Write a pass to enumerate functions which reserved the Y register
  //       but didn't end up needing a frame pointer. In these, we can
  //       convert one or two of the spills inside to use the Y register.
  Reserved.set(AVR::R28);
  Reserved.set(AVR::R29);
  Reserved.set(AVR::R29R28);

  return Reserved;
}

const TargetRegisterClass *
AVRRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                           const MachineFunction &MF) const {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  if (TRI->isTypeLegalForClass(*RC, MVT::i16)) {
    return &AVR::DREGSRegClass;
  }

  if (TRI->isTypeLegalForClass(*RC, MVT::i8)) {
    return &AVR::GPR8RegClass;
  }

  llvm_unreachable("Invalid register size");
}

/// Fold a frame offset shared between two add instructions into a single one.
static void foldFrameOffset(MachineBasicBlock::iterator &II, int &Offset,
                            Register DstReg) {
  MachineInstr &MI = *II;
  int Opcode = MI.getOpcode();

  // Don't bother trying if the next instruction is not an add or a sub.
  if ((Opcode != AVR::SUBIWRdK) && (Opcode != AVR::ADIWRdK)) {
    return;
  }

  // Check that DstReg matches with next instruction, otherwise the instruction
  // is not related to stack address manipulation.
  if (DstReg != MI.getOperand(0).getReg()) {
    return;
  }

  // Add the offset in the next instruction to our offset.
  switch (Opcode) {
  case AVR::SUBIWRdK:
    Offset += -MI.getOperand(2).getImm();
    break;
  case AVR::ADIWRdK:
    Offset += MI.getOperand(2).getImm();
    break;
  }

  // Finally remove the instruction.
  II++;
  MI.eraseFromParent();
}

bool AVRRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected SPAdj value");

  MachineInstr &MI = *II;
  DebugLoc dl = MI.getDebugLoc();
  MachineBasicBlock &MBB = *MI.getParent();
  const MachineFunction &MF = *MBB.getParent();
  const AVRTargetMachine &TM = (const AVRTargetMachine &)MF.getTarget();
  const TargetInstrInfo &TII = *TM.getSubtargetImpl()->getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFI = TM.getSubtargetImpl()->getFrameLowering();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  int Offset = MFI.getObjectOffset(FrameIndex);

  // Add one to the offset because SP points to an empty slot.
  Offset += MFI.getStackSize() - TFI->getOffsetOfLocalArea() + 1;
  // Fold incoming offset.
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  // This is actually "load effective address" of the stack slot
  // instruction. We have only two-address instructions, thus we need to
  // expand it into move + add.
  if (MI.getOpcode() == AVR::FRMIDX) {
    Register DstReg = MI.getOperand(0).getReg();
    assert(DstReg != AVR::R29R28 && "Dest reg cannot be the frame pointer");

    // Copy the frame pointer.
    if (STI.hasMOVW()) {
      BuildMI(MBB, MI, dl, TII.get(AVR::MOVWRdRr), DstReg)
          .addReg(AVR::R29R28);
    } else {
      Register DstLoReg, DstHiReg;
      splitReg(DstReg, DstLoReg, DstHiReg);
      BuildMI(MBB, MI, dl, TII.get(AVR::MOVRdRr), DstLoReg)
          .addReg(AVR::R28);
      BuildMI(MBB, MI, dl, TII.get(AVR::MOVRdRr), DstHiReg)
          .addReg(AVR::R29);
    }

    assert(Offset > 0 && "Invalid offset");

    // We need to materialize the offset via an add instruction.
    unsigned Opcode;

    II++; // Skip over the FRMIDX instruction.

    // Generally, to load a frame address two add instructions are emitted that
    // could get folded into a single one:
    //  movw    r31:r30, r29:r28
    //  adiw    r31:r30, 29
    //  adiw    r31:r30, 16
    // to:
    //  movw    r31:r30, r29:r28
    //  adiw    r31:r30, 45
    if (II != MBB.end())
      foldFrameOffset(II, Offset, DstReg);

    // Select the best opcode based on DstReg and the offset size.
    switch (DstReg) {
    case AVR::R25R24:
    case AVR::R27R26:
    case AVR::R31R30: {
      if (isUInt<6>(Offset) && STI.hasADDSUBIW()) {
        Opcode = AVR::ADIWRdK;
        break;
      }
      [[fallthrough]];
    }
    default: {
      // This opcode will get expanded into a pair of subi/sbci.
      Opcode = AVR::SUBIWRdK;
      Offset = -Offset;
      break;
    }
    }

    MachineInstr *New = BuildMI(MBB, II, dl, TII.get(Opcode), DstReg)
                            .addReg(DstReg, RegState::Kill)
                            .addImm(Offset);
    New->getOperand(3).setIsDead();

    MI.eraseFromParent(); // remove FRMIDX

    return false;
  }

  // On most AVRs, we can use an offset up to 62 for load/store with
  // displacement (63 for byte values, 62 for word values). However, the
  // "reduced tiny" cores don't support load/store with displacement. So for
  // them, we force an offset of 0 meaning that any positive offset will require
  // adjusting the frame pointer.
  int MaxOffset = STI.hasTinyEncoding() ? 0 : 62;

  // If the offset is too big we have to adjust and restore the frame pointer
  // to materialize a valid load/store with displacement.
  //: TODO: consider using only one adiw/sbiw chain for more than one frame
  //: index
  if (Offset > MaxOffset) {
    unsigned AddOpc = AVR::ADIWRdK, SubOpc = AVR::SBIWRdK;
    int AddOffset = Offset - MaxOffset;

    // For huge offsets where adiw/sbiw cannot be used use a pair of subi/sbci.
    if ((Offset - MaxOffset) > 63 || !STI.hasADDSUBIW()) {
      AddOpc = AVR::SUBIWRdK;
      SubOpc = AVR::SUBIWRdK;
      AddOffset = -AddOffset;
    }

    // It is possible that the spiller places this frame instruction in between
    // a compare and branch, invalidating the contents of SREG set by the
    // compare instruction because of the add/sub pairs. Conservatively save and
    // restore SREG before and after each add/sub pair.
    BuildMI(MBB, II, dl, TII.get(AVR::INRdA), STI.getTmpRegister())
        .addImm(STI.getIORegSREG());

    MachineInstr *New = BuildMI(MBB, II, dl, TII.get(AddOpc), AVR::R29R28)
                            .addReg(AVR::R29R28, RegState::Kill)
                            .addImm(AddOffset);
    New->getOperand(3).setIsDead();

    // Restore SREG.
    BuildMI(MBB, std::next(II), dl, TII.get(AVR::OUTARr))
        .addImm(STI.getIORegSREG())
        .addReg(STI.getTmpRegister(), RegState::Kill);

    // No need to set SREG as dead here otherwise if the next instruction is a
    // cond branch it will be using a dead register.
    BuildMI(MBB, std::next(II), dl, TII.get(SubOpc), AVR::R29R28)
        .addReg(AVR::R29R28, RegState::Kill)
        .addImm(Offset - MaxOffset);

    Offset = MaxOffset;
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(AVR::R29R28, false);
  assert(isUInt<6>(Offset) && "Offset is out of range");
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register AVRRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  if (TFI->hasFP(MF)) {
    // The Y pointer register
    return AVR::R28;
  }

  return AVR::SP;
}

const TargetRegisterClass *
AVRRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                    unsigned Kind) const {
  // FIXME: Currently we're using avr-gcc as reference, so we restrict
  // ptrs to Y and Z regs. Though avr-gcc has buggy implementation
  // of memory constraint, so we can fix it and bit avr-gcc here ;-)
  return &AVR::PTRDISPREGSRegClass;
}

void AVRRegisterInfo::splitReg(Register Reg, Register &LoReg,
                               Register &HiReg) const {
  assert(AVR::DREGSRegClass.contains(Reg) && "can only split 16-bit registers");

  LoReg = getSubReg(Reg, AVR::sub_lo);
  HiReg = getSubReg(Reg, AVR::sub_hi);
}

bool AVRRegisterInfo::shouldCoalesce(
    MachineInstr *MI, const TargetRegisterClass *SrcRC, unsigned SubReg,
    const TargetRegisterClass *DstRC, unsigned DstSubReg,
    const TargetRegisterClass *NewRC, LiveIntervals &LIS) const {
  if (this->getRegClass(AVR::PTRDISPREGSRegClassID)->hasSubClassEq(NewRC)) {
    return false;
  }

  return TargetRegisterInfo::shouldCoalesce(MI, SrcRC, SubReg, DstRC, DstSubReg,
                                            NewRC, LIS);
}

} // end of namespace llvm
