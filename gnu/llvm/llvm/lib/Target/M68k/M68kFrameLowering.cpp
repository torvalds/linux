//===-- M68kFrameLowering.cpp - M68k Frame Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the M68k implementation of TargetFrameLowering class.
///
//===----------------------------------------------------------------------===//

#include "M68kFrameLowering.h"

#include "M68kInstrBuilder.h"
#include "M68kInstrInfo.h"
#include "M68kMachineFunction.h"
#include "M68kSubtarget.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

M68kFrameLowering::M68kFrameLowering(const M68kSubtarget &STI, Align Alignment)
    : TargetFrameLowering(StackGrowsDown, Alignment, -4), STI(STI),
      TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()) {
  SlotSize = STI.getSlotSize();
  StackPtr = TRI->getStackRegister();
}

bool M68kFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken() ||
         TRI->hasStackRealignment(MF);
}

// FIXME Make sure no other factors prevent us from reserving call frame
bool M68kFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects() &&
         !MF.getInfo<M68kMachineFunctionInfo>()->getHasPushSequences();
}

bool M68kFrameLowering::canSimplifyCallFramePseudos(
    const MachineFunction &MF) const {
  return hasReservedCallFrame(MF) ||
         (hasFP(MF) && !TRI->hasStackRealignment(MF)) ||
         TRI->hasBasePointer(MF);
}

bool M68kFrameLowering::needsFrameIndexResolution(
    const MachineFunction &MF) const {
  return MF.getFrameInfo().hasStackObjects() ||
         MF.getInfo<M68kMachineFunctionInfo>()->getHasPushSequences();
}

// NOTE: this only has a subset of the full frame index logic. In
// particular, the FI < 0 and AfterFPPop logic is handled in
// M68kRegisterInfo::eliminateFrameIndex, but not here. Possibly
// (probably?) it should be moved into here.
StackOffset
M68kFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // We can't calculate offset from frame pointer if the stack is realigned,
  // so enforce usage of stack/base pointer.  The base pointer is used when we
  // have dynamic allocas in addition to dynamic realignment.
  if (TRI->hasBasePointer(MF))
    FrameReg = TRI->getBaseRegister();
  else if (TRI->hasStackRealignment(MF))
    FrameReg = TRI->getStackRegister();
  else
    FrameReg = TRI->getFrameRegister(MF);

  // Offset will hold the offset from the stack pointer at function entry to the
  // object.
  // We need to factor in additional offsets applied during the prologue to the
  // frame, base, and stack pointer depending on which is used.
  int Offset = MFI.getObjectOffset(FI) - getOffsetOfLocalArea();
  const M68kMachineFunctionInfo *MMFI = MF.getInfo<M68kMachineFunctionInfo>();
  uint64_t StackSize = MFI.getStackSize();
  bool HasFP = hasFP(MF);

  // TODO: Support tail calls
  if (TRI->hasBasePointer(MF)) {
    assert(HasFP && "VLAs and dynamic stack realign, but no FP?!");
    if (FI < 0) {
      // Skip the saved FP.
      return StackOffset::getFixed(Offset + SlotSize);
    }

    assert((-(Offset + StackSize)) % MFI.getObjectAlign(FI).value() == 0);
    return StackOffset::getFixed(Offset + StackSize);
  }
  if (TRI->hasStackRealignment(MF)) {
    if (FI < 0) {
      // Skip the saved FP.
      return StackOffset::getFixed(Offset + SlotSize);
    }

    assert((-(Offset + StackSize)) % MFI.getObjectAlign(FI).value() == 0);
    return StackOffset::getFixed(Offset + StackSize);
  }

  if (!HasFP)
    return StackOffset::getFixed(Offset + StackSize);

  // Skip the saved FP.
  Offset += SlotSize;

  // Skip the RETADDR move area
  int TailCallReturnAddrDelta = MMFI->getTCReturnAddrDelta();
  if (TailCallReturnAddrDelta < 0)
    Offset -= TailCallReturnAddrDelta;

  return StackOffset::getFixed(Offset);
}

