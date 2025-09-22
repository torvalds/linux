//===-- AVRFrameLowering.cpp - AVR Frame Information ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AVR implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "AVRFrameLowering.h"

#include "AVR.h"
#include "AVRInstrInfo.h"
#include "AVRMachineFunctionInfo.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"

namespace llvm {

AVRFrameLowering::AVRFrameLowering()
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(1), -2) {}

bool AVRFrameLowering::canSimplifyCallFramePseudos(
    const MachineFunction &MF) const {
  // Always simplify call frame pseudo instructions, even when
  // hasReservedCallFrame is false.
  return true;
}

bool AVRFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // Reserve call frame memory in function prologue under the following
  // conditions:
  // - Y pointer is reserved to be the frame pointer.
  // - The function does not contain variable sized objects.

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return hasFP(MF) && !MFI.hasVarSizedObjects();
}

void AVRFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = (MBBI != MBB.end()) ? MBBI->getDebugLoc() : DebugLoc();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const AVRInstrInfo &TII = *STI.getInstrInfo();
  const AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  bool HasFP = hasFP(MF);

  // Interrupt handlers re-enable interrupts in function entry.
  if (AFI->isInterruptHandler()) {
    BuildMI(MBB, MBBI, DL, TII.get(AVR::BSETs))
        .addImm(0x07)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Emit special prologue code to save R1, R0 and SREG in interrupt/signal
  // handlers before saving any other registers.
  if (AFI->isInterruptOrSignalHandler()) {
    BuildMI(MBB, MBBI, DL, TII.get(AVR::PUSHRr))
        .addReg(STI.getTmpRegister(), RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);

    BuildMI(MBB, MBBI, DL, TII.get(AVR::INRdA), STI.getTmpRegister())
        .addImm(STI.getIORegSREG())
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(AVR::PUSHRr))
        .addReg(STI.getTmpRegister(), RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
    if (!MRI.reg_empty(STI.getZeroRegister())) {
      BuildMI(MBB, MBBI, DL, TII.get(AVR::PUSHRr))
          .addReg(STI.getZeroRegister(), RegState::Kill)
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII.get(AVR::EORRdRr))
          .addReg(STI.getZeroRegister(), RegState::Define)
          .addReg(STI.getZeroRegister(), RegState::Kill)
          .addReg(STI.getZeroRegister(), RegState::Kill)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // Early exit if the frame pointer is not needed in this function.
  if (!HasFP) {
    return;
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned FrameSize = MFI.getStackSize() - AFI->getCalleeSavedFrameSize();

  // Skip the callee-saved push instructions.
  while (
      (MBBI != MBB.end()) && MBBI->getFlag(MachineInstr::FrameSetup) &&
      (MBBI->getOpcode() == AVR::PUSHRr || MBBI->getOpcode() == AVR::PUSHWRr)) {
    ++MBBI;
  }

  // Update Y with the new base value.
  BuildMI(MBB, MBBI, DL, TII.get(AVR::SPREAD), AVR::R29R28)
      .addReg(AVR::SP)
      .setMIFlag(MachineInstr::FrameSetup);

  // Mark the FramePtr as live-in in every block except the entry.
  for (MachineBasicBlock &MBBJ : llvm::drop_begin(MF)) {
    MBBJ.addLiveIn(AVR::R29R28);
  }

  if (!FrameSize) {
    return;
  }

  // Reserve the necessary frame memory by doing FP -= <size>.
  unsigned Opcode = (isUInt<6>(FrameSize) && STI.hasADDSUBIW()) ? AVR::SBIWRdK
                                                                : AVR::SUBIWRdK;

  MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(Opcode), AVR::R29R28)
                         .addReg(AVR::R29R28, RegState::Kill)
                         .addImm(FrameSize)
                         .setMIFlag(MachineInstr::FrameSetup);
  // The SREG implicit def is dead.
  MI->getOperand(3).setIsDead();

  // Write back R29R28 to SP and temporarily disable interrupts.
  BuildMI(MBB, MBBI, DL, TII.get(AVR::SPWRITE), AVR::SP)
      .addReg(AVR::R29R28)
      .setMIFlag(MachineInstr::FrameSetup);
}

static void restoreStatusRegister(MachineFunction &MF, MachineBasicBlock &MBB) {
  const AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();

  DebugLoc DL = MBBI->getDebugLoc();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const AVRInstrInfo &TII = *STI.getInstrInfo();

  // Emit special epilogue code to restore R1, R0 and SREG in interrupt/signal
  // handlers at the very end of the function, just before reti.
  if (AFI->isInterruptOrSignalHandler()) {
    if (!MRI.reg_empty(STI.getZeroRegister())) {
      BuildMI(MBB, MBBI, DL, TII.get(AVR::POPRd), STI.getZeroRegister());
    }
    BuildMI(MBB, MBBI, DL, TII.get(AVR::POPRd), STI.getTmpRegister());
    BuildMI(MBB, MBBI, DL, TII.get(AVR::OUTARr))
        .addImm(STI.getIORegSREG())
        .addReg(STI.getTmpRegister(), RegState::Kill);
    BuildMI(MBB, MBBI, DL, TII.get(AVR::POPRd), STI.getTmpRegister());
  }
}

void AVRFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  const AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();

  // Early exit if the frame pointer is not needed in this function except for
  // signal/interrupt handlers where special code generation is required.
  if (!hasFP(MF) && !AFI->isInterruptOrSignalHandler()) {
    return;
  }

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  assert(MBBI->getDesc().isReturn() &&
         "Can only insert epilog into returning blocks");

  DebugLoc DL = MBBI->getDebugLoc();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned FrameSize = MFI.getStackSize() - AFI->getCalleeSavedFrameSize();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const AVRInstrInfo &TII = *STI.getInstrInfo();

  // Early exit if there is no need to restore the frame pointer.
  if (!FrameSize && !MF.getFrameInfo().hasVarSizedObjects()) {
    restoreStatusRegister(MF, MBB);
    return;
  }

  // Skip the callee-saved pop instructions.
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    int Opc = PI->getOpcode();

    if (Opc != AVR::POPRd && Opc != AVR::POPWRd && !PI->isTerminator()) {
      break;
    }

    --MBBI;
  }

  if (FrameSize) {
    unsigned Opcode;

    // Select the optimal opcode depending on how big it is.
    if (isUInt<6>(FrameSize) && STI.hasADDSUBIW()) {
      Opcode = AVR::ADIWRdK;
    } else {
      Opcode = AVR::SUBIWRdK;
      FrameSize = -FrameSize;
    }

    // Restore the frame pointer by doing FP += <size>.
    MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(Opcode), AVR::R29R28)
                           .addReg(AVR::R29R28, RegState::Kill)
                           .addImm(FrameSize);
    // The SREG implicit def is dead.
    MI->getOperand(3).setIsDead();
  }

  // Write back R29R28 to SP and temporarily disable interrupts.
  BuildMI(MBB, MBBI, DL, TII.get(AVR::SPWRITE), AVR::SP)
      .addReg(AVR::R29R28, RegState::Kill);

  restoreStatusRegister(MF, MBB);
}

