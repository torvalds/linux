//===- Thumb1FrameLowering.cpp - Thumb1 Frame Information -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb1 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "Thumb1FrameLowering.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "Thumb1InstrInfo.h"
#include "ThumbRegisterInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <iterator>
#include <vector>

using namespace llvm;

Thumb1FrameLowering::Thumb1FrameLowering(const ARMSubtarget &sti)
    : ARMFrameLowering(sti) {}

bool Thumb1FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const{
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned CFSize = MFI.getMaxCallFrameSize();
  // It's not always a good idea to include the call frame as part of the
  // stack frame. ARM (especially Thumb) has small immediate offset to
  // address the stack frame. So a large call frame can cause poor codegen
  // and may even makes it impossible to scavenge a register.
  if (CFSize >= ((1 << 8) - 1) * 4 / 2) // Half of imm8 * 4
    return false;

  return !MFI.hasVarSizedObjects();
}

static void
emitPrologueEpilogueSPUpdate(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MBBI,
                             const TargetInstrInfo &TII, const DebugLoc &dl,
                             const ThumbRegisterInfo &MRI, int NumBytes,
                             unsigned ScratchReg, unsigned MIFlags) {
  // If it would take more than three instructions to adjust the stack pointer
  // using tADDspi/tSUBspi, load an immediate instead.
  if (std::abs(NumBytes) > 508 * 3) {
    // We use a different codepath here from the normal
    // emitThumbRegPlusImmediate so we don't have to deal with register
    // scavenging. (Scavenging could try to use the emergency spill slot
    // before we've actually finished setting up the stack.)
    if (ScratchReg == ARM::NoRegister)
      report_fatal_error("Failed to emit Thumb1 stack adjustment");
    MachineFunction &MF = *MBB.getParent();
    const ARMSubtarget &ST = MF.getSubtarget<ARMSubtarget>();
    if (ST.genExecuteOnly()) {
      unsigned XOInstr = ST.useMovt() ? ARM::t2MOVi32imm : ARM::tMOVi32imm;
      BuildMI(MBB, MBBI, dl, TII.get(XOInstr), ScratchReg)
          .addImm(NumBytes).setMIFlags(MIFlags);
    } else {
      MRI.emitLoadConstPool(MBB, MBBI, dl, ScratchReg, 0, NumBytes, ARMCC::AL,
                            0, MIFlags);
    }
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tADDhirr), ARM::SP)
        .addReg(ARM::SP)
        .addReg(ScratchReg, RegState::Kill)
        .add(predOps(ARMCC::AL))
        .setMIFlags(MIFlags);
    return;
  }
  // FIXME: This is assuming the heuristics in emitThumbRegPlusImmediate
  // won't change.
  emitThumbRegPlusImmediate(MBB, MBBI, dl, ARM::SP, ARM::SP, NumBytes, TII,
                            MRI, MIFlags);

}

static void emitCallSPUpdate(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MBBI,
                             const TargetInstrInfo &TII, const DebugLoc &dl,
                             const ThumbRegisterInfo &MRI, int NumBytes,
                             unsigned MIFlags = MachineInstr::NoFlags) {
  emitThumbRegPlusImmediate(MBB, MBBI, dl, ARM::SP, ARM::SP, NumBytes, TII,
                            MRI, MIFlags);
}


MachineBasicBlock::iterator Thumb1FrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const Thumb1InstrInfo &TII =
      *static_cast<const Thumb1InstrInfo *>(STI.getInstrInfo());
  const ThumbRegisterInfo *RegInfo =
      static_cast<const ThumbRegisterInfo *>(STI.getRegisterInfo());
  if (!hasReservedCallFrame(MF)) {
    // If we have alloca, convert as follows:
    // ADJCALLSTACKDOWN -> sub, sp, sp, amount
    // ADJCALLSTACKUP   -> add, sp, sp, amount
    MachineInstr &Old = *I;
    DebugLoc dl = Old.getDebugLoc();
    unsigned Amount = TII.getFrameSize(Old);
    if (Amount != 0) {
      // We need to keep the stack aligned properly.  To do this, we round the
      // amount of space needed for the outgoing arguments up to the next
      // alignment boundary.
      Amount = alignTo(Amount, getStackAlign());

      // Replace the pseudo instruction with a new instruction...
      unsigned Opc = Old.getOpcode();
      if (Opc == ARM::ADJCALLSTACKDOWN || Opc == ARM::tADJCALLSTACKDOWN) {
        emitCallSPUpdate(MBB, I, TII, dl, *RegInfo, -Amount);
      } else {
        assert(Opc == ARM::ADJCALLSTACKUP || Opc == ARM::tADJCALLSTACKUP);
        emitCallSPUpdate(MBB, I, TII, dl, *RegInfo, Amount);
      }
    }
  }
  return MBB.erase(I);
}