/// Return a caller-saved register that isn't live
/// when it reaches the "return" instruction. We can then pop a stack object
/// to this register without worry about clobbering it.
static unsigned findDeadCallerSavedReg(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator &MBBI,
                                       const M68kRegisterInfo *TRI) {
  const MachineFunction *MF = MBB.getParent();
  if (MF->callsEHReturn())
    return 0;

  const TargetRegisterClass &AvailableRegs = *TRI->getRegsForTailCall(*MF);

  if (MBBI == MBB.end())
    return 0;

  switch (MBBI->getOpcode()) {
  default:
    return 0;
  case TargetOpcode::PATCHABLE_RET:
  case M68k::RET: {
    SmallSet<uint16_t, 8> Uses;

    for (unsigned i = 0, e = MBBI->getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MBBI->getOperand(i);
      if (!MO.isReg() || MO.isDef())
        continue;
      Register Reg = MO.getReg();
      if (!Reg)
        continue;
      for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
        Uses.insert(*AI);
    }

    for (auto CS : AvailableRegs)
      if (!Uses.count(CS))
        return CS;
  }
  }

  return 0;
}

static bool isRegLiveIn(MachineBasicBlock &MBB, unsigned Reg) {
  return llvm::any_of(MBB.liveins(),
                      [Reg](MachineBasicBlock::RegisterMaskPair RegMask) {
                        return RegMask.PhysReg == Reg;
                      });
}

uint64_t
M68kFrameLowering::calculateMaxStackAlign(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t MaxAlign = MFI.getMaxAlign().value(); // Desired stack alignment.
  unsigned StackAlign = getStackAlignment();     // ABI alignment
  if (MF.getFunction().hasFnAttribute("stackrealign")) {
    if (MFI.hasCalls())
      MaxAlign = (StackAlign > MaxAlign) ? StackAlign : MaxAlign;
    else if (MaxAlign < SlotSize)
      MaxAlign = SlotSize;
  }
  return MaxAlign;
}

void M68kFrameLowering::BuildStackAlignAND(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           const DebugLoc &DL, unsigned Reg,
                                           uint64_t MaxAlign) const {
  uint64_t Val = -MaxAlign;
  unsigned AndOp = M68k::AND32di;
  unsigned MovOp = M68k::MOV32rr;

  // This function is normally used with SP which is Address Register, but AND,
  // or any other logical instructions in M68k do not support ARs so we need
  // to use a temp Data Register to perform the op.
  unsigned Tmp = M68k::D0;

  BuildMI(MBB, MBBI, DL, TII.get(MovOp), Tmp)
      .addReg(Reg)
      .setMIFlag(MachineInstr::FrameSetup);

  MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(AndOp), Tmp)
                         .addReg(Tmp)
                         .addImm(Val)
                         .setMIFlag(MachineInstr::FrameSetup);

  // The CCR implicit def is dead.
  MI->getOperand(3).setIsDead();

  BuildMI(MBB, MBBI, DL, TII.get(MovOp), Reg)
      .addReg(Tmp)
      .setMIFlag(MachineInstr::FrameSetup);
}