// Return true if the specified function should have a dedicated frame
// pointer register. This is true if the function meets any of the following
// conditions:
//  - a register has been spilled
//  - has allocas
//  - input arguments are passed using the stack
//
// Notice that strictly this is not a frame pointer because it contains SP after
// frame allocation instead of having the original SP in function entry.
bool AVRFrameLowering::hasFP(const MachineFunction &MF) const {
  const AVRMachineFunctionInfo *FuncInfo = MF.getInfo<AVRMachineFunctionInfo>();

  return (FuncInfo->getHasSpills() || FuncInfo->getHasAllocas() ||
          FuncInfo->getHasStackArgs() ||
          MF.getFrameInfo().hasVarSizedObjects());
}

bool AVRFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty()) {
    return false;
  }

  unsigned CalleeFrameSize = 0;
  DebugLoc DL = MBB.findDebugLoc(MI);
  MachineFunction &MF = *MBB.getParent();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  AVRMachineFunctionInfo *AVRFI = MF.getInfo<AVRMachineFunctionInfo>();

  for (const CalleeSavedInfo &I : llvm::reverse(CSI)) {
    Register Reg = I.getReg();
    bool IsNotLiveIn = !MBB.isLiveIn(Reg);

    // Check if Reg is a sub register of a 16-bit livein register, and then
    // add it to the livein list.
    if (IsNotLiveIn)
      for (const auto &LiveIn : MBB.liveins())
        if (STI.getRegisterInfo()->isSubRegister(LiveIn.PhysReg, Reg)) {
          IsNotLiveIn = false;
          MBB.addLiveIn(Reg);
          break;
        }

    assert(TRI->getRegSizeInBits(*TRI->getMinimalPhysRegClass(Reg)) == 8 &&
           "Invalid register size");

    // Add the callee-saved register as live-in only if it is not already a
    // live-in register, this usually happens with arguments that are passed
    // through callee-saved registers.
    if (IsNotLiveIn) {
      MBB.addLiveIn(Reg);
    }

    // Do not kill the register when it is an input argument.
    BuildMI(MBB, MI, DL, TII.get(AVR::PUSHRr))
        .addReg(Reg, getKillRegState(IsNotLiveIn))
        .setMIFlag(MachineInstr::FrameSetup);
    ++CalleeFrameSize;
  }

  AVRFI->setCalleeSavedFrameSize(CalleeFrameSize);

  return true;
}

