//===-- X86FrameLowering.cpp - X86 Frame Information ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "X86FrameLowering.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86ReturnProtectorLowering.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

#define DEBUG_TYPE "x86-fl"

STATISTIC(NumFrameLoopProbe, "Number of loop stack probes used in prologue");
STATISTIC(NumFrameExtraProbe,
          "Number of extra stack probes generated in prologue");
STATISTIC(NumFunctionUsingPush2Pop2, "Number of funtions using push2/pop2");

using namespace llvm;

X86FrameLowering::X86FrameLowering(const X86Subtarget &STI,
                                   MaybeAlign StackAlignOverride)
    : TargetFrameLowering(StackGrowsDown, StackAlignOverride.valueOrOne(),
                          STI.is64Bit() ? -8 : -4),
      STI(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()), RPL() {
  // Cache a bunch of frame-related predicates for this subtarget.
  SlotSize = TRI->getSlotSize();
  Is64Bit = STI.is64Bit();
  IsLP64 = STI.isTarget64BitLP64();
  // standard x86_64 and NaCl use 64-bit frame/stack pointers, x32 - 32-bit.
  Uses64BitFramePtr = STI.isTarget64BitLP64() || STI.isTargetNaCl64();
  StackPtr = TRI->getStackRegister();
  SaveArgs = Is64Bit ? STI.getSaveArgs() : 0;
}

bool X86FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects() &&
         !MF.getInfo<X86MachineFunctionInfo>()->getHasPushSequences() &&
         !MF.getInfo<X86MachineFunctionInfo>()->hasPreallocatedCall();
}

/// canSimplifyCallFramePseudos - If there is a reserved call frame, the
/// call frame pseudos can be simplified.  Having a FP, as in the default
/// implementation, is not sufficient here since we can't always use it.
/// Use a more nuanced condition.
bool X86FrameLowering::canSimplifyCallFramePseudos(
    const MachineFunction &MF) const {
  return hasReservedCallFrame(MF) ||
         MF.getInfo<X86MachineFunctionInfo>()->hasPreallocatedCall() ||
         (hasFP(MF) && !TRI->hasStackRealignment(MF)) ||
         TRI->hasBasePointer(MF);
}

// needsFrameIndexResolution - Do we need to perform FI resolution for
// this function. Normally, this is required only when the function
// has any stack objects. However, FI resolution actually has another job,
// not apparent from the title - it resolves callframesetup/destroy
// that were not simplified earlier.
// So, this is required for x86 functions that have push sequences even
// when there are no stack objects.
bool X86FrameLowering::needsFrameIndexResolution(
    const MachineFunction &MF) const {
  return MF.getFrameInfo().hasStackObjects() ||
         MF.getInfo<X86MachineFunctionInfo>()->getHasPushSequences();
}

/// hasFP - Return true if the specified function should have a dedicated frame
/// pointer register.  This is true if the function has variable sized allocas
/// or if frame pointer elimination is disabled.
bool X86FrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return (MF.getTarget().Options.DisableFramePointerElim(MF) ||
          TRI->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
          MFI.isFrameAddressTaken() || MFI.hasOpaqueSPAdjustment() ||
          MF.getInfo<X86MachineFunctionInfo>()->getForceFramePointer() ||
          MF.getInfo<X86MachineFunctionInfo>()->hasPreallocatedCall() ||
          MF.callsUnwindInit() || MF.hasEHFunclets() || MF.callsEHReturn() ||
          MFI.hasStackMap() || MFI.hasPatchPoint() ||
          (isWin64Prologue(MF) && MFI.hasCopyImplyingStackAdjustment()) ||
          SaveArgs);
}

static unsigned getSUBriOpcode(bool IsLP64) {
  return IsLP64 ? X86::SUB64ri32 : X86::SUB32ri;
}

static unsigned getADDriOpcode(bool IsLP64) {
  return IsLP64 ? X86::ADD64ri32 : X86::ADD32ri;
}

static unsigned getSUBrrOpcode(bool IsLP64) {
  return IsLP64 ? X86::SUB64rr : X86::SUB32rr;
}

static unsigned getADDrrOpcode(bool IsLP64) {
  return IsLP64 ? X86::ADD64rr : X86::ADD32rr;
}

static unsigned getANDriOpcode(bool IsLP64, int64_t Imm) {
  return IsLP64 ? X86::AND64ri32 : X86::AND32ri;
}

static unsigned getLEArOpcode(bool IsLP64) {
  return IsLP64 ? X86::LEA64r : X86::LEA32r;
}

static unsigned getMOVriOpcode(bool Use64BitReg, int64_t Imm) {
  if (Use64BitReg) {
    if (isUInt<32>(Imm))
      return X86::MOV32ri64;
    if (isInt<32>(Imm))
      return X86::MOV64ri32;
    return X86::MOV64ri;
  }
  return X86::MOV32ri;
}

// Push-Pop Acceleration (PPX) hint is used to indicate that the POP reads the
// value written by the PUSH from the stack. The processor tracks these marked
// instructions internally and fast-forwards register data between matching PUSH
// and POP instructions, without going through memory or through the training
// loop of the Fast Store Forwarding Predictor (FSFP). Instead, a more efficient
// memory-renaming optimization can be used.
//
// The PPX hint is purely a performance hint. Instructions with this hint have
// the same functional semantics as those without. PPX hints set by the
// compiler that violate the balancing rule may turn off the PPX optimization,
// but they will not affect program semantics.
//
// Hence, PPX is used for balanced spill/reloads (Exceptions and setjmp/longjmp
// are not considered).
//
// PUSH2 and POP2 are instructions for (respectively) pushing/popping 2
// GPRs at a time to/from the stack.
static unsigned getPUSHOpcode(const X86Subtarget &ST) {
  return ST.is64Bit() ? (ST.hasPPX() ? X86::PUSHP64r : X86::PUSH64r)
                      : X86::PUSH32r;
}
static unsigned getPOPOpcode(const X86Subtarget &ST) {
  return ST.is64Bit() ? (ST.hasPPX() ? X86::POPP64r : X86::POP64r)
                      : X86::POP32r;
}
static unsigned getPUSH2Opcode(const X86Subtarget &ST) {
  return ST.hasPPX() ? X86::PUSH2P : X86::PUSH2;
}
static unsigned getPOP2Opcode(const X86Subtarget &ST) {
  return ST.hasPPX() ? X86::POP2P : X86::POP2;
}

static bool isEAXLiveIn(MachineBasicBlock &MBB) {
  for (MachineBasicBlock::RegisterMaskPair RegMask : MBB.liveins()) {
    unsigned Reg = RegMask.PhysReg;

    if (Reg == X86::RAX || Reg == X86::EAX || Reg == X86::AX ||
        Reg == X86::AH || Reg == X86::AL)
      return true;
  }

  return false;
}

/// Check if the flags need to be preserved before the terminators.
/// This would be the case, if the eflags is live-in of the region
/// composed by the terminators or live-out of that region, without
/// being defined by a terminator.
static bool
flagsNeedToBePreservedBeforeTheTerminators(const MachineBasicBlock &MBB) {
  for (const MachineInstr &MI : MBB.terminators()) {
    bool BreakNext = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (Reg != X86::EFLAGS)
        continue;

      // This terminator needs an eflags that is not defined
      // by a previous another terminator:
      // EFLAGS is live-in of the region composed by the terminators.
      if (!MO.isDef())
        return true;
      // This terminator defines the eflags, i.e., we don't need to preserve it.
      // However, we still need to check this specific terminator does not
      // read a live-in value.
      BreakNext = true;
    }
    // We found a definition of the eflags, no need to preserve them.
    if (BreakNext)
      return false;
  }

  // None of the terminators use or define the eflags.
  // Check if they are live-out, that would imply we need to preserve them.
  for (const MachineBasicBlock *Succ : MBB.successors())
    if (Succ->isLiveIn(X86::EFLAGS))
      return true;

  return false;
}

/// emitSPUpdate - Emit a series of instructions to increment / decrement the
/// stack pointer by a constant value.
void X86FrameLowering::emitSPUpdate(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &MBBI,
                                    const DebugLoc &DL, int64_t NumBytes,
                                    bool InEpilogue) const {
  bool isSub = NumBytes < 0;
  uint64_t Offset = isSub ? -NumBytes : NumBytes;
  MachineInstr::MIFlag Flag =
      isSub ? MachineInstr::FrameSetup : MachineInstr::FrameDestroy;

  uint64_t Chunk = (1LL << 31) - 1;

  MachineFunction &MF = *MBB.getParent();
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86TargetLowering &TLI = *STI.getTargetLowering();
  const bool EmitInlineStackProbe = TLI.hasInlineStackProbe(MF);

  // It's ok to not take into account large chunks when probing, as the
  // allocation is split in smaller chunks anyway.
  if (EmitInlineStackProbe && !InEpilogue) {

    // This pseudo-instruction is going to be expanded, potentially using a
    // loop, by inlineStackProbe().
    BuildMI(MBB, MBBI, DL, TII.get(X86::STACKALLOC_W_PROBING)).addImm(Offset);
    return;
  } else if (Offset > Chunk) {
    // Rather than emit a long series of instructions for large offsets,
    // load the offset into a register and do one sub/add
    unsigned Reg = 0;
    unsigned Rax = (unsigned)(Is64Bit ? X86::RAX : X86::EAX);

    if (isSub && !isEAXLiveIn(MBB))
      Reg = Rax;
    else
      Reg = TRI->findDeadCallerSavedReg(MBB, MBBI);

    unsigned AddSubRROpc =
        isSub ? getSUBrrOpcode(Is64Bit) : getADDrrOpcode(Is64Bit);
    if (Reg) {
      BuildMI(MBB, MBBI, DL, TII.get(getMOVriOpcode(Is64Bit, Offset)), Reg)
          .addImm(Offset)
          .setMIFlag(Flag);
      MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(AddSubRROpc), StackPtr)
                             .addReg(StackPtr)
                             .addReg(Reg);
      MI->getOperand(3).setIsDead(); // The EFLAGS implicit def is dead.
      return;
    } else if (Offset > 8 * Chunk) {
      // If we would need more than 8 add or sub instructions (a >16GB stack
      // frame), it's worth spilling RAX to materialize this immediate.
      //   pushq %rax
      //   movabsq +-$Offset+-SlotSize, %rax
      //   addq %rsp, %rax
      //   xchg %rax, (%rsp)
      //   movq (%rsp), %rsp
      assert(Is64Bit && "can't have 32-bit 16GB stack frame");
      BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64r))
          .addReg(Rax, RegState::Kill)
          .setMIFlag(Flag);
      // Subtract is not commutative, so negate the offset and always use add.
      // Subtract 8 less and add 8 more to account for the PUSH we just did.
      if (isSub)
        Offset = -(Offset - SlotSize);
      else
        Offset = Offset + SlotSize;
      BuildMI(MBB, MBBI, DL, TII.get(getMOVriOpcode(Is64Bit, Offset)), Rax)
          .addImm(Offset)
          .setMIFlag(Flag);
      MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(X86::ADD64rr), Rax)
                             .addReg(Rax)
                             .addReg(StackPtr);
      MI->getOperand(3).setIsDead(); // The EFLAGS implicit def is dead.
      // Exchange the new SP in RAX with the top of the stack.
      addRegOffset(
          BuildMI(MBB, MBBI, DL, TII.get(X86::XCHG64rm), Rax).addReg(Rax),
          StackPtr, false, 0);
      // Load new SP from the top of the stack into RSP.
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64rm), StackPtr),
                   StackPtr, false, 0);
      return;
    }
  }

  while (Offset) {
    uint64_t ThisVal = std::min(Offset, Chunk);
    if (ThisVal == SlotSize) {
      // Use push / pop for slot sized adjustments as a size optimization. We
      // need to find a dead register when using pop.
      unsigned Reg = isSub ? (unsigned)(Is64Bit ? X86::RAX : X86::EAX)
                           : TRI->findDeadCallerSavedReg(MBB, MBBI);
      if (Reg) {
        unsigned Opc = isSub ? (Is64Bit ? X86::PUSH64r : X86::PUSH32r)
                             : (Is64Bit ? X86::POP64r : X86::POP32r);
        BuildMI(MBB, MBBI, DL, TII.get(Opc))
            .addReg(Reg, getDefRegState(!isSub) | getUndefRegState(isSub))
            .setMIFlag(Flag);
        Offset -= ThisVal;
        continue;
      }
    }

    BuildStackAdjustment(MBB, MBBI, DL, isSub ? -ThisVal : ThisVal, InEpilogue)
        .setMIFlag(Flag);

    Offset -= ThisVal;
  }
}

MachineInstrBuilder X86FrameLowering::BuildStackAdjustment(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, int64_t Offset, bool InEpilogue) const {
  assert(Offset != 0 && "zero offset stack adjustment requested");

  // On Atom, using LEA to adjust SP is preferred, but using it in the epilogue
  // is tricky.
  bool UseLEA;
  if (!InEpilogue) {
    // Check if inserting the prologue at the beginning
    // of MBB would require to use LEA operations.
    // We need to use LEA operations if EFLAGS is live in, because
    // it means an instruction will read it before it gets defined.
    UseLEA = STI.useLeaForSP() || MBB.isLiveIn(X86::EFLAGS);
  } else {
    // If we can use LEA for SP but we shouldn't, check that none
    // of the terminators uses the eflags. Otherwise we will insert
    // a ADD that will redefine the eflags and break the condition.
    // Alternatively, we could move the ADD, but this may not be possible
    // and is an optimization anyway.
    UseLEA = canUseLEAForSPInEpilogue(*MBB.getParent());
    if (UseLEA && !STI.useLeaForSP())
      UseLEA = flagsNeedToBePreservedBeforeTheTerminators(MBB);
    // If that assert breaks, that means we do not do the right thing
    // in canUseAsEpilogue.
    assert((UseLEA || !flagsNeedToBePreservedBeforeTheTerminators(MBB)) &&
           "We shouldn't have allowed this insertion point");
  }

  MachineInstrBuilder MI;
  if (UseLEA) {
    MI = addRegOffset(BuildMI(MBB, MBBI, DL,
                              TII.get(getLEArOpcode(Uses64BitFramePtr)),
                              StackPtr),
                      StackPtr, false, Offset);
  } else {
    bool IsSub = Offset < 0;
    uint64_t AbsOffset = IsSub ? -Offset : Offset;
    const unsigned Opc = IsSub ? getSUBriOpcode(Uses64BitFramePtr)
                               : getADDriOpcode(Uses64BitFramePtr);
    MI = BuildMI(MBB, MBBI, DL, TII.get(Opc), StackPtr)
             .addReg(StackPtr)
             .addImm(AbsOffset);
    MI->getOperand(3).setIsDead(); // The EFLAGS implicit def is dead.
  }
  return MI;
}

int X86FrameLowering::mergeSPUpdates(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &MBBI,
                                     bool doMergeWithPrevious) const {
  if ((doMergeWithPrevious && MBBI == MBB.begin()) ||
      (!doMergeWithPrevious && MBBI == MBB.end()))
    return 0;

  MachineBasicBlock::iterator PI = doMergeWithPrevious ? std::prev(MBBI) : MBBI;

  PI = skipDebugInstructionsBackward(PI, MBB.begin());
  // It is assumed that ADD/SUB/LEA instruction is succeded by one CFI
  // instruction, and that there are no DBG_VALUE or other instructions between
  // ADD/SUB/LEA and its corresponding CFI instruction.
  /* TODO: Add support for the case where there are multiple CFI instructions
    below the ADD/SUB/LEA, e.g.:
    ...
    add
    cfi_def_cfa_offset
    cfi_offset
    ...
  */
  if (doMergeWithPrevious && PI != MBB.begin() && PI->isCFIInstruction())
    PI = std::prev(PI);

  unsigned Opc = PI->getOpcode();
  int Offset = 0;

  if ((Opc == X86::ADD64ri32 || Opc == X86::ADD32ri) &&
      PI->getOperand(0).getReg() == StackPtr) {
    assert(PI->getOperand(1).getReg() == StackPtr);
    Offset = PI->getOperand(2).getImm();
  } else if ((Opc == X86::LEA32r || Opc == X86::LEA64_32r) &&
             PI->getOperand(0).getReg() == StackPtr &&
             PI->getOperand(1).getReg() == StackPtr &&
             PI->getOperand(2).getImm() == 1 &&
             PI->getOperand(3).getReg() == X86::NoRegister &&
             PI->getOperand(5).getReg() == X86::NoRegister) {
    // For LEAs we have: def = lea SP, FI, noreg, Offset, noreg.
    Offset = PI->getOperand(4).getImm();
  } else if ((Opc == X86::SUB64ri32 || Opc == X86::SUB32ri) &&
             PI->getOperand(0).getReg() == StackPtr) {
    assert(PI->getOperand(1).getReg() == StackPtr);
    Offset = -PI->getOperand(2).getImm();
  } else
    return 0;

  PI = MBB.erase(PI);
  if (PI != MBB.end() && PI->isCFIInstruction()) {
    auto CIs = MBB.getParent()->getFrameInstructions();
    MCCFIInstruction CI = CIs[PI->getOperand(0).getCFIIndex()];
    if (CI.getOperation() == MCCFIInstruction::OpDefCfaOffset ||
        CI.getOperation() == MCCFIInstruction::OpAdjustCfaOffset)
      PI = MBB.erase(PI);
  }
  if (!doMergeWithPrevious)
    MBBI = skipDebugInstructionsForward(PI, MBB.end());

  return Offset;
}

void X86FrameLowering::BuildCFI(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                const DebugLoc &DL,
                                const MCCFIInstruction &CFIInst,
                                MachineInstr::MIFlag Flag) const {
  MachineFunction &MF = *MBB.getParent();
  unsigned CFIIndex = MF.addFrameInst(CFIInst);

  if (CFIInst.getOperation() == MCCFIInstruction::OpAdjustCfaOffset)
    MF.getInfo<X86MachineFunctionInfo>()->setHasCFIAdjustCfa(true);

  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(Flag);
}

/// Emits Dwarf Info specifying offsets of callee saved registers and
/// frame pointer. This is called only when basic block sections are enabled.
void X86FrameLowering::emitCalleeSavedFrameMovesFullCFA(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) const {
  MachineFunction &MF = *MBB.getParent();
  if (!hasFP(MF)) {
    emitCalleeSavedFrameMoves(MBB, MBBI, DebugLoc{}, true);
    return;
  }
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();
  const Register FramePtr = TRI->getFrameRegister(MF);
  const Register MachineFramePtr =
      STI.isTarget64BitILP32() ? Register(getX86SubSuperRegister(FramePtr, 64))
                               : FramePtr;
  unsigned DwarfReg = MRI->getDwarfRegNum(MachineFramePtr, true);
  // Offset = space for return address + size of the frame pointer itself.
  int64_t Offset = (Is64Bit ? 8 : 4) + (Uses64BitFramePtr ? 8 : 4);
  BuildCFI(MBB, MBBI, DebugLoc{},
           MCCFIInstruction::createOffset(nullptr, DwarfReg, -Offset));
  emitCalleeSavedFrameMoves(MBB, MBBI, DebugLoc{}, true);
}

void X86FrameLowering::emitCalleeSavedFrameMoves(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, bool IsPrologue) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();

  // Add callee saved registers to move list.
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Calculate offsets.
  for (const CalleeSavedInfo &I : CSI) {
    int64_t Offset = MFI.getObjectOffset(I.getFrameIdx());
    Register Reg = I.getReg();
    unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);

    if (IsPrologue) {
      if (X86FI->getStackPtrSaveMI()) {
        // +2*SlotSize because there is return address and ebp at the bottom
        // of the stack.
        // | retaddr |
        // | ebp     |
        // |         |<--ebp
        Offset += 2 * SlotSize;
        SmallString<64> CfaExpr;
        CfaExpr.push_back(dwarf::DW_CFA_expression);
        uint8_t buffer[16];
        CfaExpr.append(buffer, buffer + encodeULEB128(DwarfReg, buffer));
        CfaExpr.push_back(2);
        Register FramePtr = TRI->getFrameRegister(MF);
        const Register MachineFramePtr =
            STI.isTarget64BitILP32()
                ? Register(getX86SubSuperRegister(FramePtr, 64))
                : FramePtr;
        unsigned DwarfFramePtr = MRI->getDwarfRegNum(MachineFramePtr, true);
        CfaExpr.push_back((uint8_t)(dwarf::DW_OP_breg0 + DwarfFramePtr));
        CfaExpr.append(buffer, buffer + encodeSLEB128(Offset, buffer));
        BuildCFI(MBB, MBBI, DL,
                 MCCFIInstruction::createEscape(nullptr, CfaExpr.str()),
                 MachineInstr::FrameSetup);
      } else {
        BuildCFI(MBB, MBBI, DL,
                 MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
      }
    } else {
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createRestore(nullptr, DwarfReg));
    }
  }
  if (auto *MI = X86FI->getStackPtrSaveMI()) {
    int FI = MI->getOperand(1).getIndex();
    int64_t Offset = MFI.getObjectOffset(FI) + 2 * SlotSize;
    SmallString<64> CfaExpr;
    Register FramePtr = TRI->getFrameRegister(MF);
    const Register MachineFramePtr =
        STI.isTarget64BitILP32()
            ? Register(getX86SubSuperRegister(FramePtr, 64))
            : FramePtr;
    unsigned DwarfFramePtr = MRI->getDwarfRegNum(MachineFramePtr, true);
    CfaExpr.push_back((uint8_t)(dwarf::DW_OP_breg0 + DwarfFramePtr));
    uint8_t buffer[16];
    CfaExpr.append(buffer, buffer + encodeSLEB128(Offset, buffer));
    CfaExpr.push_back(dwarf::DW_OP_deref);

    SmallString<64> DefCfaExpr;
    DefCfaExpr.push_back(dwarf::DW_CFA_def_cfa_expression);
    DefCfaExpr.append(buffer, buffer + encodeSLEB128(CfaExpr.size(), buffer));
    DefCfaExpr.append(CfaExpr.str());
    // DW_CFA_def_cfa_expression: DW_OP_breg5 offset, DW_OP_deref
    BuildCFI(MBB, MBBI, DL,
             MCCFIInstruction::createEscape(nullptr, DefCfaExpr.str()),
             MachineInstr::FrameSetup);
  }
}

