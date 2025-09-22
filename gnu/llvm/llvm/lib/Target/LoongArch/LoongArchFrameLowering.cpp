//===-- LoongArchFrameLowering.cpp - LoongArch Frame Information -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the LoongArch implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "LoongArchFrameLowering.h"
#include "LoongArchMachineFunctionInfo.h"
#include "LoongArchSubtarget.h"
#include "MCTargetDesc/LoongArchBaseInfo.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/MC/MCDwarf.h"

using namespace llvm;

#define DEBUG_TYPE "loongarch-frame-lowering"

// Return true if the specified function should have a dedicated frame
// pointer register.  This is true if frame pointer elimination is
// disabled, if it needs dynamic stack realignment, if the function has
// variable sized allocas, or if the frame address is taken.
bool LoongArchFrameLowering::hasFP(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         RegInfo->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
         MFI.isFrameAddressTaken();
}

bool LoongArchFrameLowering::hasBP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  return MFI.hasVarSizedObjects() && TRI->hasStackRealignment(MF);
}

void LoongArchFrameLowering::adjustReg(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       const DebugLoc &DL, Register DestReg,
                                       Register SrcReg, int64_t Val,
                                       MachineInstr::MIFlag Flag) const {
  const LoongArchInstrInfo *TII = STI.getInstrInfo();
  bool IsLA64 = STI.is64Bit();
  unsigned Addi = IsLA64 ? LoongArch::ADDI_D : LoongArch::ADDI_W;

  if (DestReg == SrcReg && Val == 0)
    return;

  if (isInt<12>(Val)) {
    // addi.w/d $DstReg, $SrcReg, Val
    BuildMI(MBB, MBBI, DL, TII->get(Addi), DestReg)
        .addReg(SrcReg)
        .addImm(Val)
        .setMIFlag(Flag);
    return;
  }

  // Try to split the offset across two ADDIs. We need to keep the stack pointer
  // aligned after each ADDI. We need to determine the maximum value we can put
  // in each ADDI. In the negative direction, we can use -2048 which is always
  // sufficiently aligned. In the positive direction, we need to find the
  // largest 12-bit immediate that is aligned. Exclude -4096 since it can be
  // created with LU12I.W.
  assert(getStackAlign().value() < 2048 && "Stack alignment too large");
  int64_t MaxPosAdjStep = 2048 - getStackAlign().value();
  if (Val > -4096 && Val <= (2 * MaxPosAdjStep)) {
    int64_t FirstAdj = Val < 0 ? -2048 : MaxPosAdjStep;
    Val -= FirstAdj;
    BuildMI(MBB, MBBI, DL, TII->get(Addi), DestReg)
        .addReg(SrcReg)
        .addImm(FirstAdj)
        .setMIFlag(Flag);
    BuildMI(MBB, MBBI, DL, TII->get(Addi), DestReg)
        .addReg(DestReg, RegState::Kill)
        .addImm(Val)
        .setMIFlag(Flag);
    return;
  }

  unsigned Opc = IsLA64 ? LoongArch::ADD_D : LoongArch::ADD_W;
  if (Val < 0) {
    Val = -Val;
    Opc = IsLA64 ? LoongArch::SUB_D : LoongArch::SUB_W;
  }

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  Register ScratchReg = MRI.createVirtualRegister(&LoongArch::GPRRegClass);
  TII->movImm(MBB, MBBI, DL, ScratchReg, Val, Flag);
  BuildMI(MBB, MBBI, DL, TII->get(Opc), DestReg)
      .addReg(SrcReg)
      .addReg(ScratchReg, RegState::Kill)
      .setMIFlag(Flag);
}

// Determine the size of the frame and maximum call frame size.
void LoongArchFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t FrameSize = MFI.getStackSize();

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, getStackAlign());

  // Update frame info.
  MFI.setStackSize(FrameSize);
}

static uint64_t estimateFunctionSizeInBytes(const LoongArchInstrInfo *TII,
                                            const MachineFunction &MF) {
  uint64_t FuncSize = 0;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      FuncSize += TII->getInstSizeInBytes(MI);
  return FuncSize;
}

static bool needScavSlotForCFR(MachineFunction &MF) {
  if (!MF.getSubtarget<LoongArchSubtarget>().hasBasicF())
    return false;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (MI.getOpcode() == LoongArch::PseudoST_CFR)
        return true;
  return false;
}

void LoongArchFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  const LoongArchRegisterInfo *RI = STI.getRegisterInfo();
  const TargetRegisterClass &RC = LoongArch::GPRRegClass;
  const LoongArchInstrInfo *TII = STI.getInstrInfo();
  LoongArchMachineFunctionInfo *LAFI =
      MF.getInfo<LoongArchMachineFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned ScavSlotsNum = 0;

  // Far branches beyond 27-bit offset require a spill slot for scratch
  // register.
  bool IsLargeFunction = !isInt<27>(estimateFunctionSizeInBytes(TII, MF));
  if (IsLargeFunction)
    ScavSlotsNum = 1;

  // estimateStackSize has been observed to under-estimate the final stack
  // size, so give ourselves wiggle-room by checking for stack size
  // representable an 11-bit signed field rather than 12-bits.
  if (!isInt<11>(MFI.estimateStackSize(MF)))
    ScavSlotsNum = std::max(ScavSlotsNum, 1u);

  // For CFR spill.
  if (needScavSlotForCFR(MF))
    ++ScavSlotsNum;

  // Create emergency spill slots.
  for (unsigned i = 0; i < ScavSlotsNum; ++i) {
    int FI = MFI.CreateStackObject(RI->getSpillSize(RC), RI->getSpillAlign(RC),
                                   false);
    RS->addScavengingFrameIndex(FI);
    if (IsLargeFunction && LAFI->getBranchRelaxationSpillFrameIndex() == -1)
      LAFI->setBranchRelaxationSpillFrameIndex(FI);
    LLVM_DEBUG(dbgs() << "Allocated FI(" << FI
                      << ") as the emergency spill slot.\n");
  }
}

void LoongArchFrameLowering::emitPrologue(MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *LoongArchFI = MF.getInfo<LoongArchMachineFunctionInfo>();
  const LoongArchRegisterInfo *RI = STI.getRegisterInfo();
  const LoongArchInstrInfo *TII = STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  bool IsLA64 = STI.is64Bit();

  Register SPReg = LoongArch::R3;
  Register FPReg = LoongArch::R22;

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;
  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;
  // Determine the correct frame layout
  determineFrameLayout(MF);

  // First, compute final stack size.
  uint64_t StackSize = MFI.getStackSize();
  uint64_t RealStackSize = StackSize;

  // Early exit if there is no need to allocate space in the stack.
  if (StackSize == 0 && !MFI.adjustsStack())
    return;

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);
  // Split the SP adjustment to reduce the offsets of callee saved spill.
  if (FirstSPAdjustAmount)
    StackSize = FirstSPAdjustAmount;

  // Adjust stack.
  adjustReg(MBB, MBBI, DL, SPReg, SPReg, -StackSize, MachineInstr::FrameSetup);
  // Emit ".cfi_def_cfa_offset StackSize".
  unsigned CFIIndex =
      MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize));
  BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(MachineInstr::FrameSetup);

  const auto &CSI = MFI.getCalleeSavedInfo();

  // The frame pointer is callee-saved, and code has been generated for us to
  // save it to the stack. We need to skip over the storing of callee-saved
  // registers as the frame pointer must be modified after it has been saved
  // to the stack, not before.
  std::advance(MBBI, CSI.size());

  // Iterate over list of callee-saved registers and emit .cfi_offset
  // directives.
  for (const auto &Entry : CSI) {
    int64_t Offset = MFI.getObjectOffset(Entry.getFrameIdx());
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, RI->getDwarfRegNum(Entry.getReg(), true), Offset));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Generate new FP.
  if (hasFP(MF)) {
    adjustReg(MBB, MBBI, DL, FPReg, SPReg,
              StackSize - LoongArchFI->getVarArgsSaveSize(),
              MachineInstr::FrameSetup);

    // Emit ".cfi_def_cfa $fp, LoongArchFI->getVarArgsSaveSize()"
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfa(nullptr, RI->getDwarfRegNum(FPReg, true),
                                    LoongArchFI->getVarArgsSaveSize()));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Emit the second SP adjustment after saving callee saved registers.
  if (FirstSPAdjustAmount) {
    uint64_t SecondSPAdjustAmount = RealStackSize - FirstSPAdjustAmount;
    assert(SecondSPAdjustAmount > 0 &&
           "SecondSPAdjustAmount should be greater than zero");
    adjustReg(MBB, MBBI, DL, SPReg, SPReg, -SecondSPAdjustAmount,
              MachineInstr::FrameSetup);

    if (!hasFP(MF)) {
      // If we are using a frame-pointer, and thus emitted ".cfi_def_cfa fp, 0",
      // don't emit an sp-based .cfi_def_cfa_offset
      // Emit ".cfi_def_cfa_offset RealStackSize"
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, RealStackSize));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  if (hasFP(MF)) {
    // Realign stack.
    if (RI->hasStackRealignment(MF)) {
      unsigned Align = Log2(MFI.getMaxAlign());
      assert(Align > 0 && "The stack realignment size is invalid!");
      BuildMI(MBB, MBBI, DL,
              TII->get(IsLA64 ? LoongArch::BSTRINS_D : LoongArch::BSTRINS_W),
              SPReg)
          .addReg(SPReg)
          .addReg(LoongArch::R0)
          .addImm(Align - 1)
          .addImm(0)
          .setMIFlag(MachineInstr::FrameSetup);
      // FP will be used to restore the frame in the epilogue, so we need
      // another base register BP to record SP after re-alignment. SP will
      // track the current stack after allocating variable sized objects.
      if (hasBP(MF)) {
        // move BP, $sp
        BuildMI(MBB, MBBI, DL, TII->get(LoongArch::OR),
                LoongArchABI::getBPReg())
            .addReg(SPReg)
            .addReg(LoongArch::R0)
            .setMIFlag(MachineInstr::FrameSetup);
      }
    }
  }
}