void Thumb1FrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();
  const ThumbRegisterInfo *RegInfo =
      static_cast<const ThumbRegisterInfo *>(STI.getRegisterInfo());
  const Thumb1InstrInfo &TII =
      *static_cast<const Thumb1InstrInfo *>(STI.getInstrInfo());

  unsigned ArgRegsSaveSize = AFI->getArgRegsSaveSize();
  unsigned NumBytes = MFI.getStackSize();
  assert(NumBytes >= ArgRegsSaveSize &&
         "ArgRegsSaveSize is included in NumBytes");
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc dl;

  Register FramePtr = RegInfo->getFrameRegister(MF);
  Register BasePtr = RegInfo->getBaseRegister();
  int CFAOffset = 0;

  // Thumb add/sub sp, imm8 instructions implicitly multiply the offset by 4.
  NumBytes = (NumBytes + 3) & ~3;
  MFI.setStackSize(NumBytes);

  // Determine the sizes of each callee-save spill areas and record which frame
  // belongs to which callee-save spill areas.
  unsigned FRSize = 0, GPRCS1Size = 0, GPRCS2Size = 0, DPRCSSize = 0;
  int FramePtrSpillFI = 0;

  if (ArgRegsSaveSize) {
    emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo, -ArgRegsSaveSize,
                                 ARM::NoRegister, MachineInstr::FrameSetup);
    CFAOffset += ArgRegsSaveSize;
    unsigned CFIIndex =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, CFAOffset));
    BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  }

  if (!AFI->hasStackFrame()) {
    if (NumBytes - ArgRegsSaveSize != 0) {
      emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo,
                                   -(NumBytes - ArgRegsSaveSize),
                                   ARM::NoRegister, MachineInstr::FrameSetup);
      CFAOffset += NumBytes - ArgRegsSaveSize;
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, CFAOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }
    return;
  }

  bool HasFrameRecordArea = hasFP(MF) && ARM::hGPRRegClass.contains(FramePtr);

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    int FI = I.getFrameIdx();
    if (Reg == FramePtr)
      FramePtrSpillFI = FI;
    switch (Reg) {
    case ARM::R11:
      if (HasFrameRecordArea) {
        FRSize += 4;
        break;
      }
      [[fallthrough]];
    case ARM::R8:
    case ARM::R9:
    case ARM::R10:
      if (STI.splitFramePushPop(MF)) {
        GPRCS2Size += 4;
        break;
      }
      [[fallthrough]];
    case ARM::LR:
      if (HasFrameRecordArea) {
        FRSize += 4;
        break;
      }
      [[fallthrough]];
    case ARM::R4:
    case ARM::R5:
    case ARM::R6:
    case ARM::R7:
      GPRCS1Size += 4;
      break;
    default:
      DPRCSSize += 8;
    }
  }

  MachineBasicBlock::iterator FRPush, GPRCS1Push, GPRCS2Push;
  if (HasFrameRecordArea) {
    // Skip Frame Record setup:
    //   push {lr}
    //   mov lr, r11
    //   push {lr}
    std::advance(MBBI, 2);
    FRPush = MBBI++;
  }

  if (MBBI != MBB.end() && MBBI->getOpcode() == ARM::tPUSH) {
    GPRCS1Push = MBBI;
    ++MBBI;
  }

  // Find last push instruction for GPRCS2 - spilling of high registers
  // (r8-r11) could consist of multiple tPUSH and tMOVr instructions.
  while (true) {
    MachineBasicBlock::iterator OldMBBI = MBBI;
    // Skip a run of tMOVr instructions
    while (MBBI != MBB.end() && MBBI->getOpcode() == ARM::tMOVr &&
           MBBI->getFlag(MachineInstr::FrameSetup))
      MBBI++;
    if (MBBI != MBB.end() && MBBI->getOpcode() == ARM::tPUSH &&
        MBBI->getFlag(MachineInstr::FrameSetup)) {
      GPRCS2Push = MBBI;
      MBBI++;
    } else {
      // We have reached an instruction which is not a push, so the previous
      // run of tMOVr instructions (which may have been empty) was not part of
      // the prologue. Reset MBBI back to the last PUSH of the prologue.
      MBBI = OldMBBI;
      break;
    }
  }

  // Determine starting offsets of spill areas.
  unsigned DPRCSOffset = NumBytes - ArgRegsSaveSize -
                         (FRSize + GPRCS1Size + GPRCS2Size + DPRCSSize);
  unsigned GPRCS2Offset = DPRCSOffset + DPRCSSize;
  unsigned GPRCS1Offset = GPRCS2Offset + GPRCS2Size;
  bool HasFP = hasFP(MF);
  if (HasFP)
    AFI->setFramePtrSpillOffset(MFI.getObjectOffset(FramePtrSpillFI) +
                                NumBytes);
  if (HasFrameRecordArea)
    AFI->setFrameRecordSavedAreaSize(FRSize);
  AFI->setGPRCalleeSavedArea1Offset(GPRCS1Offset);
  AFI->setGPRCalleeSavedArea2Offset(GPRCS2Offset);
  AFI->setDPRCalleeSavedAreaOffset(DPRCSOffset);
  NumBytes = DPRCSOffset;

  int FramePtrOffsetInBlock = 0;
  unsigned adjustedGPRCS1Size = GPRCS1Size;
  if (GPRCS1Size > 0 && GPRCS2Size == 0 &&
      tryFoldSPUpdateIntoPushPop(STI, MF, &*(GPRCS1Push), NumBytes)) {
    FramePtrOffsetInBlock = NumBytes;
    adjustedGPRCS1Size += NumBytes;
    NumBytes = 0;
  }
  CFAOffset += adjustedGPRCS1Size;

  // Adjust FP so it point to the stack slot that contains the previous FP.
  if (HasFP) {
    MachineBasicBlock::iterator AfterPush =
        HasFrameRecordArea ? std::next(FRPush) : std::next(GPRCS1Push);
    if (HasFrameRecordArea) {
      // We have just finished pushing the previous FP into the stack,
      // so simply capture the SP value as the new Frame Pointer.
      BuildMI(MBB, AfterPush, dl, TII.get(ARM::tMOVr), FramePtr)
          .addReg(ARM::SP)
          .setMIFlags(MachineInstr::FrameSetup)
          .add(predOps(ARMCC::AL));
    } else {
      FramePtrOffsetInBlock +=
          MFI.getObjectOffset(FramePtrSpillFI) + GPRCS1Size + ArgRegsSaveSize;
      BuildMI(MBB, AfterPush, dl, TII.get(ARM::tADDrSPi), FramePtr)
          .addReg(ARM::SP)
          .addImm(FramePtrOffsetInBlock / 4)
          .setMIFlags(MachineInstr::FrameSetup)
          .add(predOps(ARMCC::AL));
    }

    if(FramePtrOffsetInBlock) {
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
          nullptr, MRI->getDwarfRegNum(FramePtr, true), (CFAOffset - FramePtrOffsetInBlock)));
      BuildMI(MBB, AfterPush, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    } else {
      unsigned CFIIndex =
          MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
              nullptr, MRI->getDwarfRegNum(FramePtr, true)));
      BuildMI(MBB, AfterPush, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }
    if (NumBytes > 508)
      // If offset is > 508 then sp cannot be adjusted in a single instruction,
      // try restoring from fp instead.
      AFI->setShouldRestoreSPFromFP(true);
  }

  // Emit call frame information for the callee-saved low registers.
  if (GPRCS1Size > 0) {
    MachineBasicBlock::iterator Pos = std::next(GPRCS1Push);
    if (adjustedGPRCS1Size) {
      unsigned CFIIndex =
          MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, CFAOffset));
      BuildMI(MBB, Pos, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }
    for (const CalleeSavedInfo &I : CSI) {
      Register Reg = I.getReg();
      int FI = I.getFrameIdx();
      switch (Reg) {
      case ARM::R8:
      case ARM::R9:
      case ARM::R10:
      case ARM::R11:
      case ARM::R12:
        if (STI.splitFramePushPop(MF))
          break;
        [[fallthrough]];
      case ARM::R0:
      case ARM::R1:
      case ARM::R2:
      case ARM::R3:
      case ARM::R4:
      case ARM::R5:
      case ARM::R6:
      case ARM::R7:
      case ARM::LR:
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(Reg, true), MFI.getObjectOffset(FI)));
        BuildMI(MBB, Pos, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
        break;
      }
    }
  }

  // Emit call frame information for the callee-saved high registers.
  if (GPRCS2Size > 0) {
    MachineBasicBlock::iterator Pos = std::next(GPRCS2Push);
    for (auto &I : CSI) {
      Register Reg = I.getReg();
      int FI = I.getFrameIdx();
      switch (Reg) {
      case ARM::R8:
      case ARM::R9:
      case ARM::R10:
      case ARM::R11:
      case ARM::R12: {
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(Reg, true), MFI.getObjectOffset(FI)));
        BuildMI(MBB, Pos, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
        break;
      }
      default:
        break;
      }
    }
  }

  if (NumBytes) {
    // Insert it after all the callee-save spills.
    //
    // For a large stack frame, we might need a scratch register to store
    // the size of the frame.  We know all callee-save registers are free
    // at this point in the prologue, so pick one.
    unsigned ScratchRegister = ARM::NoRegister;
    for (auto &I : CSI) {
      Register Reg = I.getReg();
      if (isARMLowRegister(Reg) && !(HasFP && Reg == FramePtr)) {
        ScratchRegister = Reg;
        break;
      }
    }
    emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo, -NumBytes,
                                 ScratchRegister, MachineInstr::FrameSetup);
    if (!HasFP) {
      CFAOffset += NumBytes;
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, CFAOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }
  }

  if (STI.isTargetELF() && HasFP)
    MFI.setOffsetAdjustment(MFI.getOffsetAdjustment() -
                            AFI->getFramePtrSpillOffset());

  AFI->setGPRCalleeSavedArea1Size(GPRCS1Size);
  AFI->setGPRCalleeSavedArea2Size(GPRCS2Size);
  AFI->setDPRCalleeSavedAreaSize(DPRCSSize);

  if (RegInfo->hasStackRealignment(MF)) {
    const unsigned NrBitsToZero = Log2(MFI.getMaxAlign());
    // Emit the following sequence, using R4 as a temporary, since we cannot use
    // SP as a source or destination register for the shifts:
    // mov  r4, sp
    // lsrs r4, r4, #NrBitsToZero
    // lsls r4, r4, #NrBitsToZero
    // mov  sp, r4
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::R4)
      .addReg(ARM::SP, RegState::Kill)
      .add(predOps(ARMCC::AL));

    BuildMI(MBB, MBBI, dl, TII.get(ARM::tLSRri), ARM::R4)
      .addDef(ARM::CPSR)
      .addReg(ARM::R4, RegState::Kill)
      .addImm(NrBitsToZero)
      .add(predOps(ARMCC::AL));

    BuildMI(MBB, MBBI, dl, TII.get(ARM::tLSLri), ARM::R4)
      .addDef(ARM::CPSR)
      .addReg(ARM::R4, RegState::Kill)
      .addImm(NrBitsToZero)
      .add(predOps(ARMCC::AL));

    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::SP)
      .addReg(ARM::R4, RegState::Kill)
      .add(predOps(ARMCC::AL));

    AFI->setShouldRestoreSPFromFP(true);
  }

  // If we need a base pointer, set it up here. It's whatever the value
  // of the stack pointer is at this point. Any variable size objects
  // will be allocated after this, so we can still use the base pointer
  // to reference locals.
  if (RegInfo->hasBasePointer(MF))
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), BasePtr)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL));

  // If the frame has variable sized objects then the epilogue must restore
  // the sp from fp. We can assume there's an FP here since hasFP already
  // checks for hasVarSizedObjects.
  if (MFI.hasVarSizedObjects())
    AFI->setShouldRestoreSPFromFP(true);

  // In some cases, virtual registers have been introduced, e.g. by uses of
  // emitThumbRegPlusImmInReg.
  MF.getProperties().reset(MachineFunctionProperties::Property::NoVRegs);
}

