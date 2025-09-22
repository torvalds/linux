//===- ARCFrameLowering.cpp - ARC Frame Information -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARC implementation of the TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "ARCFrameLowering.h"
#include "ARCMachineFunctionInfo.h"
#include "ARCSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "arc-frame-lowering"

using namespace llvm;

static cl::opt<bool>
    UseSaveRestoreFunclet("arc-save-restore-funclet", cl::Hidden,
                          cl::desc("Use arc callee save/restore functions"),
                          cl::init(true));

static const char *store_funclet_name[] = {
    "__st_r13_to_r15", "__st_r13_to_r16", "__st_r13_to_r17", "__st_r13_to_r18",
    "__st_r13_to_r19", "__st_r13_to_r20", "__st_r13_to_r21", "__st_r13_to_r22",
    "__st_r13_to_r23", "__st_r13_to_r24", "__st_r13_to_r25",
};

static const char *load_funclet_name[] = {
    "__ld_r13_to_r15", "__ld_r13_to_r16", "__ld_r13_to_r17", "__ld_r13_to_r18",
    "__ld_r13_to_r19", "__ld_r13_to_r20", "__ld_r13_to_r21", "__ld_r13_to_r22",
    "__ld_r13_to_r23", "__ld_r13_to_r24", "__ld_r13_to_r25",
};

static void generateStackAdjustment(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI,
                                    const ARCInstrInfo &TII, DebugLoc dl,
                                    int Amount, int StackPtr) {
  unsigned AdjOp;
  if (!Amount)
    return;
  bool Positive;
  unsigned AbsAmount;
  if (Amount < 0) {
    AbsAmount = -Amount;
    Positive = false;
  } else {
    AbsAmount = Amount;
    Positive = true;
  }

  LLVM_DEBUG(dbgs() << "Internal: adjust stack by: " << Amount << ","
                    << AbsAmount << "\n");

  assert((AbsAmount % 4 == 0) && "Stack adjustments must be 4-byte aligned.");
  if (isUInt<6>(AbsAmount))
    AdjOp = Positive ? ARC::ADD_rru6 : ARC::SUB_rru6;
  else if (isInt<12>(AbsAmount))
    AdjOp = Positive ? ARC::ADD_rrs12 : ARC::SUB_rrs12;
  else
    AdjOp = Positive ? ARC::ADD_rrlimm : ARC::SUB_rrlimm;

  BuildMI(MBB, MBBI, dl, TII.get(AdjOp), StackPtr)
      .addReg(StackPtr)
      .addImm(AbsAmount);
}

static unsigned determineLastCalleeSave(ArrayRef<CalleeSavedInfo> CSI) {
  unsigned Last = 0;
  for (auto Reg : CSI) {
    assert(Reg.getReg() >= ARC::R13 && Reg.getReg() <= ARC::R25 &&
           "Unexpected callee saved reg.");
    if (Reg.getReg() > Last)
      Last = Reg.getReg();
  }
  return Last;
}

void ARCFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  LLVM_DEBUG(dbgs() << "Determine Callee Saves: " << MF.getName() << "\n");
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  SavedRegs.set(ARC::BLINK);
}

void ARCFrameLowering::adjustStackToMatchRecords(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    bool Allocate) const {
  MachineFunction &MF = *MBB.getParent();
  int ScalarAlloc = MF.getFrameInfo().getStackSize();

  if (Allocate) {
    // Allocate by adjusting by the negative of what the record holder tracked
    // it tracked a positive offset in a downward growing stack.
    ScalarAlloc = -ScalarAlloc;
  }

  generateStackAdjustment(MBB, MBBI, *ST.getInstrInfo(), DebugLoc(),
                          ScalarAlloc, ARC::SP);
}