void LoongArchFrameLowering::emitEpilogue(MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {
  const LoongArchRegisterInfo *RI = STI.getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *LoongArchFI = MF.getInfo<LoongArchMachineFunctionInfo>();
  Register SPReg = LoongArch::R3;
  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  const auto &CSI = MFI.getCalleeSavedInfo();
  // Skip to before the restores of callee-saved registers.
  auto LastFrameDestroy = MBBI;
  if (!CSI.empty())
    LastFrameDestroy = std::prev(MBBI, CSI.size());

  // Get the number of bytes from FrameInfo.
  uint64_t StackSize = MFI.getStackSize();

  // Restore the stack pointer.
  if (RI->hasStackRealignment(MF) || MFI.hasVarSizedObjects()) {
    assert(hasFP(MF) && "frame pointer should not have been eliminated");
    adjustReg(MBB, LastFrameDestroy, DL, SPReg, LoongArch::R22,
              -StackSize + LoongArchFI->getVarArgsSaveSize(),
              MachineInstr::FrameDestroy);
  }

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);
  if (FirstSPAdjustAmount) {
    uint64_t SecondSPAdjustAmount = StackSize - FirstSPAdjustAmount;
    assert(SecondSPAdjustAmount > 0 &&
           "SecondSPAdjustAmount should be greater than zero");

    adjustReg(MBB, LastFrameDestroy, DL, SPReg, SPReg, SecondSPAdjustAmount,
              MachineInstr::FrameDestroy);
    StackSize = FirstSPAdjustAmount;
  }

  // Deallocate stack
  adjustReg(MBB, MBBI, DL, SPReg, SPReg, StackSize, MachineInstr::FrameDestroy);
}

// We would like to split the SP adjustment to reduce prologue/epilogue
// as following instructions. In this way, the offset of the callee saved
// register could fit in a single store.
// e.g.
//   addi.d  $sp, $sp, -2032
//   st.d    $ra, $sp,  2024
//   st.d    $fp, $sp,  2016
//   addi.d  $sp, $sp,   -16
uint64_t LoongArchFrameLowering::getFirstSPAdjustAmount(
    const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Return the FirstSPAdjustAmount if the StackSize can not fit in a signed
  // 12-bit and there exists a callee-saved register needing to be pushed.
  if (!isInt<12>(MFI.getStackSize()) && (CSI.size() > 0)) {
    // FirstSPAdjustAmount is chosen as (2048 - StackAlign) because 2048 will
    // cause sp = sp + 2048 in the epilogue to be split into multiple
    // instructions. Offsets smaller than 2048 can fit in a single load/store
    // instruction, and we have to stick with the stack alignment.
    // So (2048 - StackAlign) will satisfy the stack alignment.
    return 2048 - getStackAlign().value();
  }
  return 0;
}

void LoongArchFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                  BitVector &SavedRegs,
                                                  RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // Unconditionally spill RA and FP only if the function uses a frame
  // pointer.
  if (hasFP(MF)) {
    SavedRegs.set(LoongArch::R1);
    SavedRegs.set(LoongArch::R22);
  }
  // Mark BP as used if function has dedicated base pointer.
  if (hasBP(MF))
    SavedRegs.set(LoongArchABI::getBPReg());
}

// Do not preserve stack space within prologue for outgoing variables if the
// function contains variable size objects.
// Let eliminateCallFramePseudoInstr preserve stack space for it.
bool LoongArchFrameLowering::hasReservedCallFrame(
    const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

// Eliminate ADJCALLSTACKDOWN, ADJCALLSTACKUP pseudo instructions.
MachineBasicBlock::iterator
LoongArchFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  Register SPReg = LoongArch::R3;
  DebugLoc DL = MI->getDebugLoc();

  if (!hasReservedCallFrame(MF)) {
    // If space has not been reserved for a call frame, ADJCALLSTACKDOWN and
    // ADJCALLSTACKUP must be converted to instructions manipulating the stack
    // pointer. This is necessary when there is a variable length stack
    // allocation (e.g. alloca), which means it's not possible to allocate
    // space for outgoing arguments from within the function prologue.
    int64_t Amount = MI->getOperand(0).getImm();

    if (Amount != 0) {
      // Ensure the stack remains aligned after adjustment.
      Amount = alignSPAdjust(Amount);

      if (MI->getOpcode() == LoongArch::ADJCALLSTACKDOWN)
        Amount = -Amount;

      adjustReg(MBB, MI, DL, SPReg, SPReg, Amount, MachineInstr::NoFlags);
    }
  }

  return MBB.erase(MI);
}

bool LoongArchFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();

  // Insert the spill to the stack frame.
  for (auto &CS : CSI) {
    Register Reg = CS.getReg();
    // If the register is RA and the return address is taken by method
    // LoongArchTargetLowering::lowerRETURNADDR, don't set kill flag.
    bool IsKill =
        !(Reg == LoongArch::R1 && MF->getFrameInfo().isReturnAddressTaken());
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.storeRegToStackSlot(MBB, MI, Reg, IsKill, CS.getFrameIdx(), RC, TRI,
                            Register());
  }

  return true;
}

StackOffset LoongArchFrameLowering::getFrameIndexReference(
    const MachineFunction &MF, int FI, Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  auto *LoongArchFI = MF.getInfo<LoongArchMachineFunctionInfo>();
  uint64_t StackSize = MFI.getStackSize();
  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);

  // Callee-saved registers should be referenced relative to the stack
  // pointer (positive offset), otherwise use the frame pointer (negative
  // offset).
  const auto &CSI = MFI.getCalleeSavedInfo();
  int MinCSFI = 0;
  int MaxCSFI = -1;
  StackOffset Offset =
      StackOffset::getFixed(MFI.getObjectOffset(FI) - getOffsetOfLocalArea() +
                            MFI.getOffsetAdjustment());

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }

  if (FI >= MinCSFI && FI <= MaxCSFI) {
    FrameReg = LoongArch::R3;
    if (FirstSPAdjustAmount)
      Offset += StackOffset::getFixed(FirstSPAdjustAmount);
    else
      Offset += StackOffset::getFixed(StackSize);
  } else if (RI->hasStackRealignment(MF) && !MFI.isFixedObjectIndex(FI)) {
    // If the stack was realigned, the frame pointer is set in order to allow
    // SP to be restored, so we need another base register to record the stack
    // after realignment.
    FrameReg = hasBP(MF) ? LoongArchABI::getBPReg() : LoongArch::R3;
    Offset += StackOffset::getFixed(StackSize);
  } else {
    FrameReg = RI->getFrameRegister(MF);
    if (hasFP(MF))
      Offset += StackOffset::getFixed(LoongArchFI->getVarArgsSaveSize());
    else
      Offset += StackOffset::getFixed(StackSize);
  }

  return Offset;
}

bool LoongArchFrameLowering::enableShrinkWrapping(
    const MachineFunction &MF) const {
  // Keep the conventional code flow when not optimizing.
  if (MF.getFunction().hasOptNone())
    return false;

  return true;
}