void Thumb1FrameLowering::emitEpilogue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  const ThumbRegisterInfo *RegInfo =
      static_cast<const ThumbRegisterInfo *>(STI.getRegisterInfo());
  const Thumb1InstrInfo &TII =
      *static_cast<const Thumb1InstrInfo *>(STI.getInstrInfo());

  unsigned ArgRegsSaveSize = AFI->getArgRegsSaveSize();
  int NumBytes = (int)MFI.getStackSize();
  assert((unsigned)NumBytes >= ArgRegsSaveSize &&
         "ArgRegsSaveSize is included in NumBytes");
  Register FramePtr = RegInfo->getFrameRegister(MF);

  if (!AFI->hasStackFrame()) {
    if (NumBytes - ArgRegsSaveSize != 0)
      emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo,
                                   NumBytes - ArgRegsSaveSize, ARM::NoRegister,
                                   MachineInstr::FrameDestroy);
  } else {
    // Unwind MBBI to point to first LDR / VLDRD.
    if (MBBI != MBB.begin()) {
      do
        --MBBI;
      while (MBBI != MBB.begin() && MBBI->getFlag(MachineInstr::FrameDestroy));
      if (!MBBI->getFlag(MachineInstr::FrameDestroy))
        ++MBBI;
    }

    // Move SP to start of FP callee save spill area.
    NumBytes -= (AFI->getFrameRecordSavedAreaSize() +
                 AFI->getGPRCalleeSavedArea1Size() +
                 AFI->getGPRCalleeSavedArea2Size() +
                 AFI->getDPRCalleeSavedAreaSize() +
                 ArgRegsSaveSize);

    // We are likely to need a scratch register and we know all callee-save
    // registers are free at this point in the epilogue, so pick one.
    unsigned ScratchRegister = ARM::NoRegister;
    bool HasFP = hasFP(MF);
    for (auto &I : MFI.getCalleeSavedInfo()) {
      Register Reg = I.getReg();
      if (isARMLowRegister(Reg) && !(HasFP && Reg == FramePtr)) {
        ScratchRegister = Reg;
        break;
      }
    }

    if (AFI->shouldRestoreSPFromFP()) {
      NumBytes = AFI->getFramePtrSpillOffset() - NumBytes;
      // Reset SP based on frame pointer only if the stack frame extends beyond
      // frame pointer stack slot, the target is ELF and the function has FP, or
      // the target uses var sized objects.
      if (NumBytes) {
        assert(ScratchRegister != ARM::NoRegister &&
               "No scratch register to restore SP from FP!");
        emitThumbRegPlusImmediate(MBB, MBBI, dl, ScratchRegister, FramePtr, -NumBytes,
                                  TII, *RegInfo, MachineInstr::FrameDestroy);
        BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::SP)
            .addReg(ScratchRegister)
            .add(predOps(ARMCC::AL))
            .setMIFlag(MachineInstr::FrameDestroy);
      } else
        BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::SP)
            .addReg(FramePtr)
            .add(predOps(ARMCC::AL))
            .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      if (MBBI != MBB.end() && MBBI->getOpcode() == ARM::tBX_RET &&
          &MBB.front() != &*MBBI && std::prev(MBBI)->getOpcode() == ARM::tPOP) {
        MachineBasicBlock::iterator PMBBI = std::prev(MBBI);
        if (!tryFoldSPUpdateIntoPushPop(STI, MF, &*PMBBI, NumBytes))
          emitPrologueEpilogueSPUpdate(MBB, PMBBI, TII, dl, *RegInfo, NumBytes,
                                       ScratchRegister, MachineInstr::FrameDestroy);
      } else if (!tryFoldSPUpdateIntoPushPop(STI, MF, &*MBBI, NumBytes))
        emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo, NumBytes,
                                     ScratchRegister, MachineInstr::FrameDestroy);
    }
  }

  if (needPopSpecialFixUp(MF)) {
    bool Done = emitPopSpecialFixUp(MBB, /* DoIt */ true);
    (void)Done;
    assert(Done && "Emission of the special fixup failed!?");
  }
}