/// Insert prolog code into the function.
/// For ARC, this inserts a call to a function that puts required callee saved
/// registers onto the stack, when enough callee saved registers are required.
void ARCFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  LLVM_DEBUG(dbgs() << "Emit Prologue: " << MF.getName() << "\n");
  auto *AFI = MF.getInfo<ARCFunctionInfo>();
  MCContext &Context = MF.getContext();
  const MCRegisterInfo *MRI = Context.getRegisterInfo();
  const ARCInstrInfo *TII = MF.getSubtarget<ARCSubtarget>().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc dl;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  unsigned Last = determineLastCalleeSave(CSI);
  unsigned StackSlotsUsedByFunclet = 0;
  bool SavedBlink = false;
  unsigned AlreadyAdjusted = 0;
  if (MF.getFunction().isVarArg()) {
    // Add in the varargs area here first.
    LLVM_DEBUG(dbgs() << "Varargs\n");
    unsigned VarArgsBytes = MFI.getObjectSize(AFI->getVarArgsFrameIndex());
    unsigned Opc = ARC::SUB_rrlimm;
    if (isUInt<6>(VarArgsBytes))
      Opc = ARC::SUB_rru6;
    else if (isInt<12>(VarArgsBytes))
      Opc = ARC::SUB_rrs12;
    BuildMI(MBB, MBBI, dl, TII->get(Opc), ARC::SP)
        .addReg(ARC::SP)
        .addImm(VarArgsBytes);
  }
  if (hasFP(MF)) {
    LLVM_DEBUG(dbgs() << "Saving FP\n");
    BuildMI(MBB, MBBI, dl, TII->get(ARC::ST_AW_rs9))
        .addReg(ARC::SP, RegState::Define)
        .addReg(ARC::FP)
        .addReg(ARC::SP)
        .addImm(-4);
    AlreadyAdjusted += 4;
  }
  if (UseSaveRestoreFunclet && Last > ARC::R14) {
    LLVM_DEBUG(dbgs() << "Creating store funclet.\n");
    // BL to __save_r13_to_<TRI->getRegAsmName()>
    StackSlotsUsedByFunclet = Last - ARC::R12;
    BuildMI(MBB, MBBI, dl, TII->get(ARC::PUSH_S_BLINK));
    BuildMI(MBB, MBBI, dl, TII->get(ARC::SUB_rru6))
        .addReg(ARC::SP)
        .addReg(ARC::SP)
        .addImm(4 * StackSlotsUsedByFunclet);
    BuildMI(MBB, MBBI, dl, TII->get(ARC::BL))
        .addExternalSymbol(store_funclet_name[Last - ARC::R15])
        .addReg(ARC::BLINK, RegState::Implicit | RegState::Kill);
    AlreadyAdjusted += 4 * (StackSlotsUsedByFunclet + 1);
    SavedBlink = true;
  }
  // If we haven't saved BLINK, but we need to...do that now.
  if (MFI.hasCalls() && !SavedBlink) {
    LLVM_DEBUG(dbgs() << "Creating save blink.\n");
    BuildMI(MBB, MBBI, dl, TII->get(ARC::PUSH_S_BLINK));
    AlreadyAdjusted += 4;
  }
  if (AFI->MaxCallStackReq > 0)
    MFI.setStackSize(MFI.getStackSize() + AFI->MaxCallStackReq);
  // We have already saved some of the stack...
  LLVM_DEBUG(dbgs() << "Adjusting stack by: "
                    << (MFI.getStackSize() - AlreadyAdjusted) << "\n");
  generateStackAdjustment(MBB, MBBI, *ST.getInstrInfo(), dl,
                          -(MFI.getStackSize() - AlreadyAdjusted), ARC::SP);

  if (hasFP(MF)) {
    LLVM_DEBUG(dbgs() << "Setting FP from SP.\n");
    BuildMI(MBB, MBBI, dl,
            TII->get(isUInt<6>(MFI.getStackSize()) ? ARC::ADD_rru6
                                                   : ARC::ADD_rrlimm),
            ARC::FP)
        .addReg(ARC::SP)
        .addImm(MFI.getStackSize());
  }

  // Emit CFI records:
  // .cfi_def_cfa_offset StackSize
  // .cfi_offset fp, -StackSize
  // .cfi_offset blink, -StackSize+4
  unsigned CFIIndex = MF.addFrameInst(
      MCCFIInstruction::cfiDefCfaOffset(nullptr, MFI.getStackSize()));
  BuildMI(MBB, MBBI, dl, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlags(MachineInstr::FrameSetup);

  int CurOffset = -4;
  if (hasFP(MF)) {
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, MRI->getDwarfRegNum(ARC::FP, true), CurOffset));
    BuildMI(MBB, MBBI, dl, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
    CurOffset -= 4;
  }

  if (MFI.hasCalls()) {
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, MRI->getDwarfRegNum(ARC::BLINK, true), CurOffset));
    BuildMI(MBB, MBBI, dl, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  }
  // CFI for the rest of the registers.
  for (const auto &Entry : CSI) {
    unsigned Reg = Entry.getReg();
    int FI = Entry.getFrameIdx();
    // Skip BLINK and FP.
    if ((hasFP(MF) && Reg == ARC::FP) || (MFI.hasCalls() && Reg == ARC::BLINK))
      continue;
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, MRI->getDwarfRegNum(Reg, true), MFI.getObjectOffset(FI)));
    BuildMI(MBB, MBBI, dl, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  }
}