MachineBasicBlock::iterator M68kFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  bool ReserveCallFrame = hasReservedCallFrame(MF);
  unsigned Opcode = I->getOpcode();
  bool IsDestroy = Opcode == TII.getCallFrameDestroyOpcode();
  DebugLoc DL = I->getDebugLoc();
  uint64_t Amount = !ReserveCallFrame ? I->getOperand(0).getImm() : 0;
  uint64_t InternalAmt = (IsDestroy && Amount) ? I->getOperand(1).getImm() : 0;
  I = MBB.erase(I);

  if (!ReserveCallFrame) {
    // If the stack pointer can be changed after prologue, turn the
    // adjcallstackup instruction into a 'sub %SP, <amt>' and the
    // adjcallstackdown instruction into 'add %SP, <amt>'

    // We need to keep the stack aligned properly.  To do this, we round the
    // amount of space needed for the outgoing arguments up to the next
    // alignment boundary.
    unsigned StackAlign = getStackAlignment();
    Amount = alignTo(Amount, StackAlign);

    bool DwarfCFI = MF.needsFrameMoves();

    // If we have any exception handlers in this function, and we adjust
    // the SP before calls, we may need to indicate this to the unwinder
    // using GNU_ARGS_SIZE. Note that this may be necessary even when
    // Amount == 0, because the preceding function may have set a non-0
    // GNU_ARGS_SIZE.
    // TODO: We don't need to reset this between subsequent functions,
    // if it didn't change.
    bool HasDwarfEHHandlers = !MF.getLandingPads().empty();

    if (HasDwarfEHHandlers && !IsDestroy &&
        MF.getInfo<M68kMachineFunctionInfo>()->getHasPushSequences()) {
      BuildCFI(MBB, I, DL,
               MCCFIInstruction::createGnuArgsSize(nullptr, Amount));
    }

    if (Amount == 0)
      return I;

    // Factor out the amount that gets handled inside the sequence
    // (Pushes of argument for frame setup, callee pops for frame destroy)
    Amount -= InternalAmt;

    // TODO: This is needed only if we require precise CFA.
    // If this is a callee-pop calling convention, emit a CFA adjust for
    // the amount the callee popped.
    if (IsDestroy && InternalAmt && DwarfCFI && !hasFP(MF))
      BuildCFI(MBB, I, DL,
               MCCFIInstruction::createAdjustCfaOffset(nullptr, -InternalAmt));

    // Add Amount to SP to destroy a frame, or subtract to setup.
    int64_t StackAdjustment = IsDestroy ? Amount : -Amount;
    int64_t CfaAdjustment = -StackAdjustment;

    if (StackAdjustment) {
      // Merge with any previous or following adjustment instruction. Note: the
      // instructions merged with here do not have CFI, so their stack
      // adjustments do not feed into CfaAdjustment.
      StackAdjustment += mergeSPUpdates(MBB, I, true);
      StackAdjustment += mergeSPUpdates(MBB, I, false);

      if (StackAdjustment) {
        BuildStackAdjustment(MBB, I, DL, StackAdjustment, false);
      }
    }

    if (DwarfCFI && !hasFP(MF)) {
      // If we don't have FP, but need to generate unwind information,
      // we need to set the correct CFA offset after the stack adjustment.
      // How much we adjust the CFA offset depends on whether we're emitting
      // CFI only for EH purposes or for debugging. EH only requires the CFA
      // offset to be correct at each call site, while for debugging we want
      // it to be more precise.

      // TODO: When not using precise CFA, we also need to adjust for the
      // InternalAmt here.
      if (CfaAdjustment) {
        BuildCFI(
            MBB, I, DL,
            MCCFIInstruction::createAdjustCfaOffset(nullptr, CfaAdjustment));
      }
    }

    return I;
  }

  if (IsDestroy && InternalAmt) {
    // If we are performing frame pointer elimination and if the callee pops
    // something off the stack pointer, add it back.  We do this until we have
    // more advanced stack pointer tracking ability.
    // We are not tracking the stack pointer adjustment by the callee, so make
    // sure we restore the stack pointer immediately after the call, there may
    // be spill code inserted between the CALL and ADJCALLSTACKUP instructions.
    MachineBasicBlock::iterator CI = I;
    MachineBasicBlock::iterator B = MBB.begin();
    while (CI != B && !std::prev(CI)->isCall())
      --CI;
    BuildStackAdjustment(MBB, CI, DL, -InternalAmt, /*InEpilogue=*/false);
  }

  return I;
}