bool Thumb1FrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  if (!needPopSpecialFixUp(*MBB.getParent()))
    return true;

  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);
  return emitPopSpecialFixUp(*TmpMBB, /* DoIt */ false);
}

bool Thumb1FrameLowering::needPopSpecialFixUp(const MachineFunction &MF) const {
  ARMFunctionInfo *AFI =
      const_cast<MachineFunction *>(&MF)->getInfo<ARMFunctionInfo>();
  if (AFI->getArgRegsSaveSize())
    return true;

  // LR cannot be encoded with Thumb1, i.e., it requires a special fix-up.
  for (const CalleeSavedInfo &CSI : MF.getFrameInfo().getCalleeSavedInfo())
    if (CSI.getReg() == ARM::LR)
      return true;

  return false;
}

static void findTemporariesForLR(const BitVector &GPRsNoLRSP,
                                 const BitVector &PopFriendly,
                                 const LiveRegUnits &UsedRegs, unsigned &PopReg,
                                 unsigned &TmpReg, MachineRegisterInfo &MRI) {
  PopReg = TmpReg = 0;
  for (auto Reg : GPRsNoLRSP.set_bits()) {
    if (UsedRegs.available(Reg)) {
      // Remember the first pop-friendly register and exit.
      if (PopFriendly.test(Reg)) {
        PopReg = Reg;
        TmpReg = 0;
        break;
      }
      // Otherwise, remember that the register will be available to
      // save a pop-friendly register.
      TmpReg = Reg;
    }
  }
}