bool AVRFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty()) {
    return false;
  }

  DebugLoc DL = MBB.findDebugLoc(MI);
  const MachineFunction &MF = *MBB.getParent();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  for (const CalleeSavedInfo &CCSI : CSI) {
    Register Reg = CCSI.getReg();

    assert(TRI->getRegSizeInBits(*TRI->getMinimalPhysRegClass(Reg)) == 8 &&
           "Invalid register size");

    BuildMI(MBB, MI, DL, TII.get(AVR::POPRd), Reg);
  }

  return true;
}

/// Replace pseudo store instructions that pass arguments through the stack with
/// real instructions.
static void fixStackStores(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator StartMI,
                           const TargetInstrInfo &TII) {
  // Iterate through the BB until we hit a call instruction or we reach the end.
  for (MachineInstr &MI :
       llvm::make_early_inc_range(llvm::make_range(StartMI, MBB.end()))) {
    if (MI.isCall())
      break;

    unsigned Opcode = MI.getOpcode();

    // Only care of pseudo store instructions where SP is the base pointer.
    if (Opcode != AVR::STDSPQRr && Opcode != AVR::STDWSPQRr)
      continue;

    assert(MI.getOperand(0).getReg() == AVR::SP &&
           "SP is expected as base pointer");

    // Replace this instruction with a regular store. Use Y as the base
    // pointer since it is guaranteed to contain a copy of SP.
    unsigned STOpc =
        (Opcode == AVR::STDWSPQRr) ? AVR::STDWPtrQRr : AVR::STDPtrQRr;

    MI.setDesc(TII.get(STOpc));
    MI.getOperand(0).setReg(AVR::R31R30);
  }
}