/// Emit a series of instructions to increment / decrement the stack pointer by
/// a constant value.
void M68kFrameLowering::emitSPUpdate(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &MBBI,
                                     int64_t NumBytes, bool InEpilogue) const {
  bool IsSub = NumBytes < 0;
  uint64_t Offset = IsSub ? -NumBytes : NumBytes;

  uint64_t Chunk = (1LL << 31) - 1;
  DebugLoc DL = MBB.findDebugLoc(MBBI);

  while (Offset) {
    if (Offset > Chunk) {
      // Rather than emit a long series of instructions for large offsets,
      // load the offset into a register and do one sub/add
      Register Reg;

      if (IsSub && !isRegLiveIn(MBB, M68k::D0))
        Reg = M68k::D0;
      else
        Reg = findDeadCallerSavedReg(MBB, MBBI, TRI);

      if (Reg) {
        unsigned Opc = M68k::MOV32ri;
        BuildMI(MBB, MBBI, DL, TII.get(Opc), Reg).addImm(Offset);
        Opc = IsSub ? M68k::SUB32ar : M68k::ADD32ar;
        MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(Opc), StackPtr)
                               .addReg(StackPtr)
                               .addReg(Reg);
        // ??? still no CCR
        MI->getOperand(3).setIsDead(); // The CCR implicit def is dead.
        Offset = 0;
        continue;
      }
    }

    uint64_t ThisVal = std::min(Offset, Chunk);

    MachineInstrBuilder MI = BuildStackAdjustment(
        MBB, MBBI, DL, IsSub ? -ThisVal : ThisVal, InEpilogue);
    if (IsSub)
      MI.setMIFlag(MachineInstr::FrameSetup);
    else
      MI.setMIFlag(MachineInstr::FrameDestroy);

    Offset -= ThisVal;
  }
}

int M68kFrameLowering::mergeSPUpdates(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator &MBBI,
                                      bool MergeWithPrevious) const {
  if ((MergeWithPrevious && MBBI == MBB.begin()) ||
      (!MergeWithPrevious && MBBI == MBB.end()))
    return 0;

  MachineBasicBlock::iterator PI = MergeWithPrevious ? std::prev(MBBI) : MBBI;
  MachineBasicBlock::iterator NI =
      MergeWithPrevious ? nullptr : std::next(MBBI);
  unsigned Opc = PI->getOpcode();
  int Offset = 0;

  if (!MergeWithPrevious && NI != MBB.end() &&
      NI->getOpcode() == TargetOpcode::CFI_INSTRUCTION) {
    // Don't merge with the next instruction if it has CFI.
    return Offset;
  }

  if (Opc == M68k::ADD32ai && PI->getOperand(0).getReg() == StackPtr) {
    assert(PI->getOperand(1).getReg() == StackPtr);
    Offset += PI->getOperand(2).getImm();
    MBB.erase(PI);
    if (!MergeWithPrevious)
      MBBI = NI;
  } else if (Opc == M68k::SUB32ai && PI->getOperand(0).getReg() == StackPtr) {
    assert(PI->getOperand(1).getReg() == StackPtr);
    Offset -= PI->getOperand(2).getImm();
    MBB.erase(PI);
    if (!MergeWithPrevious)
      MBBI = NI;
  }

  return Offset;
}

MachineInstrBuilder M68kFrameLowering::BuildStackAdjustment(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, int64_t Offset, bool InEpilogue) const {
  assert(Offset != 0 && "zero offset stack adjustment requested");

  // TODO can `lea` be used to adjust stack?

  bool IsSub = Offset < 0;
  uint64_t AbsOffset = IsSub ? -Offset : Offset;
  unsigned Opc = IsSub ? M68k::SUB32ai : M68k::ADD32ai;

  MachineInstrBuilder MI = BuildMI(MBB, MBBI, DL, TII.get(Opc), StackPtr)
                               .addReg(StackPtr)
                               .addImm(AbsOffset);
  // FIXME Update CCR as well. For now we just
  // conservatively say CCR implicit def is dead
  MI->getOperand(3).setIsDead();
  return MI;
}

void M68kFrameLowering::BuildCFI(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 const DebugLoc &DL,
                                 const MCCFIInstruction &CFIInst) const {
  MachineFunction &MF = *MBB.getParent();
  unsigned CFIIndex = MF.addFrameInst(CFIInst);
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);
}