bool Thumb1FrameLowering::emitPopSpecialFixUp(MachineBasicBlock &MBB,
                                              bool DoIt) const {
  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned ArgRegsSaveSize = AFI->getArgRegsSaveSize();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const ThumbRegisterInfo *RegInfo =
      static_cast<const ThumbRegisterInfo *>(STI.getRegisterInfo());

  // If MBBI is a return instruction, or is a tPOP followed by a return
  // instruction in the successor BB, we may be able to directly restore
  // LR in the PC.
  // This is only possible with v5T ops (v4T can't change the Thumb bit via
  // a POP PC instruction), and only if we do not need to emit any SP update.
  // Otherwise, we need a temporary register to pop the value
  // and copy that value into LR.
  auto MBBI = MBB.getFirstTerminator();
  bool CanRestoreDirectly = STI.hasV5TOps() && !ArgRegsSaveSize;
  if (CanRestoreDirectly) {
    if (MBBI != MBB.end() && MBBI->getOpcode() != ARM::tB)
      CanRestoreDirectly = (MBBI->getOpcode() == ARM::tBX_RET ||
                            MBBI->getOpcode() == ARM::tPOP_RET);
    else {
      auto MBBI_prev = MBBI;
      MBBI_prev--;
      assert(MBBI_prev->getOpcode() == ARM::tPOP);
      assert(MBB.succ_size() == 1);
      if ((*MBB.succ_begin())->begin()->getOpcode() == ARM::tBX_RET)
        MBBI = MBBI_prev; // Replace the final tPOP with a tPOP_RET.
      else
        CanRestoreDirectly = false;
    }
  }

  if (CanRestoreDirectly) {
    if (!DoIt || MBBI->getOpcode() == ARM::tPOP_RET)
      return true;
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII.get(ARM::tPOP_RET))
            .add(predOps(ARMCC::AL))
            .setMIFlag(MachineInstr::FrameDestroy);
    // Copy implicit ops and popped registers, if any.
    for (auto MO: MBBI->operands())
      if (MO.isReg() && (MO.isImplicit() || MO.isDef()))
        MIB.add(MO);
    MIB.addReg(ARM::PC, RegState::Define);
    // Erase the old instruction (tBX_RET or tPOP).
    MBB.erase(MBBI);
    return true;
  }

  // Look for a temporary register to use.
  // First, compute the liveness information.
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  LiveRegUnits UsedRegs(TRI);
  UsedRegs.addLiveOuts(MBB);
  // The semantic of pristines changed recently and now,
  // the callee-saved registers that are touched in the function
  // are not part of the pristines set anymore.
  // Add those callee-saved now.
  const MCPhysReg *CSRegs = TRI.getCalleeSavedRegs(&MF);
  for (unsigned i = 0; CSRegs[i]; ++i)
    UsedRegs.addReg(CSRegs[i]);

  DebugLoc dl = DebugLoc();
  if (MBBI != MBB.end()) {
    dl = MBBI->getDebugLoc();
    auto InstUpToMBBI = MBB.end();
    while (InstUpToMBBI != MBBI)
      // The pre-decrement is on purpose here.
      // We want to have the liveness right before MBBI.
      UsedRegs.stepBackward(*--InstUpToMBBI);
  }

  // Look for a register that can be directly use in the POP.
  unsigned PopReg = 0;
  // And some temporary register, just in case.
  unsigned TemporaryReg = 0;
  BitVector PopFriendly =
      TRI.getAllocatableSet(MF, TRI.getRegClass(ARM::tGPRRegClassID));

  assert(PopFriendly.any() && "No allocatable pop-friendly register?!");
  // Rebuild the GPRs from the high registers because they are removed
  // form the GPR reg class for thumb1.
  BitVector GPRsNoLRSP =
      TRI.getAllocatableSet(MF, TRI.getRegClass(ARM::hGPRRegClassID));
  GPRsNoLRSP |= PopFriendly;
  GPRsNoLRSP.reset(ARM::LR);
  GPRsNoLRSP.reset(ARM::SP);
  GPRsNoLRSP.reset(ARM::PC);
  findTemporariesForLR(GPRsNoLRSP, PopFriendly, UsedRegs, PopReg, TemporaryReg,
                       MF.getRegInfo());

  // If we couldn't find a pop-friendly register, try restoring LR before
  // popping the other callee-saved registers, so we could use one of them as a
  // temporary.
  bool UseLDRSP = false;
  if (!PopReg && MBBI != MBB.begin()) {
    auto PrevMBBI = MBBI;
    PrevMBBI--;
    if (PrevMBBI->getOpcode() == ARM::tPOP) {
      UsedRegs.stepBackward(*PrevMBBI);
      findTemporariesForLR(GPRsNoLRSP, PopFriendly, UsedRegs, PopReg,
                           TemporaryReg, MF.getRegInfo());
      if (PopReg) {
        MBBI = PrevMBBI;
        UseLDRSP = true;
      }
    }
  }

  if (!DoIt && !PopReg && !TemporaryReg)
    return false;

  assert((PopReg || TemporaryReg) && "Cannot get LR");

  if (UseLDRSP) {
    assert(PopReg && "Do not know how to get LR");
    // Load the LR via LDR tmp, [SP, #off]
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tLDRspi))
      .addReg(PopReg, RegState::Define)
      .addReg(ARM::SP)
      .addImm(MBBI->getNumExplicitOperands() - 2)
      .add(predOps(ARMCC::AL))
      .setMIFlag(MachineInstr::FrameDestroy);
    // Move from the temporary register to the LR.
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr))
      .addReg(ARM::LR, RegState::Define)
      .addReg(PopReg, RegState::Kill)
      .add(predOps(ARMCC::AL))
      .setMIFlag(MachineInstr::FrameDestroy);
    // Advance past the pop instruction.
    MBBI++;
    // Increment the SP.
    emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo,
                                 ArgRegsSaveSize + 4, ARM::NoRegister,
                                 MachineInstr::FrameDestroy);
    return true;
  }

  if (TemporaryReg) {
    assert(!PopReg && "Unnecessary MOV is about to be inserted");
    PopReg = PopFriendly.find_first();
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr))
        .addReg(TemporaryReg, RegState::Define)
        .addReg(PopReg, RegState::Kill)
        .add(predOps(ARMCC::AL))
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  if (MBBI != MBB.end() && MBBI->getOpcode() == ARM::tPOP_RET) {
    // We couldn't use the direct restoration above, so
    // perform the opposite conversion: tPOP_RET to tPOP.
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII.get(ARM::tPOP))
            .add(predOps(ARMCC::AL))
            .setMIFlag(MachineInstr::FrameDestroy);
    bool Popped = false;
    for (auto MO: MBBI->operands())
      if (MO.isReg() && (MO.isImplicit() || MO.isDef()) &&
          MO.getReg() != ARM::PC) {
        MIB.add(MO);
        if (!MO.isImplicit())
          Popped = true;
      }
    // Is there anything left to pop?
    if (!Popped)
      MBB.erase(MIB.getInstr());
    // Erase the old instruction.
    MBB.erase(MBBI);
    MBBI = BuildMI(MBB, MBB.end(), dl, TII.get(ARM::tBX_RET))
               .add(predOps(ARMCC::AL))
               .setMIFlag(MachineInstr::FrameDestroy);
  }

  assert(PopReg && "Do not know how to get LR");
  BuildMI(MBB, MBBI, dl, TII.get(ARM::tPOP))
      .add(predOps(ARMCC::AL))
      .addReg(PopReg, RegState::Define)
      .setMIFlag(MachineInstr::FrameDestroy);

  emitPrologueEpilogueSPUpdate(MBB, MBBI, TII, dl, *RegInfo, ArgRegsSaveSize,
                               ARM::NoRegister, MachineInstr::FrameDestroy);

  BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr))
      .addReg(ARM::LR, RegState::Define)
      .addReg(PopReg, RegState::Kill)
      .add(predOps(ARMCC::AL))
      .setMIFlag(MachineInstr::FrameDestroy);

  if (TemporaryReg)
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr))
        .addReg(PopReg, RegState::Define)
        .addReg(TemporaryReg, RegState::Kill)
        .add(predOps(ARMCC::AL))
        .setMIFlag(MachineInstr::FrameDestroy);

  return true;
}