void X86FrameLowering::emitZeroCallUsedRegs(BitVector RegsToZero,
                                            MachineBasicBlock &MBB) const {
  const MachineFunction &MF = *MBB.getParent();

  // Insertion point.
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();

  // Fake a debug loc.
  DebugLoc DL;
  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();

  // Zero out FP stack if referenced. Do this outside of the loop below so that
  // it's done only once.
  const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
  for (MCRegister Reg : RegsToZero.set_bits()) {
    if (!X86::RFP80RegClass.contains(Reg))
      continue;

    unsigned NumFPRegs = ST.is64Bit() ? 8 : 7;
    for (unsigned i = 0; i != NumFPRegs; ++i)
      BuildMI(MBB, MBBI, DL, TII.get(X86::LD_F0));

    for (unsigned i = 0; i != NumFPRegs; ++i)
      BuildMI(MBB, MBBI, DL, TII.get(X86::ST_FPrr)).addReg(X86::ST0);
    break;
  }

  // For GPRs, we only care to clear out the 32-bit register.
  BitVector GPRsToZero(TRI->getNumRegs());
  for (MCRegister Reg : RegsToZero.set_bits())
    if (TRI->isGeneralPurposeRegister(MF, Reg)) {
      GPRsToZero.set(getX86SubSuperRegister(Reg, 32));
      RegsToZero.reset(Reg);
    }

  // Zero out the GPRs first.
  for (MCRegister Reg : GPRsToZero.set_bits())
    TII.buildClearRegister(Reg, MBB, MBBI, DL);

  // Zero out the remaining registers.
  for (MCRegister Reg : RegsToZero.set_bits())
    TII.buildClearRegister(Reg, MBB, MBBI, DL);
}

void X86FrameLowering::emitStackProbe(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, bool InProlog,
    std::optional<MachineFunction::DebugInstrOperandPair> InstrNum) const {
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  if (STI.isTargetWindowsCoreCLR()) {
    if (InProlog) {
      BuildMI(MBB, MBBI, DL, TII.get(X86::STACKALLOC_W_PROBING))
          .addImm(0 /* no explicit stack size */);
    } else {
      emitStackProbeInline(MF, MBB, MBBI, DL, false);
    }
  } else {
    emitStackProbeCall(MF, MBB, MBBI, DL, InProlog, InstrNum);
  }
}

bool X86FrameLowering::stackProbeFunctionModifiesSP() const {
  return STI.isOSWindows() && !STI.isTargetWin64();
}

void X86FrameLowering::inlineStackProbe(MachineFunction &MF,
                                        MachineBasicBlock &PrologMBB) const {
  auto Where = llvm::find_if(PrologMBB, [](MachineInstr &MI) {
    return MI.getOpcode() == X86::STACKALLOC_W_PROBING;
  });
  if (Where != PrologMBB.end()) {
    DebugLoc DL = PrologMBB.findDebugLoc(Where);
    emitStackProbeInline(MF, PrologMBB, Where, DL, true);
    Where->eraseFromParent();
  }
}

void X86FrameLowering::emitStackProbeInline(MachineFunction &MF,
                                            MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MBBI,
                                            const DebugLoc &DL,
                                            bool InProlog) const {
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  if (STI.isTargetWindowsCoreCLR() && STI.is64Bit())
    emitStackProbeInlineWindowsCoreCLR64(MF, MBB, MBBI, DL, InProlog);
  else
    emitStackProbeInlineGeneric(MF, MBB, MBBI, DL, InProlog);
}

void X86FrameLowering::emitStackProbeInlineGeneric(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, bool InProlog) const {
  MachineInstr &AllocWithProbe = *MBBI;
  uint64_t Offset = AllocWithProbe.getOperand(0).getImm();

  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86TargetLowering &TLI = *STI.getTargetLowering();
  assert(!(STI.is64Bit() && STI.isTargetWindowsCoreCLR()) &&
         "different expansion expected for CoreCLR 64 bit");

  const uint64_t StackProbeSize = TLI.getStackProbeSize(MF);
  uint64_t ProbeChunk = StackProbeSize * 8;

  uint64_t MaxAlign =
      TRI->hasStackRealignment(MF) ? calculateMaxStackAlign(MF) : 0;

  // Synthesize a loop or unroll it, depending on the number of iterations.
  // BuildStackAlignAND ensures that only MaxAlign % StackProbeSize bits left
  // between the unaligned rsp and current rsp.
  if (Offset > ProbeChunk) {
    emitStackProbeInlineGenericLoop(MF, MBB, MBBI, DL, Offset,
                                    MaxAlign % StackProbeSize);
  } else {
    emitStackProbeInlineGenericBlock(MF, MBB, MBBI, DL, Offset,
                                     MaxAlign % StackProbeSize);
  }
}

void X86FrameLowering::emitStackProbeInlineGenericBlock(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, uint64_t Offset,
    uint64_t AlignOffset) const {

  const bool NeedsDwarfCFI = needsDwarfCFI(MF);
  const bool HasFP = hasFP(MF);
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86TargetLowering &TLI = *STI.getTargetLowering();
  const unsigned MovMIOpc = Is64Bit ? X86::MOV64mi32 : X86::MOV32mi;
  const uint64_t StackProbeSize = TLI.getStackProbeSize(MF);

  uint64_t CurrentOffset = 0;

  assert(AlignOffset < StackProbeSize);

  // If the offset is so small it fits within a page, there's nothing to do.
  if (StackProbeSize < Offset + AlignOffset) {

    uint64_t StackAdjustment = StackProbeSize - AlignOffset;
    BuildStackAdjustment(MBB, MBBI, DL, -StackAdjustment, /*InEpilogue=*/false)
        .setMIFlag(MachineInstr::FrameSetup);
    if (!HasFP && NeedsDwarfCFI) {
      BuildCFI(
          MBB, MBBI, DL,
          MCCFIInstruction::createAdjustCfaOffset(nullptr, StackAdjustment));
    }

    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(MovMIOpc))
                     .setMIFlag(MachineInstr::FrameSetup),
                 StackPtr, false, 0)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
    NumFrameExtraProbe++;
    CurrentOffset = StackProbeSize - AlignOffset;
  }

  // For the next N - 1 pages, just probe. I tried to take advantage of
  // natural probes but it implies much more logic and there was very few
  // interesting natural probes to interleave.
  while (CurrentOffset + StackProbeSize < Offset) {
    BuildStackAdjustment(MBB, MBBI, DL, -StackProbeSize, /*InEpilogue=*/false)
        .setMIFlag(MachineInstr::FrameSetup);

    if (!HasFP && NeedsDwarfCFI) {
      BuildCFI(
          MBB, MBBI, DL,
          MCCFIInstruction::createAdjustCfaOffset(nullptr, StackProbeSize));
    }
    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(MovMIOpc))
                     .setMIFlag(MachineInstr::FrameSetup),
                 StackPtr, false, 0)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
    NumFrameExtraProbe++;
    CurrentOffset += StackProbeSize;
  }

  // No need to probe the tail, it is smaller than a Page.
  uint64_t ChunkSize = Offset - CurrentOffset;
  if (ChunkSize == SlotSize) {
    // Use push for slot sized adjustments as a size optimization,
    // like emitSPUpdate does when not probing.
    unsigned Reg = Is64Bit ? X86::RAX : X86::EAX;
    unsigned Opc = Is64Bit ? X86::PUSH64r : X86::PUSH32r;
    BuildMI(MBB, MBBI, DL, TII.get(Opc))
        .addReg(Reg, RegState::Undef)
        .setMIFlag(MachineInstr::FrameSetup);
  } else {
    BuildStackAdjustment(MBB, MBBI, DL, -ChunkSize, /*InEpilogue=*/false)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  // No need to adjust Dwarf CFA offset here, the last position of the stack has
  // been defined
}

void X86FrameLowering::emitStackProbeInlineGenericLoop(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, uint64_t Offset,
    uint64_t AlignOffset) const {
  assert(Offset && "null offset");

  assert(MBB.computeRegisterLiveness(TRI, X86::EFLAGS, MBBI) !=
             MachineBasicBlock::LQR_Live &&
         "Inline stack probe loop will clobber live EFLAGS.");

  const bool NeedsDwarfCFI = needsDwarfCFI(MF);
  const bool HasFP = hasFP(MF);
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86TargetLowering &TLI = *STI.getTargetLowering();
  const unsigned MovMIOpc = Is64Bit ? X86::MOV64mi32 : X86::MOV32mi;
  const uint64_t StackProbeSize = TLI.getStackProbeSize(MF);

  if (AlignOffset) {
    if (AlignOffset < StackProbeSize) {
      // Perform a first smaller allocation followed by a probe.
      BuildStackAdjustment(MBB, MBBI, DL, -AlignOffset, /*InEpilogue=*/false)
          .setMIFlag(MachineInstr::FrameSetup);

      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(MovMIOpc))
                       .setMIFlag(MachineInstr::FrameSetup),
                   StackPtr, false, 0)
          .addImm(0)
          .setMIFlag(MachineInstr::FrameSetup);
      NumFrameExtraProbe++;
      Offset -= AlignOffset;
    }
  }

  // Synthesize a loop
  NumFrameLoopProbe++;
  const BasicBlock *LLVM_BB = MBB.getBasicBlock();

  MachineBasicBlock *testMBB = MF.CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *tailMBB = MF.CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator MBBIter = ++MBB.getIterator();
  MF.insert(MBBIter, testMBB);
  MF.insert(MBBIter, tailMBB);

  Register FinalStackProbed = Uses64BitFramePtr ? X86::R11
                              : Is64Bit         ? X86::R11D
                                                : X86::EAX;

  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::COPY), FinalStackProbed)
      .addReg(StackPtr)
      .setMIFlag(MachineInstr::FrameSetup);

  // save loop bound
  {
    const unsigned BoundOffset = alignDown(Offset, StackProbeSize);
    const unsigned SUBOpc = getSUBriOpcode(Uses64BitFramePtr);
    BuildMI(MBB, MBBI, DL, TII.get(SUBOpc), FinalStackProbed)
        .addReg(FinalStackProbed)
        .addImm(BoundOffset)
        .setMIFlag(MachineInstr::FrameSetup);

    // while in the loop, use loop-invariant reg for CFI,
    // instead of the stack pointer, which changes during the loop
    if (!HasFP && NeedsDwarfCFI) {
      // x32 uses the same DWARF register numbers as x86-64,
      // so there isn't a register number for r11d, we must use r11 instead
      const Register DwarfFinalStackProbed =
          STI.isTarget64BitILP32()
              ? Register(getX86SubSuperRegister(FinalStackProbed, 64))
              : FinalStackProbed;

      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createDefCfaRegister(
                   nullptr, TRI->getDwarfRegNum(DwarfFinalStackProbed, true)));
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createAdjustCfaOffset(nullptr, BoundOffset));
    }
  }

  // allocate a page
  BuildStackAdjustment(*testMBB, testMBB->end(), DL, -StackProbeSize,
                       /*InEpilogue=*/false)
      .setMIFlag(MachineInstr::FrameSetup);

  // touch the page
  addRegOffset(BuildMI(testMBB, DL, TII.get(MovMIOpc))
                   .setMIFlag(MachineInstr::FrameSetup),
               StackPtr, false, 0)
      .addImm(0)
      .setMIFlag(MachineInstr::FrameSetup);

  // cmp with stack pointer bound
  BuildMI(testMBB, DL, TII.get(Uses64BitFramePtr ? X86::CMP64rr : X86::CMP32rr))
      .addReg(StackPtr)
      .addReg(FinalStackProbed)
      .setMIFlag(MachineInstr::FrameSetup);

  // jump
  BuildMI(testMBB, DL, TII.get(X86::JCC_1))
      .addMBB(testMBB)
      .addImm(X86::COND_NE)
      .setMIFlag(MachineInstr::FrameSetup);
  testMBB->addSuccessor(testMBB);
  testMBB->addSuccessor(tailMBB);

  // BB management
  tailMBB->splice(tailMBB->end(), &MBB, MBBI, MBB.end());
  tailMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  MBB.addSuccessor(testMBB);

  // handle tail
  const uint64_t TailOffset = Offset % StackProbeSize;
  MachineBasicBlock::iterator TailMBBIter = tailMBB->begin();
  if (TailOffset) {
    BuildStackAdjustment(*tailMBB, TailMBBIter, DL, -TailOffset,
                         /*InEpilogue=*/false)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // after the loop, switch back to stack pointer for CFI
  if (!HasFP && NeedsDwarfCFI) {
    // x32 uses the same DWARF register numbers as x86-64,
    // so there isn't a register number for esp, we must use rsp instead
    const Register DwarfStackPtr =
        STI.isTarget64BitILP32()
            ? Register(getX86SubSuperRegister(StackPtr, 64))
            : Register(StackPtr);

    BuildCFI(*tailMBB, TailMBBIter, DL,
             MCCFIInstruction::createDefCfaRegister(
                 nullptr, TRI->getDwarfRegNum(DwarfStackPtr, true)));
  }

  // Update Live In information
  fullyRecomputeLiveIns({tailMBB, testMBB});
}

void X86FrameLowering::emitStackProbeInlineWindowsCoreCLR64(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, bool InProlog) const {
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  assert(STI.is64Bit() && "different expansion needed for 32 bit");
  assert(STI.isTargetWindowsCoreCLR() && "custom expansion expects CoreCLR");
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const BasicBlock *LLVM_BB = MBB.getBasicBlock();

  assert(MBB.computeRegisterLiveness(TRI, X86::EFLAGS, MBBI) !=
             MachineBasicBlock::LQR_Live &&
         "Inline stack probe loop will clobber live EFLAGS.");

  // RAX contains the number of bytes of desired stack adjustment.
  // The handling here assumes this value has already been updated so as to
  // maintain stack alignment.
  //
  // We need to exit with RSP modified by this amount and execute suitable
  // page touches to notify the OS that we're growing the stack responsibly.
  // All stack probing must be done without modifying RSP.
  //
  // MBB:
  //    SizeReg = RAX;
  //    ZeroReg = 0
  //    CopyReg = RSP
  //    Flags, TestReg = CopyReg - SizeReg
  //    FinalReg = !Flags.Ovf ? TestReg : ZeroReg
  //    LimitReg = gs magic thread env access
  //    if FinalReg >= LimitReg goto ContinueMBB
  // RoundBB:
  //    RoundReg = page address of FinalReg
  // LoopMBB:
  //    LoopReg = PHI(LimitReg,ProbeReg)
  //    ProbeReg = LoopReg - PageSize
  //    [ProbeReg] = 0
  //    if (ProbeReg > RoundReg) goto LoopMBB
  // ContinueMBB:
  //    RSP = RSP - RAX
  //    [rest of original MBB]

  // Set up the new basic blocks
  MachineBasicBlock *RoundMBB = MF.CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *LoopMBB = MF.CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *ContinueMBB = MF.CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator MBBIter = std::next(MBB.getIterator());
  MF.insert(MBBIter, RoundMBB);
  MF.insert(MBBIter, LoopMBB);
  MF.insert(MBBIter, ContinueMBB);

  // Split MBB and move the tail portion down to ContinueMBB.
  MachineBasicBlock::iterator BeforeMBBI = std::prev(MBBI);
  ContinueMBB->splice(ContinueMBB->begin(), &MBB, MBBI, MBB.end());
  ContinueMBB->transferSuccessorsAndUpdatePHIs(&MBB);

  // Some useful constants
  const int64_t ThreadEnvironmentStackLimit = 0x10;
  const int64_t PageSize = 0x1000;
  const int64_t PageMask = ~(PageSize - 1);

  // Registers we need. For the normal case we use virtual
  // registers. For the prolog expansion we use RAX, RCX and RDX.
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterClass *RegClass = &X86::GR64RegClass;
  const Register
      SizeReg = InProlog ? X86::RAX : MRI.createVirtualRegister(RegClass),
      ZeroReg = InProlog ? X86::RCX : MRI.createVirtualRegister(RegClass),
      CopyReg = InProlog ? X86::RDX : MRI.createVirtualRegister(RegClass),
      TestReg = InProlog ? X86::RDX : MRI.createVirtualRegister(RegClass),
      FinalReg = InProlog ? X86::RDX : MRI.createVirtualRegister(RegClass),
      RoundedReg = InProlog ? X86::RDX : MRI.createVirtualRegister(RegClass),
      LimitReg = InProlog ? X86::RCX : MRI.createVirtualRegister(RegClass),
      JoinReg = InProlog ? X86::RCX : MRI.createVirtualRegister(RegClass),
      ProbeReg = InProlog ? X86::RCX : MRI.createVirtualRegister(RegClass);

  // SP-relative offsets where we can save RCX and RDX.
  int64_t RCXShadowSlot = 0;
  int64_t RDXShadowSlot = 0;

  // If inlining in the prolog, save RCX and RDX.
  if (InProlog) {
    // Compute the offsets. We need to account for things already
    // pushed onto the stack at this point: return address, frame
    // pointer (if used), and callee saves.
    X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
    const int64_t CalleeSaveSize = X86FI->getCalleeSavedFrameSize();
    const bool HasFP = hasFP(MF);

    // Check if we need to spill RCX and/or RDX.
    // Here we assume that no earlier prologue instruction changes RCX and/or
    // RDX, so checking the block live-ins is enough.
    const bool IsRCXLiveIn = MBB.isLiveIn(X86::RCX);
    const bool IsRDXLiveIn = MBB.isLiveIn(X86::RDX);
    int64_t InitSlot = 8 + CalleeSaveSize + (HasFP ? 8 : 0);
    // Assign the initial slot to both registers, then change RDX's slot if both
    // need to be spilled.
    if (IsRCXLiveIn)
      RCXShadowSlot = InitSlot;
    if (IsRDXLiveIn)
      RDXShadowSlot = InitSlot;
    if (IsRDXLiveIn && IsRCXLiveIn)
      RDXShadowSlot += 8;
    // Emit the saves if needed.
    if (IsRCXLiveIn)
      addRegOffset(BuildMI(&MBB, DL, TII.get(X86::MOV64mr)), X86::RSP, false,
                   RCXShadowSlot)
          .addReg(X86::RCX);
    if (IsRDXLiveIn)
      addRegOffset(BuildMI(&MBB, DL, TII.get(X86::MOV64mr)), X86::RSP, false,
                   RDXShadowSlot)
          .addReg(X86::RDX);
  } else {
    // Not in the prolog. Copy RAX to a virtual reg.
    BuildMI(&MBB, DL, TII.get(X86::MOV64rr), SizeReg).addReg(X86::RAX);
  }

  // Add code to MBB to check for overflow and set the new target stack pointer
  // to zero if so.
  BuildMI(&MBB, DL, TII.get(X86::XOR64rr), ZeroReg)
      .addReg(ZeroReg, RegState::Undef)
      .addReg(ZeroReg, RegState::Undef);
  BuildMI(&MBB, DL, TII.get(X86::MOV64rr), CopyReg).addReg(X86::RSP);
  BuildMI(&MBB, DL, TII.get(X86::SUB64rr), TestReg)
      .addReg(CopyReg)
      .addReg(SizeReg);
  BuildMI(&MBB, DL, TII.get(X86::CMOV64rr), FinalReg)
      .addReg(TestReg)
      .addReg(ZeroReg)
      .addImm(X86::COND_B);

  // FinalReg now holds final stack pointer value, or zero if
  // allocation would overflow. Compare against the current stack
  // limit from the thread environment block. Note this limit is the
  // lowest touched page on the stack, not the point at which the OS
  // will cause an overflow exception, so this is just an optimization
  // to avoid unnecessarily touching pages that are below the current
  // SP but already committed to the stack by the OS.
  BuildMI(&MBB, DL, TII.get(X86::MOV64rm), LimitReg)
      .addReg(0)
      .addImm(1)
      .addReg(0)
      .addImm(ThreadEnvironmentStackLimit)
      .addReg(X86::GS);
  BuildMI(&MBB, DL, TII.get(X86::CMP64rr)).addReg(FinalReg).addReg(LimitReg);
  // Jump if the desired stack pointer is at or above the stack limit.
  BuildMI(&MBB, DL, TII.get(X86::JCC_1))
      .addMBB(ContinueMBB)
      .addImm(X86::COND_AE);

  // Add code to roundMBB to round the final stack pointer to a page boundary.
  RoundMBB->addLiveIn(FinalReg);
  BuildMI(RoundMBB, DL, TII.get(X86::AND64ri32), RoundedReg)
      .addReg(FinalReg)
      .addImm(PageMask);
  BuildMI(RoundMBB, DL, TII.get(X86::JMP_1)).addMBB(LoopMBB);

  // LimitReg now holds the current stack limit, RoundedReg page-rounded
  // final RSP value. Add code to loopMBB to decrement LimitReg page-by-page
  // and probe until we reach RoundedReg.
  if (!InProlog) {
    BuildMI(LoopMBB, DL, TII.get(X86::PHI), JoinReg)
        .addReg(LimitReg)
        .addMBB(RoundMBB)
        .addReg(ProbeReg)
        .addMBB(LoopMBB);
  }

  LoopMBB->addLiveIn(JoinReg);
  addRegOffset(BuildMI(LoopMBB, DL, TII.get(X86::LEA64r), ProbeReg), JoinReg,
               false, -PageSize);

  // Probe by storing a byte onto the stack.
  BuildMI(LoopMBB, DL, TII.get(X86::MOV8mi))
      .addReg(ProbeReg)
      .addImm(1)
      .addReg(0)
      .addImm(0)
      .addReg(0)
      .addImm(0);

  LoopMBB->addLiveIn(RoundedReg);
  BuildMI(LoopMBB, DL, TII.get(X86::CMP64rr))
      .addReg(RoundedReg)
      .addReg(ProbeReg);
  BuildMI(LoopMBB, DL, TII.get(X86::JCC_1))
      .addMBB(LoopMBB)
      .addImm(X86::COND_NE);

  MachineBasicBlock::iterator ContinueMBBI = ContinueMBB->getFirstNonPHI();

  // If in prolog, restore RDX and RCX.
  if (InProlog) {
    if (RCXShadowSlot) // It means we spilled RCX in the prologue.
      addRegOffset(BuildMI(*ContinueMBB, ContinueMBBI, DL,
                           TII.get(X86::MOV64rm), X86::RCX),
                   X86::RSP, false, RCXShadowSlot);
    if (RDXShadowSlot) // It means we spilled RDX in the prologue.
      addRegOffset(BuildMI(*ContinueMBB, ContinueMBBI, DL,
                           TII.get(X86::MOV64rm), X86::RDX),
                   X86::RSP, false, RDXShadowSlot);
  }

  // Now that the probing is done, add code to continueMBB to update
  // the stack pointer for real.
  ContinueMBB->addLiveIn(SizeReg);
  BuildMI(*ContinueMBB, ContinueMBBI, DL, TII.get(X86::SUB64rr), X86::RSP)
      .addReg(X86::RSP)
      .addReg(SizeReg);

  // Add the control flow edges we need.
  MBB.addSuccessor(ContinueMBB);
  MBB.addSuccessor(RoundMBB);
  RoundMBB->addSuccessor(LoopMBB);
  LoopMBB->addSuccessor(ContinueMBB);
  LoopMBB->addSuccessor(LoopMBB);

  // Mark all the instructions added to the prolog as frame setup.
  if (InProlog) {
    for (++BeforeMBBI; BeforeMBBI != MBB.end(); ++BeforeMBBI) {
      BeforeMBBI->setFlag(MachineInstr::FrameSetup);
    }
    for (MachineInstr &MI : *RoundMBB) {
      MI.setFlag(MachineInstr::FrameSetup);
    }
    for (MachineInstr &MI : *LoopMBB) {
      MI.setFlag(MachineInstr::FrameSetup);
    }
    for (MachineInstr &MI :
         llvm::make_range(ContinueMBB->begin(), ContinueMBBI)) {
      MI.setFlag(MachineInstr::FrameSetup);
    }
  }
}