MachineBasicBlock::iterator AVRFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const AVRInstrInfo &TII = *STI.getInstrInfo();

  if (hasReservedCallFrame(MF)) {
    return MBB.erase(MI);
  }

  DebugLoc DL = MI->getDebugLoc();
  unsigned int Opcode = MI->getOpcode();
  int Amount = TII.getFrameSize(*MI);

  if (Amount == 0) {
    return MBB.erase(MI);
  }

  assert(getStackAlign() == Align(1) && "Unsupported stack alignment");

  if (Opcode == TII.getCallFrameSetupOpcode()) {
    // Update the stack pointer.
    // In many cases this can be done far more efficiently by pushing the
    // relevant values directly to the stack. However, doing that correctly
    // (in the right order, possibly skipping some empty space for undef
    // values, etc) is tricky and thus left to be optimized in the future.
    BuildMI(MBB, MI, DL, TII.get(AVR::SPREAD), AVR::R31R30).addReg(AVR::SP);

    MachineInstr *New =
        BuildMI(MBB, MI, DL, TII.get(AVR::SUBIWRdK), AVR::R31R30)
            .addReg(AVR::R31R30, RegState::Kill)
            .addImm(Amount);
    New->getOperand(3).setIsDead();

    BuildMI(MBB, MI, DL, TII.get(AVR::SPWRITE), AVR::SP).addReg(AVR::R31R30);

    // Make sure the remaining stack stores are converted to real store
    // instructions.
    fixStackStores(MBB, MI, TII);
  } else {
    assert(Opcode == TII.getCallFrameDestroyOpcode());

    // Note that small stack changes could be implemented more efficiently
    // with a few pop instructions instead of the 8-9 instructions now
    // required.

    // Select the best opcode to adjust SP based on the offset size.
    unsigned AddOpcode;

    if (isUInt<6>(Amount) && STI.hasADDSUBIW()) {
      AddOpcode = AVR::ADIWRdK;
    } else {
      AddOpcode = AVR::SUBIWRdK;
      Amount = -Amount;
    }

    // Build the instruction sequence.
    BuildMI(MBB, MI, DL, TII.get(AVR::SPREAD), AVR::R31R30).addReg(AVR::SP);

    MachineInstr *New = BuildMI(MBB, MI, DL, TII.get(AddOpcode), AVR::R31R30)
                            .addReg(AVR::R31R30, RegState::Kill)
                            .addImm(Amount);
    New->getOperand(3).setIsDead();

    BuildMI(MBB, MI, DL, TII.get(AVR::SPWRITE), AVR::SP)
        .addReg(AVR::R31R30, RegState::Kill);
  }

  return MBB.erase(MI);
}

void AVRFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // If we have a frame pointer, the Y register needs to be saved as well.
  if (hasFP(MF)) {
    SavedRegs.set(AVR::R29);
    SavedRegs.set(AVR::R28);
  }
}
/// The frame analyzer pass.
///
/// Scans the function for allocas and used arguments
/// that are passed through the stack.
struct AVRFrameAnalyzer : public MachineFunctionPass {
  static char ID;
  AVRFrameAnalyzer() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();

    // If there are no fixed frame indexes during this stage it means there
    // are allocas present in the function.
    if (MFI.getNumObjects() != MFI.getNumFixedObjects()) {
      // Check for the type of allocas present in the function. We only care
      // about fixed size allocas so do not give false positives if only
      // variable sized allocas are present.
      for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
        // Variable sized objects have size 0.
        if (MFI.getObjectSize(i)) {
          AFI->setHasAllocas(true);
          break;
        }
      }
    }

    // If there are fixed frame indexes present, scan the function to see if
    // they are really being used.
    if (MFI.getNumFixedObjects() == 0) {
      return false;
    }

    // Ok fixed frame indexes present, now scan the function to see if they
    // are really being used, otherwise we can ignore them.
    for (const MachineBasicBlock &BB : MF) {
      for (const MachineInstr &MI : BB) {
        int Opcode = MI.getOpcode();

        if ((Opcode != AVR::LDDRdPtrQ) && (Opcode != AVR::LDDWRdPtrQ) &&
            (Opcode != AVR::STDPtrQRr) && (Opcode != AVR::STDWPtrQRr) &&
            (Opcode != AVR::FRMIDX)) {
          continue;
        }

        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isFI()) {
            continue;
          }

          if (MFI.isFixedObjectIndex(MO.getIndex())) {
            AFI->setHasStackArgs(true);
            return false;
          }
        }
      }
    }

    return false;
  }

  StringRef getPassName() const override { return "AVR Frame Analyzer"; }
};

char AVRFrameAnalyzer::ID = 0;

/// Creates instance of the frame analyzer pass.
FunctionPass *createAVRFrameAnalyzerPass() { return new AVRFrameAnalyzer(); }

} // end of namespace llvm