static const SmallVector<Register> OrderedLowRegs = {ARM::R4, ARM::R5, ARM::R6,
                                                     ARM::R7, ARM::LR};
static const SmallVector<Register> OrderedHighRegs = {ARM::R8, ARM::R9,
                                                      ARM::R10, ARM::R11};
static const SmallVector<Register> OrderedCopyRegs = {
    ARM::R0, ARM::R1, ARM::R2, ARM::R3, ARM::R4,
    ARM::R5, ARM::R6, ARM::R7, ARM::LR};

static void splitLowAndHighRegs(const std::set<Register> &Regs,
                                std::set<Register> &LowRegs,
                                std::set<Register> &HighRegs) {
  for (Register Reg : Regs) {
    if (ARM::tGPRRegClass.contains(Reg) || Reg == ARM::LR) {
      LowRegs.insert(Reg);
    } else if (ARM::hGPRRegClass.contains(Reg) && Reg != ARM::LR) {
      HighRegs.insert(Reg);
    } else {
      llvm_unreachable("callee-saved register of unexpected class");
    }
  }
}

template <typename It>
It getNextOrderedReg(It OrderedStartIt, It OrderedEndIt,
                     const std::set<Register> &RegSet) {
  return std::find_if(OrderedStartIt, OrderedEndIt,
                      [&](Register Reg) { return RegSet.count(Reg); });
}