void X86FrameLowering::emitStackProbeCall(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, bool InProlog,
    std::optional<MachineFunction::DebugInstrOperandPair> InstrNum) const {
  bool IsLargeCodeModel = MF.getTarget().getCodeModel() == CodeModel::Large;

  // FIXME: Add indirect thunk support and remove this.
  if (Is64Bit && IsLargeCodeModel && STI.useIndirectThunkCalls())
    report_fatal_error("Emitting stack probe calls on 64-bit with the large "
                       "code model and indirect thunks not yet implemented.");

  assert(MBB.computeRegisterLiveness(TRI, X86::EFLAGS, MBBI) !=
             MachineBasicBlock::LQR_Live &&
         "Stack probe calls will clobber live EFLAGS.");

  unsigned CallOp;
  if (Is64Bit)
    CallOp = IsLargeCodeModel ? X86::CALL64r : X86::CALL64pcrel32;
  else
    CallOp = X86::CALLpcrel32;

  StringRef Symbol = STI.getTargetLowering()->getStackProbeSymbolName(MF);

  MachineInstrBuilder CI;
  MachineBasicBlock::iterator ExpansionMBBI = std::prev(MBBI);

  // All current stack probes take AX and SP as input, clobber flags, and
  // preserve all registers. x86_64 probes leave RSP unmodified.
  if (Is64Bit && MF.getTarget().getCodeModel() == CodeModel::Large) {
    // For the large code model, we have to call through a register. Use R11,
    // as it is scratch in all supported calling conventions.
    BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64ri), X86::R11)
        .addExternalSymbol(MF.createExternalSymbolName(Symbol));
    CI = BuildMI(MBB, MBBI, DL, TII.get(CallOp)).addReg(X86::R11);
  } else {
    CI = BuildMI(MBB, MBBI, DL, TII.get(CallOp))
             .addExternalSymbol(MF.createExternalSymbolName(Symbol));
  }

  unsigned AX = Uses64BitFramePtr ? X86::RAX : X86::EAX;
  unsigned SP = Uses64BitFramePtr ? X86::RSP : X86::ESP;
  CI.addReg(AX, RegState::Implicit)
      .addReg(SP, RegState::Implicit)
      .addReg(AX, RegState::Define | RegState::Implicit)
      .addReg(SP, RegState::Define | RegState::Implicit)
      .addReg(X86::EFLAGS, RegState::Define | RegState::Implicit);

  MachineInstr *ModInst = CI;
  if (STI.isTargetWin64() || !STI.isOSWindows()) {
    // MSVC x32's _chkstk and cygwin/mingw's _alloca adjust %esp themselves.
    // MSVC x64's __chkstk and cygwin/mingw's ___chkstk_ms do not adjust %rsp
    // themselves. They also does not clobber %rax so we can reuse it when
    // adjusting %rsp.
    // All other platforms do not specify a particular ABI for the stack probe
    // function, so we arbitrarily define it to not adjust %esp/%rsp itself.
    ModInst =
        BuildMI(MBB, MBBI, DL, TII.get(getSUBrrOpcode(Uses64BitFramePtr)), SP)
            .addReg(SP)
            .addReg(AX);
  }

  // DebugInfo variable locations -- if there's an instruction number for the
  // allocation (i.e., DYN_ALLOC_*), substitute it for the instruction that
  // modifies SP.
  if (InstrNum) {
    if (STI.isTargetWin64() || !STI.isOSWindows()) {
      // Label destination operand of the subtract.
      MF.makeDebugValueSubstitution(*InstrNum,
                                    {ModInst->getDebugInstrNum(), 0});
    } else {
      // Label the call. The operand number is the penultimate operand, zero
      // based.
      unsigned SPDefOperand = ModInst->getNumOperands() - 2;
      MF.makeDebugValueSubstitution(
          *InstrNum, {ModInst->getDebugInstrNum(), SPDefOperand});
    }
  }

  if (InProlog) {
    // Apply the frame setup flag to all inserted instrs.
    for (++ExpansionMBBI; ExpansionMBBI != MBBI; ++ExpansionMBBI)
      ExpansionMBBI->setFlag(MachineInstr::FrameSetup);
  }
}

static unsigned calculateSetFPREG(uint64_t SPAdjust) {
  // Win64 ABI has a less restrictive limitation of 240; 128 works equally well
  // and might require smaller successive adjustments.
  const uint64_t Win64MaxSEHOffset = 128;
  uint64_t SEHFrameOffset = std::min(SPAdjust, Win64MaxSEHOffset);
  // Win64 ABI requires 16-byte alignment for the UWOP_SET_FPREG opcode.
  return SEHFrameOffset & -16;
}

// If we're forcing a stack realignment we can't rely on just the frame
// info, we need to know the ABI stack alignment as well in case we
// have a call out.  Otherwise just make sure we have some alignment - we'll
// go with the minimum SlotSize.
uint64_t
X86FrameLowering::calculateMaxStackAlign(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  Align MaxAlign = MFI.getMaxAlign(); // Desired stack alignment.
  Align StackAlign = getStackAlign();
  bool HasRealign = MF.getFunction().hasFnAttribute("stackrealign");
  if (HasRealign) {
    if (MFI.hasCalls())
      MaxAlign = (StackAlign > MaxAlign) ? StackAlign : MaxAlign;
    else if (MaxAlign < SlotSize)
      MaxAlign = Align(SlotSize);
  }

  if (!Is64Bit && MF.getFunction().getCallingConv() == CallingConv::X86_INTR) {
    if (HasRealign)
      MaxAlign = (MaxAlign > 16) ? MaxAlign : Align(16);
    else
      MaxAlign = Align(16);
  }
  return MaxAlign.value();
}

void X86FrameLowering::BuildStackAlignAND(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MBBI,
                                          const DebugLoc &DL, unsigned Reg,
                                          uint64_t MaxAlign) const {
  uint64_t Val = -MaxAlign;
  unsigned AndOp = getANDriOpcode(Uses64BitFramePtr, Val);

  MachineFunction &MF = *MBB.getParent();
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86TargetLowering &TLI = *STI.getTargetLowering();
  const uint64_t StackProbeSize = TLI.getStackProbeSize(MF);
  const bool EmitInlineStackProbe = TLI.hasInlineStackProbe(MF);

  // We want to make sure that (in worst case) less than StackProbeSize bytes
  // are not probed after the AND. This assumption is used in
  // emitStackProbeInlineGeneric.
  if (Reg == StackPtr && EmitInlineStackProbe && MaxAlign >= StackProbeSize) {
    {
      NumFrameLoopProbe++;
      MachineBasicBlock *entryMBB =
          MF.CreateMachineBasicBlock(MBB.getBasicBlock());
      MachineBasicBlock *headMBB =
          MF.CreateMachineBasicBlock(MBB.getBasicBlock());
      MachineBasicBlock *bodyMBB =
          MF.CreateMachineBasicBlock(MBB.getBasicBlock());
      MachineBasicBlock *footMBB =
          MF.CreateMachineBasicBlock(MBB.getBasicBlock());

      MachineFunction::iterator MBBIter = MBB.getIterator();
      MF.insert(MBBIter, entryMBB);
      MF.insert(MBBIter, headMBB);
      MF.insert(MBBIter, bodyMBB);
      MF.insert(MBBIter, footMBB);
      const unsigned MovMIOpc = Is64Bit ? X86::MOV64mi32 : X86::MOV32mi;
      Register FinalStackProbed = Uses64BitFramePtr ? X86::R11
                                  : Is64Bit         ? X86::R11D
                                                    : X86::EAX;

      // Setup entry block
      {

        entryMBB->splice(entryMBB->end(), &MBB, MBB.begin(), MBBI);
        BuildMI(entryMBB, DL, TII.get(TargetOpcode::COPY), FinalStackProbed)
            .addReg(StackPtr)
            .setMIFlag(MachineInstr::FrameSetup);
        MachineInstr *MI =
            BuildMI(entryMBB, DL, TII.get(AndOp), FinalStackProbed)
                .addReg(FinalStackProbed)
                .addImm(Val)
                .setMIFlag(MachineInstr::FrameSetup);

        // The EFLAGS implicit def is dead.
        MI->getOperand(3).setIsDead();

        BuildMI(entryMBB, DL,
                TII.get(Uses64BitFramePtr ? X86::CMP64rr : X86::CMP32rr))
            .addReg(FinalStackProbed)
            .addReg(StackPtr)
            .setMIFlag(MachineInstr::FrameSetup);
        BuildMI(entryMBB, DL, TII.get(X86::JCC_1))
            .addMBB(&MBB)
            .addImm(X86::COND_E)
            .setMIFlag(MachineInstr::FrameSetup);
        entryMBB->addSuccessor(headMBB);
        entryMBB->addSuccessor(&MBB);
      }

      // Loop entry block

      {
        const unsigned SUBOpc = getSUBriOpcode(Uses64BitFramePtr);
        BuildMI(headMBB, DL, TII.get(SUBOpc), StackPtr)
            .addReg(StackPtr)
            .addImm(StackProbeSize)
            .setMIFlag(MachineInstr::FrameSetup);

        BuildMI(headMBB, DL,
                TII.get(Uses64BitFramePtr ? X86::CMP64rr : X86::CMP32rr))
            .addReg(StackPtr)
            .addReg(FinalStackProbed)
            .setMIFlag(MachineInstr::FrameSetup);

        // jump to the footer if StackPtr < FinalStackProbed
        BuildMI(headMBB, DL, TII.get(X86::JCC_1))
            .addMBB(footMBB)
            .addImm(X86::COND_B)
            .setMIFlag(MachineInstr::FrameSetup);

        headMBB->addSuccessor(bodyMBB);
        headMBB->addSuccessor(footMBB);
      }

      // setup loop body
      {
        addRegOffset(BuildMI(bodyMBB, DL, TII.get(MovMIOpc))
                         .setMIFlag(MachineInstr::FrameSetup),
                     StackPtr, false, 0)
            .addImm(0)
            .setMIFlag(MachineInstr::FrameSetup);

        const unsigned SUBOpc = getSUBriOpcode(Uses64BitFramePtr);
        BuildMI(bodyMBB, DL, TII.get(SUBOpc), StackPtr)
            .addReg(StackPtr)
            .addImm(StackProbeSize)
            .setMIFlag(MachineInstr::FrameSetup);

        // cmp with stack pointer bound
        BuildMI(bodyMBB, DL,
                TII.get(Uses64BitFramePtr ? X86::CMP64rr : X86::CMP32rr))
            .addReg(FinalStackProbed)
            .addReg(StackPtr)
            .setMIFlag(MachineInstr::FrameSetup);

        // jump back while FinalStackProbed < StackPtr
        BuildMI(bodyMBB, DL, TII.get(X86::JCC_1))
            .addMBB(bodyMBB)
            .addImm(X86::COND_B)
            .setMIFlag(MachineInstr::FrameSetup);
        bodyMBB->addSuccessor(bodyMBB);
        bodyMBB->addSuccessor(footMBB);
      }

      // setup loop footer
      {
        BuildMI(footMBB, DL, TII.get(TargetOpcode::COPY), StackPtr)
            .addReg(FinalStackProbed)
            .setMIFlag(MachineInstr::FrameSetup);
        addRegOffset(BuildMI(footMBB, DL, TII.get(MovMIOpc))
                         .setMIFlag(MachineInstr::FrameSetup),
                     StackPtr, false, 0)
            .addImm(0)
            .setMIFlag(MachineInstr::FrameSetup);
        footMBB->addSuccessor(&MBB);
      }

      fullyRecomputeLiveIns({footMBB, bodyMBB, headMBB, &MBB});
    }
  } else {
    MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(AndOp), Reg)
                           .addReg(Reg)
                           .addImm(Val)
                           .setMIFlag(MachineInstr::FrameSetup);

    // The EFLAGS implicit def is dead.
    MI->getOperand(3).setIsDead();
  }
}

// FIXME: Get this from tablegen.
static ArrayRef<MCPhysReg> get64BitArgumentGPRs(CallingConv::ID CallConv,
                                                const X86Subtarget &Subtarget) {
  assert(Subtarget.is64Bit());

  if (Subtarget.isCallingConvWin64(CallConv)) {
    static const MCPhysReg GPR64ArgRegsWin64[] = {
      X86::RCX, X86::RDX, X86::R8,  X86::R9
    };
    return ArrayRef(std::begin(GPR64ArgRegsWin64), std::end(GPR64ArgRegsWin64));
  }

  static const MCPhysReg GPR64ArgRegs64Bit[] = {
    X86::RDI, X86::RSI, X86::RDX, X86::RCX, X86::R8, X86::R9
  };
  return ArrayRef(std::begin(GPR64ArgRegs64Bit), std::end(GPR64ArgRegs64Bit));
}

bool X86FrameLowering::has128ByteRedZone(const MachineFunction &MF) const {
  // x86-64 (non Win64) has a 128 byte red zone which is guaranteed not to be
  // clobbered by any interrupt handler.
  assert(&STI == &MF.getSubtarget<X86Subtarget>() &&
         "MF used frame lowering for wrong subtarget");
  const Function &Fn = MF.getFunction();
  const bool IsWin64CC = STI.isCallingConvWin64(Fn.getCallingConv());
  return Is64Bit && !IsWin64CC && !Fn.hasFnAttribute(Attribute::NoRedZone);
}

/// Return true if we need to use the restricted Windows x64 prologue and
/// epilogue code patterns that can be described with WinCFI (.seh_*
/// directives).
bool X86FrameLowering::isWin64Prologue(const MachineFunction &MF) const {
  return MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
}

bool X86FrameLowering::needsDwarfCFI(const MachineFunction &MF) const {
  return !isWin64Prologue(MF) && MF.needsFrameMoves();
}

/// Return true if an opcode is part of the REP group of instructions
static bool isOpcodeRep(unsigned Opcode) {
  switch (Opcode) {
  case X86::REPNE_PREFIX:
  case X86::REP_MOVSB_32:
  case X86::REP_MOVSB_64:
  case X86::REP_MOVSD_32:
  case X86::REP_MOVSD_64:
  case X86::REP_MOVSQ_32:
  case X86::REP_MOVSQ_64:
  case X86::REP_MOVSW_32:
  case X86::REP_MOVSW_64:
  case X86::REP_PREFIX:
  case X86::REP_STOSB_32:
  case X86::REP_STOSB_64:
  case X86::REP_STOSD_32:
  case X86::REP_STOSD_64:
  case X86::REP_STOSQ_32:
  case X86::REP_STOSQ_64:
  case X86::REP_STOSW_32:
  case X86::REP_STOSW_64:
    return true;
  default:
    break;
  }
  return false;
}

/// emitPrologue - Push callee-saved registers onto the stack, which
/// automatically adjust the stack pointer. Adjust the stack pointer to allocate
/// space for local variables. Also emit labels used by the exception handler to
/// generate the exception handling frames.

/*
  Here's a gist of what gets emitted:

  ; Establish frame pointer, if needed
  [if needs FP]
      push  %rbp
      .cfi_def_cfa_offset 16
      .cfi_offset %rbp, -16
      .seh_pushreg %rpb
      mov  %rsp, %rbp
      .cfi_def_cfa_register %rbp

  ; Spill general-purpose registers
  [for all callee-saved GPRs]
      pushq %<reg>
      [if not needs FP]
         .cfi_def_cfa_offset (offset from RETADDR)
      .seh_pushreg %<reg>

  ; If the required stack alignment > default stack alignment
  ; rsp needs to be re-aligned.  This creates a "re-alignment gap"
  ; of unknown size in the stack frame.
  [if stack needs re-alignment]
      and  $MASK, %rsp

  ; Allocate space for locals
  [if target is Windows and allocated space > 4096 bytes]
      ; Windows needs special care for allocations larger
      ; than one page.
      mov $NNN, %rax
      call ___chkstk_ms/___chkstk
      sub  %rax, %rsp
  [else]
      sub  $NNN, %rsp

  [if needs FP]
      .seh_stackalloc (size of XMM spill slots)
      .seh_setframe %rbp, SEHFrameOffset ; = size of all spill slots
  [else]
      .seh_stackalloc NNN

  ; Spill XMMs
  ; Note, that while only Windows 64 ABI specifies XMMs as callee-preserved,
  ; they may get spilled on any platform, if the current function
  ; calls @llvm.eh.unwind.init
  [if needs FP]
      [for all callee-saved XMM registers]
          movaps  %<xmm reg>, -MMM(%rbp)
      [for all callee-saved XMM registers]
          .seh_savexmm %<xmm reg>, (-MMM + SEHFrameOffset)
              ; i.e. the offset relative to (%rbp - SEHFrameOffset)
  [else]
      [for all callee-saved XMM registers]
          movaps  %<xmm reg>, KKK(%rsp)
      [for all callee-saved XMM registers]
          .seh_savexmm %<xmm reg>, KKK

  .seh_endprologue

  [if needs base pointer]
      mov  %rsp, %rbx
      [if needs to restore base pointer]
          mov %rsp, -MMM(%rbp)

  ; Emit CFI info
  [if needs FP]
      [for all callee-saved registers]
          .cfi_offset %<reg>, (offset from %rbp)
  [else]
       .cfi_def_cfa_offset (offset from RETADDR)
      [for all callee-saved registers]
          .cfi_offset %<reg>, (offset from %rsp)

  Notes:
  - .seh directives are emitted only for Windows 64 ABI
  - .cv_fpo directives are emitted on win32 when emitting CodeView
  - .cfi directives are emitted for all other ABIs
  - for 32-bit code, substitute %e?? registers for %r??
*/