void M68kFrameLowering::emitPrologueCalleeSavedFrameMoves(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();

  // Add callee saved registers to move list.
  const auto &CSI = MFI.getCalleeSavedInfo();
  if (CSI.empty())
    return;

  // Calculate offsets.
  for (const auto &I : CSI) {
    int64_t Offset = MFI.getObjectOffset(I.getFrameIdx());
    Register Reg = I.getReg();

    unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);
    BuildCFI(MBB, MBBI, DL,
             MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
  }
}

void M68kFrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  assert(&STI == &MF.getSubtarget<M68kSubtarget>() &&
         "MF used frame lowering for wrong subtarget");

  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &Fn = MF.getFunction();
  MachineModuleInfo &MMI = MF.getMMI();
  M68kMachineFunctionInfo *MMFI = MF.getInfo<M68kMachineFunctionInfo>();
  uint64_t MaxAlign = calculateMaxStackAlign(MF); // Desired stack alignment.
  uint64_t StackSize = MFI.getStackSize(); // Number of bytes to allocate.
  bool HasFP = hasFP(MF);
  bool NeedsDwarfCFI = MMI.hasDebugInfo() || Fn.needsUnwindTableEntry();
  Register FramePtr = TRI->getFrameRegister(MF);
  const unsigned MachineFramePtr = FramePtr;
  unsigned BasePtr = TRI->getBaseRegister();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  // Add RETADDR move area to callee saved frame size.
  int TailCallReturnAddrDelta = MMFI->getTCReturnAddrDelta();

  if (TailCallReturnAddrDelta < 0) {
    MMFI->setCalleeSavedFrameSize(MMFI->getCalleeSavedFrameSize() -
                                  TailCallReturnAddrDelta);
  }

  // Insert stack pointer adjustment for later moving of return addr.  Only
  // applies to tail call optimized functions where the callee argument stack
  // size is bigger than the callers.
  if (TailCallReturnAddrDelta < 0) {
    BuildStackAdjustment(MBB, MBBI, DL, TailCallReturnAddrDelta,
                         /*InEpilogue=*/false)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Mapping for machine moves:
  //
  //   DST: VirtualFP AND
  //        SRC: VirtualFP              => DW_CFA_def_cfa_offset
  //        ELSE                        => DW_CFA_def_cfa
  //
  //   SRC: VirtualFP AND
  //        DST: Register               => DW_CFA_def_cfa_register
  //
  //   ELSE
  //        OFFSET < 0                  => DW_CFA_offset_extended_sf
  //        REG < 64                    => DW_CFA_offset + Reg
  //        ELSE                        => DW_CFA_offset_extended

  uint64_t NumBytes = 0;
  int stackGrowth = -SlotSize;

  if (HasFP) {
    // Calculate required stack adjustment.
    uint64_t FrameSize = StackSize - SlotSize;
    // If required, include space for extra hidden slot for stashing base
    // pointer.
    if (MMFI->getRestoreBasePointer())
      FrameSize += SlotSize;

    NumBytes = FrameSize - MMFI->getCalleeSavedFrameSize();

    // Callee-saved registers are pushed on stack before the stack is realigned.
    if (TRI->hasStackRealignment(MF))
      NumBytes = alignTo(NumBytes, MaxAlign);

    // Get the offset of the stack slot for the FP register, which is
    // guaranteed to be the last slot by processFunctionBeforeFrameFinalized.
    // Update the frame offset adjustment.
    MFI.setOffsetAdjustment(-NumBytes);

    BuildMI(MBB, MBBI, DL, TII.get(M68k::LINK16))
        .addReg(M68k::WA6, RegState::Kill)
        .addImm(-NumBytes)
        .setMIFlag(MachineInstr::FrameSetup);

    if (NeedsDwarfCFI) {
      // Mark the place where FP was saved.
      // Define the current CFA rule to use the provided offset.
      assert(StackSize);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfaOffset(nullptr, 2 * stackGrowth));

      // Change the rule for the FramePtr to be an "offset" rule.
      int DwarfFramePtr = TRI->getDwarfRegNum(MachineFramePtr, true);
      assert(DwarfFramePtr > 0);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createOffset(nullptr, DwarfFramePtr,
                                              2 * stackGrowth));
    }

    if (NeedsDwarfCFI) {
      // Mark effective beginning of when frame pointer becomes valid.
      // Define the current CFA to use the FP register.
      unsigned DwarfFramePtr = TRI->getDwarfRegNum(MachineFramePtr, true);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createDefCfaRegister(nullptr, DwarfFramePtr));
    }

    // Mark the FramePtr as live-in in every block. Don't do this again for
    // funclet prologues.
    for (MachineBasicBlock &EveryMBB : MF)
      EveryMBB.addLiveIn(MachineFramePtr);
  } else {
    NumBytes = StackSize - MMFI->getCalleeSavedFrameSize();
  }

  // Skip the callee-saved push instructions.
  bool PushedRegs = false;
  int StackOffset = 2 * stackGrowth;

  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup) &&
         MBBI->getOpcode() == M68k::PUSH32r) {
    PushedRegs = true;
    ++MBBI;

    if (!HasFP && NeedsDwarfCFI) {
      // Mark callee-saved push instruction.
      // Define the current CFA rule to use the provided offset.
      assert(StackSize);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfaOffset(nullptr, StackOffset));
      StackOffset += stackGrowth;
    }
  }

  // Realign stack after we pushed callee-saved registers (so that we'll be
  // able to calculate their offsets from the frame pointer).
  if (TRI->hasStackRealignment(MF)) {
    assert(HasFP && "There should be a frame pointer if stack is realigned.");
    BuildStackAlignAND(MBB, MBBI, DL, StackPtr, MaxAlign);
  }

  // If there is an SUB32ri of SP immediately before this instruction, merge
  // the two. This can be the case when tail call elimination is enabled and
  // the callee has more arguments then the caller.
  NumBytes -= mergeSPUpdates(MBB, MBBI, true);

  // Adjust stack pointer: ESP -= numbytes.
  if (!HasFP)
    emitSPUpdate(MBB, MBBI, -(int64_t)NumBytes, /*InEpilogue=*/false);

  unsigned SPOrEstablisher = StackPtr;

  // If we need a base pointer, set it up here. It's whatever the value
  // of the stack pointer is at this point. Any variable size objects
  // will be allocated after this, so we can still use the base pointer
  // to reference locals.
  if (TRI->hasBasePointer(MF)) {
    // Update the base pointer with the current stack pointer.
    BuildMI(MBB, MBBI, DL, TII.get(M68k::MOV32aa), BasePtr)
        .addReg(SPOrEstablisher)
        .setMIFlag(MachineInstr::FrameSetup);
    if (MMFI->getRestoreBasePointer()) {
      // Stash value of base pointer.  Saving SP instead of FP shortens
      // dependence chain. Used by SjLj EH.
      unsigned Opm = M68k::MOV32ja;
      M68k::addRegIndirectWithDisp(BuildMI(MBB, MBBI, DL, TII.get(Opm)),
                                   FramePtr, true,
                                   MMFI->getRestoreBasePointerOffset())
          .addReg(SPOrEstablisher)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  if (((!HasFP && NumBytes) || PushedRegs) && NeedsDwarfCFI) {
    // Mark end of stack pointer adjustment.
    if (!HasFP && NumBytes) {
      // Define the current CFA rule to use the provided offset.
      assert(StackSize);
      BuildCFI(
          MBB, MBBI, DL,
          MCCFIInstruction::cfiDefCfaOffset(nullptr, -StackSize + stackGrowth));
    }

    // Emit DWARF info specifying the offsets of the callee-saved registers.
    if (PushedRegs)
      emitPrologueCalleeSavedFrameMoves(MBB, MBBI, DL);
  }

  // TODO Interrupt handlers
  // M68k Interrupt handling function cannot assume anything about the
  // direction flag (DF in CCR register). Clear this flag by creating "cld"
  // instruction in each prologue of interrupt handler function. The "cld"
  // instruction should only in these cases:
  // 1. The interrupt handling function uses any of the "rep" instructions.
  // 2. Interrupt handling function calls another function.
}

static bool isTailCallOpcode(unsigned Opc) {
  return Opc == M68k::TCRETURNj || Opc == M68k::TCRETURNq;
}

void M68kFrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  M68kMachineFunctionInfo *MMFI = MF.getInfo<M68kMachineFunctionInfo>();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  std::optional<unsigned> RetOpcode;
  if (MBBI != MBB.end())
    RetOpcode = MBBI->getOpcode();
  DebugLoc DL;
  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();
  Register FramePtr = TRI->getFrameRegister(MF);
  unsigned MachineFramePtr = FramePtr;

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();
  uint64_t MaxAlign = calculateMaxStackAlign(MF);
  unsigned CSSize = MMFI->getCalleeSavedFrameSize();
  uint64_t NumBytes = 0;

  if (hasFP(MF)) {
    // Calculate required stack adjustment.
    uint64_t FrameSize = StackSize - SlotSize;
    NumBytes = FrameSize - CSSize;

    // Callee-saved registers were pushed on stack before the stack was
    // realigned.
    if (TRI->hasStackRealignment(MF))
      NumBytes = alignTo(FrameSize, MaxAlign);

  } else {
    NumBytes = StackSize - CSSize;
  }

  // Skip the callee-saved pop instructions.
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    unsigned Opc = PI->getOpcode();

    if ((Opc != M68k::POP32r || !PI->getFlag(MachineInstr::FrameDestroy)) &&
        Opc != M68k::DBG_VALUE && !PI->isTerminator())
      break;

    --MBBI;
  }
  MachineBasicBlock::iterator FirstCSPop = MBBI;

  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();

  // If there is an ADD32ri or SUB32ri of SP immediately before this
  // instruction, merge the two instructions.
  if (NumBytes || MFI.hasVarSizedObjects())
    NumBytes += mergeSPUpdates(MBB, MBBI, true);

  // If dynamic alloca is used, then reset SP to point to the last callee-saved
  // slot before popping them off! Same applies for the case, when stack was
  // realigned. Don't do this if this was a funclet epilogue, since the funclets
  // will not do realignment or dynamic stack allocation.
  if ((TRI->hasStackRealignment(MF) || MFI.hasVarSizedObjects())) {
    if (TRI->hasStackRealignment(MF))
      MBBI = FirstCSPop;
    uint64_t LEAAmount = -CSSize;

    // 'move %FramePtr, SP' will not be recognized as an epilogue sequence.
    // However, we may use this sequence if we have a frame pointer because the
    // effects of the prologue can safely be undone.
    if (LEAAmount != 0) {
      unsigned Opc = M68k::LEA32p;
      M68k::addRegIndirectWithDisp(
          BuildMI(MBB, MBBI, DL, TII.get(Opc), StackPtr), FramePtr, false,
          LEAAmount);
      --MBBI;
    } else {
      BuildMI(MBB, MBBI, DL, TII.get(M68k::UNLK))
          .addReg(MachineFramePtr, RegState::Kill)
          .setMIFlag(MachineInstr::FrameDestroy);
      --MBBI;
    }
  } else if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(M68k::UNLK))
        .addReg(MachineFramePtr, RegState::Kill)
        .setMIFlag(MachineInstr::FrameDestroy);
  } else if (NumBytes) {
    // Adjust stack pointer back: SP += numbytes.
    emitSPUpdate(MBB, MBBI, NumBytes, /*InEpilogue=*/true);
    --MBBI;
  }

  if (!RetOpcode || !isTailCallOpcode(*RetOpcode)) {
    // Add the return addr area delta back since we are not tail calling.
    int Offset = -1 * MMFI->getTCReturnAddrDelta();
    assert(Offset >= 0 && "TCDelta should never be positive");
    if (Offset) {
      MBBI = MBB.getFirstTerminator();

      // Check for possible merge with preceding ADD instruction.
      Offset += mergeSPUpdates(MBB, MBBI, true);
      emitSPUpdate(MBB, MBBI, Offset, /*InEpilogue=*/true);
    }
  }
}