static void pushRegsToStack(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            const TargetInstrInfo &TII,
                            const std::set<Register> &RegsToSave,
                            const std::set<Register> &CopyRegs) {
  MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL;

  std::set<Register> LowRegs, HighRegs;
  splitLowAndHighRegs(RegsToSave, LowRegs, HighRegs);

  // Push low regs first
  if (!LowRegs.empty()) {
    MachineInstrBuilder MIB =
        BuildMI(MBB, MI, DL, TII.get(ARM::tPUSH)).add(predOps(ARMCC::AL));
    for (unsigned Reg : OrderedLowRegs) {
      if (LowRegs.count(Reg)) {
        bool isKill = !MRI.isLiveIn(Reg);
        if (isKill && !MRI.isReserved(Reg))
          MBB.addLiveIn(Reg);

        MIB.addReg(Reg, getKillRegState(isKill));
      }
    }
    MIB.setMIFlags(MachineInstr::FrameSetup);
  }

  // Now push the high registers
  // There are no store instructions that can access high registers directly,
  // so we have to move them to low registers, and push them.
  // This might take multiple pushes, as it is possible for there to
  // be fewer low registers available than high registers which need saving.

  // Find the first register to save.
  // Registers must be processed in reverse order so that in case we need to use
  // multiple PUSH instructions, the order of the registers on the stack still
  // matches the unwind info. They need to be swicthed back to ascending order
  // before adding to the PUSH instruction.
  auto HiRegToSave = getNextOrderedReg(OrderedHighRegs.rbegin(),
                                       OrderedHighRegs.rend(),
                                       HighRegs);

  while (HiRegToSave != OrderedHighRegs.rend()) {
    // Find the first low register to use.
    auto CopyRegIt = getNextOrderedReg(OrderedCopyRegs.rbegin(),
                                       OrderedCopyRegs.rend(),
                                       CopyRegs);

    // Create the PUSH, but don't insert it yet (the MOVs need to come first).
    MachineInstrBuilder PushMIB = BuildMI(MF, DL, TII.get(ARM::tPUSH))
                                      .add(predOps(ARMCC::AL))
                                      .setMIFlags(MachineInstr::FrameSetup);

    SmallVector<unsigned, 4> RegsToPush;
    while (HiRegToSave != OrderedHighRegs.rend() &&
           CopyRegIt != OrderedCopyRegs.rend()) {
      if (HighRegs.count(*HiRegToSave)) {
        bool isKill = !MRI.isLiveIn(*HiRegToSave);
        if (isKill && !MRI.isReserved(*HiRegToSave))
          MBB.addLiveIn(*HiRegToSave);

        // Emit a MOV from the high reg to the low reg.
        BuildMI(MBB, MI, DL, TII.get(ARM::tMOVr))
            .addReg(*CopyRegIt, RegState::Define)
            .addReg(*HiRegToSave, getKillRegState(isKill))
            .add(predOps(ARMCC::AL))
            .setMIFlags(MachineInstr::FrameSetup);

        // Record the register that must be added to the PUSH.
        RegsToPush.push_back(*CopyRegIt);

        CopyRegIt = getNextOrderedReg(std::next(CopyRegIt),
                                      OrderedCopyRegs.rend(),
                                      CopyRegs);
        HiRegToSave = getNextOrderedReg(std::next(HiRegToSave),
                                        OrderedHighRegs.rend(),
                                        HighRegs);
      }
    }

    // Add the low registers to the PUSH, in ascending order.
    for (unsigned Reg : llvm::reverse(RegsToPush))
      PushMIB.addReg(Reg, RegState::Kill);

    // Insert the PUSH instruction after the MOVs.
    MBB.insert(MI, PushMIB);
  }
}

static void popRegsFromStack(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MI,
                             const TargetInstrInfo &TII,
                             const std::set<Register> &RegsToRestore,
                             const std::set<Register> &AvailableCopyRegs,
                             bool IsVarArg, bool HasV5Ops) {
  if (RegsToRestore.empty())
    return;

  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();

  std::set<Register> LowRegs, HighRegs;
  splitLowAndHighRegs(RegsToRestore, LowRegs, HighRegs);

  // Pop the high registers first
  // There are no store instructions that can access high registers directly,
  // so we have to pop into low registers and them move to  the high registers.
  // This might take multiple pops, as it is possible for there to
  // be fewer low registers available than high registers which need restoring.

  // Find the first register to restore.
  auto HiRegToRestore = getNextOrderedReg(OrderedHighRegs.begin(),
                                          OrderedHighRegs.end(),
                                          HighRegs);

  std::set<Register> CopyRegs = AvailableCopyRegs;
  Register LowScratchReg;
  if (!HighRegs.empty() && CopyRegs.empty()) {
    // No copy regs are available to pop high regs. Let's make use of a return
    // register and the scratch register (IP/R12) to copy things around.
    LowScratchReg = ARM::R0;
    BuildMI(MBB, MI, DL, TII.get(ARM::tMOVr))
        .addReg(ARM::R12, RegState::Define)
        .addReg(LowScratchReg, RegState::Kill)
        .add(predOps(ARMCC::AL))
        .setMIFlag(MachineInstr::FrameDestroy);
    CopyRegs.insert(LowScratchReg);
  }

  while (HiRegToRestore != OrderedHighRegs.end()) {
    assert(!CopyRegs.empty());
    // Find the first low register to use.
    auto CopyReg = getNextOrderedReg(OrderedCopyRegs.begin(),
                                     OrderedCopyRegs.end(),
                                     CopyRegs);

    // Create the POP instruction.
    MachineInstrBuilder PopMIB = BuildMI(MBB, MI, DL, TII.get(ARM::tPOP))
                                     .add(predOps(ARMCC::AL))
                                     .setMIFlag(MachineInstr::FrameDestroy);

    while (HiRegToRestore != OrderedHighRegs.end() &&
           CopyReg != OrderedCopyRegs.end()) {
      // Add the low register to the POP.
      PopMIB.addReg(*CopyReg, RegState::Define);

      // Create the MOV from low to high register.
      BuildMI(MBB, MI, DL, TII.get(ARM::tMOVr))
          .addReg(*HiRegToRestore, RegState::Define)
          .addReg(*CopyReg, RegState::Kill)
          .add(predOps(ARMCC::AL))
          .setMIFlag(MachineInstr::FrameDestroy);

      CopyReg = getNextOrderedReg(std::next(CopyReg),
                                  OrderedCopyRegs.end(),
                                  CopyRegs);
      HiRegToRestore = getNextOrderedReg(std::next(HiRegToRestore),
                                         OrderedHighRegs.end(),
                                         HighRegs);
    }
  }

  // Restore low register used as scratch if necessary
  if (LowScratchReg.isValid()) {
    BuildMI(MBB, MI, DL, TII.get(ARM::tMOVr))
        .addReg(LowScratchReg, RegState::Define)
        .addReg(ARM::R12, RegState::Kill)
        .add(predOps(ARMCC::AL))
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  // Now pop the low registers
  if (!LowRegs.empty()) {
    MachineInstrBuilder MIB = BuildMI(MF, DL, TII.get(ARM::tPOP))
                                  .add(predOps(ARMCC::AL))
                                  .setMIFlag(MachineInstr::FrameDestroy);

    bool NeedsPop = false;
    for (Register Reg : OrderedLowRegs) {
      if (!LowRegs.count(Reg))
        continue;

      if (Reg == ARM::LR) {
        if (!MBB.succ_empty() || MI->getOpcode() == ARM::TCRETURNdi ||
            MI->getOpcode() == ARM::TCRETURNri ||
            MI->getOpcode() == ARM::TCRETURNrinotr12)
          // LR may only be popped into PC, as part of return sequence.
          // If this isn't the return sequence, we'll need emitPopSpecialFixUp
          // to restore LR the hard way.
          // FIXME: if we don't pass any stack arguments it would be actually
          // advantageous *and* correct to do the conversion to an ordinary call
          // instruction here.
          continue;
        // Special epilogue for vararg functions. See emitEpilogue
        if (IsVarArg)
          continue;
        // ARMv4T requires BX, see emitEpilogue
        if (!HasV5Ops)
          continue;

        // CMSE entry functions must return via BXNS, see emitEpilogue.
        if (AFI->isCmseNSEntryFunction())
          continue;

        // Pop LR into PC.
        Reg = ARM::PC;
        (*MIB).setDesc(TII.get(ARM::tPOP_RET));
        if (MI != MBB.end())
          MIB.copyImplicitOps(*MI);
        MI = MBB.erase(MI);
      }
      MIB.addReg(Reg, getDefRegState(true));
      NeedsPop = true;
    }

    // It's illegal to emit pop instruction without operands.
    if (NeedsPop)
      MBB.insert(MI, &*MIB);
    else
      MF.deleteMachineInstr(MIB);
  }
}

bool Thumb1FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineFunction &MF = *MBB.getParent();
  const ARMBaseRegisterInfo *RegInfo = static_cast<const ARMBaseRegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  Register FPReg = RegInfo->getFrameRegister(MF);

  // In case FP is a high reg, we need a separate push sequence to generate
  // a correct Frame Record
  bool NeedsFrameRecordPush = hasFP(MF) && ARM::hGPRRegClass.contains(FPReg);

  std::set<Register> FrameRecord;
  std::set<Register> SpilledGPRs;
  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    if (NeedsFrameRecordPush && (Reg == FPReg || Reg == ARM::LR))
      FrameRecord.insert(Reg);
    else
      SpilledGPRs.insert(Reg);
  }

  pushRegsToStack(MBB, MI, TII, FrameRecord, {ARM::LR});

  // Determine intermediate registers which can be used for pushing high regs:
  // - Spilled low regs
  // - Unused argument registers
  std::set<Register> CopyRegs;
  for (Register Reg : SpilledGPRs)
    if ((ARM::tGPRRegClass.contains(Reg) || Reg == ARM::LR) &&
        !MF.getRegInfo().isLiveIn(Reg) && !(hasFP(MF) && Reg == FPReg))
      CopyRegs.insert(Reg);
  for (unsigned ArgReg : {ARM::R0, ARM::R1, ARM::R2, ARM::R3})
    if (!MF.getRegInfo().isLiveIn(ArgReg))
      CopyRegs.insert(ArgReg);

  pushRegsToStack(MBB, MI, TII, SpilledGPRs, CopyRegs);

  return true;
}

