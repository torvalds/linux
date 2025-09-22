//===-- MSP430FrameLowering.cpp - MSP430 Frame Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MSP430 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "MSP430FrameLowering.h"
#include "MSP430InstrInfo.h"
#include "MSP430MachineFunctionInfo.h"
#include "MSP430Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

MSP430FrameLowering::MSP430FrameLowering(const MSP430Subtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(2), -2,
                          Align(2)),
      STI(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()) {}

bool MSP430FrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  return (MF.getTarget().Options.DisableFramePointerElim(MF) ||
          MF.getFrameInfo().hasVarSizedObjects() ||
          MFI.isFrameAddressTaken());
}

bool MSP430FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

void MSP430FrameLowering::BuildCFI(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   const DebugLoc &DL,
                                   const MCCFIInstruction &CFIInst,
                                   MachineInstr::MIFlag Flag) const {
  MachineFunction &MF = *MBB.getParent();
  unsigned CFIIndex = MF.addFrameInst(CFIInst);
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(Flag);
}

void MSP430FrameLowering::emitCalleeSavedFrameMoves(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, bool IsPrologue) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();

  // Add callee saved registers to move list.
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Calculate offsets.
  for (const CalleeSavedInfo &I : CSI) {
    int64_t Offset = MFI.getObjectOffset(I.getFrameIdx());
    Register Reg = I.getReg();
    unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);

    if (IsPrologue) {
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
    } else {
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createRestore(nullptr, DwarfReg));
    }
  }
}

void MSP430FrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MSP430MachineFunctionInfo *MSP430FI = MF.getInfo<MSP430MachineFunctionInfo>();
  const MSP430InstrInfo &TII =
      *static_cast<const MSP430InstrInfo *>(MF.getSubtarget().getInstrInfo());

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();
  int stackGrowth = -2;

  uint64_t NumBytes = 0;
  if (hasFP(MF)) {
    // Calculate required stack adjustment
    uint64_t FrameSize = StackSize - 2;
    NumBytes = FrameSize - MSP430FI->getCalleeSavedFrameSize();

    // Get the offset of the stack slot for the EBP register... which is
    // guaranteed to be the last slot by processFunctionBeforeFrameFinalized.
    // Update the frame offset adjustment.
    MFI.setOffsetAdjustment(-NumBytes);

    // Save FP into the appropriate stack slot...
    BuildMI(MBB, MBBI, DL, TII.get(MSP430::PUSH16r))
        .addReg(MSP430::R4, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);

    // Mark the place where FP was saved.
    // Define the current CFA rule to use the provided offset.
    BuildCFI(MBB, MBBI, DL,
             MCCFIInstruction::cfiDefCfaOffset(nullptr, -2 * stackGrowth),
             MachineInstr::FrameSetup);

    // Change the rule for the FramePtr to be an "offset" rule.
    unsigned DwarfFramePtr = TRI->getDwarfRegNum(MSP430::R4, true);
    BuildCFI(
        MBB, MBBI, DL,
        MCCFIInstruction::createOffset(nullptr, DwarfFramePtr, 2 * stackGrowth),
        MachineInstr::FrameSetup);

    // Update FP with the new base value...
    BuildMI(MBB, MBBI, DL, TII.get(MSP430::MOV16rr), MSP430::R4)
        .addReg(MSP430::SP)
        .setMIFlag(MachineInstr::FrameSetup);

    // Mark effective beginning of when frame pointer becomes valid.
    // Define the current CFA to use the FP register.
    BuildCFI(MBB, MBBI, DL,
             MCCFIInstruction::createDefCfaRegister(nullptr, DwarfFramePtr),
             MachineInstr::FrameSetup);

    // Mark the FramePtr as live-in in every block except the entry.
    for (MachineBasicBlock &MBBJ : llvm::drop_begin(MF))
      MBBJ.addLiveIn(MSP430::R4);
  } else
    NumBytes = StackSize - MSP430FI->getCalleeSavedFrameSize();

  // Skip the callee-saved push instructions.
  int StackOffset = 2 * stackGrowth;
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup) &&
         (MBBI->getOpcode() == MSP430::PUSH16r)) {
    ++MBBI;

    if (!hasFP(MF)) {
      // Mark callee-saved push instruction.
      // Define the current CFA rule to use the provided offset.
      assert(StackSize && "Expected stack frame");
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfaOffset(nullptr, -StackOffset),
               MachineInstr::FrameSetup);
      StackOffset += stackGrowth;
    }
  }

  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();

  if (NumBytes) { // adjust stack pointer: SP -= numbytes
    // If there is an SUB16ri of SP immediately before this instruction, merge
    // the two.
    //NumBytes -= mergeSPUpdates(MBB, MBBI, true);
    // If there is an ADD16ri or SUB16ri of SP immediately after this
    // instruction, merge the two instructions.
    // mergeSPUpdatesDown(MBB, MBBI, &NumBytes);

    if (NumBytes) {
      MachineInstr *MI =
          BuildMI(MBB, MBBI, DL, TII.get(MSP430::SUB16ri), MSP430::SP)
              .addReg(MSP430::SP)
              .addImm(NumBytes)
              .setMIFlag(MachineInstr::FrameSetup);
      // The SRW implicit def is dead.
      MI->getOperand(3).setIsDead();
    }
    if (!hasFP(MF)) {
      // Adjust the previous CFA value if CFA was not redefined by FP
      BuildCFI(
          MBB, MBBI, DL,
          MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize - stackGrowth),
          MachineInstr::FrameSetup);
    }
  }

  emitCalleeSavedFrameMoves(MBB, MBBI, DL, true);
}