void M68kFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                             BitVector &SavedRegs,
                                             RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  MachineFrameInfo &MFI = MF.getFrameInfo();

  M68kMachineFunctionInfo *M68kFI = MF.getInfo<M68kMachineFunctionInfo>();
  int64_t TailCallReturnAddrDelta = M68kFI->getTCReturnAddrDelta();

  if (TailCallReturnAddrDelta < 0) {
    // create RETURNADDR area
    //   arg
    //   arg
    //   RETADDR
    //   { ...
    //     RETADDR area
    //     ...
    //   }
    //   [FP]
    MFI.CreateFixedObject(-TailCallReturnAddrDelta,
                          TailCallReturnAddrDelta - SlotSize, true);
  }

  // Spill the BasePtr if it's used.
  if (TRI->hasBasePointer(MF)) {
    SavedRegs.set(TRI->getBaseRegister());
  }
}

bool M68kFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  M68kMachineFunctionInfo *M68kFI = MF.getInfo<M68kMachineFunctionInfo>();

  int SpillSlotOffset = getOffsetOfLocalArea() + M68kFI->getTCReturnAddrDelta();

  if (hasFP(MF)) {
    // emitPrologue always spills frame register the first thing.
    SpillSlotOffset -= SlotSize;
    MFI.CreateFixedSpillStackObject(SlotSize, SpillSlotOffset);

    // Since emitPrologue and emitEpilogue will handle spilling and restoring of
    // the frame register, we can delete it from CSI list and not have to worry
    // about avoiding it later.
    Register FPReg = TRI->getFrameRegister(MF);
    for (unsigned i = 0, e = CSI.size(); i < e; ++i) {
      if (TRI->regsOverlap(CSI[i].getReg(), FPReg)) {
        CSI.erase(CSI.begin() + i);
        break;
      }
    }
  }

  // The rest is fine
  return false;
}