/// Insert epilog code into the function.
/// For ARC, this inserts a call to a function that restores callee saved
/// registers onto the stack, when enough callee saved registers are required.
void ARCFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  LLVM_DEBUG(dbgs() << "Emit Epilogue: " << MF.getName() << "\n");
  auto *AFI = MF.getInfo<ARCFunctionInfo>();
  const ARCInstrInfo *TII = MF.getSubtarget<ARCSubtarget>().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MF.getFrameInfo().getStackSize();
  bool SavedBlink = false;
  unsigned AmountAboveFunclet = 0;
  // If we have variable sized frame objects, then we have to move
  // the stack pointer to a known spot (fp - StackSize).
  // Then, replace the frame pointer by (new) [sp,StackSize-4].
  // Then, move the stack pointer the rest of the way (sp = sp + StackSize).
  if (hasFP(MF)) {
    unsigned Opc = ARC::SUB_rrlimm;
    if (isUInt<6>(StackSize))
      Opc = ARC::SUB_rru6;
    BuildMI(MBB, MBBI, DebugLoc(), TII->get(Opc), ARC::SP)
        .addReg(ARC::FP)
        .addImm(StackSize);
    AmountAboveFunclet += 4;
  }

  // Now, move the stack pointer to the bottom of the save area for the funclet.
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  unsigned Last = determineLastCalleeSave(CSI);
  unsigned StackSlotsUsedByFunclet = 0;
  // Now, restore the callee save registers.
  if (UseSaveRestoreFunclet && Last > ARC::R14) {
    // BL to __ld_r13_to_<TRI->getRegAsmName()>
    StackSlotsUsedByFunclet = Last - ARC::R12;
    AmountAboveFunclet += 4 * (StackSlotsUsedByFunclet + 1);
    SavedBlink = true;
  }

  if (MFI.hasCalls() && !SavedBlink) {
    AmountAboveFunclet += 4;
    SavedBlink = true;
  }

  // Move the stack pointer up to the point of the funclet.
  if (unsigned MoveAmount = StackSize - AmountAboveFunclet) {
    unsigned Opc = ARC::ADD_rrlimm;
    if (isUInt<6>(MoveAmount))
      Opc = ARC::ADD_rru6;
    else if (isInt<12>(MoveAmount))
      Opc = ARC::ADD_rrs12;
    BuildMI(MBB, MBBI, MBB.findDebugLoc(MBBI), TII->get(Opc), ARC::SP)
        .addReg(ARC::SP)
        .addImm(StackSize - AmountAboveFunclet);
  }

  if (StackSlotsUsedByFunclet) {
    // This part of the adjustment will always be < 64 bytes.
    BuildMI(MBB, MBBI, MBB.findDebugLoc(MBBI), TII->get(ARC::BL))
        .addExternalSymbol(load_funclet_name[Last - ARC::R15])
        .addReg(ARC::BLINK, RegState::Implicit | RegState::Kill);
    unsigned Opc = ARC::ADD_rrlimm;
    if (isUInt<6>(4 * StackSlotsUsedByFunclet))
      Opc = ARC::ADD_rru6;
    else if (isInt<12>(4 * StackSlotsUsedByFunclet))
      Opc = ARC::ADD_rrs12;
    BuildMI(MBB, MBBI, MBB.findDebugLoc(MBBI), TII->get(Opc), ARC::SP)
        .addReg(ARC::SP)
        .addImm(4 * (StackSlotsUsedByFunclet));
  }
  // Now, pop blink if necessary.
  if (SavedBlink) {
    BuildMI(MBB, MBBI, MBB.findDebugLoc(MBBI), TII->get(ARC::POP_S_BLINK));
  }
  // Now, pop fp if necessary.
  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, MBB.findDebugLoc(MBBI), TII->get(ARC::LD_AB_rs9))
        .addReg(ARC::FP, RegState::Define)
        .addReg(ARC::SP, RegState::Define)
        .addReg(ARC::SP)
        .addImm(4);
  }

  // Relieve the varargs area if necessary.
  if (MF.getFunction().isVarArg()) {
    // Add in the varargs area here first.
    LLVM_DEBUG(dbgs() << "Varargs\n");
    unsigned VarArgsBytes = MFI.getObjectSize(AFI->getVarArgsFrameIndex());
    unsigned Opc = ARC::ADD_rrlimm;
    if (isUInt<6>(VarArgsBytes))
      Opc = ARC::ADD_rru6;
    else if (isInt<12>(VarArgsBytes))
      Opc = ARC::ADD_rrs12;
    BuildMI(MBB, MBBI, MBB.findDebugLoc(MBBI), TII->get(Opc))
        .addReg(ARC::SP)
        .addReg(ARC::SP)
        .addImm(VarArgsBytes);
  }
}