void X86FrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  assert(&STI == &MF.getSubtarget<X86Subtarget>() &&
         "MF used frame lowering for wrong subtarget");
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const Function &Fn = MF.getFunction();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  uint64_t MaxAlign = calculateMaxStackAlign(MF); // Desired stack alignment.
  uint64_t StackSize = MFI.getStackSize(); // Number of bytes to allocate.
  bool IsFunclet = MBB.isEHFuncletEntry();
  EHPersonality Personality = EHPersonality::Unknown;
  if (Fn.hasPersonalityFn())
    Personality = classifyEHPersonality(Fn.getPersonalityFn());
  bool FnHasClrFunclet =
      MF.hasEHFunclets() && Personality == EHPersonality::CoreCLR;
  bool IsClrFunclet = IsFunclet && FnHasClrFunclet;
  bool HasFP = hasFP(MF);
  bool IsWin64Prologue = isWin64Prologue(MF);
  bool NeedsWin64CFI = IsWin64Prologue && Fn.needsUnwindTableEntry();
  // FIXME: Emit FPO data for EH funclets.
  bool NeedsWinFPO = !IsFunclet && STI.isTargetWin32() &&
                     MF.getFunction().getParent()->getCodeViewFlag();
  bool NeedsWinCFI = NeedsWin64CFI || NeedsWinFPO;
  bool NeedsDwarfCFI = needsDwarfCFI(MF);
  Register FramePtr = TRI->getFrameRegister(MF);
  const Register MachineFramePtr =
      STI.isTarget64BitILP32() ? Register(getX86SubSuperRegister(FramePtr, 64))
                               : FramePtr;
  Register BasePtr = TRI->getBaseRegister();
  bool HasWinCFI = false;

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;
  Register ArgBaseReg;

  // Emit extra prolog for argument stack slot reference.
  if (auto *MI = X86FI->getStackPtrSaveMI()) {
    // MI is lea instruction that created in X86ArgumentStackSlotPass.
    // Creat extra prolog for stack realignment.
    ArgBaseReg = MI->getOperand(0).getReg();
    // leal    4(%esp), %basereg
    // .cfi_def_cfa %basereg, 0
    // andl    $-128, %esp
    // pushl   -4(%basereg)
    BuildMI(MBB, MBBI, DL, TII.get(Is64Bit ? X86::LEA64r : X86::LEA32r),
            ArgBaseReg)
        .addUse(StackPtr)
        .addImm(1)
        .addUse(X86::NoRegister)
        .addImm(SlotSize)
        .addUse(X86::NoRegister)
        .setMIFlag(MachineInstr::FrameSetup);
    if (NeedsDwarfCFI) {
      // .cfi_def_cfa %basereg, 0
      unsigned DwarfStackPtr = TRI->getDwarfRegNum(ArgBaseReg, true);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfa(nullptr, DwarfStackPtr, 0),
               MachineInstr::FrameSetup);
    }
    BuildStackAlignAND(MBB, MBBI, DL, StackPtr, MaxAlign);
    int64_t Offset = -(int64_t)SlotSize;
    BuildMI(MBB, MBBI, DL, TII.get(Is64Bit ? X86::PUSH64rmm : X86::PUSH32rmm))
        .addReg(ArgBaseReg)
        .addImm(1)
        .addReg(X86::NoRegister)
        .addImm(Offset)
        .addReg(X86::NoRegister)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Space reserved for stack-based arguments when making a (ABI-guaranteed)
  // tail call.
  unsigned TailCallArgReserveSize = -X86FI->getTCReturnAddrDelta();
  if (TailCallArgReserveSize && IsWin64Prologue)
    report_fatal_error("Can't handle guaranteed tail call under win64 yet");

  const bool EmitStackProbeCall =
      STI.getTargetLowering()->hasStackProbeSymbol(MF);
  unsigned StackProbeSize = STI.getTargetLowering()->getStackProbeSize(MF);

  if (HasFP && X86FI->hasSwiftAsyncContext()) {
    switch (MF.getTarget().Options.SwiftAsyncFramePointer) {
    case SwiftAsyncFramePointerMode::DeploymentBased:
      if (STI.swiftAsyncContextIsDynamicallySet()) {
        // The special symbol below is absolute and has a *value* suitable to be
        // combined with the frame pointer directly.
        BuildMI(MBB, MBBI, DL, TII.get(X86::OR64rm), MachineFramePtr)
            .addUse(MachineFramePtr)
            .addUse(X86::RIP)
            .addImm(1)
            .addUse(X86::NoRegister)
            .addExternalSymbol("swift_async_extendedFramePointerFlags",
                               X86II::MO_GOTPCREL)
            .addUse(X86::NoRegister);
        break;
      }
      [[fallthrough]];

    case SwiftAsyncFramePointerMode::Always:
      assert(
          !IsWin64Prologue &&
          "win64 prologue does not set the bit 60 in the saved frame pointer");
      BuildMI(MBB, MBBI, DL, TII.get(X86::BTS64ri8), MachineFramePtr)
          .addUse(MachineFramePtr)
          .addImm(60)
          .setMIFlag(MachineInstr::FrameSetup);
      break;

    case SwiftAsyncFramePointerMode::Never:
      break;
    }
  }

  // Re-align the stack on 64-bit if the x86-interrupt calling convention is
  // used and an error code was pushed, since the x86-64 ABI requires a 16-byte
  // stack alignment.
  if (Fn.getCallingConv() == CallingConv::X86_INTR && Is64Bit &&
      Fn.arg_size() == 2) {
    StackSize += 8;
    MFI.setStackSize(StackSize);

    // Update the stack pointer by pushing a register. This is the instruction
    // emitted that would be end up being emitted by a call to `emitSPUpdate`.
    // Hard-coding the update to a push avoids emitting a second
    // `STACKALLOC_W_PROBING` instruction in the save block: We know that stack
    // probing isn't needed anyways for an 8-byte update.
    // Pushing a register leaves us in a similar situation to a regular
    // function call where we know that the address at (rsp-8) is writeable.
    // That way we avoid any off-by-ones with stack probing for additional
    // stack pointer updates later on.
    BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64r))
        .addReg(X86::RAX, RegState::Undef)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // If this is x86-64 and the Red Zone is not disabled, if we are a leaf
  // function, and use up to 128 bytes of stack space, don't have a frame
  // pointer, calls, or dynamic alloca then we do not need to adjust the
  // stack pointer (we fit in the Red Zone). We also check that we don't
  // push and pop from the stack.
  if (has128ByteRedZone(MF) && !TRI->hasStackRealignment(MF) &&
      !MFI.hasVarSizedObjects() &&             // No dynamic alloca.
      !MFI.adjustsStack() &&                   // No calls.
      !EmitStackProbeCall &&                   // No stack probes.
      !MFI.hasCopyImplyingStackAdjustment() && // Don't push and pop.
      !MF.shouldSplitStack()) {                // Regular stack
    uint64_t MinSize =
        X86FI->getCalleeSavedFrameSize() - X86FI->getTCReturnAddrDelta();
    if (HasFP)
      MinSize += SlotSize;
    X86FI->setUsesRedZone(MinSize > 0 || StackSize > 0);
    StackSize = std::max(MinSize, StackSize > 128 ? StackSize - 128 : 0);
    MFI.setStackSize(StackSize);
  }

  // Insert stack pointer adjustment for later moving of return addr.  Only
  // applies to tail call optimized functions where the callee argument stack
  // size is bigger than the callers.
  if (TailCallArgReserveSize != 0) {
    BuildStackAdjustment(MBB, MBBI, DL, -(int)TailCallArgReserveSize,
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

  // Find the funclet establisher parameter
  Register Establisher = X86::NoRegister;
  if (IsClrFunclet)
    Establisher = Uses64BitFramePtr ? X86::RCX : X86::ECX;
  else if (IsFunclet)
    Establisher = Uses64BitFramePtr ? X86::RDX : X86::EDX;

  if (IsWin64Prologue && IsFunclet && !IsClrFunclet) {
    // Immediately spill establisher into the home slot.
    // The runtime cares about this.
    // MOV64mr %rdx, 16(%rsp)
    unsigned MOVmr = Uses64BitFramePtr ? X86::MOV64mr : X86::MOV32mr;
    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(MOVmr)), StackPtr, true, 16)
        .addReg(Establisher)
        .setMIFlag(MachineInstr::FrameSetup);
    MBB.addLiveIn(Establisher);
  }

  if (HasFP) {
    assert(MF.getRegInfo().isReserved(MachineFramePtr) && "FP reserved");

    // Calculate required stack adjustment.
    uint64_t FrameSize = StackSize - SlotSize;
    NumBytes =
        FrameSize - (X86FI->getCalleeSavedFrameSize() + TailCallArgReserveSize);

    // Callee-saved registers are pushed on stack before the stack is realigned.
    if (TRI->hasStackRealignment(MF) && !IsWin64Prologue)
      NumBytes = alignTo(NumBytes, MaxAlign);

    // Save EBP/RBP into the appropriate stack slot.
    BuildMI(MBB, MBBI, DL,
            TII.get(getPUSHOpcode(MF.getSubtarget<X86Subtarget>())))
        .addReg(MachineFramePtr, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);

    if (NeedsDwarfCFI && !ArgBaseReg.isValid()) {
      // Mark the place where EBP/RBP was saved.
      // Define the current CFA rule to use the provided offset.
      assert(StackSize);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfaOffset(
                   nullptr, -2 * stackGrowth + (int)TailCallArgReserveSize),
               MachineInstr::FrameSetup);

      // Change the rule for the FramePtr to be an "offset" rule.
      unsigned DwarfFramePtr = TRI->getDwarfRegNum(MachineFramePtr, true);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::createOffset(nullptr, DwarfFramePtr,
                                              2 * stackGrowth -
                                                  (int)TailCallArgReserveSize),
               MachineInstr::FrameSetup);
    }

    if (NeedsWinCFI) {
      HasWinCFI = true;
      BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_PushReg))
          .addImm(FramePtr)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    if (!IsFunclet) {
      if (X86FI->hasSwiftAsyncContext()) {
        assert(!IsWin64Prologue &&
               "win64 prologue does not store async context right below rbp");
        const auto &Attrs = MF.getFunction().getAttributes();

        // Before we update the live frame pointer we have to ensure there's a
        // valid (or null) asynchronous context in its slot just before FP in
        // the frame record, so store it now.
        if (Attrs.hasAttrSomewhere(Attribute::SwiftAsync)) {
          // We have an initial context in r14, store it just before the frame
          // pointer.
          MBB.addLiveIn(X86::R14);
          BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64r))
              .addReg(X86::R14)
              .setMIFlag(MachineInstr::FrameSetup);
        } else {
          // No initial context, store null so that there's no pointer that
          // could be misused.
          BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64i32))
              .addImm(0)
              .setMIFlag(MachineInstr::FrameSetup);
        }

        if (NeedsWinCFI) {
          HasWinCFI = true;
          BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_PushReg))
              .addImm(X86::R14)
              .setMIFlag(MachineInstr::FrameSetup);
        }

        BuildMI(MBB, MBBI, DL, TII.get(X86::LEA64r), FramePtr)
            .addUse(X86::RSP)
            .addImm(1)
            .addUse(X86::NoRegister)
            .addImm(8)
            .addUse(X86::NoRegister)
            .setMIFlag(MachineInstr::FrameSetup);
        BuildMI(MBB, MBBI, DL, TII.get(X86::SUB64ri32), X86::RSP)
            .addUse(X86::RSP)
            .addImm(8)
            .setMIFlag(MachineInstr::FrameSetup);
      }

      if (!IsWin64Prologue && !IsFunclet) {
        // Update EBP with the new base value.
        if (!X86FI->hasSwiftAsyncContext())
          BuildMI(MBB, MBBI, DL,
                  TII.get(Uses64BitFramePtr ? X86::MOV64rr : X86::MOV32rr),
                  FramePtr)
              .addReg(StackPtr)
              .setMIFlag(MachineInstr::FrameSetup);

      if (SaveArgs && !Fn.arg_empty()) {
        ArrayRef<MCPhysReg> GPRs =
          get64BitArgumentGPRs(Fn.getCallingConv(), STI);
        unsigned arg_size = Fn.arg_size();
        unsigned RI = 0;
        int64_t SaveSize = 0;

        if (Fn.hasStructRetAttr()) {
          GPRs = GPRs.drop_front(1);
          arg_size--;
        }

        for (MCPhysReg Reg : GPRs) {
          if (++RI > arg_size)
            break;

          SaveSize += SlotSize;

          BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64r))
            .addReg(Reg)
            .setMIFlag(MachineInstr::FrameSetup);
        }

        // Realign the stack. PUSHes are the most space efficient.
        while (SaveSize % getStackAlignment()) {
          BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64r))
            .addReg(GPRs.front())
            .setMIFlag(MachineInstr::FrameSetup);

          SaveSize += SlotSize;
        }

        //dlg StackSize -= SaveSize;
        //dlg MFI.setStackSize(StackSize);
        X86FI->setSaveArgSize(SaveSize);
      }

        if (NeedsDwarfCFI) {
          if (ArgBaseReg.isValid()) {
            SmallString<64> CfaExpr;
            CfaExpr.push_back(dwarf::DW_CFA_expression);
            uint8_t buffer[16];
            unsigned DwarfReg = TRI->getDwarfRegNum(MachineFramePtr, true);
            CfaExpr.append(buffer, buffer + encodeULEB128(DwarfReg, buffer));
            CfaExpr.push_back(2);
            CfaExpr.push_back((uint8_t)(dwarf::DW_OP_breg0 + DwarfReg));
            CfaExpr.push_back(0);
            // DW_CFA_expression: reg5 DW_OP_breg5 +0
            BuildCFI(MBB, MBBI, DL,
                     MCCFIInstruction::createEscape(nullptr, CfaExpr.str()),
                     MachineInstr::FrameSetup);
          } else {
            // Mark effective beginning of when frame pointer becomes valid.
            // Define the current CFA to use the EBP/RBP register.
            unsigned DwarfFramePtr = TRI->getDwarfRegNum(MachineFramePtr, true);
            BuildCFI(
                MBB, MBBI, DL,
                MCCFIInstruction::createDefCfaRegister(nullptr, DwarfFramePtr),
                MachineInstr::FrameSetup);
          }
        }

        if (NeedsWinFPO) {
          // .cv_fpo_setframe $FramePtr
          HasWinCFI = true;
          BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_SetFrame))
              .addImm(FramePtr)
              .addImm(0)
              .setMIFlag(MachineInstr::FrameSetup);
        }
      }
    }
  } else {
    assert(!IsFunclet && "funclets without FPs not yet implemented");
    NumBytes =
        StackSize - (X86FI->getCalleeSavedFrameSize() + TailCallArgReserveSize);
  }

  // Update the offset adjustment, which is mainly used by codeview to translate
  // from ESP to VFRAME relative local variable offsets.
  if (!IsFunclet) {
    if (HasFP && TRI->hasStackRealignment(MF))
      MFI.setOffsetAdjustment(-NumBytes);
    else
      MFI.setOffsetAdjustment(-StackSize);
  }

  // For EH funclets, only allocate enough space for outgoing calls. Save the
  // NumBytes value that we would've used for the parent frame.
  unsigned ParentFrameNumBytes = NumBytes;
  if (IsFunclet)
    NumBytes = getWinEHFuncletFrameSize(MF);

  // Skip the callee-saved push instructions.
  bool PushedRegs = false;
  int StackOffset = 2 * stackGrowth;
  MachineBasicBlock::const_iterator LastCSPush = MBBI;
  auto IsCSPush = [&](const MachineBasicBlock::iterator &MBBI) {
    if (MBBI == MBB.end() || !MBBI->getFlag(MachineInstr::FrameSetup))
      return false;
    unsigned Opc = MBBI->getOpcode();
    return Opc == X86::PUSH32r || Opc == X86::PUSH64r || Opc == X86::PUSHP64r ||
           Opc == X86::PUSH2 || Opc == X86::PUSH2P;
  };

  while (IsCSPush(MBBI)) {
    PushedRegs = true;
    Register Reg = MBBI->getOperand(0).getReg();
    LastCSPush = MBBI;
    ++MBBI;
    unsigned Opc = LastCSPush->getOpcode();

    if (!HasFP && NeedsDwarfCFI) {
      // Mark callee-saved push instruction.
      // Define the current CFA rule to use the provided offset.
      assert(StackSize);
      // Compared to push, push2 introduces more stack offset (one more
      // register).
      if (Opc == X86::PUSH2 || Opc == X86::PUSH2P)
        StackOffset += stackGrowth;
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfaOffset(nullptr, -StackOffset),
               MachineInstr::FrameSetup);
      StackOffset += stackGrowth;
    }

    if (NeedsWinCFI) {
      HasWinCFI = true;
      BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_PushReg))
          .addImm(Reg)
          .setMIFlag(MachineInstr::FrameSetup);
      if (Opc == X86::PUSH2 || Opc == X86::PUSH2P)
        BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_PushReg))
            .addImm(LastCSPush->getOperand(1).getReg())
            .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // Realign stack after we pushed callee-saved registers (so that we'll be
  // able to calculate their offsets from the frame pointer).
  // Don't do this for Win64, it needs to realign the stack after the prologue.
  if (!IsWin64Prologue && !IsFunclet && TRI->hasStackRealignment(MF) &&
      !ArgBaseReg.isValid()) {
    assert(HasFP && "There should be a frame pointer if stack is realigned.");
    BuildStackAlignAND(MBB, MBBI, DL, StackPtr, MaxAlign);

    if (NeedsWinCFI) {
      HasWinCFI = true;
      BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_StackAlign))
          .addImm(MaxAlign)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // If there is an SUB32ri of ESP immediately before this instruction, merge
  // the two. This can be the case when tail call elimination is enabled and
  // the callee has more arguments then the caller.
  NumBytes -= mergeSPUpdates(MBB, MBBI, true);

  // Adjust stack pointer: ESP -= numbytes.

  // Windows and cygwin/mingw require a prologue helper routine when allocating
  // more than 4K bytes on the stack.  Windows uses __chkstk and cygwin/mingw
  // uses __alloca.  __alloca and the 32-bit version of __chkstk will probe the
  // stack and adjust the stack pointer in one go.  The 64-bit version of
  // __chkstk is only responsible for probing the stack.  The 64-bit prologue is
  // responsible for adjusting the stack pointer.  Touching the stack at 4K
  // increments is necessary to ensure that the guard pages used by the OS
  // virtual memory manager are allocated in correct sequence.
  uint64_t AlignedNumBytes = NumBytes;
  if (IsWin64Prologue && !IsFunclet && TRI->hasStackRealignment(MF))
    AlignedNumBytes = alignTo(AlignedNumBytes, MaxAlign);
  if (AlignedNumBytes >= StackProbeSize && EmitStackProbeCall) {
    assert(!X86FI->getUsesRedZone() &&
           "The Red Zone is not accounted for in stack probes");

    // Check whether EAX is livein for this block.
    bool isEAXAlive = isEAXLiveIn(MBB);

    if (isEAXAlive) {
      if (Is64Bit) {
        // Save RAX
        BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH64r))
            .addReg(X86::RAX, RegState::Kill)
            .setMIFlag(MachineInstr::FrameSetup);
      } else {
        // Save EAX
        BuildMI(MBB, MBBI, DL, TII.get(X86::PUSH32r))
            .addReg(X86::EAX, RegState::Kill)
            .setMIFlag(MachineInstr::FrameSetup);
      }
    }

    if (Is64Bit) {
      // Handle the 64-bit Windows ABI case where we need to call __chkstk.
      // Function prologue is responsible for adjusting the stack pointer.
      int64_t Alloc = isEAXAlive ? NumBytes - 8 : NumBytes;
      BuildMI(MBB, MBBI, DL, TII.get(getMOVriOpcode(Is64Bit, Alloc)), X86::RAX)
          .addImm(Alloc)
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      // Allocate NumBytes-4 bytes on stack in case of isEAXAlive.
      // We'll also use 4 already allocated bytes for EAX.
      BuildMI(MBB, MBBI, DL, TII.get(X86::MOV32ri), X86::EAX)
          .addImm(isEAXAlive ? NumBytes - 4 : NumBytes)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    // Call __chkstk, __chkstk_ms, or __alloca.
    emitStackProbe(MF, MBB, MBBI, DL, true);

    if (isEAXAlive) {
      // Restore RAX/EAX
      MachineInstr *MI;
      if (Is64Bit)
        MI = addRegOffset(BuildMI(MF, DL, TII.get(X86::MOV64rm), X86::RAX),
                          StackPtr, false, NumBytes - 8);
      else
        MI = addRegOffset(BuildMI(MF, DL, TII.get(X86::MOV32rm), X86::EAX),
                          StackPtr, false, NumBytes - 4);
      MI->setFlag(MachineInstr::FrameSetup);
      MBB.insert(MBBI, MI);
    }
  } else if (NumBytes) {
    emitSPUpdate(MBB, MBBI, DL, -(int64_t)NumBytes, /*InEpilogue=*/false);
  }

  if (NeedsWinCFI && NumBytes) {
    HasWinCFI = true;
    BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_StackAlloc))
        .addImm(NumBytes)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  int SEHFrameOffset = 0;
  unsigned SPOrEstablisher;
  if (IsFunclet) {
    if (IsClrFunclet) {
      // The establisher parameter passed to a CLR funclet is actually a pointer
      // to the (mostly empty) frame of its nearest enclosing funclet; we have
      // to find the root function establisher frame by loading the PSPSym from
      // the intermediate frame.
      unsigned PSPSlotOffset = getPSPSlotOffsetFromSP(MF);
      MachinePointerInfo NoInfo;
      MBB.addLiveIn(Establisher);
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64rm), Establisher),
                   Establisher, false, PSPSlotOffset)
          .addMemOperand(MF.getMachineMemOperand(
              NoInfo, MachineMemOperand::MOLoad, SlotSize, Align(SlotSize)));
      ;
      // Save the root establisher back into the current funclet's (mostly
      // empty) frame, in case a sub-funclet or the GC needs it.
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64mr)), StackPtr,
                   false, PSPSlotOffset)
          .addReg(Establisher)
          .addMemOperand(MF.getMachineMemOperand(
              NoInfo,
              MachineMemOperand::MOStore | MachineMemOperand::MOVolatile,
              SlotSize, Align(SlotSize)));
    }
    SPOrEstablisher = Establisher;
  } else {
    SPOrEstablisher = StackPtr;
  }

  if (IsWin64Prologue && HasFP) {
    // Set RBP to a small fixed offset from RSP. In the funclet case, we base
    // this calculation on the incoming establisher, which holds the value of
    // RSP from the parent frame at the end of the prologue.
    SEHFrameOffset = calculateSetFPREG(ParentFrameNumBytes);
    if (SEHFrameOffset)
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::LEA64r), FramePtr),
                   SPOrEstablisher, false, SEHFrameOffset);
    else
      BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64rr), FramePtr)
          .addReg(SPOrEstablisher);

    // If this is not a funclet, emit the CFI describing our frame pointer.
    if (NeedsWinCFI && !IsFunclet) {
      assert(!NeedsWinFPO && "this setframe incompatible with FPO data");
      HasWinCFI = true;
      BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_SetFrame))
          .addImm(FramePtr)
          .addImm(SEHFrameOffset)
          .setMIFlag(MachineInstr::FrameSetup);
      if (isAsynchronousEHPersonality(Personality))
        MF.getWinEHFuncInfo()->SEHSetFrameOffset = SEHFrameOffset;
    }
  } else if (IsFunclet && STI.is32Bit()) {
    // Reset EBP / ESI to something good for funclets.
    MBBI = restoreWin32EHStackPointers(MBB, MBBI, DL);
    // If we're a catch funclet, we can be returned to via catchret. Save ESP
    // into the registration node so that the runtime will restore it for us.
    if (!MBB.isCleanupFuncletEntry()) {
      assert(Personality == EHPersonality::MSVC_CXX);
      Register FrameReg;
      int FI = MF.getWinEHFuncInfo()->EHRegNodeFrameIndex;
      int64_t EHRegOffset = getFrameIndexReference(MF, FI, FrameReg).getFixed();
      // ESP is the first field, so no extra displacement is needed.
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV32mr)), FrameReg,
                   false, EHRegOffset)
          .addReg(X86::ESP);
    }
  }

  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup)) {
    const MachineInstr &FrameInstr = *MBBI;
    ++MBBI;

    if (NeedsWinCFI) {
      int FI;
      if (Register Reg = TII.isStoreToStackSlot(FrameInstr, FI)) {
        if (X86::FR64RegClass.contains(Reg)) {
          int Offset;
          Register IgnoredFrameReg;
          if (IsWin64Prologue && IsFunclet)
            Offset = getWin64EHFrameIndexRef(MF, FI, IgnoredFrameReg);
          else
            Offset =
                getFrameIndexReference(MF, FI, IgnoredFrameReg).getFixed() +
                SEHFrameOffset;

          HasWinCFI = true;
          assert(!NeedsWinFPO && "SEH_SaveXMM incompatible with FPO data");
          BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_SaveXMM))
              .addImm(Reg)
              .addImm(Offset)
              .setMIFlag(MachineInstr::FrameSetup);
        }
      }
    }
  }

  if (NeedsWinCFI && HasWinCFI)
    BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_EndPrologue))
        .setMIFlag(MachineInstr::FrameSetup);

  if (FnHasClrFunclet && !IsFunclet) {
    // Save the so-called Initial-SP (i.e. the value of the stack pointer
    // immediately after the prolog)  into the PSPSlot so that funclets
    // and the GC can recover it.
    unsigned PSPSlotOffset = getPSPSlotOffsetFromSP(MF);
    auto PSPInfo = MachinePointerInfo::getFixedStack(
        MF, MF.getWinEHFuncInfo()->PSPSymFrameIdx);
    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64mr)), StackPtr, false,
                 PSPSlotOffset)
        .addReg(StackPtr)
        .addMemOperand(MF.getMachineMemOperand(
            PSPInfo, MachineMemOperand::MOStore | MachineMemOperand::MOVolatile,
            SlotSize, Align(SlotSize)));
  }

  // Realign stack after we spilled callee-saved registers (so that we'll be
  // able to calculate their offsets from the frame pointer).
  // Win64 requires aligning the stack after the prologue.
  if (IsWin64Prologue && TRI->hasStackRealignment(MF)) {
    assert(HasFP && "There should be a frame pointer if stack is realigned.");
    BuildStackAlignAND(MBB, MBBI, DL, SPOrEstablisher, MaxAlign);
  }

  // We already dealt with stack realignment and funclets above.
  if (IsFunclet && STI.is32Bit())
    return;

  // If we need a base pointer, set it up here. It's whatever the value
  // of the stack pointer is at this point. Any variable size objects
  // will be allocated after this, so we can still use the base pointer
  // to reference locals.
  if (TRI->hasBasePointer(MF)) {
    // Update the base pointer with the current stack pointer.
    unsigned Opc = Uses64BitFramePtr ? X86::MOV64rr : X86::MOV32rr;
    BuildMI(MBB, MBBI, DL, TII.get(Opc), BasePtr)
        .addReg(SPOrEstablisher)
        .setMIFlag(MachineInstr::FrameSetup);
    if (X86FI->getRestoreBasePointer()) {
      // Stash value of base pointer.  Saving RSP instead of EBP shortens
      // dependence chain. Used by SjLj EH.
      unsigned Opm = Uses64BitFramePtr ? X86::MOV64mr : X86::MOV32mr;
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(Opm)), FramePtr, true,
                   X86FI->getRestoreBasePointerOffset())
          .addReg(SPOrEstablisher)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    if (X86FI->getHasSEHFramePtrSave() && !IsFunclet) {
      // Stash the value of the frame pointer relative to the base pointer for
      // Win32 EH. This supports Win32 EH, which does the inverse of the above:
      // it recovers the frame pointer from the base pointer rather than the
      // other way around.
      unsigned Opm = Uses64BitFramePtr ? X86::MOV64mr : X86::MOV32mr;
      Register UsedReg;
      int Offset =
          getFrameIndexReference(MF, X86FI->getSEHFramePtrSaveIndex(), UsedReg)
              .getFixed();
      assert(UsedReg == BasePtr);
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(Opm)), UsedReg, true, Offset)
          .addReg(FramePtr)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }
  if (ArgBaseReg.isValid()) {
    // Save argument base pointer.
    auto *MI = X86FI->getStackPtrSaveMI();
    int FI = MI->getOperand(1).getIndex();
    unsigned MOVmr = Is64Bit ? X86::MOV64mr : X86::MOV32mr;
    // movl    %basereg, offset(%ebp)
    addFrameReference(BuildMI(MBB, MBBI, DL, TII.get(MOVmr)), FI)
        .addReg(ArgBaseReg)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  if (((!HasFP && NumBytes) || PushedRegs) && NeedsDwarfCFI) {
    // Mark end of stack pointer adjustment.
    if (!HasFP && NumBytes) {
      // Define the current CFA rule to use the provided offset.
      assert(StackSize);
      BuildCFI(
          MBB, MBBI, DL,
          MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize - stackGrowth),
          MachineInstr::FrameSetup);
    }

    // Emit DWARF info specifying the offsets of the callee-saved registers.
    emitCalleeSavedFrameMoves(MBB, MBBI, DL, true);
  }

  // X86 Interrupt handling function cannot assume anything about the direction
  // flag (DF in EFLAGS register). Clear this flag by creating "cld" instruction
  // in each prologue of interrupt handler function.
  //
  // Create "cld" instruction only in these cases:
  // 1. The interrupt handling function uses any of the "rep" instructions.
  // 2. Interrupt handling function calls another function.
  // 3. If there are any inline asm blocks, as we do not know what they do
  //
  // TODO: We should also emit cld if we detect the use of std, but as of now,
  // the compiler does not even emit that instruction or even define it, so in
  // practice, this would only happen with inline asm, which we cover anyway.
  if (Fn.getCallingConv() == CallingConv::X86_INTR) {
    bool NeedsCLD = false;

    for (const MachineBasicBlock &B : MF) {
      for (const MachineInstr &MI : B) {
        if (MI.isCall()) {
          NeedsCLD = true;
          break;
        }

        if (isOpcodeRep(MI.getOpcode())) {
          NeedsCLD = true;
          break;
        }

        if (MI.isInlineAsm()) {
          // TODO: Parse asm for rep instructions or call sites?
          // For now, let's play it safe and emit a cld instruction
          // just in case.
          NeedsCLD = true;
          break;
        }
      }
    }

    if (NeedsCLD) {
      BuildMI(MBB, MBBI, DL, TII.get(X86::CLD))
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // At this point we know if the function has WinCFI or not.
  MF.setHasWinCFI(HasWinCFI);
}

bool X86FrameLowering::canUseLEAForSPInEpilogue(
    const MachineFunction &MF) const {
  // We can't use LEA instructions for adjusting the stack pointer if we don't
  // have a frame pointer in the Win64 ABI.  Only ADD instructions may be used
  // to deallocate the stack.
  // This means that we can use LEA for SP in two situations:
  // 1. We *aren't* using the Win64 ABI which means we are free to use LEA.
  // 2. We *have* a frame pointer which means we are permitted to use LEA.
  return !MF.getTarget().getMCAsmInfo()->usesWindowsCFI() || hasFP(MF);
}

static bool isFuncletReturnInstr(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case X86::CATCHRET:
  case X86::CLEANUPRET:
    return true;
  default:
    return false;
  }
  llvm_unreachable("impossible");
}

