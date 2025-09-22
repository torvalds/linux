//===-- CSKYFrameLowering.cpp - CSKY Frame Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the CSKY implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "CSKYFrameLowering.h"
#include "CSKYMachineFunctionInfo.h"
#include "CSKYSubtarget.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/MC/MCDwarf.h"

using namespace llvm;

#define DEBUG_TYPE "csky-frame-lowering"

// Returns the register used to hold the frame pointer.
static Register getFPReg(const CSKYSubtarget &STI) { return CSKY::R8; }

// To avoid the BP value clobbered by a function call, we need to choose a
// callee saved register to save the value.
static Register getBPReg(const CSKYSubtarget &STI) { return CSKY::R7; }

bool CSKYFrameLowering::hasFP(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         RegInfo->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
         MFI.isFrameAddressTaken();
}

bool CSKYFrameLowering::hasBP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  return MFI.hasVarSizedObjects();
}

// Determines the size of the frame and maximum call frame size.
void CSKYFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const CSKYRegisterInfo *RI = STI.getRegisterInfo();

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t FrameSize = MFI.getStackSize();

  // Get the alignment.
  Align StackAlign = getStackAlign();
  if (RI->hasStackRealignment(MF)) {
    Align MaxStackAlign = std::max(StackAlign, MFI.getMaxAlign());
    FrameSize += (MaxStackAlign.value() - StackAlign.value());
    StackAlign = MaxStackAlign;
  }

  // Set Max Call Frame Size
  uint64_t MaxCallSize = alignTo(MFI.getMaxCallFrameSize(), StackAlign);
  MFI.setMaxCallFrameSize(MaxCallSize);

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, StackAlign);

  // Update frame info.
  MFI.setStackSize(FrameSize);
}

void CSKYFrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const CSKYRegisterInfo *RI = STI.getRegisterInfo();
  const CSKYInstrInfo *TII = STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  Register FPReg = getFPReg(STI);
  Register SPReg = CSKY::R14;
  Register BPReg = getBPReg(STI);

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  if (MF.getFunction().hasFnAttribute("interrupt"))
    BuildMI(MBB, MBBI, DL, TII->get(CSKY::NIE));

  // Determine the correct frame layout
  determineFrameLayout(MF);

  // FIXME (note copied from Lanai): This appears to be overallocating.  Needs
  // investigation. Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();

  // Early exit if there is no need to allocate on the stack
  if (StackSize == 0 && !MFI.adjustsStack())
    return;

  const auto &CSI = MFI.getCalleeSavedInfo();

  unsigned spillAreaSize = CFI->getCalleeSaveAreaSize();

  uint64_t ActualSize = spillAreaSize + CFI->getVarArgsSaveSize();

  // First part stack allocation.
  adjustReg(MBB, MBBI, DL, SPReg, SPReg, -(static_cast<int64_t>(ActualSize)),
            MachineInstr::NoFlags);

  // Emit ".cfi_def_cfa_offset FirstSPAdjustAmount"
  unsigned CFIIndex =
      MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, ActualSize));
  BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);

  // The frame pointer is callee-saved, and code has been generated for us to
  // save it to the stack. We need to skip over the storing of callee-saved
  // registers as the frame pointer must be modified after it has been saved
  // to the stack, not before.
  // FIXME: assumes exactly one instruction is used to save each callee-saved
  // register.
  std::advance(MBBI, CSI.size());

  // Iterate over list of callee-saved registers and emit .cfi_offset
  // directives.
  for (const auto &Entry : CSI) {
    int64_t Offset = MFI.getObjectOffset(Entry.getFrameIdx());
    Register Reg = Entry.getReg();

    unsigned Num = TRI->getRegSizeInBits(Reg, MRI) / 32;
    for (unsigned i = 0; i < Num; i++) {
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
          nullptr, RI->getDwarfRegNum(Reg, true) + i, Offset + i * 4));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  // Generate new FP.
  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::COPY), FPReg)
        .addReg(SPReg)
        .setMIFlag(MachineInstr::FrameSetup);

    // Emit ".cfi_def_cfa_register $fp"
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
        nullptr, RI->getDwarfRegNum(FPReg, true)));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    // Second part stack allocation.
    adjustReg(MBB, MBBI, DL, SPReg, SPReg,
              -(static_cast<int64_t>(StackSize - ActualSize)),
              MachineInstr::NoFlags);

    // Realign Stack
    const CSKYRegisterInfo *RI = STI.getRegisterInfo();
    if (RI->hasStackRealignment(MF)) {
      Align MaxAlignment = MFI.getMaxAlign();

      const CSKYInstrInfo *TII = STI.getInstrInfo();
      if (STI.hasE2() && isUInt<12>(~(-(int)MaxAlignment.value()))) {
        BuildMI(MBB, MBBI, DL, TII->get(CSKY::ANDNI32), SPReg)
            .addReg(SPReg)
            .addImm(~(-(int)MaxAlignment.value()));
      } else {
        unsigned ShiftAmount = Log2(MaxAlignment);

        if (STI.hasE2()) {
          Register VR =
              MF.getRegInfo().createVirtualRegister(&CSKY::GPRRegClass);
          BuildMI(MBB, MBBI, DL, TII->get(CSKY::LSRI32), VR)
              .addReg(SPReg)
              .addImm(ShiftAmount);
          BuildMI(MBB, MBBI, DL, TII->get(CSKY::LSLI32), SPReg)
              .addReg(VR)
              .addImm(ShiftAmount);
        } else {
          Register VR =
              MF.getRegInfo().createVirtualRegister(&CSKY::mGPRRegClass);
          BuildMI(MBB, MBBI, DL, TII->get(CSKY::MOV16), VR).addReg(SPReg);
          BuildMI(MBB, MBBI, DL, TII->get(CSKY::LSRI16), VR)
              .addReg(VR)
              .addImm(ShiftAmount);
          BuildMI(MBB, MBBI, DL, TII->get(CSKY::LSLI16), VR)
              .addReg(VR)
              .addImm(ShiftAmount);
          BuildMI(MBB, MBBI, DL, TII->get(CSKY::MOV16), SPReg).addReg(VR);
        }
      }
    }

    // FP will be used to restore the frame in the epilogue, so we need
    // another base register BP to record SP after re-alignment. SP will
    // track the current stack after allocating variable sized objects.
    if (hasBP(MF)) {
      // move BP, SP
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::COPY), BPReg).addReg(SPReg);
    }

  } else {
    adjustReg(MBB, MBBI, DL, SPReg, SPReg,
              -(static_cast<int64_t>(StackSize - ActualSize)),
              MachineInstr::NoFlags);
    // Emit ".cfi_def_cfa_offset StackSize"
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, MFI.getStackSize()));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }
}

void CSKYFrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();

  MachineFrameInfo &MFI = MF.getFrameInfo();
  Register FPReg = getFPReg(STI);
  Register SPReg = CSKY::R14;

  // Get the insert location for the epilogue. If there were no terminators in
  // the block, get the last instruction.
  MachineBasicBlock::iterator MBBI = MBB.end();
  DebugLoc DL;
  if (!MBB.empty()) {
    MBBI = MBB.getFirstTerminator();
    if (MBBI == MBB.end())
      MBBI = MBB.getLastNonDebugInstr();
    DL = MBBI->getDebugLoc();

    // If this is not a terminator, the actual insert location should be after
    // the last instruction.
    if (!MBBI->isTerminator())
      MBBI = std::next(MBBI);
  }

  const auto &CSI = MFI.getCalleeSavedInfo();
  uint64_t StackSize = MFI.getStackSize();

  uint64_t ActualSize =
      CFI->getCalleeSaveAreaSize() + CFI->getVarArgsSaveSize();

  // Skip to before the restores of callee-saved registers
  // FIXME: assumes exactly one instruction is used to restore each
  // callee-saved register.
  auto LastFrameDestroy = MBBI;
  if (!CSI.empty())
    LastFrameDestroy = std::prev(MBBI, CSI.size());

  if (hasFP(MF)) {
    const CSKYInstrInfo *TII = STI.getInstrInfo();
    BuildMI(MBB, LastFrameDestroy, DL, TII->get(TargetOpcode::COPY), SPReg)
        .addReg(FPReg)
        .setMIFlag(MachineInstr::NoFlags);
  } else {
    adjustReg(MBB, LastFrameDestroy, DL, SPReg, SPReg, (StackSize - ActualSize),
              MachineInstr::FrameDestroy);
  }

  adjustReg(MBB, MBBI, DL, SPReg, SPReg, ActualSize,
            MachineInstr::FrameDestroy);
}