static std::vector<CalleeSavedInfo>::iterator
getSavedReg(std::vector<CalleeSavedInfo> &V, unsigned reg) {
  for (auto I = V.begin(), E = V.end(); I != E; ++I) {
    if (reg == I->getReg())
      return I;
  }
  return V.end();
}

bool ARCFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  // Use this opportunity to assign the spill slots for all of the potential
  // callee save registers (blink, fp, r13->r25) that we care about the
  // placement for.  We can calculate all of that data here.
  int CurOffset = -4;
  unsigned Last = determineLastCalleeSave(CSI);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (hasFP(MF)) {
    // Create a fixed slot at for FP
    int StackObj = MFI.CreateFixedSpillStackObject(4, CurOffset, true);
    LLVM_DEBUG(dbgs() << "Creating fixed object (" << StackObj << ") for FP at "
                      << CurOffset << "\n");
    (void)StackObj;
    CurOffset -= 4;
  }
  if (MFI.hasCalls() || (UseSaveRestoreFunclet && Last > ARC::R14)) {
    // Create a fixed slot for BLINK.
    int StackObj  = MFI.CreateFixedSpillStackObject(4, CurOffset, true);
    LLVM_DEBUG(dbgs() << "Creating fixed object (" << StackObj
                      << ") for BLINK at " << CurOffset << "\n");
    (void)StackObj;
    CurOffset -= 4;
  }

  // Create slots for last down to r13.
  for (unsigned Which = Last; Which > ARC::R12; Which--) {
    auto RegI = getSavedReg(CSI, Which);
    if (RegI == CSI.end() || RegI->getFrameIdx() == 0) {
      // Always create the stack slot.  If for some reason the register isn't in
      // the save list, then don't worry about it.
      int FI = MFI.CreateFixedSpillStackObject(4, CurOffset, true);
      if (RegI != CSI.end())
        RegI->setFrameIdx(FI);
    } else
      MFI.setObjectOffset(RegI->getFrameIdx(), CurOffset);
    CurOffset -= 4;
  }
  for (auto &I : CSI) {
    if (I.getReg() > ARC::R12)
      continue;
    if (I.getFrameIdx() == 0) {
      I.setFrameIdx(MFI.CreateFixedSpillStackObject(4, CurOffset, true));
      LLVM_DEBUG(dbgs() << "Creating fixed object (" << I.getFrameIdx()
                        << ") for other register at " << CurOffset << "\n");
    } else {
      MFI.setObjectOffset(I.getFrameIdx(), CurOffset);
      LLVM_DEBUG(dbgs() << "Updating fixed object (" << I.getFrameIdx()
                        << ") for other register at " << CurOffset << "\n");
    }
    CurOffset -= 4;
  }
  return true;
}