// CLR funclets use a special "Previous Stack Pointer Symbol" slot on the
// stack. It holds a pointer to the bottom of the root function frame.  The
// establisher frame pointer passed to a nested funclet may point to the
// (mostly empty) frame of its parent funclet, but it will need to find
// the frame of the root function to access locals.  To facilitate this,
// every funclet copies the pointer to the bottom of the root function
// frame into a PSPSym slot in its own (mostly empty) stack frame. Using the
// same offset for the PSPSym in the root function frame that's used in the
// funclets' frames allows each funclet to dynamically accept any ancestor
// frame as its establisher argument (the runtime doesn't guarantee the
// immediate parent for some reason lost to history), and also allows the GC,
// which uses the PSPSym for some bookkeeping, to find it in any funclet's
// frame with only a single offset reported for the entire method.
unsigned
X86FrameLowering::getPSPSlotOffsetFromSP(const MachineFunction &MF) const {
  const WinEHFuncInfo &Info = *MF.getWinEHFuncInfo();
  Register SPReg;
  int Offset = getFrameIndexReferencePreferSP(MF, Info.PSPSymFrameIdx, SPReg,
                                              /*IgnoreSPUpdates*/ true)
                   .getFixed();
  assert(Offset >= 0 && SPReg == TRI->getStackRegister());
  return static_cast<unsigned>(Offset);
}

unsigned
X86FrameLowering::getWinEHFuncletFrameSize(const MachineFunction &MF) const {
  const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  // This is the size of the pushed CSRs.
  unsigned CSSize = X86FI->getCalleeSavedFrameSize();
  // This is the size of callee saved XMMs.
  const auto &WinEHXMMSlotInfo = X86FI->getWinEHXMMSlotInfo();
  unsigned XMMSize =
      WinEHXMMSlotInfo.size() * TRI->getSpillSize(X86::VR128RegClass);
  // This is the amount of stack a funclet needs to allocate.
  unsigned UsedSize;
  EHPersonality Personality =
      classifyEHPersonality(MF.getFunction().getPersonalityFn());
  if (Personality == EHPersonality::CoreCLR) {
    // CLR funclets need to hold enough space to include the PSPSym, at the
    // same offset from the stack pointer (immediately after the prolog) as it
    // resides at in the main function.
    UsedSize = getPSPSlotOffsetFromSP(MF) + SlotSize;
  } else {
    // Other funclets just need enough stack for outgoing call arguments.
    UsedSize = MF.getFrameInfo().getMaxCallFrameSize();
  }
  // RBP is not included in the callee saved register block. After pushing RBP,
  // everything is 16 byte aligned. Everything we allocate before an outgoing
  // call must also be 16 byte aligned.
  unsigned FrameSizeMinusRBP = alignTo(CSSize + UsedSize, getStackAlign());
  // Subtract out the size of the callee saved registers. This is how much stack
  // each funclet will allocate.
  return FrameSizeMinusRBP + XMMSize - CSSize;
}

static bool isTailCallOpcode(unsigned Opc) {
  return Opc == X86::TCRETURNri || Opc == X86::TCRETURNdi ||
         Opc == X86::TCRETURNmi || Opc == X86::TCRETURNri64 ||
         Opc == X86::TCRETURNdi64 || Opc == X86::TCRETURNmi64;
}

void X86FrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  MachineBasicBlock::iterator Terminator = MBB.getFirstTerminator();
  MachineBasicBlock::iterator MBBI = Terminator;
  DebugLoc DL;
  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();
  // standard x86_64 and NaCl use 64-bit frame/stack pointers, x32 - 32-bit.
  const bool Is64BitILP32 = STI.isTarget64BitILP32();
  Register FramePtr = TRI->getFrameRegister(MF);
  Register MachineFramePtr =
      Is64BitILP32 ? Register(getX86SubSuperRegister(FramePtr, 64)) : FramePtr;

  bool IsWin64Prologue = MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
  bool NeedsWin64CFI =
      IsWin64Prologue && MF.getFunction().needsUnwindTableEntry();
  bool IsFunclet = MBBI == MBB.end() ? false : isFuncletReturnInstr(*MBBI);

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t StackSize = MFI.getStackSize();
  uint64_t MaxAlign = calculateMaxStackAlign(MF);
  unsigned CSSize = X86FI->getCalleeSavedFrameSize();
  unsigned TailCallArgReserveSize = -X86FI->getTCReturnAddrDelta();
  bool HasFP = hasFP(MF);
  uint64_t NumBytes = 0;

  bool NeedsDwarfCFI = (!MF.getTarget().getTargetTriple().isOSDarwin() &&
                        !MF.getTarget().getTargetTriple().isOSWindows()) &&
                       MF.needsFrameMoves();

  Register ArgBaseReg;
  if (auto *MI = X86FI->getStackPtrSaveMI()) {
    unsigned Opc = X86::LEA32r;
    Register StackReg = X86::ESP;
    ArgBaseReg = MI->getOperand(0).getReg();
    if (STI.is64Bit()) {
      Opc = X86::LEA64r;
      StackReg = X86::RSP;
    }
    // leal    -4(%basereg), %esp
    // .cfi_def_cfa %esp, 4
    BuildMI(MBB, MBBI, DL, TII.get(Opc), StackReg)
        .addUse(ArgBaseReg)
        .addImm(1)
        .addUse(X86::NoRegister)
        .addImm(-(int64_t)SlotSize)
        .addUse(X86::NoRegister)
        .setMIFlag(MachineInstr::FrameDestroy);
    if (NeedsDwarfCFI) {
      unsigned DwarfStackPtr = TRI->getDwarfRegNum(StackReg, true);
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfa(nullptr, DwarfStackPtr, SlotSize),
               MachineInstr::FrameDestroy);
      --MBBI;
    }
    --MBBI;
  }

  if (IsFunclet) {
    assert(HasFP && "EH funclets without FP not yet implemented");
    NumBytes = getWinEHFuncletFrameSize(MF);
  } else if (HasFP) {
    // Calculate required stack adjustment.
    uint64_t FrameSize = StackSize - SlotSize;
    NumBytes = FrameSize - CSSize - TailCallArgReserveSize;

    // Callee-saved registers were pushed on stack before the stack was
    // realigned.
    if (TRI->hasStackRealignment(MF) && !IsWin64Prologue)
      NumBytes = alignTo(FrameSize, MaxAlign);
  } else {
    NumBytes = StackSize - CSSize - TailCallArgReserveSize;
  }
  uint64_t SEHStackAllocAmt = NumBytes;

  // AfterPop is the position to insert .cfi_restore.
  MachineBasicBlock::iterator AfterPop = MBBI;
  if (HasFP) {
    if (X86FI->hasSwiftAsyncContext()) {
      // Discard the context.
      int Offset = 16 + mergeSPUpdates(MBB, MBBI, true);
      emitSPUpdate(MBB, MBBI, DL, Offset, /*InEpilogue*/ true);
    }
    // Pop EBP.
//    BuildMI(MBB, MBBI, DL,
//            TII.get(getPOPOpcode(MF.getSubtarget<X86Subtarget>())),
//            MachineFramePtr)
//        .setMIFlag(MachineInstr::FrameDestroy);

    if (X86FI->getSaveArgSize()) {
      // LEAVE is effectively mov rbp,rsp; pop rbp
      BuildMI(MBB, MBBI, DL, TII.get(X86::LEAVE64))
        .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      // Pop EBP.
      BuildMI(MBB, MBBI, DL, TII.get(Is64Bit ? X86::POP64r : X86::POP32r),
             MachineFramePtr)
         .setMIFlag(MachineInstr::FrameDestroy);
    }


    // We need to reset FP to its untagged state on return. Bit 60 is currently
    // used to show the presence of an extended frame.
    if (X86FI->hasSwiftAsyncContext()) {
      BuildMI(MBB, MBBI, DL, TII.get(X86::BTR64ri8), MachineFramePtr)
          .addUse(MachineFramePtr)
          .addImm(60)
          .setMIFlag(MachineInstr::FrameDestroy);
    }

    if (NeedsDwarfCFI) {
      if (!ArgBaseReg.isValid()) {
        unsigned DwarfStackPtr =
            TRI->getDwarfRegNum(Is64Bit ? X86::RSP : X86::ESP, true);
        BuildCFI(MBB, MBBI, DL,
                 MCCFIInstruction::cfiDefCfa(nullptr, DwarfStackPtr, SlotSize),
                 MachineInstr::FrameDestroy);
      }
      if (!MBB.succ_empty() && !MBB.isReturnBlock()) {
        unsigned DwarfFramePtr = TRI->getDwarfRegNum(MachineFramePtr, true);
        BuildCFI(MBB, AfterPop, DL,
                 MCCFIInstruction::createRestore(nullptr, DwarfFramePtr),
                 MachineInstr::FrameDestroy);
        --MBBI;
        --AfterPop;
      }
      --MBBI;
    }
  }

  MachineBasicBlock::iterator FirstCSPop = MBBI;
  // Skip the callee-saved pop instructions.
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    unsigned Opc = PI->getOpcode();

    if (Opc != X86::DBG_VALUE && !PI->isTerminator()) {
      if (!PI->getFlag(MachineInstr::FrameDestroy) ||
          (Opc != X86::POP32r && Opc != X86::POP64r && Opc != X86::BTR64ri8 &&
           Opc != X86::ADD64ri32 && Opc != X86::POPP64r && Opc != X86::POP2 &&
           Opc != X86::POP2P && Opc != X86::LEA64r && Opc != X86::LEAVE64))
        break;
      FirstCSPop = PI;
    }

    --MBBI;
  }
  if (ArgBaseReg.isValid()) {
    // Restore argument base pointer.
    auto *MI = X86FI->getStackPtrSaveMI();
    int FI = MI->getOperand(1).getIndex();
    unsigned MOVrm = Is64Bit ? X86::MOV64rm : X86::MOV32rm;
    // movl   offset(%ebp), %basereg
    addFrameReference(BuildMI(MBB, MBBI, DL, TII.get(MOVrm), ArgBaseReg), FI)
        .setMIFlag(MachineInstr::FrameDestroy);
  }
  MBBI = FirstCSPop;

  if (IsFunclet && Terminator->getOpcode() == X86::CATCHRET)
    emitCatchRetReturnValue(MBB, FirstCSPop, &*Terminator);

  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();
  // If there is an ADD32ri or SUB32ri of ESP immediately before this
  // instruction, merge the two instructions.
  if (NumBytes || MFI.hasVarSizedObjects())
    NumBytes += mergeSPUpdates(MBB, MBBI, true);

  // If dynamic alloca is used, then reset esp to point to the last callee-saved
  // slot before popping them off! Same applies for the case, when stack was
  // realigned. Don't do this if this was a funclet epilogue, since the funclets
  // will not do realignment or dynamic stack allocation.
  if (((TRI->hasStackRealignment(MF)) || MFI.hasVarSizedObjects()) &&
      !IsFunclet) {
    if (TRI->hasStackRealignment(MF))
      MBBI = FirstCSPop;
    unsigned SEHFrameOffset = calculateSetFPREG(SEHStackAllocAmt);
    uint64_t LEAAmount =
        IsWin64Prologue ? SEHStackAllocAmt - SEHFrameOffset : -CSSize;

    if (X86FI->hasSwiftAsyncContext())
      LEAAmount -= 16;

    // There are only two legal forms of epilogue:
    // - add SEHAllocationSize, %rsp
    // - lea SEHAllocationSize(%FramePtr), %rsp
    //
    // 'mov %FramePtr, %rsp' will not be recognized as an epilogue sequence.
    // However, we may use this sequence if we have a frame pointer because the
    // effects of the prologue can safely be undone.
    if (LEAAmount != 0) {
      unsigned Opc = getLEArOpcode(Uses64BitFramePtr);
      addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(Opc), StackPtr), FramePtr,
                   false, LEAAmount);
      --MBBI;
    } else {
      unsigned Opc = (Uses64BitFramePtr ? X86::MOV64rr : X86::MOV32rr);
      BuildMI(MBB, MBBI, DL, TII.get(Opc), StackPtr).addReg(FramePtr);
      --MBBI;
    }
  } else if (NumBytes) {
    // Adjust stack pointer back: ESP += numbytes.
    emitSPUpdate(MBB, MBBI, DL, NumBytes, /*InEpilogue=*/true);
    if (!HasFP && NeedsDwarfCFI) {
      // Define the current CFA rule to use the provided offset.
      BuildCFI(MBB, MBBI, DL,
               MCCFIInstruction::cfiDefCfaOffset(
                   nullptr, CSSize + TailCallArgReserveSize + SlotSize),
               MachineInstr::FrameDestroy);
    }
    --MBBI;
  }

  // Windows unwinder will not invoke function's exception handler if IP is
  // either in prologue or in epilogue.  This behavior causes a problem when a
  // call immediately precedes an epilogue, because the return address points
  // into the epilogue.  To cope with that, we insert an epilogue marker here,
  // then replace it with a 'nop' if it ends up immediately after a CALL in the
  // final emitted code.
  if (NeedsWin64CFI && MF.hasWinCFI())
    BuildMI(MBB, MBBI, DL, TII.get(X86::SEH_Epilogue));

  if (!HasFP && NeedsDwarfCFI) {
    MBBI = FirstCSPop;
    int64_t Offset = -(int64_t)CSSize - SlotSize;
    // Mark callee-saved pop instruction.
    // Define the current CFA rule to use the provided offset.
    while (MBBI != MBB.end()) {
      MachineBasicBlock::iterator PI = MBBI;
      unsigned Opc = PI->getOpcode();
      ++MBBI;
      if (Opc == X86::POP32r || Opc == X86::POP64r || Opc == X86::POPP64r ||
          Opc == X86::POP2 || Opc == X86::POP2P) {
        Offset += SlotSize;
        // Compared to pop, pop2 introduces more stack offset (one more
        // register).
        if (Opc == X86::POP2 || Opc == X86::POP2P)
          Offset += SlotSize;
        BuildCFI(MBB, MBBI, DL,
                 MCCFIInstruction::cfiDefCfaOffset(nullptr, -Offset),
                 MachineInstr::FrameDestroy);
      }
    }
  }

  // Emit DWARF info specifying the restores of the callee-saved registers.
  // For epilogue with return inside or being other block without successor,
  // no need to generate .cfi_restore for callee-saved registers.
  if (NeedsDwarfCFI && !MBB.succ_empty())
    emitCalleeSavedFrameMoves(MBB, AfterPop, DL, false);

  if (Terminator == MBB.end() || !isTailCallOpcode(Terminator->getOpcode())) {
    // Add the return addr area delta back since we are not tail calling.
    int Offset = -1 * X86FI->getTCReturnAddrDelta();
    assert(Offset >= 0 && "TCDelta should never be positive");
    if (Offset) {
      // Check for possible merge with preceding ADD instruction.
      Offset += mergeSPUpdates(MBB, Terminator, true);
      emitSPUpdate(MBB, Terminator, DL, Offset, /*InEpilogue=*/true);
    }
  }

  // Emit tilerelease for AMX kernel.
  if (X86FI->getAMXProgModel() == AMXProgModelEnum::ManagedRA)
    BuildMI(MBB, Terminator, DL, TII.get(X86::TILERELEASE));
}