bool Thumb1FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const ARMBaseRegisterInfo *RegInfo = static_cast<const ARMBaseRegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  bool IsVarArg = AFI->getArgRegsSaveSize() > 0;
  Register FPReg = RegInfo->getFrameRegister(MF);

  // In case FP is a high reg, we need a separate pop sequence to generate
  // a correct Frame Record
  bool NeedsFrameRecordPop = hasFP(MF) && ARM::hGPRRegClass.contains(FPReg);

  std::set<Register> FrameRecord;
  std::set<Register> SpilledGPRs;
  for (CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    if (NeedsFrameRecordPop && (Reg == FPReg || Reg == ARM::LR))
      FrameRecord.insert(Reg);
    else
      SpilledGPRs.insert(Reg);

    if (Reg == ARM::LR)
      I.setRestored(false);
  }

  // Determine intermidiate registers which can be used for popping high regs:
  // - Spilled low regs
  // - Unused return registers
  std::set<Register> CopyRegs;
  std::set<Register> UnusedReturnRegs;
  for (Register Reg : SpilledGPRs)
    if ((ARM::tGPRRegClass.contains(Reg)) && !(hasFP(MF) && Reg == FPReg))
      CopyRegs.insert(Reg);
  auto Terminator = MBB.getFirstTerminator();
  if (Terminator != MBB.end() && Terminator->getOpcode() == ARM::tBX_RET) {
    UnusedReturnRegs.insert(ARM::R0);
    UnusedReturnRegs.insert(ARM::R1);
    UnusedReturnRegs.insert(ARM::R2);
    UnusedReturnRegs.insert(ARM::R3);
    for (auto Op : Terminator->implicit_operands()) {
      if (Op.isReg())
        UnusedReturnRegs.erase(Op.getReg());
    }
  }
  CopyRegs.insert(UnusedReturnRegs.begin(), UnusedReturnRegs.end());

  // First pop regular spilled regs.
  popRegsFromStack(MBB, MI, TII, SpilledGPRs, CopyRegs, IsVarArg,
                   STI.hasV5TOps());

  // LR may only be popped into pc, as part of a return sequence.
  // Check that no other pop instructions are inserted after that.
  assert((!SpilledGPRs.count(ARM::LR) || FrameRecord.empty()) &&
         "Can't insert pop after return sequence");

  // Now pop Frame Record regs.
  // Only unused return registers can be used as copy regs at this point.
  popRegsFromStack(MBB, MI, TII, FrameRecord, UnusedReturnRegs, IsVarArg,
                   STI.hasV5TOps());

  return true;
}