static unsigned EstimateFunctionSizeInBytes(const MachineFunction &MF,
                                            const CSKYInstrInfo &TII) {
  unsigned FnSize = 0;
  for (auto &MBB : MF) {
    for (auto &MI : MBB)
      FnSize += TII.getInstSizeInBytes(MI);
  }
  FnSize += MF.getConstantPool()->getConstants().size() * 4;
  return FnSize;
}

static unsigned estimateRSStackSizeLimit(MachineFunction &MF,
                                         const CSKYSubtarget &STI) {
  unsigned Limit = (1 << 12) - 1;

  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        if (!MI.getOperand(i).isFI())
          continue;

        if (MI.getOpcode() == CSKY::SPILL_CARRY ||
            MI.getOpcode() == CSKY::RESTORE_CARRY ||
            MI.getOpcode() == CSKY::STORE_PAIR ||
            MI.getOpcode() == CSKY::LOAD_PAIR) {
          Limit = std::min(Limit, ((1U << 12) - 1) * 4);
          break;
        }

        if (MI.getOpcode() == CSKY::ADDI32) {
          Limit = std::min(Limit, (1U << 12));
          break;
        }

        if (MI.getOpcode() == CSKY::ADDI16XZ) {
          Limit = std::min(Limit, (1U << 3));
          break;
        }

        // ADDI16 will not require an extra register,
        // it can reuse the destination.
        if (MI.getOpcode() == CSKY::ADDI16)
          break;

        // Otherwise check the addressing mode.
        switch (MI.getDesc().TSFlags & CSKYII::AddrModeMask) {
        default:
          LLVM_DEBUG(MI.dump());
          llvm_unreachable(
              "Unhandled addressing mode in stack size limit calculation");
        case CSKYII::AddrMode32B:
          Limit = std::min(Limit, (1U << 12) - 1);
          break;
        case CSKYII::AddrMode32H:
          Limit = std::min(Limit, ((1U << 12) - 1) * 2);
          break;
        case CSKYII::AddrMode32WD:
          Limit = std::min(Limit, ((1U << 12) - 1) * 4);
          break;
        case CSKYII::AddrMode16B:
          Limit = std::min(Limit, (1U << 5) - 1);
          break;
        case CSKYII::AddrMode16H:
          Limit = std::min(Limit, ((1U << 5) - 1) * 2);
          break;
        case CSKYII::AddrMode16W:
          Limit = std::min(Limit, ((1U << 5) - 1) * 4);
          break;
        case CSKYII::AddrMode32SDF:
          Limit = std::min(Limit, ((1U << 8) - 1) * 4);
          break;
        }
        break; // At most one FI per instruction
      }
    }
  }

  return Limit;
}

void CSKYFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                             BitVector &SavedRegs,
                                             RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const CSKYInstrInfo *TII = STI.getInstrInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (hasFP(MF))
    SavedRegs.set(CSKY::R8);

  // Mark BP as used if function has dedicated base pointer.
  if (hasBP(MF))
    SavedRegs.set(CSKY::R7);

  // If interrupt is enabled and there are calls in the handler,
  // unconditionally save all Caller-saved registers and
  // all FP registers, regardless whether they are used.
  if (MF.getFunction().hasFnAttribute("interrupt") && MFI.hasCalls()) {

    static const MCPhysReg CSRegs[] = {CSKY::R0,  CSKY::R1,  CSKY::R2, CSKY::R3,
                                       CSKY::R12, CSKY::R13, 0};

    for (unsigned i = 0; CSRegs[i]; ++i)
      SavedRegs.set(CSRegs[i]);

    if (STI.hasHighRegisters()) {

      static const MCPhysReg CSHRegs[] = {CSKY::R18, CSKY::R19, CSKY::R20,
                                          CSKY::R21, CSKY::R22, CSKY::R23,
                                          CSKY::R24, CSKY::R25, 0};

      for (unsigned i = 0; CSHRegs[i]; ++i)
        SavedRegs.set(CSHRegs[i]);
    }

    static const MCPhysReg CSF32Regs[] = {
        CSKY::F8_32,  CSKY::F9_32,  CSKY::F10_32,
        CSKY::F11_32, CSKY::F12_32, CSKY::F13_32,
        CSKY::F14_32, CSKY::F15_32, 0};
    static const MCPhysReg CSF64Regs[] = {
        CSKY::F8_64,  CSKY::F9_64,  CSKY::F10_64,
        CSKY::F11_64, CSKY::F12_64, CSKY::F13_64,
        CSKY::F14_64, CSKY::F15_64, 0};

    const MCPhysReg *FRegs = NULL;
    if (STI.hasFPUv2DoubleFloat() || STI.hasFPUv3DoubleFloat())
      FRegs = CSF64Regs;
    else if (STI.hasFPUv2SingleFloat() || STI.hasFPUv3SingleFloat())
      FRegs = CSF32Regs;

    if (FRegs != NULL) {
      const MCPhysReg *Regs = MF.getRegInfo().getCalleeSavedRegs();

      for (unsigned i = 0; Regs[i]; ++i)
        if (CSKY::FPR32RegClass.contains(Regs[i]) ||
            CSKY::FPR64RegClass.contains(Regs[i])) {
          unsigned x = 0;
          for (; FRegs[x]; ++x)
            if (FRegs[x] == Regs[i])
              break;
          if (FRegs[x] == 0)
            SavedRegs.set(Regs[i]);
        }
    }
  }

  unsigned CSStackSize = 0;
  for (unsigned Reg : SavedRegs.set_bits()) {
    auto RegSize = TRI->getRegSizeInBits(Reg, MRI) / 8;
    CSStackSize += RegSize;
  }

  CFI->setCalleeSaveAreaSize(CSStackSize);

  uint64_t Limit = estimateRSStackSizeLimit(MF, STI);

  bool BigFrame = (MFI.estimateStackSize(MF) + CSStackSize >= Limit);

  if (BigFrame || CFI->isCRSpilled() || !STI.hasE2()) {
    const TargetRegisterClass *RC = &CSKY::GPRRegClass;
    unsigned size = TRI->getSpillSize(*RC);
    Align align = TRI->getSpillAlign(*RC);

    RS->addScavengingFrameIndex(MFI.CreateStackObject(size, align, false));
  }

  unsigned FnSize = EstimateFunctionSizeInBytes(MF, *TII);
  // Force R15 to be spilled if the function size is > 65534. This enables
  // use of BSR to implement far jump.
  if (FnSize >= ((1 << (16 - 1)) * 2))
    SavedRegs.set(CSKY::R15);

  CFI->setLRIsSpilled(SavedRegs.test(CSKY::R15));
}

// Not preserve stack space within prologue for outgoing variables when the
// function contains variable size objects and let eliminateCallFramePseudoInstr
// preserve stack space for it.
bool CSKYFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

bool CSKYFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  for (auto &CS : CSI) {
    // Insert the spill to the stack frame.
    Register Reg = CS.getReg();
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.storeRegToStackSlot(MBB, MI, Reg, true, CS.getFrameIdx(), RC, TRI,
                            Register());
  }

  return true;
}