StackOffset X86FrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                                     int FI,
                                                     Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  bool IsFixed = MFI.isFixedObjectIndex(FI);
  // We can't calculate offset from frame pointer if the stack is realigned,
  // so enforce usage of stack/base pointer.  The base pointer is used when we
  // have dynamic allocas in addition to dynamic realignment.
  if (TRI->hasBasePointer(MF))
    FrameReg = IsFixed ? TRI->getFramePtr() : TRI->getBaseRegister();
  else if (TRI->hasStackRealignment(MF))
    FrameReg = IsFixed ? TRI->getFramePtr() : TRI->getStackRegister();
  else
    FrameReg = TRI->getFrameRegister(MF);

  // Offset will hold the offset from the stack pointer at function entry to the
  // object.
  // We need to factor in additional offsets applied during the prologue to the
  // frame, base, and stack pointer depending on which is used.
  int Offset = MFI.getObjectOffset(FI) - getOffsetOfLocalArea();
  const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  unsigned CSSize = X86FI->getCalleeSavedFrameSize();
  uint64_t StackSize = MFI.getStackSize();
  bool IsWin64Prologue = MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
  int64_t FPDelta = 0;

  // In an x86 interrupt, remove the offset we added to account for the return
  // address from any stack object allocated in the caller's frame. Interrupts
  // do not have a standard return address. Fixed objects in the current frame,
  // such as SSE register spills, should not get this treatment.
  if (MF.getFunction().getCallingConv() == CallingConv::X86_INTR &&
      Offset >= 0) {
    Offset += getOffsetOfLocalArea();
  }

  if (IsWin64Prologue) {
    assert(!MFI.hasCalls() || (StackSize % 16) == 8);

    // Calculate required stack adjustment.
    uint64_t FrameSize = StackSize - SlotSize;
    // If required, include space for extra hidden slot for stashing base
    // pointer.
    if (X86FI->getRestoreBasePointer())
      FrameSize += SlotSize;
    uint64_t NumBytes = FrameSize - CSSize;

    uint64_t SEHFrameOffset = calculateSetFPREG(NumBytes);
    if (FI && FI == X86FI->getFAIndex())
      return StackOffset::getFixed(-SEHFrameOffset);

    // FPDelta is the offset from the "traditional" FP location of the old base
    // pointer followed by return address and the location required by the
    // restricted Win64 prologue.
    // Add FPDelta to all offsets below that go through the frame pointer.
    FPDelta = FrameSize - SEHFrameOffset;
    assert((!MFI.hasCalls() || (FPDelta % 16) == 0) &&
           "FPDelta isn't aligned per the Win64 ABI!");
  }

  if (FI >= 0)
    Offset -= X86FI->getSaveArgSize();

  if (FrameReg == TRI->getFramePtr()) {
    // Skip saved EBP/RBP
    Offset += SlotSize;

    // Account for restricted Windows prologue.
    Offset += FPDelta;

    // Skip the RETADDR move area
    int TailCallReturnAddrDelta = X86FI->getTCReturnAddrDelta();
    if (TailCallReturnAddrDelta < 0)
      Offset -= TailCallReturnAddrDelta;

    return StackOffset::getFixed(Offset);
  }

  // FrameReg is either the stack pointer or a base pointer. But the base is
  // located at the end of the statically known StackSize so the distinction
  // doesn't really matter.
  if (TRI->hasStackRealignment(MF) || TRI->hasBasePointer(MF))
    assert(isAligned(MFI.getObjectAlign(FI), -(Offset + StackSize)));
  return StackOffset::getFixed(Offset + StackSize);
}

int X86FrameLowering::getWin64EHFrameIndexRef(const MachineFunction &MF, int FI,
                                              Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  const auto &WinEHXMMSlotInfo = X86FI->getWinEHXMMSlotInfo();
  const auto it = WinEHXMMSlotInfo.find(FI);

  if (it == WinEHXMMSlotInfo.end())
    return getFrameIndexReference(MF, FI, FrameReg).getFixed();

  FrameReg = TRI->getStackRegister();
  return alignDown(MFI.getMaxCallFrameSize(), getStackAlign().value()) +
         it->second;
}

StackOffset
X86FrameLowering::getFrameIndexReferenceSP(const MachineFunction &MF, int FI,
                                           Register &FrameReg,
                                           int Adjustment) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  FrameReg = TRI->getStackRegister();
  return StackOffset::getFixed(MFI.getObjectOffset(FI) -
                               getOffsetOfLocalArea() + Adjustment);
}

StackOffset
X86FrameLowering::getFrameIndexReferencePreferSP(const MachineFunction &MF,
                                                 int FI, Register &FrameReg,
                                                 bool IgnoreSPUpdates) const {

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // Does not include any dynamic realign.
  const uint64_t StackSize = MFI.getStackSize();
  // LLVM arranges the stack as follows:
  //   ...
  //   ARG2
  //   ARG1
  //   RETADDR
  //   PUSH RBP   <-- RBP points here
  //   PUSH CSRs
  //   ~~~~~~~    <-- possible stack realignment (non-win64)
  //   ...
  //   STACK OBJECTS
  //   ...        <-- RSP after prologue points here
  //   ~~~~~~~    <-- possible stack realignment (win64)
  //
  // if (hasVarSizedObjects()):
  //   ...        <-- "base pointer" (ESI/RBX) points here
  //   DYNAMIC ALLOCAS
  //   ...        <-- RSP points here
  //
  // Case 1: In the simple case of no stack realignment and no dynamic
  // allocas, both "fixed" stack objects (arguments and CSRs) are addressable
  // with fixed offsets from RSP.
  //
  // Case 2: In the case of stack realignment with no dynamic allocas, fixed
  // stack objects are addressed with RBP and regular stack objects with RSP.
  //
  // Case 3: In the case of dynamic allocas and stack realignment, RSP is used
  // to address stack arguments for outgoing calls and nothing else. The "base
  // pointer" points to local variables, and RBP points to fixed objects.
  //
  // In cases 2 and 3, we can only answer for non-fixed stack objects, and the
  // answer we give is relative to the SP after the prologue, and not the
  // SP in the middle of the function.

  if (MFI.isFixedObjectIndex(FI) && TRI->hasStackRealignment(MF) &&
      !STI.isTargetWin64())
    return getFrameIndexReference(MF, FI, FrameReg);

  // If !hasReservedCallFrame the function might have SP adjustement in the
  // body.  So, even though the offset is statically known, it depends on where
  // we are in the function.
  if (!IgnoreSPUpdates && !hasReservedCallFrame(MF))
    return getFrameIndexReference(MF, FI, FrameReg);

  // We don't handle tail calls, and shouldn't be seeing them either.
  assert(MF.getInfo<X86MachineFunctionInfo>()->getTCReturnAddrDelta() >= 0 &&
         "we don't handle this case!");

  // This is how the math works out:
  //
  //  %rsp grows (i.e. gets lower) left to right. Each box below is
  //  one word (eight bytes).  Obj0 is the stack slot we're trying to
  //  get to.
  //
  //    ----------------------------------
  //    | BP | Obj0 | Obj1 | ... | ObjN |
  //    ----------------------------------
  //    ^    ^      ^                   ^
  //    A    B      C                   E
  //
  // A is the incoming stack pointer.
  // (B - A) is the local area offset (-8 for x86-64) [1]
  // (C - A) is the Offset returned by MFI.getObjectOffset for Obj0 [2]
  //
  // |(E - B)| is the StackSize (absolute value, positive).  For a
  // stack that grown down, this works out to be (B - E). [3]
  //
  // E is also the value of %rsp after stack has been set up, and we
  // want (C - E) -- the value we can add to %rsp to get to Obj0.  Now
  // (C - E) == (C - A) - (B - A) + (B - E)
  //            { Using [1], [2] and [3] above }
  //         == getObjectOffset - LocalAreaOffset + StackSize

  return getFrameIndexReferenceSP(MF, FI, FrameReg, StackSize);
}

bool X86FrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();

  unsigned CalleeSavedFrameSize = 0;
  unsigned XMMCalleeSavedFrameSize = 0;
  auto &WinEHXMMSlotInfo = X86FI->getWinEHXMMSlotInfo();
  int SpillSlotOffset = getOffsetOfLocalArea() + X86FI->getTCReturnAddrDelta();

  int64_t TailCallReturnAddrDelta = X86FI->getTCReturnAddrDelta();

  if (TailCallReturnAddrDelta < 0) {
    // create RETURNADDR area
    //   arg
    //   arg
    //   RETADDR
    //   { ...
    //     RETADDR area
    //     ...
    //   }
    //   [EBP]
    MFI.CreateFixedObject(-TailCallReturnAddrDelta,
                          TailCallReturnAddrDelta - SlotSize, true);
  }

  // Spill the BasePtr if it's used.
  if (this->TRI->hasBasePointer(MF)) {
    // Allocate a spill slot for EBP if we have a base pointer and EH funclets.
    if (MF.hasEHFunclets()) {
      int FI = MFI.CreateSpillStackObject(SlotSize, Align(SlotSize));
      X86FI->setHasSEHFramePtrSave(true);
      X86FI->setSEHFramePtrSaveIndex(FI);
    }
  }

  if (hasFP(MF)) {
    // emitPrologue always spills frame register the first thing.
    SpillSlotOffset -= SlotSize;
    MFI.CreateFixedSpillStackObject(SlotSize, SpillSlotOffset);

    // The async context lives directly before the frame pointer, and we
    // allocate a second slot to preserve stack alignment.
    if (X86FI->hasSwiftAsyncContext()) {
      SpillSlotOffset -= SlotSize;
      MFI.CreateFixedSpillStackObject(SlotSize, SpillSlotOffset);
      SpillSlotOffset -= SlotSize;
    }

    // Since emitPrologue and emitEpilogue will handle spilling and restoring of
    // the frame register, we can delete it from CSI list and not have to worry
    // about avoiding it later.
    Register FPReg = TRI->getFrameRegister(MF);
    for (unsigned i = 0; i < CSI.size(); ++i) {
      if (TRI->regsOverlap(CSI[i].getReg(), FPReg)) {
        CSI.erase(CSI.begin() + i);
        break;
      }
    }
  }

  // Strategy:
  // 1. Use push2 when
  //       a) number of CSR > 1 if no need padding
  //       b) number of CSR > 2 if need padding
  // 2. When the number of CSR push is odd
  //    a. Start to use push2 from the 1st push if stack is 16B aligned.
  //    b. Start to use push2 from the 2nd push if stack is not 16B aligned.
  // 3. When the number of CSR push is even, start to use push2 from the 1st
  //    push and make the stack 16B aligned before the push
  unsigned NumRegsForPush2 = 0;
  if (STI.hasPush2Pop2()) {
    unsigned NumCSGPR = llvm::count_if(CSI, [](const CalleeSavedInfo &I) {
      return X86::GR64RegClass.contains(I.getReg());
    });
    bool NeedPadding = (SpillSlotOffset % 16 != 0) && (NumCSGPR % 2 == 0);
    bool UsePush2Pop2 = NeedPadding ? NumCSGPR > 2 : NumCSGPR > 1;
    X86FI->setPadForPush2Pop2(NeedPadding && UsePush2Pop2);
    NumRegsForPush2 = UsePush2Pop2 ? alignDown(NumCSGPR, 2) : 0;
    if (X86FI->padForPush2Pop2()) {
      SpillSlotOffset -= SlotSize;
      MFI.CreateFixedSpillStackObject(SlotSize, SpillSlotOffset);
    }
  }

  // Assign slots for GPRs. It increases frame size.
  for (CalleeSavedInfo &I : llvm::reverse(CSI)) {
    Register Reg = I.getReg();

    if (!X86::GR64RegClass.contains(Reg) && !X86::GR32RegClass.contains(Reg))
      continue;

    // A CSR is a candidate for push2/pop2 when it's slot offset is 16B aligned
    // or only an odd number of registers in the candidates.
    if (X86FI->getNumCandidatesForPush2Pop2() < NumRegsForPush2 &&
        (SpillSlotOffset % 16 == 0 ||
         X86FI->getNumCandidatesForPush2Pop2() % 2))
      X86FI->addCandidateForPush2Pop2(Reg);

    SpillSlotOffset -= SlotSize;
    CalleeSavedFrameSize += SlotSize;

    int SlotIndex = MFI.CreateFixedSpillStackObject(SlotSize, SpillSlotOffset);
    I.setFrameIdx(SlotIndex);
  }

  // Adjust the offset of spill slot as we know the accurate callee saved frame
  // size.
  if (X86FI->getRestoreBasePointer()) {
    SpillSlotOffset -= SlotSize;
    CalleeSavedFrameSize += SlotSize;

    MFI.CreateFixedSpillStackObject(SlotSize, SpillSlotOffset);
    // TODO: saving the slot index is better?
    X86FI->setRestoreBasePointer(CalleeSavedFrameSize);
  }
  assert(X86FI->getNumCandidatesForPush2Pop2() % 2 == 0 &&
         "Expect even candidates for push2/pop2");
  if (X86FI->getNumCandidatesForPush2Pop2())
    ++NumFunctionUsingPush2Pop2;
  X86FI->setCalleeSavedFrameSize(CalleeSavedFrameSize);
  MFI.setCVBytesOfCalleeSavedRegisters(CalleeSavedFrameSize);

  // Assign slots for XMMs.
  for (CalleeSavedInfo &I : llvm::reverse(CSI)) {
    Register Reg = I.getReg();
    if (X86::GR64RegClass.contains(Reg) || X86::GR32RegClass.contains(Reg))
      continue;

    // If this is k-register make sure we lookup via the largest legal type.
    MVT VT = MVT::Other;
    if (X86::VK16RegClass.contains(Reg))
      VT = STI.hasBWI() ? MVT::v64i1 : MVT::v16i1;

    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg, VT);
    unsigned Size = TRI->getSpillSize(*RC);
    Align Alignment = TRI->getSpillAlign(*RC);
    // ensure alignment
    assert(SpillSlotOffset < 0 && "SpillSlotOffset should always < 0 on X86");
    SpillSlotOffset = -alignTo(-SpillSlotOffset, Alignment);

    // spill into slot
    SpillSlotOffset -= Size;
    int SlotIndex = MFI.CreateFixedSpillStackObject(Size, SpillSlotOffset);
    I.setFrameIdx(SlotIndex);
    MFI.ensureMaxAlignment(Alignment);

    // Save the start offset and size of XMM in stack frame for funclets.
    if (X86::VR128RegClass.contains(Reg)) {
      WinEHXMMSlotInfo[SlotIndex] = XMMCalleeSavedFrameSize;
      XMMCalleeSavedFrameSize += Size;
    }
  }

  return true;
}

bool X86FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  DebugLoc DL = MBB.findDebugLoc(MI);

  // Don't save CSRs in 32-bit EH funclets. The caller saves EBX, EBP, ESI, EDI
  // for us, and there are no XMM CSRs on Win32.
  if (MBB.isEHFuncletEntry() && STI.is32Bit() && STI.isOSWindows())
    return true;

  // Push GPRs. It increases frame size.
  const MachineFunction &MF = *MBB.getParent();
  const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  if (X86FI->padForPush2Pop2())
    emitSPUpdate(MBB, MI, DL, -(int64_t)SlotSize, /*InEpilogue=*/false);

  // Update LiveIn of the basic block and decide whether we can add a kill flag
  // to the use.
  auto UpdateLiveInCheckCanKill = [&](Register Reg) {
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    // Do not set a kill flag on values that are also marked as live-in. This
    // happens with the @llvm-returnaddress intrinsic and with arguments
    // passed in callee saved registers.
    // Omitting the kill flags is conservatively correct even if the live-in
    // is not used after all.
    if (MRI.isLiveIn(Reg))
      return false;
    MBB.addLiveIn(Reg);
    // Check if any subregister is live-in
    for (MCRegAliasIterator AReg(Reg, TRI, false); AReg.isValid(); ++AReg)
      if (MRI.isLiveIn(*AReg))
        return false;
    return true;
  };
  auto UpdateLiveInGetKillRegState = [&](Register Reg) {
    return getKillRegState(UpdateLiveInCheckCanKill(Reg));
  };

  for (auto RI = CSI.rbegin(), RE = CSI.rend(); RI != RE; ++RI) {
    Register Reg = RI->getReg();
    if (!X86::GR64RegClass.contains(Reg) && !X86::GR32RegClass.contains(Reg))
      continue;

    if (X86FI->isCandidateForPush2Pop2(Reg)) {
      Register Reg2 = (++RI)->getReg();
      BuildMI(MBB, MI, DL, TII.get(getPUSH2Opcode(STI)))
          .addReg(Reg, UpdateLiveInGetKillRegState(Reg))
          .addReg(Reg2, UpdateLiveInGetKillRegState(Reg2))
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      BuildMI(MBB, MI, DL, TII.get(getPUSHOpcode(STI)))
          .addReg(Reg, UpdateLiveInGetKillRegState(Reg))
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  if (X86FI->getRestoreBasePointer()) {
    unsigned Opc = STI.is64Bit() ? X86::PUSH64r : X86::PUSH32r;
    Register BaseReg = this->TRI->getBaseRegister();
    BuildMI(MBB, MI, DL, TII.get(Opc))
        .addReg(BaseReg, getKillRegState(true))
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Make XMM regs spilled. X86 does not have ability of push/pop XMM.
  // It can be done by spilling XMMs to stack frame.
  for (const CalleeSavedInfo &I : llvm::reverse(CSI)) {
    Register Reg = I.getReg();
    if (X86::GR64RegClass.contains(Reg) || X86::GR32RegClass.contains(Reg))
      continue;

    // If this is k-register make sure we lookup via the largest legal type.
    MVT VT = MVT::Other;
    if (X86::VK16RegClass.contains(Reg))
      VT = STI.hasBWI() ? MVT::v64i1 : MVT::v16i1;

    // Add the callee-saved register as live-in. It's killed at the spill.
    MBB.addLiveIn(Reg);
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg, VT);

    TII.storeRegToStackSlot(MBB, MI, Reg, true, I.getFrameIdx(), RC, TRI,
                            Register());
    --MI;
    MI->setFlag(MachineInstr::FrameSetup);
    ++MI;
  }

  return true;
}

void X86FrameLowering::emitCatchRetReturnValue(MachineBasicBlock &MBB,
                                               MachineBasicBlock::iterator MBBI,
                                               MachineInstr *CatchRet) const {
  // SEH shouldn't use catchret.
  assert(!isAsynchronousEHPersonality(classifyEHPersonality(
             MBB.getParent()->getFunction().getPersonalityFn())) &&
         "SEH should not use CATCHRET");
  const DebugLoc &DL = CatchRet->getDebugLoc();
  MachineBasicBlock *CatchRetTarget = CatchRet->getOperand(0).getMBB();

  // Fill EAX/RAX with the address of the target block.
  if (STI.is64Bit()) {
    // LEA64r CatchRetTarget(%rip), %rax
    BuildMI(MBB, MBBI, DL, TII.get(X86::LEA64r), X86::RAX)
        .addReg(X86::RIP)
        .addImm(0)
        .addReg(0)
        .addMBB(CatchRetTarget)
        .addReg(0);
  } else {
    // MOV32ri $CatchRetTarget, %eax
    BuildMI(MBB, MBBI, DL, TII.get(X86::MOV32ri), X86::EAX)
        .addMBB(CatchRetTarget);
  }

  // Record that we've taken the address of CatchRetTarget and no longer just
  // reference it in a terminator.
  CatchRetTarget->setMachineBlockAddressTaken();
}

bool X86FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  if (MI != MBB.end() && isFuncletReturnInstr(*MI) && STI.isOSWindows()) {
    // Don't restore CSRs in 32-bit EH funclets. Matches
    // spillCalleeSavedRegisters.
    if (STI.is32Bit())
      return true;
    // Don't restore CSRs before an SEH catchret. SEH except blocks do not form
    // funclets. emitEpilogue transforms these to normal jumps.
    if (MI->getOpcode() == X86::CATCHRET) {
      const Function &F = MBB.getParent()->getFunction();
      bool IsSEH = isAsynchronousEHPersonality(
          classifyEHPersonality(F.getPersonalityFn()));
      if (IsSEH)
        return true;
    }
  }

  DebugLoc DL = MBB.findDebugLoc(MI);

  // Reload XMMs from stack frame.
  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    if (X86::GR64RegClass.contains(Reg) || X86::GR32RegClass.contains(Reg))
      continue;

    // If this is k-register make sure we lookup via the largest legal type.
    MVT VT = MVT::Other;
    if (X86::VK16RegClass.contains(Reg))
      VT = STI.hasBWI() ? MVT::v64i1 : MVT::v16i1;

    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg, VT);
    TII.loadRegFromStackSlot(MBB, MI, Reg, I.getFrameIdx(), RC, TRI,
                             Register());
  }

  // Clear the stack slot for spill base pointer register.
  MachineFunction &MF = *MBB.getParent();
  const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  if (X86FI->getRestoreBasePointer()) {
    unsigned Opc = STI.is64Bit() ? X86::POP64r : X86::POP32r;
    Register BaseReg = this->TRI->getBaseRegister();
    BuildMI(MBB, MI, DL, TII.get(Opc), BaseReg)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  // POP GPRs.
  for (auto I = CSI.begin(), E = CSI.end(); I != E; ++I) {
    Register Reg = I->getReg();
    if (!X86::GR64RegClass.contains(Reg) && !X86::GR32RegClass.contains(Reg))
      continue;

    if (X86FI->isCandidateForPush2Pop2(Reg))
      BuildMI(MBB, MI, DL, TII.get(getPOP2Opcode(STI)), Reg)
          .addReg((++I)->getReg(), RegState::Define)
          .setMIFlag(MachineInstr::FrameDestroy);
    else
      BuildMI(MBB, MI, DL, TII.get(getPOPOpcode(STI)), Reg)
          .setMIFlag(MachineInstr::FrameDestroy);
  }
  if (X86FI->padForPush2Pop2())
    emitSPUpdate(MBB, MI, DL, SlotSize, /*InEpilogue=*/true);

  return true;
}