bool ARCFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  LLVM_DEBUG(dbgs() << "Spill callee saved registers: "
                    << MBB.getParent()->getName() << "\n");
  // There are routines for saving at least 3 registers (r13 to r15, etc.)
  unsigned Last = determineLastCalleeSave(CSI);
  if (UseSaveRestoreFunclet && Last > ARC::R14) {
    // Use setObjectOffset for these registers.
    // Needs to be in or before processFunctionBeforeFrameFinalized.
    // Or, do assignCalleeSaveSpillSlots?
    // Will be handled in prolog.
    return true;
  }
  return false;
}

bool ARCFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  LLVM_DEBUG(dbgs() << "Restore callee saved registers: "
                    << MBB.getParent()->getName() << "\n");
  // There are routines for saving at least 3 registers (r13 to r15, etc.)
  unsigned Last = determineLastCalleeSave(CSI);
  if (UseSaveRestoreFunclet && Last > ARC::R14) {
    // Will be handled in epilog.
    return true;
  }
  return false;
}

// Adjust local variables that are 4-bytes or larger to 4-byte boundary
void ARCFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  LLVM_DEBUG(dbgs() << "Process function before frame finalized: "
                    << MF.getName() << "\n");
  MachineFrameInfo &MFI = MF.getFrameInfo();
  LLVM_DEBUG(dbgs() << "Current stack size: " << MFI.getStackSize() << "\n");
  const TargetRegisterClass *RC = &ARC::GPR32RegClass;
  if (MFI.hasStackObjects()) {
    int RegScavFI = MFI.CreateStackObject(RegInfo->getSpillSize(*RC),
                                          RegInfo->getSpillAlign(*RC), false);
    RS->addScavengingFrameIndex(RegScavFI);
    LLVM_DEBUG(dbgs() << "Created scavenging index RegScavFI=" << RegScavFI
                      << "\n");
  }
}

static void emitRegUpdate(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MBBI, DebugLoc dl,
                          unsigned Reg, int NumBytes, bool IsAdd,
                          const ARCInstrInfo *TII) {
  unsigned Opc;
  if (isUInt<6>(NumBytes))
    Opc = IsAdd ? ARC::ADD_rru6 : ARC::SUB_rru6;
  else if (isInt<12>(NumBytes))
    Opc = IsAdd ? ARC::ADD_rrs12 : ARC::SUB_rrs12;
  else
    Opc = IsAdd ? ARC::ADD_rrlimm : ARC::SUB_rrlimm;

  BuildMI(MBB, MBBI, dl, TII->get(Opc), Reg)
      .addReg(Reg, RegState::Kill)
      .addImm(NumBytes);
}

MachineBasicBlock::iterator ARCFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  LLVM_DEBUG(dbgs() << "EmitCallFramePseudo: " << MF.getName() << "\n");
  const ARCInstrInfo *TII = MF.getSubtarget<ARCSubtarget>().getInstrInfo();
  MachineInstr &Old = *I;
  DebugLoc dl = Old.getDebugLoc();
  unsigned Amt = Old.getOperand(0).getImm();
  auto *AFI = MF.getInfo<ARCFunctionInfo>();
  if (!hasFP(MF)) {
    if (Amt > AFI->MaxCallStackReq && Old.getOpcode() == ARC::ADJCALLSTACKDOWN)
      AFI->MaxCallStackReq = Amt;
  } else {
    if (Amt != 0) {
      assert((Old.getOpcode() == ARC::ADJCALLSTACKDOWN ||
              Old.getOpcode() == ARC::ADJCALLSTACKUP) &&
             "Unknown Frame Pseudo.");
      bool IsAdd = (Old.getOpcode() == ARC::ADJCALLSTACKUP);
      emitRegUpdate(MBB, I, dl, ARC::SP, Amt, IsAdd, TII);
    }
  }
  return MBB.erase(I);
}

bool ARCFrameLowering::hasFP(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  bool HasFP = MF.getTarget().Options.DisableFramePointerElim(MF) ||
               MF.getFrameInfo().hasVarSizedObjects() ||
               MF.getFrameInfo().isFrameAddressTaken() ||
               RegInfo->hasStackRealignment(MF);
  return HasFP;
}