void MSP430FrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MSP430MachineFunctionInfo *MSP430FI = MF.getInfo<MSP430MachineFunctionInfo>();
  const MSP430InstrInfo &TII =
      *static_cast<const MSP430InstrInfo *>(MF.getSubtarget().getInstrInfo());

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  unsigned RetOpcode = MBBI->getOpcode();
  DebugLoc DL = MBBI->getDebugLoc();

  switch (RetOpcode) {
  case MSP430::RET:
  case MSP430::RETI: break;  // These are ok
  default:
    llvm_unreachable("Can only insert epilog into returning blocks");
  }

  // Get the number of bytes to allocate from the FrameInfo
  uint64_t StackSize = MFI.getStackSize();
  unsigned CSSize = MSP430FI->getCalleeSavedFrameSize();
  uint64_t NumBytes = 0;

  MachineBasicBlock::iterator AfterPop = MBBI;
  if (hasFP(MF)) {
    // Calculate required stack adjustment
    uint64_t FrameSize = StackSize - 2;
    NumBytes = FrameSize - CSSize;

    // pop FP.
    BuildMI(MBB, MBBI, DL, TII.get(MSP430::POP16r), MSP430::R4)
        .setMIFlag(MachineInstr::FrameDestroy);
    unsigned DwarfStackPtr = TRI->getDwarfRegNum(MSP430::SP, true);
    BuildCFI(MBB, MBBI, DL,
             MCCFIInstruction::cfiDefCfa(nullptr, DwarfStackPtr, 2),
             MachineInstr::FrameDestroy);
    --MBBI;
    if (!MBB.succ_empty() && !MBB.isReturnBlock()) {
      unsigned DwarfFramePtr = TRI->getDwarfRegNum(MSP430::R4, true);
      BuildCFI(MBB, AfterPop, DL,
               MCCFIInstruction::createRestore(nullptr, DwarfFramePtr),
               MachineInstr::FrameDestroy);
      --MBBI;
      --AfterPop;
    }
  } else
    NumBytes = StackSize - CSSize;

  // Skip the callee-saved pop instructions.
  MachineBasicBlock::iterator FirstCSPop = MBBI;
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    unsigned Opc = PI->getOpcode();
    if ((Opc != MSP430::POP16r || !PI->getFlag(MachineInstr::FrameDestroy)) &&
        !PI->isTerminator())
      break;
    FirstCSPop = PI;
    --MBBI;
  }
  MBBI = FirstCSPop;

  DL = MBBI->getDebugLoc();

  // If there is an ADD16ri or SUB16ri of SP immediately before this
  // instruction, merge the two instructions.
  //if (NumBytes || MFI.hasVarSizedObjects())
  //  mergeSPUpdatesUp(MBB, MBBI, StackPtr, &NumBytes);

  if (MFI.hasVarSizedObjects()) {
    BuildMI(MBB, MBBI, DL, TII.get(MSP430::MOV16rr), MSP430::SP)
        .addReg(MSP430::R4)
        .setMIFlag(MachineInstr::FrameDestroy);
    if (CSSize) {
      MachineInstr *MI =
          BuildMI(MBB, MBBI, DL, TII.get(MSP430::SUB16ri), MSP430::SP)
              .addReg(MSP430::SP)
              .addImm(CSSize)
              .setMIFlag(MachineInstr::FrameDestroy);
      // The SRW implicit def is dead.
      MI->getOperand(3).setIsDead();
    }
  } else {
    // adjust stack pointer back: SP += numbytes
    if (NumBytes) {
      MachineInstr *MI =
          BuildMI(MBB, MBBI, DL, TII.get(MSP430::ADD16ri), MSP430::SP)
              .addReg(MSP430::SP)
              .addImm(NumBytes)
              .setMIFlag(MachineInstr::FrameDestroy);
      // The SRW implicit def is dead.
      MI->getOperand(3).setIsDead();

      if (!hasFP(MF)) {
        // Adjust CFA value if it was defined by SP
        BuildCFI(MBB, MBBI, DL,
                 MCCFIInstruction::cfiDefCfaOffset(nullptr, CSSize + 2),
                 MachineInstr::FrameDestroy);
      }
    }
  }

  if (!hasFP(MF)) {
    MBBI = FirstCSPop;
    int64_t Offset = -(int64_t)CSSize - 2;
    // Mark callee-saved pop instruction.
    // Define the current CFA rule to use the provided offset.
    while (MBBI != MBB.end()) {
      MachineBasicBlock::iterator PI = MBBI;
      unsigned Opc = PI->getOpcode();
      ++MBBI;
      if (Opc == MSP430::POP16r) {
        Offset += 2;
        BuildCFI(MBB, MBBI, DL,
                 MCCFIInstruction::cfiDefCfaOffset(nullptr, -Offset),
                 MachineInstr::FrameDestroy);
      }
    }
  }
  emitCalleeSavedFrameMoves(MBB, AfterPop, DL, false);
}