bool CSKYFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  for (auto &CS : reverse(CSI)) {
    Register Reg = CS.getReg();
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.loadRegFromStackSlot(MBB, MI, Reg, CS.getFrameIdx(), RC, TRI,
                             Register());
    assert(MI != MBB.begin() && "loadRegFromStackSlot didn't insert any code!");
  }

  return true;
}

// Eliminate ADJCALLSTACKDOWN, ADJCALLSTACKUP pseudo instructions.
MachineBasicBlock::iterator CSKYFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  Register SPReg = CSKY::R14;
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

      if (MI->getOpcode() == CSKY::ADJCALLSTACKDOWN)
        Amount = -Amount;

      adjustReg(MBB, MI, DL, SPReg, SPReg, Amount, MachineInstr::NoFlags);
    }
  }

  return MBB.erase(MI);
}

void CSKYFrameLowering::adjustReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  const DebugLoc &DL, Register DestReg,
                                  Register SrcReg, int64_t Val,
                                  MachineInstr::MIFlag Flag) const {
  const CSKYInstrInfo *TII = STI.getInstrInfo();

  if (DestReg == SrcReg && Val == 0)
    return;

  // TODO: Add 16-bit instruction support with immediate num
  if (STI.hasE2() && isUInt<12>(std::abs(Val) - 1)) {
    BuildMI(MBB, MBBI, DL, TII->get(Val < 0 ? CSKY::SUBI32 : CSKY::ADDI32),
            DestReg)
        .addReg(SrcReg)
        .addImm(std::abs(Val))
        .setMIFlag(Flag);
  } else if (!STI.hasE2() && isShiftedUInt<7, 2>(std::abs(Val))) {
    BuildMI(MBB, MBBI, DL,
            TII->get(Val < 0 ? CSKY::SUBI16SPSP : CSKY::ADDI16SPSP), CSKY::R14)
        .addReg(CSKY::R14, RegState::Kill)
        .addImm(std::abs(Val))
        .setMIFlag(Flag);
  } else {

    unsigned Op = 0;

    if (STI.hasE2()) {
      Op = Val < 0 ? CSKY::SUBU32 : CSKY::ADDU32;
    } else {
      assert(SrcReg == DestReg);
      Op = Val < 0 ? CSKY::SUBU16XZ : CSKY::ADDU16XZ;
    }

    Register ScratchReg = TII->movImm(MBB, MBBI, DL, std::abs(Val), Flag);

    BuildMI(MBB, MBBI, DL, TII->get(Op), DestReg)
        .addReg(SrcReg)
        .addReg(ScratchReg, RegState::Kill)
        .setMIFlag(Flag);
  }
}

StackOffset
CSKYFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  const auto &CSI = MFI.getCalleeSavedInfo();

  int MinCSFI = 0;
  int MaxCSFI = -1;

  int Offset = MFI.getObjectOffset(FI) + MFI.getOffsetAdjustment();

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }

  if (FI >= MinCSFI && FI <= MaxCSFI) {
    FrameReg = CSKY::R14;
    Offset += CFI->getVarArgsSaveSize() + CFI->getCalleeSaveAreaSize();
  } else if (RI->hasStackRealignment(MF)) {
    assert(hasFP(MF));
    if (!MFI.isFixedObjectIndex(FI)) {
      FrameReg = hasBP(MF) ? getBPReg(STI) : CSKY::R14;
      Offset += MFI.getStackSize();
    } else {
      FrameReg = getFPReg(STI);
      Offset += CFI->getVarArgsSaveSize() + CFI->getCalleeSaveAreaSize();
    }
  } else {
    if (MFI.isFixedObjectIndex(FI) && hasFP(MF)) {
      FrameReg = getFPReg(STI);
      Offset += CFI->getVarArgsSaveSize() + CFI->getCalleeSaveAreaSize();
    } else {
      FrameReg = hasBP(MF) ? getBPReg(STI) : CSKY::R14;
      Offset += MFI.getStackSize();
    }
  }

  return StackOffset::getFixed(Offset);
}