void X86FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // Spill the BasePtr if it's used.
  if (TRI->hasBasePointer(MF)) {
    Register BasePtr = TRI->getBaseRegister();
    if (STI.isTarget64BitILP32())
      BasePtr = getX86SubSuperRegister(BasePtr, 64);
    SavedRegs.set(BasePtr);
  }
}

static bool HasNestArgument(const MachineFunction *MF) {
  const Function &F = MF->getFunction();
  for (Function::const_arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E;
       I++) {
    if (I->hasNestAttr() && !I->use_empty())
      return true;
  }
  return false;
}

/// GetScratchRegister - Get a temp register for performing work in the
/// segmented stack and the Erlang/HiPE stack prologue. Depending on platform
/// and the properties of the function either one or two registers will be
/// needed. Set primary to true for the first register, false for the second.
static unsigned GetScratchRegister(bool Is64Bit, bool IsLP64,
                                   const MachineFunction &MF, bool Primary) {
  CallingConv::ID CallingConvention = MF.getFunction().getCallingConv();

  // Erlang stuff.
  if (CallingConvention == CallingConv::HiPE) {
    if (Is64Bit)
      return Primary ? X86::R14 : X86::R13;
    else
      return Primary ? X86::EBX : X86::EDI;
  }

  if (Is64Bit) {
    if (IsLP64)
      return Primary ? X86::R11 : X86::R12;
    else
      return Primary ? X86::R11D : X86::R12D;
  }

  bool IsNested = HasNestArgument(&MF);

  if (CallingConvention == CallingConv::X86_FastCall ||
      CallingConvention == CallingConv::Fast ||
      CallingConvention == CallingConv::Tail) {
    if (IsNested)
      report_fatal_error("Segmented stacks does not support fastcall with "
                         "nested function.");
    return Primary ? X86::EAX : X86::ECX;
  }
  if (IsNested)
    return Primary ? X86::EDX : X86::EAX;
  return Primary ? X86::ECX : X86::EAX;
}

// The stack limit in the TCB is set to this many bytes above the actual stack
// limit.
static const uint64_t kSplitStackAvailable = 256;

void X86FrameLowering::adjustForSegmentedStacks(
    MachineFunction &MF, MachineBasicBlock &PrologueMBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize;
  unsigned TlsReg, TlsOffset;
  DebugLoc DL;

  // To support shrink-wrapping we would need to insert the new blocks
  // at the right place and update the branches to PrologueMBB.
  assert(&(*MF.begin()) == &PrologueMBB && "Shrink-wrapping not supported yet");

  unsigned ScratchReg = GetScratchRegister(Is64Bit, IsLP64, MF, true);
  assert(!MF.getRegInfo().isLiveIn(ScratchReg) &&
         "Scratch register is live-in");

  if (MF.getFunction().isVarArg())
    report_fatal_error("Segmented stacks do not support vararg functions.");
  if (!STI.isTargetLinux() && !STI.isTargetDarwin() && !STI.isTargetWin32() &&
      !STI.isTargetWin64() && !STI.isTargetFreeBSD() &&
      !STI.isTargetDragonFly())
    report_fatal_error("Segmented stacks not supported on this platform.");

  // Eventually StackSize will be calculated by a link-time pass; which will
  // also decide whether checking code needs to be injected into this particular
  // prologue.
  StackSize = MFI.getStackSize();

  if (!MFI.needsSplitStackProlog())
    return;

  MachineBasicBlock *allocMBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *checkMBB = MF.CreateMachineBasicBlock();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  bool IsNested = false;

  // We need to know if the function has a nest argument only in 64 bit mode.
  if (Is64Bit)
    IsNested = HasNestArgument(&MF);

  // The MOV R10, RAX needs to be in a different block, since the RET we emit in
  // allocMBB needs to be last (terminating) instruction.

  for (const auto &LI : PrologueMBB.liveins()) {
    allocMBB->addLiveIn(LI);
    checkMBB->addLiveIn(LI);
  }

  if (IsNested)
    allocMBB->addLiveIn(IsLP64 ? X86::R10 : X86::R10D);

  MF.push_front(allocMBB);
  MF.push_front(checkMBB);

  // When the frame size is less than 256 we just compare the stack
  // boundary directly to the value of the stack pointer, per gcc.
  bool CompareStackPointer = StackSize < kSplitStackAvailable;

  // Read the limit off the current stacklet off the stack_guard location.
  if (Is64Bit) {
    if (STI.isTargetLinux()) {
      TlsReg = X86::FS;
      TlsOffset = IsLP64 ? 0x70 : 0x40;
    } else if (STI.isTargetDarwin()) {
      TlsReg = X86::GS;
      TlsOffset = 0x60 + 90 * 8; // See pthread_machdep.h. Steal TLS slot 90.
    } else if (STI.isTargetWin64()) {
      TlsReg = X86::GS;
      TlsOffset = 0x28; // pvArbitrary, reserved for application use
    } else if (STI.isTargetFreeBSD()) {
      TlsReg = X86::FS;
      TlsOffset = 0x18;
    } else if (STI.isTargetDragonFly()) {
      TlsReg = X86::FS;
      TlsOffset = 0x20; // use tls_tcb.tcb_segstack
    } else {
      report_fatal_error("Segmented stacks not supported on this platform.");
    }

    if (CompareStackPointer)
      ScratchReg = IsLP64 ? X86::RSP : X86::ESP;
    else
      BuildMI(checkMBB, DL, TII.get(IsLP64 ? X86::LEA64r : X86::LEA64_32r),
              ScratchReg)
          .addReg(X86::RSP)
          .addImm(1)
          .addReg(0)
          .addImm(-StackSize)
          .addReg(0);

    BuildMI(checkMBB, DL, TII.get(IsLP64 ? X86::CMP64rm : X86::CMP32rm))
        .addReg(ScratchReg)
        .addReg(0)
        .addImm(1)
        .addReg(0)
        .addImm(TlsOffset)
        .addReg(TlsReg);
  } else {
    if (STI.isTargetLinux()) {
      TlsReg = X86::GS;
      TlsOffset = 0x30;
    } else if (STI.isTargetDarwin()) {
      TlsReg = X86::GS;
      TlsOffset = 0x48 + 90 * 4;
    } else if (STI.isTargetWin32()) {
      TlsReg = X86::FS;
      TlsOffset = 0x14; // pvArbitrary, reserved for application use
    } else if (STI.isTargetDragonFly()) {
      TlsReg = X86::FS;
      TlsOffset = 0x10; // use tls_tcb.tcb_segstack
    } else if (STI.isTargetFreeBSD()) {
      report_fatal_error("Segmented stacks not supported on FreeBSD i386.");
    } else {
      report_fatal_error("Segmented stacks not supported on this platform.");
    }

    if (CompareStackPointer)
      ScratchReg = X86::ESP;
    else
      BuildMI(checkMBB, DL, TII.get(X86::LEA32r), ScratchReg)
          .addReg(X86::ESP)
          .addImm(1)
          .addReg(0)
          .addImm(-StackSize)
          .addReg(0);

    if (STI.isTargetLinux() || STI.isTargetWin32() || STI.isTargetWin64() ||
        STI.isTargetDragonFly()) {
      BuildMI(checkMBB, DL, TII.get(X86::CMP32rm))
          .addReg(ScratchReg)
          .addReg(0)
          .addImm(0)
          .addReg(0)
          .addImm(TlsOffset)
          .addReg(TlsReg);
    } else if (STI.isTargetDarwin()) {

      // TlsOffset doesn't fit into a mod r/m byte so we need an extra register.
      unsigned ScratchReg2;
      bool SaveScratch2;
      if (CompareStackPointer) {
        // The primary scratch register is available for holding the TLS offset.
        ScratchReg2 = GetScratchRegister(Is64Bit, IsLP64, MF, true);
        SaveScratch2 = false;
      } else {
        // Need to use a second register to hold the TLS offset
        ScratchReg2 = GetScratchRegister(Is64Bit, IsLP64, MF, false);

        // Unfortunately, with fastcc the second scratch register may hold an
        // argument.
        SaveScratch2 = MF.getRegInfo().isLiveIn(ScratchReg2);
      }

      // If Scratch2 is live-in then it needs to be saved.
      assert((!MF.getRegInfo().isLiveIn(ScratchReg2) || SaveScratch2) &&
             "Scratch register is live-in and not saved");

      if (SaveScratch2)
        BuildMI(checkMBB, DL, TII.get(X86::PUSH32r))
            .addReg(ScratchReg2, RegState::Kill);

      BuildMI(checkMBB, DL, TII.get(X86::MOV32ri), ScratchReg2)
          .addImm(TlsOffset);
      BuildMI(checkMBB, DL, TII.get(X86::CMP32rm))
          .addReg(ScratchReg)
          .addReg(ScratchReg2)
          .addImm(1)
          .addReg(0)
          .addImm(0)
          .addReg(TlsReg);

      if (SaveScratch2)
        BuildMI(checkMBB, DL, TII.get(X86::POP32r), ScratchReg2);
    }
  }

  // This jump is taken if SP >= (Stacklet Limit + Stack Space required).
  // It jumps to normal execution of the function body.
  BuildMI(checkMBB, DL, TII.get(X86::JCC_1))
      .addMBB(&PrologueMBB)
      .addImm(X86::COND_A);

  // On 32 bit we first push the arguments size and then the frame size. On 64
  // bit, we pass the stack frame size in r10 and the argument size in r11.
  if (Is64Bit) {
    // Functions with nested arguments use R10, so it needs to be saved across
    // the call to _morestack

    const unsigned RegAX = IsLP64 ? X86::RAX : X86::EAX;
    const unsigned Reg10 = IsLP64 ? X86::R10 : X86::R10D;
    const unsigned Reg11 = IsLP64 ? X86::R11 : X86::R11D;
    const unsigned MOVrr = IsLP64 ? X86::MOV64rr : X86::MOV32rr;

    if (IsNested)
      BuildMI(allocMBB, DL, TII.get(MOVrr), RegAX).addReg(Reg10);

    BuildMI(allocMBB, DL, TII.get(getMOVriOpcode(IsLP64, StackSize)), Reg10)
        .addImm(StackSize);
    BuildMI(allocMBB, DL,
            TII.get(getMOVriOpcode(IsLP64, X86FI->getArgumentStackSize())),
            Reg11)
        .addImm(X86FI->getArgumentStackSize());
  } else {
    BuildMI(allocMBB, DL, TII.get(X86::PUSH32i))
        .addImm(X86FI->getArgumentStackSize());
    BuildMI(allocMBB, DL, TII.get(X86::PUSH32i)).addImm(StackSize);
  }

  // __morestack is in libgcc
  if (Is64Bit && MF.getTarget().getCodeModel() == CodeModel::Large) {
    // Under the large code model, we cannot assume that __morestack lives
    // within 2^31 bytes of the call site, so we cannot use pc-relative
    // addressing. We cannot perform the call via a temporary register,
    // as the rax register may be used to store the static chain, and all
    // other suitable registers may be either callee-save or used for
    // parameter passing. We cannot use the stack at this point either
    // because __morestack manipulates the stack directly.
    //
    // To avoid these issues, perform an indirect call via a read-only memory
    // location containing the address.
    //
    // This solution is not perfect, as it assumes that the .rodata section
    // is laid out within 2^31 bytes of each function body, but this seems
    // to be sufficient for JIT.
    // FIXME: Add retpoline support and remove the error here..
    if (STI.useIndirectThunkCalls())
      report_fatal_error("Emitting morestack calls on 64-bit with the large "
                         "code model and thunks not yet implemented.");
    BuildMI(allocMBB, DL, TII.get(X86::CALL64m))
        .addReg(X86::RIP)
        .addImm(0)
        .addReg(0)
        .addExternalSymbol("__morestack_addr")
        .addReg(0);
  } else {
    if (Is64Bit)
      BuildMI(allocMBB, DL, TII.get(X86::CALL64pcrel32))
          .addExternalSymbol("__morestack");
    else
      BuildMI(allocMBB, DL, TII.get(X86::CALLpcrel32))
          .addExternalSymbol("__morestack");
  }

  if (IsNested)
    BuildMI(allocMBB, DL, TII.get(X86::MORESTACK_RET_RESTORE_R10));
  else
    BuildMI(allocMBB, DL, TII.get(X86::MORESTACK_RET));

  allocMBB->addSuccessor(&PrologueMBB);

  checkMBB->addSuccessor(allocMBB, BranchProbability::getZero());
  checkMBB->addSuccessor(&PrologueMBB, BranchProbability::getOne());

#ifdef EXPENSIVE_CHECKS
  MF.verify();
#endif
}

/// Lookup an ERTS parameter in the !hipe.literals named metadata node.
/// HiPE provides Erlang Runtime System-internal parameters, such as PCB offsets
/// to fields it needs, through a named metadata node "hipe.literals" containing
/// name-value pairs.
static unsigned getHiPELiteral(NamedMDNode *HiPELiteralsMD,
                               const StringRef LiteralName) {
  for (int i = 0, e = HiPELiteralsMD->getNumOperands(); i != e; ++i) {
    MDNode *Node = HiPELiteralsMD->getOperand(i);
    if (Node->getNumOperands() != 2)
      continue;
    MDString *NodeName = dyn_cast<MDString>(Node->getOperand(0));
    ValueAsMetadata *NodeVal = dyn_cast<ValueAsMetadata>(Node->getOperand(1));
    if (!NodeName || !NodeVal)
      continue;
    ConstantInt *ValConst = dyn_cast_or_null<ConstantInt>(NodeVal->getValue());
    if (ValConst && NodeName->getString() == LiteralName) {
      return ValConst->getZExtValue();
    }
  }

  report_fatal_error("HiPE literal " + LiteralName +
                     " required but not provided");
}

// Return true if there are no non-ehpad successors to MBB and there are no
// non-meta instructions between MBBI and MBB.end().
static bool blockEndIsUnreachable(const MachineBasicBlock &MBB,
                                  MachineBasicBlock::const_iterator MBBI) {
  return llvm::all_of(
             MBB.successors(),
             [](const MachineBasicBlock *Succ) { return Succ->isEHPad(); }) &&
         std::all_of(MBBI, MBB.end(), [](const MachineInstr &MI) {
           return MI.isMetaInstruction();
         });
}

/// Erlang programs may need a special prologue to handle the stack size they
/// might need at runtime. That is because Erlang/OTP does not implement a C
/// stack but uses a custom implementation of hybrid stack/heap architecture.
/// (for more information see Eric Stenman's Ph.D. thesis:
/// http://publications.uu.se/uu/fulltext/nbn_se_uu_diva-2688.pdf)
///
/// CheckStack:
///       temp0 = sp - MaxStack
///       if( temp0 < SP_LIMIT(P) ) goto IncStack else goto OldStart
/// OldStart:
///       ...
/// IncStack:
///       call inc_stack   # doubles the stack space
///       temp0 = sp - MaxStack
///       if( temp0 < SP_LIMIT(P) ) goto IncStack else goto OldStart
void X86FrameLowering::adjustForHiPEPrologue(
    MachineFunction &MF, MachineBasicBlock &PrologueMBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc DL;

  // To support shrink-wrapping we would need to insert the new blocks
  // at the right place and update the branches to PrologueMBB.
  assert(&(*MF.begin()) == &PrologueMBB && "Shrink-wrapping not supported yet");

  // HiPE-specific values
  NamedMDNode *HiPELiteralsMD =
      MF.getFunction().getParent()->getNamedMetadata("hipe.literals");
  if (!HiPELiteralsMD)
    report_fatal_error(
        "Can't generate HiPE prologue without runtime parameters");
  const unsigned HipeLeafWords = getHiPELiteral(
      HiPELiteralsMD, Is64Bit ? "AMD64_LEAF_WORDS" : "X86_LEAF_WORDS");
  const unsigned CCRegisteredArgs = Is64Bit ? 6 : 5;
  const unsigned Guaranteed = HipeLeafWords * SlotSize;
  unsigned CallerStkArity = MF.getFunction().arg_size() > CCRegisteredArgs
                                ? MF.getFunction().arg_size() - CCRegisteredArgs
                                : 0;
  unsigned MaxStack = MFI.getStackSize() + CallerStkArity * SlotSize + SlotSize;

  assert(STI.isTargetLinux() &&
         "HiPE prologue is only supported on Linux operating systems.");

  // Compute the largest caller's frame that is needed to fit the callees'
  // frames. This 'MaxStack' is computed from:
  //
  // a) the fixed frame size, which is the space needed for all spilled temps,
  // b) outgoing on-stack parameter areas, and
  // c) the minimum stack space this function needs to make available for the
  //    functions it calls (a tunable ABI property).
  if (MFI.hasCalls()) {
    unsigned MoreStackForCalls = 0;

    for (auto &MBB : MF) {
      for (auto &MI : MBB) {
        if (!MI.isCall())
          continue;

        // Get callee operand.
        const MachineOperand &MO = MI.getOperand(0);

        // Only take account of global function calls (no closures etc.).
        if (!MO.isGlobal())
          continue;

        const Function *F = dyn_cast<Function>(MO.getGlobal());
        if (!F)
          continue;

        // Do not update 'MaxStack' for primitive and built-in functions
        // (encoded with names either starting with "erlang."/"bif_" or not
        // having a ".", such as a simple <Module>.<Function>.<Arity>, or an
        // "_", such as the BIF "suspend_0") as they are executed on another
        // stack.
        if (F->getName().contains("erlang.") || F->getName().contains("bif_") ||
            F->getName().find_first_of("._") == StringRef::npos)
          continue;

        unsigned CalleeStkArity = F->arg_size() > CCRegisteredArgs
                                      ? F->arg_size() - CCRegisteredArgs
                                      : 0;
        if (HipeLeafWords - 1 > CalleeStkArity)
          MoreStackForCalls =
              std::max(MoreStackForCalls,
                       (HipeLeafWords - 1 - CalleeStkArity) * SlotSize);
      }
    }
    MaxStack += MoreStackForCalls;
  }

  // If the stack frame needed is larger than the guaranteed then runtime checks
  // and calls to "inc_stack_0" BIF should be inserted in the assembly prologue.
  if (MaxStack > Guaranteed) {
    MachineBasicBlock *stackCheckMBB = MF.CreateMachineBasicBlock();
    MachineBasicBlock *incStackMBB = MF.CreateMachineBasicBlock();

    for (const auto &LI : PrologueMBB.liveins()) {
      stackCheckMBB->addLiveIn(LI);
      incStackMBB->addLiveIn(LI);
    }

    MF.push_front(incStackMBB);
    MF.push_front(stackCheckMBB);

    unsigned ScratchReg, SPReg, PReg, SPLimitOffset;
    unsigned LEAop, CMPop, CALLop;
    SPLimitOffset = getHiPELiteral(HiPELiteralsMD, "P_NSP_LIMIT");
    if (Is64Bit) {
      SPReg = X86::RSP;
      PReg = X86::RBP;
      LEAop = X86::LEA64r;
      CMPop = X86::CMP64rm;
      CALLop = X86::CALL64pcrel32;
    } else {
      SPReg = X86::ESP;
      PReg = X86::EBP;
      LEAop = X86::LEA32r;
      CMPop = X86::CMP32rm;
      CALLop = X86::CALLpcrel32;
    }

    ScratchReg = GetScratchRegister(Is64Bit, IsLP64, MF, true);
    assert(!MF.getRegInfo().isLiveIn(ScratchReg) &&
           "HiPE prologue scratch register is live-in");

    // Create new MBB for StackCheck:
    addRegOffset(BuildMI(stackCheckMBB, DL, TII.get(LEAop), ScratchReg), SPReg,
                 false, -MaxStack);
    // SPLimitOffset is in a fixed heap location (pointed by BP).
    addRegOffset(BuildMI(stackCheckMBB, DL, TII.get(CMPop)).addReg(ScratchReg),
                 PReg, false, SPLimitOffset);
    BuildMI(stackCheckMBB, DL, TII.get(X86::JCC_1))
        .addMBB(&PrologueMBB)
        .addImm(X86::COND_AE);

    // Create new MBB for IncStack:
    BuildMI(incStackMBB, DL, TII.get(CALLop)).addExternalSymbol("inc_stack_0");
    addRegOffset(BuildMI(incStackMBB, DL, TII.get(LEAop), ScratchReg), SPReg,
                 false, -MaxStack);
    addRegOffset(BuildMI(incStackMBB, DL, TII.get(CMPop)).addReg(ScratchReg),
                 PReg, false, SPLimitOffset);
    BuildMI(incStackMBB, DL, TII.get(X86::JCC_1))
        .addMBB(incStackMBB)
        .addImm(X86::COND_LE);

    stackCheckMBB->addSuccessor(&PrologueMBB, {99, 100});
    stackCheckMBB->addSuccessor(incStackMBB, {1, 100});
    incStackMBB->addSuccessor(&PrologueMBB, {99, 100});
    incStackMBB->addSuccessor(incStackMBB, {1, 100});
  }
#ifdef EXPENSIVE_CHECKS
  MF.verify();
#endif
}

bool X86FrameLowering::adjustStackWithPops(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           const DebugLoc &DL,
                                           int Offset) const {
  if (Offset <= 0)
    return false;

  if (Offset % SlotSize)
    return false;

  int NumPops = Offset / SlotSize;
  // This is only worth it if we have at most 2 pops.
  if (NumPops != 1 && NumPops != 2)
    return false;

  // Handle only the trivial case where the adjustment directly follows
  // a call. This is the most common one, anyway.
  if (MBBI == MBB.begin())
    return false;
  MachineBasicBlock::iterator Prev = std::prev(MBBI);
  if (!Prev->isCall() || !Prev->getOperand(1).isRegMask())
    return false;

  unsigned Regs[2];
  unsigned FoundRegs = 0;

  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const MachineOperand &RegMask = Prev->getOperand(1);

  auto &RegClass =
      Is64Bit ? X86::GR64_NOREX_NOSPRegClass : X86::GR32_NOREX_NOSPRegClass;
  // Try to find up to NumPops free registers.
  for (auto Candidate : RegClass) {
    // Poor man's liveness:
    // Since we're immediately after a call, any register that is clobbered
    // by the call and not defined by it can be considered dead.
    if (!RegMask.clobbersPhysReg(Candidate))
      continue;

    // Don't clobber reserved registers
    if (MRI.isReserved(Candidate))
      continue;

    bool IsDef = false;
    for (const MachineOperand &MO : Prev->implicit_operands()) {
      if (MO.isReg() && MO.isDef() &&
          TRI->isSuperOrSubRegisterEq(MO.getReg(), Candidate)) {
        IsDef = true;
        break;
      }
    }

    if (IsDef)
      continue;

    Regs[FoundRegs++] = Candidate;
    if (FoundRegs == (unsigned)NumPops)
      break;
  }

  if (FoundRegs == 0)
    return false;

  // If we found only one free register, but need two, reuse the same one twice.
  while (FoundRegs < (unsigned)NumPops)
    Regs[FoundRegs++] = Regs[0];

  for (int i = 0; i < NumPops; ++i)
    BuildMI(MBB, MBBI, DL, TII.get(STI.is64Bit() ? X86::POP64r : X86::POP32r),
            Regs[i]);

  return true;
}