// FIXME: Can we eleminate these in favour of generic code?
bool MSP430FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MSP430MachineFunctionInfo *MFI = MF.getInfo<MSP430MachineFunctionInfo>();
  MFI->setCalleeSavedFrameSize(CSI.size() * 2);

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    // Add the callee-saved register as live-in. It's killed at the spill.
    MBB.addLiveIn(Reg);
    BuildMI(MBB, MI, DL, TII.get(MSP430::PUSH16r))
        .addReg(Reg, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}

bool MSP430FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  for (const CalleeSavedInfo &I : llvm::reverse(CSI))
    BuildMI(MBB, MI, DL, TII.get(MSP430::POP16r), I.getReg())
        .setMIFlag(MachineInstr::FrameDestroy);

  return true;
}

MachineBasicBlock::iterator MSP430FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const MSP430InstrInfo &TII =
      *static_cast<const MSP430InstrInfo *>(MF.getSubtarget().getInstrInfo());
  if (!hasReservedCallFrame(MF)) {
    // If the stack pointer can be changed after prologue, turn the
    // adjcallstackup instruction into a 'sub SP, <amt>' and the
    // adjcallstackdown instruction into 'add SP, <amt>'
    // TODO: consider using push / pop instead of sub + store / add
    MachineInstr &Old = *I;
    uint64_t Amount = TII.getFrameSize(Old);
    if (Amount != 0) {
      // We need to keep the stack aligned properly.  To do this, we round the
      // amount of space needed for the outgoing arguments up to the next
      // alignment boundary.
      Amount = alignTo(Amount, getStackAlign());

      MachineInstr *New = nullptr;
      if (Old.getOpcode() == TII.getCallFrameSetupOpcode()) {
        New =
            BuildMI(MF, Old.getDebugLoc(), TII.get(MSP430::SUB16ri), MSP430::SP)
                .addReg(MSP430::SP)
                .addImm(Amount);
      } else {
        assert(Old.getOpcode() == TII.getCallFrameDestroyOpcode());
        // factor out the amount the callee already popped.
        Amount -= TII.getFramePoppedByCallee(Old);
        if (Amount)
          New = BuildMI(MF, Old.getDebugLoc(), TII.get(MSP430::ADD16ri),
                        MSP430::SP)
                    .addReg(MSP430::SP)
                    .addImm(Amount);
      }

      if (New) {
        // The SRW implicit def is dead.
        New->getOperand(3).setIsDead();

        // Replace the pseudo instruction with a new instruction...
        MBB.insert(I, New);
      }
    }
  } else if (I->getOpcode() == TII.getCallFrameDestroyOpcode()) {
    // If we are performing frame pointer elimination and if the callee pops
    // something off the stack pointer, add it back.
    if (uint64_t CalleeAmt = TII.getFramePoppedByCallee(*I)) {
      MachineInstr &Old = *I;
      MachineInstr *New =
          BuildMI(MF, Old.getDebugLoc(), TII.get(MSP430::SUB16ri), MSP430::SP)
              .addReg(MSP430::SP)
              .addImm(CalleeAmt);
      if (!hasFP(MF)) {
        DebugLoc DL = I->getDebugLoc();
        BuildCFI(MBB, I, DL,
                 MCCFIInstruction::createAdjustCfaOffset(nullptr, CalleeAmt));
      }
      // The SRW implicit def is dead.
      New->getOperand(3).setIsDead();

      MBB.insert(I, New);
    }
  }

  return MBB.erase(I);
}

void
MSP430FrameLowering::processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                                         RegScavenger *) const {
  // Create a frame entry for the FP register that must be saved.
  if (hasFP(MF)) {
    int FrameIdx = MF.getFrameInfo().CreateFixedObject(2, -4, true);
    (void)FrameIdx;
    assert(FrameIdx == MF.getFrameInfo().getObjectIndexBegin() &&
           "Slot for FP register must be last in order to be found!");
  }
}