bool M68kFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  auto &MRI = *static_cast<const M68kRegisterInfo *>(TRI);
  auto DL = MBB.findDebugLoc(MI);

  int FI = 0;
  unsigned Mask = 0;
  for (const auto &Info : CSI) {
    FI = std::max(FI, Info.getFrameIdx());
    Register Reg = Info.getReg();
    unsigned Shift = MRI.getSpillRegisterOrder(Reg);
    Mask |= 1 << Shift;
  }

  auto I =
      M68k::addFrameReference(BuildMI(MBB, MI, DL, TII.get(M68k::MOVM32pm)), FI)
          .addImm(Mask)
          .setMIFlag(MachineInstr::FrameSetup);

  // Append implicit registers and mem locations
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &RI = MF.getRegInfo();
  for (const auto &Info : CSI) {
    Register Reg = Info.getReg();
    bool IsLiveIn = RI.isLiveIn(Reg);
    if (!IsLiveIn)
      MBB.addLiveIn(Reg);
    I.addReg(Reg, IsLiveIn ? RegState::Implicit : RegState::ImplicitKill);
    M68k::addMemOperand(I, Info.getFrameIdx(), 0);
  }

  return true;
}

bool M68kFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  auto &MRI = *static_cast<const M68kRegisterInfo *>(TRI);
  auto DL = MBB.findDebugLoc(MI);

  int FI = 0;
  unsigned Mask = 0;
  for (const auto &Info : CSI) {
    FI = std::max(FI, Info.getFrameIdx());
    Register Reg = Info.getReg();
    unsigned Shift = MRI.getSpillRegisterOrder(Reg);
    Mask |= 1 << Shift;
  }

  auto I = M68k::addFrameReference(
               BuildMI(MBB, MI, DL, TII.get(M68k::MOVM32mp)).addImm(Mask), FI)
               .setMIFlag(MachineInstr::FrameDestroy);

  // Append implicit registers and mem locations
  for (const auto &Info : CSI) {
    I.addReg(Info.getReg(), RegState::ImplicitDefine);
    M68k::addMemOperand(I, Info.getFrameIdx(), 0);
  }

  return true;
}