MachineBasicBlock::iterator X86FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  bool reserveCallFrame = hasReservedCallFrame(MF);
  unsigned Opcode = I->getOpcode();
  bool isDestroy = Opcode == TII.getCallFrameDestroyOpcode();
  DebugLoc DL = I->getDebugLoc(); // copy DebugLoc as I will be erased.
  uint64_t Amount = TII.getFrameSize(*I);
  uint64_t InternalAmt = (isDestroy || Amount) ? TII.getFrameAdjustment(*I) : 0;
  I = MBB.erase(I);
  auto InsertPos = skipDebugInstructionsForward(I, MBB.end());

  // Try to avoid emitting dead SP adjustments if the block end is unreachable,
  // typically because the function is marked noreturn (abort, throw,
  // assert_fail, etc).
  if (isDestroy && blockEndIsUnreachable(MBB, I))
    return I;

  if (!reserveCallFrame) {
    // If the stack pointer can be changed after prologue, turn the
    // adjcallstackup instruction into a 'sub ESP, <amt>' and the
    // adjcallstackdown instruction into 'add ESP, <amt>'

    // We need to keep the stack aligned properly.  To do this, we round the
    // amount of space needed for the outgoing arguments up to the next
    // alignment boundary.
    Amount = alignTo(Amount, getStackAlign());

    const Function &F = MF.getFunction();
    bool WindowsCFI = MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
    bool DwarfCFI = !WindowsCFI && MF.needsFrameMoves();

    // If we have any exception handlers in this function, and we adjust
    // the SP before calls, we may need to indicate this to the unwinder
    // using GNU_ARGS_SIZE. Note that this may be necessary even when
    // Amount == 0, because the preceding function may have set a non-0
    // GNU_ARGS_SIZE.
    // TODO: We don't need to reset this between subsequent functions,
    // if it didn't change.
    bool HasDwarfEHHandlers = !WindowsCFI && !MF.getLandingPads().empty();

    if (HasDwarfEHHandlers && !isDestroy &&
        MF.getInfo<X86MachineFunctionInfo>()->getHasPushSequences())
      BuildCFI(MBB, InsertPos, DL,
               MCCFIInstruction::createGnuArgsSize(nullptr, Amount));

    if (Amount == 0)
      return I;

    // Factor out the amount that gets handled inside the sequence
    // (Pushes of argument for frame setup, callee pops for frame destroy)
    Amount -= InternalAmt;

    // TODO: This is needed only if we require precise CFA.
    // If this is a callee-pop calling convention, emit a CFA adjust for
    // the amount the callee popped.
    if (isDestroy && InternalAmt && DwarfCFI && !hasFP(MF))
      BuildCFI(MBB, InsertPos, DL,
               MCCFIInstruction::createAdjustCfaOffset(nullptr, -InternalAmt));

    // Add Amount to SP to destroy a frame, or subtract to setup.
    int64_t StackAdjustment = isDestroy ? Amount : -Amount;

    if (StackAdjustment) {
      // Merge with any previous or following adjustment instruction. Note: the
      // instructions merged with here do not have CFI, so their stack
      // adjustments do not feed into CfaAdjustment.
      StackAdjustment += mergeSPUpdates(MBB, InsertPos, true);
      StackAdjustment += mergeSPUpdates(MBB, InsertPos, false);

      if (StackAdjustment) {
        if (!(F.hasMinSize() &&
              adjustStackWithPops(MBB, InsertPos, DL, StackAdjustment)))
          BuildStackAdjustment(MBB, InsertPos, DL, StackAdjustment,
                               /*InEpilogue=*/false);
      }
    }

    if (DwarfCFI && !hasFP(MF)) {
      // If we don't have FP, but need to generate unwind information,
      // we need to set the correct CFA offset after the stack adjustment.
      // How much we adjust the CFA offset depends on whether we're emitting
      // CFI only for EH purposes or for debugging. EH only requires the CFA
      // offset to be correct at each call site, while for debugging we want
      // it to be more precise.

      int64_t CfaAdjustment = -StackAdjustment;
      // TODO: When not using precise CFA, we also need to adjust for the
      // InternalAmt here.
      if (CfaAdjustment) {
        BuildCFI(
            MBB, InsertPos, DL,
            MCCFIInstruction::createAdjustCfaOffset(nullptr, CfaAdjustment));
      }
    }

    return I;
  }

  if (InternalAmt) {
    MachineBasicBlock::iterator CI = I;
    MachineBasicBlock::iterator B = MBB.begin();
    while (CI != B && !std::prev(CI)->isCall())
      --CI;
    BuildStackAdjustment(MBB, CI, DL, -InternalAmt, /*InEpilogue=*/false);
  }

  return I;
}

bool X86FrameLowering::canUseAsPrologue(const MachineBasicBlock &MBB) const {
  assert(MBB.getParent() && "Block is not attached to a function!");
  const MachineFunction &MF = *MBB.getParent();
  if (!MBB.isLiveIn(X86::EFLAGS))
    return true;

  // If stack probes have to loop inline or call, that will clobber EFLAGS.
  // FIXME: we could allow cases that will use emitStackProbeInlineGenericBlock.
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86TargetLowering &TLI = *STI.getTargetLowering();
  if (TLI.hasInlineStackProbe(MF) || TLI.hasStackProbeSymbol(MF))
    return false;

  const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  return !TRI->hasStackRealignment(MF) && !X86FI->hasSwiftAsyncContext();
}

bool X86FrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  assert(MBB.getParent() && "Block is not attached to a function!");

  // Win64 has strict requirements in terms of epilogue and we are
  // not taking a chance at messing with them.
  // I.e., unless this block is already an exit block, we can't use
  // it as an epilogue.
  if (STI.isTargetWin64() && !MBB.succ_empty() && !MBB.isReturnBlock())
    return false;

  // Swift async context epilogue has a BTR instruction that clobbers parts of
  // EFLAGS.
  const MachineFunction &MF = *MBB.getParent();
  if (MF.getInfo<X86MachineFunctionInfo>()->hasSwiftAsyncContext())
    return !flagsNeedToBePreservedBeforeTheTerminators(MBB);

  if (canUseLEAForSPInEpilogue(*MBB.getParent()))
    return true;

  // If we cannot use LEA to adjust SP, we may need to use ADD, which
  // clobbers the EFLAGS. Check that we do not need to preserve it,
  // otherwise, conservatively assume this is not
  // safe to insert the epilogue here.
  return !flagsNeedToBePreservedBeforeTheTerminators(MBB);
}

bool X86FrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  // If we may need to emit frameless compact unwind information, give
  // up as this is currently broken: PR25614.
  bool CompactUnwind =
      MF.getContext().getObjectFileInfo()->getCompactUnwindSection() != nullptr;
  return (MF.getFunction().hasFnAttribute(Attribute::NoUnwind) || hasFP(MF) ||
          !CompactUnwind) &&
         // The lowering of segmented stack and HiPE only support entry
         // blocks as prologue blocks: PR26107. This limitation may be
         // lifted if we fix:
         // - adjustForSegmentedStacks
         // - adjustForHiPEPrologue
         MF.getFunction().getCallingConv() != CallingConv::HiPE &&
         !MF.shouldSplitStack();
}

MachineBasicBlock::iterator X86FrameLowering::restoreWin32EHStackPointers(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, bool RestoreSP) const {
  assert(STI.isTargetWindowsMSVC() && "funclets only supported in MSVC env");
  assert(STI.isTargetWin32() && "EBP/ESI restoration only required on win32");
  assert(STI.is32Bit() && !Uses64BitFramePtr &&
         "restoring EBP/ESI on non-32-bit target");

  MachineFunction &MF = *MBB.getParent();
  Register FramePtr = TRI->getFrameRegister(MF);
  Register BasePtr = TRI->getBaseRegister();
  WinEHFuncInfo &FuncInfo = *MF.getWinEHFuncInfo();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // FIXME: Don't set FrameSetup flag in catchret case.

  int FI = FuncInfo.EHRegNodeFrameIndex;
  int EHRegSize = MFI.getObjectSize(FI);

  if (RestoreSP) {
    // MOV32rm -EHRegSize(%ebp), %esp
    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV32rm), X86::ESP),
                 X86::EBP, true, -EHRegSize)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  Register UsedReg;
  int EHRegOffset = getFrameIndexReference(MF, FI, UsedReg).getFixed();
  int EndOffset = -EHRegOffset - EHRegSize;
  FuncInfo.EHRegNodeEndOffset = EndOffset;

  if (UsedReg == FramePtr) {
    // ADD $offset, %ebp
    unsigned ADDri = getADDriOpcode(false);
    BuildMI(MBB, MBBI, DL, TII.get(ADDri), FramePtr)
        .addReg(FramePtr)
        .addImm(EndOffset)
        .setMIFlag(MachineInstr::FrameSetup)
        ->getOperand(3)
        .setIsDead();
    assert(EndOffset >= 0 &&
           "end of registration object above normal EBP position!");
  } else if (UsedReg == BasePtr) {
    // LEA offset(%ebp), %esi
    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::LEA32r), BasePtr),
                 FramePtr, false, EndOffset)
        .setMIFlag(MachineInstr::FrameSetup);
    // MOV32rm SavedEBPOffset(%esi), %ebp
    assert(X86FI->getHasSEHFramePtrSave());
    int Offset =
        getFrameIndexReference(MF, X86FI->getSEHFramePtrSaveIndex(), UsedReg)
            .getFixed();
    assert(UsedReg == BasePtr);
    addRegOffset(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV32rm), FramePtr),
                 UsedReg, true, Offset)
        .setMIFlag(MachineInstr::FrameSetup);
  } else {
    llvm_unreachable("32-bit frames with WinEH must use FramePtr or BasePtr");
  }
  return MBBI;
}

int X86FrameLowering::getInitialCFAOffset(const MachineFunction &MF) const {
  return TRI->getSlotSize();
}

Register
X86FrameLowering::getInitialCFARegister(const MachineFunction &MF) const {
  return StackPtr;
}

TargetFrameLowering::DwarfFrameBase
X86FrameLowering::getDwarfFrameBase(const MachineFunction &MF) const {
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  Register FrameRegister = RI->getFrameRegister(MF);
  if (getInitialCFARegister(MF) == FrameRegister &&
      MF.getInfo<X86MachineFunctionInfo>()->hasCFIAdjustCfa()) {
    DwarfFrameBase FrameBase;
    FrameBase.Kind = DwarfFrameBase::CFA;
    FrameBase.Location.Offset =
        -MF.getFrameInfo().getStackSize() - getInitialCFAOffset(MF);
    return FrameBase;
  }

  return DwarfFrameBase{DwarfFrameBase::Register, {FrameRegister}};
}

namespace {
// Struct used by orderFrameObjects to help sort the stack objects.
struct X86FrameSortingObject {
  bool IsValid = false;             // true if we care about this Object.
  unsigned ObjectIndex = 0;         // Index of Object into MFI list.
  unsigned ObjectSize = 0;          // Size of Object in bytes.
  Align ObjectAlignment = Align(1); // Alignment of Object in bytes.
  unsigned ObjectNumUses = 0;       // Object static number of uses.
};

// The comparison function we use for std::sort to order our local
// stack symbols. The current algorithm is to use an estimated
// "density". This takes into consideration the size and number of
// uses each object has in order to roughly minimize code size.
// So, for example, an object of size 16B that is referenced 5 times
// will get higher priority than 4 4B objects referenced 1 time each.
// It's not perfect and we may be able to squeeze a few more bytes out of
// it (for example : 0(esp) requires fewer bytes, symbols allocated at the
// fringe end can have special consideration, given their size is less
// important, etc.), but the algorithmic complexity grows too much to be
// worth the extra gains we get. This gets us pretty close.
// The final order leaves us with objects with highest priority going
// at the end of our list.
struct X86FrameSortingComparator {
  inline bool operator()(const X86FrameSortingObject &A,
                         const X86FrameSortingObject &B) const {
    uint64_t DensityAScaled, DensityBScaled;

    // For consistency in our comparison, all invalid objects are placed
    // at the end. This also allows us to stop walking when we hit the
    // first invalid item after it's all sorted.
    if (!A.IsValid)
      return false;
    if (!B.IsValid)
      return true;

    // The density is calculated by doing :
    //     (double)DensityA = A.ObjectNumUses / A.ObjectSize
    //     (double)DensityB = B.ObjectNumUses / B.ObjectSize
    // Since this approach may cause inconsistencies in
    // the floating point <, >, == comparisons, depending on the floating
    // point model with which the compiler was built, we're going
    // to scale both sides by multiplying with
    // A.ObjectSize * B.ObjectSize. This ends up factoring away
    // the division and, with it, the need for any floating point
    // arithmetic.
    DensityAScaled = static_cast<uint64_t>(A.ObjectNumUses) *
                     static_cast<uint64_t>(B.ObjectSize);
    DensityBScaled = static_cast<uint64_t>(B.ObjectNumUses) *
                     static_cast<uint64_t>(A.ObjectSize);

    // If the two densities are equal, prioritize highest alignment
    // objects. This allows for similar alignment objects
    // to be packed together (given the same density).
    // There's room for improvement here, also, since we can pack
    // similar alignment (different density) objects next to each
    // other to save padding. This will also require further
    // complexity/iterations, and the overall gain isn't worth it,
    // in general. Something to keep in mind, though.
    if (DensityAScaled == DensityBScaled)
      return A.ObjectAlignment < B.ObjectAlignment;

    return DensityAScaled < DensityBScaled;
  }
};
} // namespace

// Order the symbols in the local stack.
// We want to place the local stack objects in some sort of sensible order.
// The heuristic we use is to try and pack them according to static number
// of uses and size of object in order to minimize code size.
void X86FrameLowering::orderFrameObjects(
    const MachineFunction &MF, SmallVectorImpl<int> &ObjectsToAllocate) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Don't waste time if there's nothing to do.
  if (ObjectsToAllocate.empty())
    return;

  // Create an array of all MFI objects. We won't need all of these
  // objects, but we're going to create a full array of them to make
  // it easier to index into when we're counting "uses" down below.
  // We want to be able to easily/cheaply access an object by simply
  // indexing into it, instead of having to search for it every time.
  std::vector<X86FrameSortingObject> SortingObjects(MFI.getObjectIndexEnd());

  // Walk the objects we care about and mark them as such in our working
  // struct.
  for (auto &Obj : ObjectsToAllocate) {
    SortingObjects[Obj].IsValid = true;
    SortingObjects[Obj].ObjectIndex = Obj;
    SortingObjects[Obj].ObjectAlignment = MFI.getObjectAlign(Obj);
    // Set the size.
    int ObjectSize = MFI.getObjectSize(Obj);
    if (ObjectSize == 0)
      // Variable size. Just use 4.
      SortingObjects[Obj].ObjectSize = 4;
    else
      SortingObjects[Obj].ObjectSize = ObjectSize;
  }

  // Count the number of uses for each object.
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        // Check to see if it's a local stack symbol.
        if (!MO.isFI())
          continue;
        int Index = MO.getIndex();
        // Check to see if it falls within our range, and is tagged
        // to require ordering.
        if (Index >= 0 && Index < MFI.getObjectIndexEnd() &&
            SortingObjects[Index].IsValid)
          SortingObjects[Index].ObjectNumUses++;
      }
    }
  }

  // Sort the objects using X86FrameSortingAlgorithm (see its comment for
  // info).
  llvm::stable_sort(SortingObjects, X86FrameSortingComparator());

  // Now modify the original list to represent the final order that
  // we want. The order will depend on whether we're going to access them
  // from the stack pointer or the frame pointer. For SP, the list should
  // end up with the END containing objects that we want with smaller offsets.
  // For FP, it should be flipped.
  int i = 0;
  for (auto &Obj : SortingObjects) {
    // All invalid items are sorted at the end, so it's safe to stop.
    if (!Obj.IsValid)
      break;
    ObjectsToAllocate[i++] = Obj.ObjectIndex;
  }

  // Flip it if we're accessing off of the FP.
  if (!TRI->hasStackRealignment(MF) && hasFP(MF))
    std::reverse(ObjectsToAllocate.begin(), ObjectsToAllocate.end());
}

unsigned
X86FrameLowering::getWinEHParentFrameOffset(const MachineFunction &MF) const {
  // RDX, the parent frame pointer, is homed into 16(%rsp) in the prologue.
  unsigned Offset = 16;
  // RBP is immediately pushed.
  Offset += SlotSize;
  // All callee-saved registers are then pushed.
  Offset += MF.getInfo<X86MachineFunctionInfo>()->getCalleeSavedFrameSize();
  // Every funclet allocates enough stack space for the largest outgoing call.
  Offset += getWinEHFuncletFrameSize(MF);
  return Offset;
}

void X86FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  // Mark the function as not having WinCFI. We will set it back to true in
  // emitPrologue if it gets called and emits CFI.
  MF.setHasWinCFI(false);

  // If we are using Windows x64 CFI, ensure that the stack is always 8 byte
  // aligned. The format doesn't support misaligned stack adjustments.
  if (MF.getTarget().getMCAsmInfo()->usesWindowsCFI())
    MF.getFrameInfo().ensureMaxAlignment(Align(SlotSize));

  // If this function isn't doing Win64-style C++ EH, we don't need to do
  // anything.
  if (STI.is64Bit() && MF.hasEHFunclets() &&
      classifyEHPersonality(MF.getFunction().getPersonalityFn()) ==
          EHPersonality::MSVC_CXX) {
    adjustFrameForMsvcCxxEh(MF);
  }
}

void X86FrameLowering::adjustFrameForMsvcCxxEh(MachineFunction &MF) const {
  // Win64 C++ EH needs to allocate the UnwindHelp object at some fixed offset
  // relative to RSP after the prologue.  Find the offset of the last fixed
  // object, so that we can allocate a slot immediately following it. If there
  // were no fixed objects, use offset -SlotSize, which is immediately after the
  // return address. Fixed objects have negative frame indices.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  WinEHFuncInfo &EHInfo = *MF.getWinEHFuncInfo();
  int64_t MinFixedObjOffset = -SlotSize;
  for (int I = MFI.getObjectIndexBegin(); I < 0; ++I)
    MinFixedObjOffset = std::min(MinFixedObjOffset, MFI.getObjectOffset(I));

  for (WinEHTryBlockMapEntry &TBME : EHInfo.TryBlockMap) {
    for (WinEHHandlerType &H : TBME.HandlerArray) {
      int FrameIndex = H.CatchObj.FrameIndex;
      if (FrameIndex != INT_MAX) {
        // Ensure alignment.
        unsigned Align = MFI.getObjectAlign(FrameIndex).value();
        MinFixedObjOffset -= std::abs(MinFixedObjOffset) % Align;
        MinFixedObjOffset -= MFI.getObjectSize(FrameIndex);
        MFI.setObjectOffset(FrameIndex, MinFixedObjOffset);
      }
    }
  }

  // Ensure alignment.
  MinFixedObjOffset -= std::abs(MinFixedObjOffset) % 8;
  int64_t UnwindHelpOffset = MinFixedObjOffset - SlotSize;
  int UnwindHelpFI =
      MFI.CreateFixedObject(SlotSize, UnwindHelpOffset, /*IsImmutable=*/false);
  EHInfo.UnwindHelpFrameIdx = UnwindHelpFI;

  // Store -2 into UnwindHelp on function entry. We have to scan forwards past
  // other frame setup instructions.
  MachineBasicBlock &MBB = MF.front();
  auto MBBI = MBB.begin();
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
    ++MBBI;

  DebugLoc DL = MBB.findDebugLoc(MBBI);
  addFrameReference(BuildMI(MBB, MBBI, DL, TII.get(X86::MOV64mi32)),
                    UnwindHelpFI)
      .addImm(-2);
}

const ReturnProtectorLowering *X86FrameLowering::getReturnProtector() const {
  return &RPL;
}

void X86FrameLowering::processFunctionBeforeFrameIndicesReplaced(
    MachineFunction &MF, RegScavenger *RS) const {
  auto *X86FI = MF.getInfo<X86MachineFunctionInfo>();

  if (STI.is32Bit() && MF.hasEHFunclets())
    restoreWinEHStackPointersInParent(MF);
  // We have emitted prolog and epilog. Don't need stack pointer saving
  // instruction any more.
  if (MachineInstr *MI = X86FI->getStackPtrSaveMI()) {
    MI->eraseFromParent();
    X86FI->setStackPtrSaveMI(nullptr);
  }
}

void X86FrameLowering::restoreWinEHStackPointersInParent(
    MachineFunction &MF) const {
  // 32-bit functions have to restore stack pointers when control is transferred
  // back to the parent function. These blocks are identified as eh pads that
  // are not funclet entries.
  bool IsSEH = isAsynchronousEHPersonality(
      classifyEHPersonality(MF.getFunction().getPersonalityFn()));
  for (MachineBasicBlock &MBB : MF) {
    bool NeedsRestore = MBB.isEHPad() && !MBB.isEHFuncletEntry();
    if (NeedsRestore)
      restoreWin32EHStackPointers(MBB, MBB.begin(), DebugLoc(),
                                  /*RestoreSP=*/IsSEH);
  }
}
