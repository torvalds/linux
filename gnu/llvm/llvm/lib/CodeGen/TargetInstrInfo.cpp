//===-- TargetInstrInfo.cpp - Target Instruction Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static cl::opt<bool> DisableHazardRecognizer(
  "disable-sched-hazard", cl::Hidden, cl::init(false),
  cl::desc("Disable hazard detection during preRA scheduling"));

TargetInstrInfo::~TargetInstrInfo() = default;

const TargetRegisterClass*
TargetInstrInfo::getRegClass(const MCInstrDesc &MCID, unsigned OpNum,
                             const TargetRegisterInfo *TRI,
                             const MachineFunction &MF) const {
  if (OpNum >= MCID.getNumOperands())
    return nullptr;

  short RegClass = MCID.operands()[OpNum].RegClass;
  if (MCID.operands()[OpNum].isLookupPtrRegClass())
    return TRI->getPointerRegClass(MF, RegClass);

  // Instructions like INSERT_SUBREG do not have fixed register classes.
  if (RegClass < 0)
    return nullptr;

  // Otherwise just look it up normally.
  return TRI->getRegClass(RegClass);
}

/// insertNoop - Insert a noop into the instruction stream at the specified
/// point.
void TargetInstrInfo::insertNoop(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI) const {
  llvm_unreachable("Target didn't implement insertNoop!");
}

/// insertNoops - Insert noops into the instruction stream at the specified
/// point.
void TargetInstrInfo::insertNoops(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  unsigned Quantity) const {
  for (unsigned i = 0; i < Quantity; ++i)
    insertNoop(MBB, MI);
}

static bool isAsmComment(const char *Str, const MCAsmInfo &MAI) {
  return strncmp(Str, MAI.getCommentString().data(),
                 MAI.getCommentString().size()) == 0;
}

/// Measure the specified inline asm to determine an approximation of its
/// length.
/// Comments (which run till the next SeparatorString or newline) do not
/// count as an instruction.
/// Any other non-whitespace text is considered an instruction, with
/// multiple instructions separated by SeparatorString or newlines.
/// Variable-length instructions are not handled here; this function
/// may be overloaded in the target code to do that.
/// We implement a special case of the .space directive which takes only a
/// single integer argument in base 10 that is the size in bytes. This is a
/// restricted form of the GAS directive in that we only interpret
/// simple--i.e. not a logical or arithmetic expression--size values without
/// the optional fill value. This is primarily used for creating arbitrary
/// sized inline asm blocks for testing purposes.
unsigned TargetInstrInfo::getInlineAsmLength(
  const char *Str,
  const MCAsmInfo &MAI, const TargetSubtargetInfo *STI) const {
  // Count the number of instructions in the asm.
  bool AtInsnStart = true;
  unsigned Length = 0;
  const unsigned MaxInstLength = MAI.getMaxInstLength(STI);
  for (; *Str; ++Str) {
    if (*Str == '\n' || strncmp(Str, MAI.getSeparatorString(),
                                strlen(MAI.getSeparatorString())) == 0) {
      AtInsnStart = true;
    } else if (isAsmComment(Str, MAI)) {
      // Stop counting as an instruction after a comment until the next
      // separator.
      AtInsnStart = false;
    }

    if (AtInsnStart && !isSpace(static_cast<unsigned char>(*Str))) {
      unsigned AddLength = MaxInstLength;
      if (strncmp(Str, ".space", 6) == 0) {
        char *EStr;
        int SpaceSize;
        SpaceSize = strtol(Str + 6, &EStr, 10);
        SpaceSize = SpaceSize < 0 ? 0 : SpaceSize;
        while (*EStr != '\n' && isSpace(static_cast<unsigned char>(*EStr)))
          ++EStr;
        if (*EStr == '\0' || *EStr == '\n' ||
            isAsmComment(EStr, MAI)) // Successfully parsed .space argument
          AddLength = SpaceSize;
      }
      Length += AddLength;
      AtInsnStart = false;
    }
  }

  return Length;
}

/// ReplaceTailWithBranchTo - Delete the instruction OldInst and everything
/// after it, replacing it with an unconditional branch to NewDest.
void
TargetInstrInfo::ReplaceTailWithBranchTo(MachineBasicBlock::iterator Tail,
                                         MachineBasicBlock *NewDest) const {
  MachineBasicBlock *MBB = Tail->getParent();

  // Remove all the old successors of MBB from the CFG.
  while (!MBB->succ_empty())
    MBB->removeSuccessor(MBB->succ_begin());

  // Save off the debug loc before erasing the instruction.
  DebugLoc DL = Tail->getDebugLoc();

  // Update call site info and remove all the dead instructions
  // from the end of MBB.
  while (Tail != MBB->end()) {
    auto MI = Tail++;
    if (MI->shouldUpdateCallSiteInfo())
      MBB->getParent()->eraseCallSiteInfo(&*MI);
    MBB->erase(MI);
  }

  // If MBB isn't immediately before MBB, insert a branch to it.
  if (++MachineFunction::iterator(MBB) != MachineFunction::iterator(NewDest))
    insertBranch(*MBB, NewDest, nullptr, SmallVector<MachineOperand, 0>(), DL);
  MBB->addSuccessor(NewDest);
}

MachineInstr *TargetInstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                      bool NewMI, unsigned Idx1,
                                                      unsigned Idx2) const {
  const MCInstrDesc &MCID = MI.getDesc();
  bool HasDef = MCID.getNumDefs();
  if (HasDef && !MI.getOperand(0).isReg())
    // No idea how to commute this instruction. Target should implement its own.
    return nullptr;

  unsigned CommutableOpIdx1 = Idx1; (void)CommutableOpIdx1;
  unsigned CommutableOpIdx2 = Idx2; (void)CommutableOpIdx2;
  assert(findCommutedOpIndices(MI, CommutableOpIdx1, CommutableOpIdx2) &&
         CommutableOpIdx1 == Idx1 && CommutableOpIdx2 == Idx2 &&
         "TargetInstrInfo::CommuteInstructionImpl(): not commutable operands.");
  assert(MI.getOperand(Idx1).isReg() && MI.getOperand(Idx2).isReg() &&
         "This only knows how to commute register operands so far");

  Register Reg0 = HasDef ? MI.getOperand(0).getReg() : Register();
  Register Reg1 = MI.getOperand(Idx1).getReg();
  Register Reg2 = MI.getOperand(Idx2).getReg();
  unsigned SubReg0 = HasDef ? MI.getOperand(0).getSubReg() : 0;
  unsigned SubReg1 = MI.getOperand(Idx1).getSubReg();
  unsigned SubReg2 = MI.getOperand(Idx2).getSubReg();
  bool Reg1IsKill = MI.getOperand(Idx1).isKill();
  bool Reg2IsKill = MI.getOperand(Idx2).isKill();
  bool Reg1IsUndef = MI.getOperand(Idx1).isUndef();
  bool Reg2IsUndef = MI.getOperand(Idx2).isUndef();
  bool Reg1IsInternal = MI.getOperand(Idx1).isInternalRead();
  bool Reg2IsInternal = MI.getOperand(Idx2).isInternalRead();
  // Avoid calling isRenamable for virtual registers since we assert that
  // renamable property is only queried/set for physical registers.
  bool Reg1IsRenamable =
      Reg1.isPhysical() ? MI.getOperand(Idx1).isRenamable() : false;
  bool Reg2IsRenamable =
      Reg2.isPhysical() ? MI.getOperand(Idx2).isRenamable() : false;
  // If destination is tied to either of the commuted source register, then
  // it must be updated.
  if (HasDef && Reg0 == Reg1 &&
      MI.getDesc().getOperandConstraint(Idx1, MCOI::TIED_TO) == 0) {
    Reg2IsKill = false;
    Reg0 = Reg2;
    SubReg0 = SubReg2;
  } else if (HasDef && Reg0 == Reg2 &&
             MI.getDesc().getOperandConstraint(Idx2, MCOI::TIED_TO) == 0) {
    Reg1IsKill = false;
    Reg0 = Reg1;
    SubReg0 = SubReg1;
  }

  MachineInstr *CommutedMI = nullptr;
  if (NewMI) {
    // Create a new instruction.
    MachineFunction &MF = *MI.getMF();
    CommutedMI = MF.CloneMachineInstr(&MI);
  } else {
    CommutedMI = &MI;
  }

  if (HasDef) {
    CommutedMI->getOperand(0).setReg(Reg0);
    CommutedMI->getOperand(0).setSubReg(SubReg0);
  }
  CommutedMI->getOperand(Idx2).setReg(Reg1);
  CommutedMI->getOperand(Idx1).setReg(Reg2);
  CommutedMI->getOperand(Idx2).setSubReg(SubReg1);
  CommutedMI->getOperand(Idx1).setSubReg(SubReg2);
  CommutedMI->getOperand(Idx2).setIsKill(Reg1IsKill);
  CommutedMI->getOperand(Idx1).setIsKill(Reg2IsKill);
  CommutedMI->getOperand(Idx2).setIsUndef(Reg1IsUndef);
  CommutedMI->getOperand(Idx1).setIsUndef(Reg2IsUndef);
  CommutedMI->getOperand(Idx2).setIsInternalRead(Reg1IsInternal);
  CommutedMI->getOperand(Idx1).setIsInternalRead(Reg2IsInternal);
  // Avoid calling setIsRenamable for virtual registers since we assert that
  // renamable property is only queried/set for physical registers.
  if (Reg1.isPhysical())
    CommutedMI->getOperand(Idx2).setIsRenamable(Reg1IsRenamable);
  if (Reg2.isPhysical())
    CommutedMI->getOperand(Idx1).setIsRenamable(Reg2IsRenamable);
  return CommutedMI;
}

MachineInstr *TargetInstrInfo::commuteInstruction(MachineInstr &MI, bool NewMI,
                                                  unsigned OpIdx1,
                                                  unsigned OpIdx2) const {
  // If OpIdx1 or OpIdx2 is not specified, then this method is free to choose
  // any commutable operand, which is done in findCommutedOpIndices() method
  // called below.
  if ((OpIdx1 == CommuteAnyOperandIndex || OpIdx2 == CommuteAnyOperandIndex) &&
      !findCommutedOpIndices(MI, OpIdx1, OpIdx2)) {
    assert(MI.isCommutable() &&
           "Precondition violation: MI must be commutable.");
    return nullptr;
  }
  return commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

bool TargetInstrInfo::fixCommutedOpIndices(unsigned &ResultIdx1,
                                           unsigned &ResultIdx2,
                                           unsigned CommutableOpIdx1,
                                           unsigned CommutableOpIdx2) {
  if (ResultIdx1 == CommuteAnyOperandIndex &&
      ResultIdx2 == CommuteAnyOperandIndex) {
    ResultIdx1 = CommutableOpIdx1;
    ResultIdx2 = CommutableOpIdx2;
  } else if (ResultIdx1 == CommuteAnyOperandIndex) {
    if (ResultIdx2 == CommutableOpIdx1)
      ResultIdx1 = CommutableOpIdx2;
    else if (ResultIdx2 == CommutableOpIdx2)
      ResultIdx1 = CommutableOpIdx1;
    else
      return false;
  } else if (ResultIdx2 == CommuteAnyOperandIndex) {
    if (ResultIdx1 == CommutableOpIdx1)
      ResultIdx2 = CommutableOpIdx2;
    else if (ResultIdx1 == CommutableOpIdx2)
      ResultIdx2 = CommutableOpIdx1;
    else
      return false;
  } else
    // Check that the result operand indices match the given commutable
    // operand indices.
    return (ResultIdx1 == CommutableOpIdx1 && ResultIdx2 == CommutableOpIdx2) ||
           (ResultIdx1 == CommutableOpIdx2 && ResultIdx2 == CommutableOpIdx1);

  return true;
}

bool TargetInstrInfo::findCommutedOpIndices(const MachineInstr &MI,
                                            unsigned &SrcOpIdx1,
                                            unsigned &SrcOpIdx2) const {
  assert(!MI.isBundle() &&
         "TargetInstrInfo::findCommutedOpIndices() can't handle bundles");

  const MCInstrDesc &MCID = MI.getDesc();
  if (!MCID.isCommutable())
    return false;

  // This assumes v0 = op v1, v2 and commuting would swap v1 and v2. If this
  // is not true, then the target must implement this.
  unsigned CommutableOpIdx1 = MCID.getNumDefs();
  unsigned CommutableOpIdx2 = CommutableOpIdx1 + 1;
  if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2,
                            CommutableOpIdx1, CommutableOpIdx2))
    return false;

  if (!MI.getOperand(SrcOpIdx1).isReg() || !MI.getOperand(SrcOpIdx2).isReg())
    // No idea.
    return false;
  return true;
}

bool TargetInstrInfo::isUnpredicatedTerminator(const MachineInstr &MI) const {
  if (!MI.isTerminator()) return false;

  // Conditional branch is a special case.
  if (MI.isBranch() && !MI.isBarrier())
    return true;
  if (!MI.isPredicable())
    return true;
  return !isPredicated(MI);
}

bool TargetInstrInfo::PredicateInstruction(
    MachineInstr &MI, ArrayRef<MachineOperand> Pred) const {
  bool MadeChange = false;

  assert(!MI.isBundle() &&
         "TargetInstrInfo::PredicateInstruction() can't handle bundles");

  const MCInstrDesc &MCID = MI.getDesc();
  if (!MI.isPredicable())
    return false;

  for (unsigned j = 0, i = 0, e = MI.getNumOperands(); i != e; ++i) {
    if (MCID.operands()[i].isPredicate()) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.isReg()) {
        MO.setReg(Pred[j].getReg());
        MadeChange = true;
      } else if (MO.isImm()) {
        MO.setImm(Pred[j].getImm());
        MadeChange = true;
      } else if (MO.isMBB()) {
        MO.setMBB(Pred[j].getMBB());
        MadeChange = true;
      }
      ++j;
    }
  }
  return MadeChange;
}

bool TargetInstrInfo::hasLoadFromStackSlot(
    const MachineInstr &MI,
    SmallVectorImpl<const MachineMemOperand *> &Accesses) const {
  size_t StartSize = Accesses.size();
  for (MachineInstr::mmo_iterator o = MI.memoperands_begin(),
                                  oe = MI.memoperands_end();
       o != oe; ++o) {
    if ((*o)->isLoad() &&
        isa_and_nonnull<FixedStackPseudoSourceValue>((*o)->getPseudoValue()))
      Accesses.push_back(*o);
  }
  return Accesses.size() != StartSize;
}

bool TargetInstrInfo::hasStoreToStackSlot(
    const MachineInstr &MI,
    SmallVectorImpl<const MachineMemOperand *> &Accesses) const {
  size_t StartSize = Accesses.size();
  for (MachineInstr::mmo_iterator o = MI.memoperands_begin(),
                                  oe = MI.memoperands_end();
       o != oe; ++o) {
    if ((*o)->isStore() &&
        isa_and_nonnull<FixedStackPseudoSourceValue>((*o)->getPseudoValue()))
      Accesses.push_back(*o);
  }
  return Accesses.size() != StartSize;
}

bool TargetInstrInfo::getStackSlotRange(const TargetRegisterClass *RC,
                                        unsigned SubIdx, unsigned &Size,
                                        unsigned &Offset,
                                        const MachineFunction &MF) const {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  if (!SubIdx) {
    Size = TRI->getSpillSize(*RC);
    Offset = 0;
    return true;
  }
  unsigned BitSize = TRI->getSubRegIdxSize(SubIdx);
  // Convert bit size to byte size.
  if (BitSize % 8)
    return false;

  int BitOffset = TRI->getSubRegIdxOffset(SubIdx);
  if (BitOffset < 0 || BitOffset % 8)
    return false;

  Size = BitSize / 8;
  Offset = (unsigned)BitOffset / 8;

  assert(TRI->getSpillSize(*RC) >= (Offset + Size) && "bad subregister range");

  if (!MF.getDataLayout().isLittleEndian()) {
    Offset = TRI->getSpillSize(*RC) - (Offset + Size);
  }
  return true;
}

void TargetInstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    Register DestReg, unsigned SubIdx,
                                    const MachineInstr &Orig,
                                    const TargetRegisterInfo &TRI) const {
  MachineInstr *MI = MBB.getParent()->CloneMachineInstr(&Orig);
  MI->substituteRegister(MI->getOperand(0).getReg(), DestReg, SubIdx, TRI);
  MBB.insert(I, MI);
}

bool TargetInstrInfo::produceSameValue(const MachineInstr &MI0,
                                       const MachineInstr &MI1,
                                       const MachineRegisterInfo *MRI) const {
  return MI0.isIdenticalTo(MI1, MachineInstr::IgnoreVRegDefs);
}

MachineInstr &
TargetInstrInfo::duplicate(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator InsertBefore,
                           const MachineInstr &Orig) const {
  MachineFunction &MF = *MBB.getParent();
  // CFI instructions are marked as non-duplicable, because Darwin compact
  // unwind info emission can't handle multiple prologue setups.
  assert((!Orig.isNotDuplicable() ||
          (!MF.getTarget().getTargetTriple().isOSDarwin() &&
           Orig.isCFIInstruction())) &&
         "Instruction cannot be duplicated");

  return MF.cloneMachineInstrBundle(MBB, InsertBefore, Orig);
}

// If the COPY instruction in MI can be folded to a stack operation, return
// the register class to use.
static const TargetRegisterClass *canFoldCopy(const MachineInstr &MI,
                                              const TargetInstrInfo &TII,
                                              unsigned FoldIdx) {
  assert(TII.isCopyInstr(MI) && "MI must be a COPY instruction");
  if (MI.getNumOperands() != 2)
    return nullptr;
  assert(FoldIdx<2 && "FoldIdx refers no nonexistent operand");

  const MachineOperand &FoldOp = MI.getOperand(FoldIdx);
  const MachineOperand &LiveOp = MI.getOperand(1 - FoldIdx);

  if (FoldOp.getSubReg() || LiveOp.getSubReg())
    return nullptr;

  Register FoldReg = FoldOp.getReg();
  Register LiveReg = LiveOp.getReg();

  assert(FoldReg.isVirtual() && "Cannot fold physregs");

  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  const TargetRegisterClass *RC = MRI.getRegClass(FoldReg);

  if (LiveOp.getReg().isPhysical())
    return RC->contains(LiveOp.getReg()) ? RC : nullptr;

  if (RC->hasSubClassEq(MRI.getRegClass(LiveReg)))
    return RC;

  // FIXME: Allow folding when register classes are memory compatible.
  return nullptr;
}

MCInst TargetInstrInfo::getNop() const { llvm_unreachable("Not implemented"); }

std::pair<unsigned, unsigned>
TargetInstrInfo::getPatchpointUnfoldableRange(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::STACKMAP:
    // StackMapLiveValues are foldable
    return std::make_pair(0, StackMapOpers(&MI).getVarIdx());
  case TargetOpcode::PATCHPOINT:
    // For PatchPoint, the call args are not foldable (even if reported in the
    // stackmap e.g. via anyregcc).
    return std::make_pair(0, PatchPointOpers(&MI).getVarIdx());
  case TargetOpcode::STATEPOINT:
    // For statepoints, fold deopt and gc arguments, but not call arguments.
    return std::make_pair(MI.getNumDefs(), StatepointOpers(&MI).getVarIdx());
  default:
    llvm_unreachable("unexpected stackmap opcode");
  }
}

static MachineInstr *foldPatchpoint(MachineFunction &MF, MachineInstr &MI,
                                    ArrayRef<unsigned> Ops, int FrameIndex,
                                    const TargetInstrInfo &TII) {
  unsigned StartIdx = 0;
  unsigned NumDefs = 0;
  // getPatchpointUnfoldableRange throws guarantee if MI is not a patchpoint.
  std::tie(NumDefs, StartIdx) = TII.getPatchpointUnfoldableRange(MI);

  unsigned DefToFoldIdx = MI.getNumOperands();

  // Return false if any operands requested for folding are not foldable (not
  // part of the stackmap's live values).
  for (unsigned Op : Ops) {
    if (Op < NumDefs) {
      assert(DefToFoldIdx == MI.getNumOperands() && "Folding multiple defs");
      DefToFoldIdx = Op;
    } else if (Op < StartIdx) {
      return nullptr;
    }
    if (MI.getOperand(Op).isTied())
      return nullptr;
  }

  MachineInstr *NewMI =
      MF.CreateMachineInstr(TII.get(MI.getOpcode()), MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, NewMI);

  // No need to fold return, the meta data, and function arguments
  for (unsigned i = 0; i < StartIdx; ++i)
    if (i != DefToFoldIdx)
      MIB.add(MI.getOperand(i));

  for (unsigned i = StartIdx, e = MI.getNumOperands(); i < e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    unsigned TiedTo = e;
    (void)MI.isRegTiedToDefOperand(i, &TiedTo);

    if (is_contained(Ops, i)) {
      assert(TiedTo == e && "Cannot fold tied operands");
      unsigned SpillSize;
      unsigned SpillOffset;
      // Compute the spill slot size and offset.
      const TargetRegisterClass *RC =
        MF.getRegInfo().getRegClass(MO.getReg());
      bool Valid =
          TII.getStackSlotRange(RC, MO.getSubReg(), SpillSize, SpillOffset, MF);
      if (!Valid)
        report_fatal_error("cannot spill patchpoint subregister operand");
      MIB.addImm(StackMaps::IndirectMemRefOp);
      MIB.addImm(SpillSize);
      MIB.addFrameIndex(FrameIndex);
      MIB.addImm(SpillOffset);
    } else {
      MIB.add(MO);
      if (TiedTo < e) {
        assert(TiedTo < NumDefs && "Bad tied operand");
        if (TiedTo > DefToFoldIdx)
          --TiedTo;
        NewMI->tieOperands(TiedTo, NewMI->getNumOperands() - 1);
      }
    }
  }
  return NewMI;
}

static void foldInlineAsmMemOperand(MachineInstr *MI, unsigned OpNo, int FI,
                                    const TargetInstrInfo &TII) {
  // If the machine operand is tied, untie it first.
  if (MI->getOperand(OpNo).isTied()) {
    unsigned TiedTo = MI->findTiedOperandIdx(OpNo);
    MI->untieRegOperand(OpNo);
    // Intentional recursion!
    foldInlineAsmMemOperand(MI, TiedTo, FI, TII);
  }

  SmallVector<MachineOperand, 5> NewOps;
  TII.getFrameIndexOperands(NewOps, FI);
  assert(!NewOps.empty() && "getFrameIndexOperands didn't create any operands");
  MI->removeOperand(OpNo);
  MI->insert(MI->operands_begin() + OpNo, NewOps);

  // Change the previous operand to a MemKind InlineAsm::Flag. The second param
  // is the per-target number of operands that represent the memory operand
  // excluding this one (MD). This includes MO.
  InlineAsm::Flag F(InlineAsm::Kind::Mem, NewOps.size());
  F.setMemConstraint(InlineAsm::ConstraintCode::m);
  MachineOperand &MD = MI->getOperand(OpNo - 1);
  MD.setImm(F);
}

// Returns nullptr if not possible to fold.
static MachineInstr *foldInlineAsmMemOperand(MachineInstr &MI,
                                             ArrayRef<unsigned> Ops, int FI,
                                             const TargetInstrInfo &TII) {
  assert(MI.isInlineAsm() && "wrong opcode");
  if (Ops.size() > 1)
    return nullptr;
  unsigned Op = Ops[0];
  assert(Op && "should never be first operand");
  assert(MI.getOperand(Op).isReg() && "shouldn't be folding non-reg operands");

  if (!MI.mayFoldInlineAsmRegOp(Op))
    return nullptr;

  MachineInstr &NewMI = TII.duplicate(*MI.getParent(), MI.getIterator(), MI);

  foldInlineAsmMemOperand(&NewMI, Op, FI, TII);

  // Update mayload/maystore metadata, and memoperands.
  const VirtRegInfo &RI =
      AnalyzeVirtRegInBundle(MI, MI.getOperand(Op).getReg());
  MachineOperand &ExtraMO = NewMI.getOperand(InlineAsm::MIOp_ExtraInfo);
  MachineMemOperand::Flags Flags = MachineMemOperand::MONone;
  if (RI.Reads) {
    ExtraMO.setImm(ExtraMO.getImm() | InlineAsm::Extra_MayLoad);
    Flags |= MachineMemOperand::MOLoad;
  }
  if (RI.Writes) {
    ExtraMO.setImm(ExtraMO.getImm() | InlineAsm::Extra_MayStore);
    Flags |= MachineMemOperand::MOStore;
  }
  MachineFunction *MF = NewMI.getMF();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MachinePointerInfo::getFixedStack(*MF, FI), Flags, MFI.getObjectSize(FI),
      MFI.getObjectAlign(FI));
  NewMI.addMemOperand(*MF, MMO);

  return &NewMI;
}

MachineInstr *TargetInstrInfo::foldMemoryOperand(MachineInstr &MI,
                                                 ArrayRef<unsigned> Ops, int FI,
                                                 LiveIntervals *LIS,
                                                 VirtRegMap *VRM) const {
  auto Flags = MachineMemOperand::MONone;
  for (unsigned OpIdx : Ops)
    Flags |= MI.getOperand(OpIdx).isDef() ? MachineMemOperand::MOStore
                                          : MachineMemOperand::MOLoad;

  MachineBasicBlock *MBB = MI.getParent();
  assert(MBB && "foldMemoryOperand needs an inserted instruction");
  MachineFunction &MF = *MBB->getParent();

  // If we're not folding a load into a subreg, the size of the load is the
  // size of the spill slot. But if we are, we need to figure out what the
  // actual load size is.
  int64_t MemSize = 0;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  if (Flags & MachineMemOperand::MOStore) {
    MemSize = MFI.getObjectSize(FI);
  } else {
    for (unsigned OpIdx : Ops) {
      int64_t OpSize = MFI.getObjectSize(FI);

      if (auto SubReg = MI.getOperand(OpIdx).getSubReg()) {
        unsigned SubRegSize = TRI->getSubRegIdxSize(SubReg);
        if (SubRegSize > 0 && !(SubRegSize % 8))
          OpSize = SubRegSize / 8;
      }

      MemSize = std::max(MemSize, OpSize);
    }
  }

  assert(MemSize && "Did not expect a zero-sized stack slot");

  MachineInstr *NewMI = nullptr;

  if (MI.getOpcode() == TargetOpcode::STACKMAP ||
      MI.getOpcode() == TargetOpcode::PATCHPOINT ||
      MI.getOpcode() == TargetOpcode::STATEPOINT) {
    // Fold stackmap/patchpoint.
    NewMI = foldPatchpoint(MF, MI, Ops, FI, *this);
    if (NewMI)
      MBB->insert(MI, NewMI);
  } else if (MI.isInlineAsm()) {
    return foldInlineAsmMemOperand(MI, Ops, FI, *this);
  } else {
    // Ask the target to do the actual folding.
    NewMI = foldMemoryOperandImpl(MF, MI, Ops, MI, FI, LIS, VRM);
  }

  if (NewMI) {
    NewMI->setMemRefs(MF, MI.memoperands());
    // Add a memory operand, foldMemoryOperandImpl doesn't do that.
    assert((!(Flags & MachineMemOperand::MOStore) ||
            NewMI->mayStore()) &&
           "Folded a def to a non-store!");
    assert((!(Flags & MachineMemOperand::MOLoad) ||
            NewMI->mayLoad()) &&
           "Folded a use to a non-load!");
    assert(MFI.getObjectOffset(FI) != -1);
    MachineMemOperand *MMO =
        MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI),
                                Flags, MemSize, MFI.getObjectAlign(FI));
    NewMI->addMemOperand(MF, MMO);

    // The pass "x86 speculative load hardening" always attaches symbols to
    // call instructions. We need copy it form old instruction.
    NewMI->cloneInstrSymbols(MF, MI);

    return NewMI;
  }

  // Straight COPY may fold as load/store.
  if (!isCopyInstr(MI) || Ops.size() != 1)
    return nullptr;

  const TargetRegisterClass *RC = canFoldCopy(MI, *this, Ops[0]);
  if (!RC)
    return nullptr;

  const MachineOperand &MO = MI.getOperand(1 - Ops[0]);
  MachineBasicBlock::iterator Pos = MI;

  if (Flags == MachineMemOperand::MOStore)
    storeRegToStackSlot(*MBB, Pos, MO.getReg(), MO.isKill(), FI, RC, TRI,
                        Register());
  else
    loadRegFromStackSlot(*MBB, Pos, MO.getReg(), FI, RC, TRI, Register());
  return &*--Pos;
}

MachineInstr *TargetInstrInfo::foldMemoryOperand(MachineInstr &MI,
                                                 ArrayRef<unsigned> Ops,
                                                 MachineInstr &LoadMI,
                                                 LiveIntervals *LIS) const {
  assert(LoadMI.canFoldAsLoad() && "LoadMI isn't foldable!");
#ifndef NDEBUG
  for (unsigned OpIdx : Ops)
    assert(MI.getOperand(OpIdx).isUse() && "Folding load into def!");
#endif

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();

  // Ask the target to do the actual folding.
  MachineInstr *NewMI = nullptr;
  int FrameIndex = 0;

  if ((MI.getOpcode() == TargetOpcode::STACKMAP ||
       MI.getOpcode() == TargetOpcode::PATCHPOINT ||
       MI.getOpcode() == TargetOpcode::STATEPOINT) &&
      isLoadFromStackSlot(LoadMI, FrameIndex)) {
    // Fold stackmap/patchpoint.
    NewMI = foldPatchpoint(MF, MI, Ops, FrameIndex, *this);
    if (NewMI)
      NewMI = &*MBB.insert(MI, NewMI);
  } else if (MI.isInlineAsm() && isLoadFromStackSlot(LoadMI, FrameIndex)) {
    return foldInlineAsmMemOperand(MI, Ops, FrameIndex, *this);
  } else {
    // Ask the target to do the actual folding.
    NewMI = foldMemoryOperandImpl(MF, MI, Ops, MI, LoadMI, LIS);
  }

  if (!NewMI)
    return nullptr;

  // Copy the memoperands from the load to the folded instruction.
  if (MI.memoperands_empty()) {
    NewMI->setMemRefs(MF, LoadMI.memoperands());
  } else {
    // Handle the rare case of folding multiple loads.
    NewMI->setMemRefs(MF, MI.memoperands());
    for (MachineInstr::mmo_iterator I = LoadMI.memoperands_begin(),
                                    E = LoadMI.memoperands_end();
         I != E; ++I) {
      NewMI->addMemOperand(MF, *I);
    }
  }
  return NewMI;
}

/// transferImplicitOperands - MI is a pseudo-instruction, and the lowered
/// replacement instructions immediately precede it.  Copy any implicit
/// operands from MI to the replacement instruction.
static void transferImplicitOperands(MachineInstr *MI,
                                     const TargetRegisterInfo *TRI) {
  MachineBasicBlock::iterator CopyMI = MI;
  --CopyMI;

  Register DstReg = MI->getOperand(0).getReg();
  for (const MachineOperand &MO : MI->implicit_operands()) {
    CopyMI->addOperand(MO);

    // Be conservative about preserving kills when subregister defs are
    // involved. If there was implicit kill of a super-register overlapping the
    // copy result, we would kill the subregisters previous copies defined.

    if (MO.isKill() && TRI->regsOverlap(DstReg, MO.getReg()))
      CopyMI->getOperand(CopyMI->getNumOperands() - 1).setIsKill(false);
  }
}

void TargetInstrInfo::lowerCopy(MachineInstr *MI,
                                const TargetRegisterInfo *TRI) const {
  if (MI->allDefsAreDead()) {
    MI->setDesc(get(TargetOpcode::KILL));
    return;
  }

  MachineOperand &DstMO = MI->getOperand(0);
  MachineOperand &SrcMO = MI->getOperand(1);

  bool IdentityCopy = (SrcMO.getReg() == DstMO.getReg());
  if (IdentityCopy || SrcMO.isUndef()) {
    // No need to insert an identity copy instruction, but replace with a KILL
    // if liveness is changed.
    if (SrcMO.isUndef() || MI->getNumOperands() > 2) {
      // We must make sure the super-register gets killed. Replace the
      // instruction with KILL.
      MI->setDesc(get(TargetOpcode::KILL));
      return;
    }
    // Vanilla identity copy.
    MI->eraseFromParent();
    return;
  }

  copyPhysReg(*MI->getParent(), MI, MI->getDebugLoc(), DstMO.getReg(),
              SrcMO.getReg(), SrcMO.isKill());

  if (MI->getNumOperands() > 2)
    transferImplicitOperands(MI, TRI);
  MI->eraseFromParent();
}

bool TargetInstrInfo::hasReassociableOperands(
    const MachineInstr &Inst, const MachineBasicBlock *MBB) const {
  const MachineOperand &Op1 = Inst.getOperand(1);
  const MachineOperand &Op2 = Inst.getOperand(2);
  const MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

  // We need virtual register definitions for the operands that we will
  // reassociate.
  MachineInstr *MI1 = nullptr;
  MachineInstr *MI2 = nullptr;
  if (Op1.isReg() && Op1.getReg().isVirtual())
    MI1 = MRI.getUniqueVRegDef(Op1.getReg());
  if (Op2.isReg() && Op2.getReg().isVirtual())
    MI2 = MRI.getUniqueVRegDef(Op2.getReg());

  // And at least one operand must be defined in MBB.
  return MI1 && MI2 && (MI1->getParent() == MBB || MI2->getParent() == MBB);
}

bool TargetInstrInfo::areOpcodesEqualOrInverse(unsigned Opcode1,
                                               unsigned Opcode2) const {
  return Opcode1 == Opcode2 || getInverseOpcode(Opcode1) == Opcode2;
}

bool TargetInstrInfo::hasReassociableSibling(const MachineInstr &Inst,
                                             bool &Commuted) const {
  const MachineBasicBlock *MBB = Inst.getParent();
  const MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  MachineInstr *MI1 = MRI.getUniqueVRegDef(Inst.getOperand(1).getReg());
  MachineInstr *MI2 = MRI.getUniqueVRegDef(Inst.getOperand(2).getReg());
  unsigned Opcode = Inst.getOpcode();

  // If only one operand has the same or inverse opcode and it's the second
  // source operand, the operands must be commuted.
  Commuted = !areOpcodesEqualOrInverse(Opcode, MI1->getOpcode()) &&
             areOpcodesEqualOrInverse(Opcode, MI2->getOpcode());
  if (Commuted)
    std::swap(MI1, MI2);

  // 1. The previous instruction must be the same type as Inst.
  // 2. The previous instruction must also be associative/commutative or be the
  //    inverse of such an operation (this can be different even for
  //    instructions with the same opcode if traits like fast-math-flags are
  //    included).
  // 3. The previous instruction must have virtual register definitions for its
  //    operands in the same basic block as Inst.
  // 4. The previous instruction's result must only be used by Inst.
  return areOpcodesEqualOrInverse(Opcode, MI1->getOpcode()) &&
         (isAssociativeAndCommutative(*MI1) ||
          isAssociativeAndCommutative(*MI1, /* Invert */ true)) &&
         hasReassociableOperands(*MI1, MBB) &&
         MRI.hasOneNonDBGUse(MI1->getOperand(0).getReg());
}

// 1. The operation must be associative and commutative or be the inverse of
//    such an operation.
// 2. The instruction must have virtual register definitions for its
//    operands in the same basic block.
// 3. The instruction must have a reassociable sibling.
bool TargetInstrInfo::isReassociationCandidate(const MachineInstr &Inst,
                                               bool &Commuted) const {
  return (isAssociativeAndCommutative(Inst) ||
          isAssociativeAndCommutative(Inst, /* Invert */ true)) &&
         hasReassociableOperands(Inst, Inst.getParent()) &&
         hasReassociableSibling(Inst, Commuted);
}

// The concept of the reassociation pass is that these operations can benefit
// from this kind of transformation:
//
// A = ? op ?
// B = A op X (Prev)
// C = B op Y (Root)
// -->
// A = ? op ?
// B = X op Y
// C = A op B
//
// breaking the dependency between A and B, allowing them to be executed in
// parallel (or back-to-back in a pipeline) instead of depending on each other.

// FIXME: This has the potential to be expensive (compile time) while not
// improving the code at all. Some ways to limit the overhead:
// 1. Track successful transforms; bail out if hit rate gets too low.
// 2. Only enable at -O3 or some other non-default optimization level.
// 3. Pre-screen pattern candidates here: if an operand of the previous
//    instruction is known to not increase the critical path, then don't match
//    that pattern.
bool TargetInstrInfo::getMachineCombinerPatterns(
    MachineInstr &Root, SmallVectorImpl<unsigned> &Patterns,
    bool DoRegPressureReduce) const {
  bool Commute;
  if (isReassociationCandidate(Root, Commute)) {
    // We found a sequence of instructions that may be suitable for a
    // reassociation of operands to increase ILP. Specify each commutation
    // possibility for the Prev instruction in the sequence and let the
    // machine combiner decide if changing the operands is worthwhile.
    if (Commute) {
      Patterns.push_back(MachineCombinerPattern::REASSOC_AX_YB);
      Patterns.push_back(MachineCombinerPattern::REASSOC_XA_YB);
    } else {
      Patterns.push_back(MachineCombinerPattern::REASSOC_AX_BY);
      Patterns.push_back(MachineCombinerPattern::REASSOC_XA_BY);
    }
    return true;
  }

  return false;
}

/// Return true when a code sequence can improve loop throughput.
bool TargetInstrInfo::isThroughputPattern(unsigned Pattern) const {
  return false;
}

CombinerObjective
TargetInstrInfo::getCombinerObjective(unsigned Pattern) const {
  return CombinerObjective::Default;
}

std::pair<unsigned, unsigned>
TargetInstrInfo::getReassociationOpcodes(unsigned Pattern,
                                         const MachineInstr &Root,
                                         const MachineInstr &Prev) const {
  bool AssocCommutRoot = isAssociativeAndCommutative(Root);
  bool AssocCommutPrev = isAssociativeAndCommutative(Prev);

  // Early exit if both opcodes are associative and commutative. It's a trivial
  // reassociation when we only change operands order. In this case opcodes are
  // not required to have inverse versions.
  if (AssocCommutRoot && AssocCommutPrev) {
    assert(Root.getOpcode() == Prev.getOpcode() && "Expected to be equal");
    return std::make_pair(Root.getOpcode(), Root.getOpcode());
  }

  // At least one instruction is not associative or commutative.
  // Since we have matched one of the reassociation patterns, we expect that the
  // instructions' opcodes are equal or one of them is the inversion of the
  // other.
  assert(areOpcodesEqualOrInverse(Root.getOpcode(), Prev.getOpcode()) &&
         "Incorrectly matched pattern");
  unsigned AssocCommutOpcode = Root.getOpcode();
  unsigned InverseOpcode = *getInverseOpcode(Root.getOpcode());
  if (!AssocCommutRoot)
    std::swap(AssocCommutOpcode, InverseOpcode);

  // The transformation rule (`+` is any associative and commutative binary
  // operation, `-` is the inverse):
  // REASSOC_AX_BY:
  //   (A + X) + Y => A + (X + Y)
  //   (A + X) - Y => A + (X - Y)
  //   (A - X) + Y => A - (X - Y)
  //   (A - X) - Y => A - (X + Y)
  // REASSOC_XA_BY:
  //   (X + A) + Y => (X + Y) + A
  //   (X + A) - Y => (X - Y) + A
  //   (X - A) + Y => (X + Y) - A
  //   (X - A) - Y => (X - Y) - A
  // REASSOC_AX_YB:
  //   Y + (A + X) => (Y + X) + A
  //   Y - (A + X) => (Y - X) - A
  //   Y + (A - X) => (Y - X) + A
  //   Y - (A - X) => (Y + X) - A
  // REASSOC_XA_YB:
  //   Y + (X + A) => (Y + X) + A
  //   Y - (X + A) => (Y - X) - A
  //   Y + (X - A) => (Y + X) - A
  //   Y - (X - A) => (Y - X) + A
  switch (Pattern) {
  default:
    llvm_unreachable("Unexpected pattern");
  case MachineCombinerPattern::REASSOC_AX_BY:
    if (!AssocCommutRoot && AssocCommutPrev)
      return {AssocCommutOpcode, InverseOpcode};
    if (AssocCommutRoot && !AssocCommutPrev)
      return {InverseOpcode, InverseOpcode};
    if (!AssocCommutRoot && !AssocCommutPrev)
      return {InverseOpcode, AssocCommutOpcode};
    break;
  case MachineCombinerPattern::REASSOC_XA_BY:
    if (!AssocCommutRoot && AssocCommutPrev)
      return {AssocCommutOpcode, InverseOpcode};
    if (AssocCommutRoot && !AssocCommutPrev)
      return {InverseOpcode, AssocCommutOpcode};
    if (!AssocCommutRoot && !AssocCommutPrev)
      return {InverseOpcode, InverseOpcode};
    break;
  case MachineCombinerPattern::REASSOC_AX_YB:
    if (!AssocCommutRoot && AssocCommutPrev)
      return {InverseOpcode, InverseOpcode};
    if (AssocCommutRoot && !AssocCommutPrev)
      return {AssocCommutOpcode, InverseOpcode};
    if (!AssocCommutRoot && !AssocCommutPrev)
      return {InverseOpcode, AssocCommutOpcode};
    break;
  case MachineCombinerPattern::REASSOC_XA_YB:
    if (!AssocCommutRoot && AssocCommutPrev)
      return {InverseOpcode, InverseOpcode};
    if (AssocCommutRoot && !AssocCommutPrev)
      return {InverseOpcode, AssocCommutOpcode};
    if (!AssocCommutRoot && !AssocCommutPrev)
      return {AssocCommutOpcode, InverseOpcode};
    break;
  }
  llvm_unreachable("Unhandled combination");
}

// Return a pair of boolean flags showing if the new root and new prev operands
// must be swapped. See visual example of the rule in
// TargetInstrInfo::getReassociationOpcodes.
static std::pair<bool, bool> mustSwapOperands(unsigned Pattern) {
  switch (Pattern) {
  default:
    llvm_unreachable("Unexpected pattern");
  case MachineCombinerPattern::REASSOC_AX_BY:
    return {false, false};
  case MachineCombinerPattern::REASSOC_XA_BY:
    return {true, false};
  case MachineCombinerPattern::REASSOC_AX_YB:
    return {true, true};
  case MachineCombinerPattern::REASSOC_XA_YB:
    return {true, true};
  }
}

void TargetInstrInfo::getReassociateOperandIndices(
    const MachineInstr &Root, unsigned Pattern,
    std::array<unsigned, 5> &OperandIndices) const {
  switch (Pattern) {
  case MachineCombinerPattern::REASSOC_AX_BY:
    OperandIndices = {1, 1, 1, 2, 2};
    break;
  case MachineCombinerPattern::REASSOC_AX_YB:
    OperandIndices = {2, 1, 2, 2, 1};
    break;
  case MachineCombinerPattern::REASSOC_XA_BY:
    OperandIndices = {1, 2, 1, 1, 2};
    break;
  case MachineCombinerPattern::REASSOC_XA_YB:
    OperandIndices = {2, 2, 2, 1, 1};
    break;
  default:
    llvm_unreachable("unexpected MachineCombinerPattern");
  }
}

/// Attempt the reassociation transformation to reduce critical path length.
/// See the above comments before getMachineCombinerPatterns().
void TargetInstrInfo::reassociateOps(
    MachineInstr &Root, MachineInstr &Prev, unsigned Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    ArrayRef<unsigned> OperandIndices,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const {
  MachineFunction *MF = Root.getMF();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = Root.getRegClassConstraint(0, TII, TRI);

  MachineOperand &OpA = Prev.getOperand(OperandIndices[1]);
  MachineOperand &OpB = Root.getOperand(OperandIndices[2]);
  MachineOperand &OpX = Prev.getOperand(OperandIndices[3]);
  MachineOperand &OpY = Root.getOperand(OperandIndices[4]);
  MachineOperand &OpC = Root.getOperand(0);

  Register RegA = OpA.getReg();
  Register RegB = OpB.getReg();
  Register RegX = OpX.getReg();
  Register RegY = OpY.getReg();
  Register RegC = OpC.getReg();

  if (RegA.isVirtual())
    MRI.constrainRegClass(RegA, RC);
  if (RegB.isVirtual())
    MRI.constrainRegClass(RegB, RC);
  if (RegX.isVirtual())
    MRI.constrainRegClass(RegX, RC);
  if (RegY.isVirtual())
    MRI.constrainRegClass(RegY, RC);
  if (RegC.isVirtual())
    MRI.constrainRegClass(RegC, RC);

  // Create a new virtual register for the result of (X op Y) instead of
  // recycling RegB because the MachineCombiner's computation of the critical
  // path requires a new register definition rather than an existing one.
  Register NewVR = MRI.createVirtualRegister(RC);
  InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));

  auto [NewRootOpc, NewPrevOpc] = getReassociationOpcodes(Pattern, Root, Prev);
  bool KillA = OpA.isKill();
  bool KillX = OpX.isKill();
  bool KillY = OpY.isKill();
  bool KillNewVR = true;

  auto [SwapRootOperands, SwapPrevOperands] = mustSwapOperands(Pattern);

  if (SwapPrevOperands) {
    std::swap(RegX, RegY);
    std::swap(KillX, KillY);
  }

  unsigned PrevFirstOpIdx, PrevSecondOpIdx;
  unsigned RootFirstOpIdx, RootSecondOpIdx;
  switch (Pattern) {
  case MachineCombinerPattern::REASSOC_AX_BY:
    PrevFirstOpIdx = OperandIndices[1];
    PrevSecondOpIdx = OperandIndices[3];
    RootFirstOpIdx = OperandIndices[2];
    RootSecondOpIdx = OperandIndices[4];
    break;
  case MachineCombinerPattern::REASSOC_AX_YB:
    PrevFirstOpIdx = OperandIndices[1];
    PrevSecondOpIdx = OperandIndices[3];
    RootFirstOpIdx = OperandIndices[4];
    RootSecondOpIdx = OperandIndices[2];
    break;
  case MachineCombinerPattern::REASSOC_XA_BY:
    PrevFirstOpIdx = OperandIndices[3];
    PrevSecondOpIdx = OperandIndices[1];
    RootFirstOpIdx = OperandIndices[2];
    RootSecondOpIdx = OperandIndices[4];
    break;
  case MachineCombinerPattern::REASSOC_XA_YB:
    PrevFirstOpIdx = OperandIndices[3];
    PrevSecondOpIdx = OperandIndices[1];
    RootFirstOpIdx = OperandIndices[4];
    RootSecondOpIdx = OperandIndices[2];
    break;
  default:
    llvm_unreachable("unexpected MachineCombinerPattern");
  }

  // Basically BuildMI but doesn't add implicit operands by default.
  auto buildMINoImplicit = [](MachineFunction &MF, const MIMetadata &MIMD,
                              const MCInstrDesc &MCID, Register DestReg) {
    return MachineInstrBuilder(
               MF, MF.CreateMachineInstr(MCID, MIMD.getDL(), /*NoImpl=*/true))
        .setPCSections(MIMD.getPCSections())
        .addReg(DestReg, RegState::Define);
  };

  // Create new instructions for insertion.
  MachineInstrBuilder MIB1 =
      buildMINoImplicit(*MF, MIMetadata(Prev), TII->get(NewPrevOpc), NewVR);
  for (const auto &MO : Prev.explicit_operands()) {
    unsigned Idx = MO.getOperandNo();
    // Skip the result operand we'd already added.
    if (Idx == 0)
      continue;
    if (Idx == PrevFirstOpIdx)
      MIB1.addReg(RegX, getKillRegState(KillX));
    else if (Idx == PrevSecondOpIdx)
      MIB1.addReg(RegY, getKillRegState(KillY));
    else
      MIB1.add(MO);
  }
  MIB1.copyImplicitOps(Prev);

  if (SwapRootOperands) {
    std::swap(RegA, NewVR);
    std::swap(KillA, KillNewVR);
  }

  MachineInstrBuilder MIB2 =
      buildMINoImplicit(*MF, MIMetadata(Root), TII->get(NewRootOpc), RegC);
  for (const auto &MO : Root.explicit_operands()) {
    unsigned Idx = MO.getOperandNo();
    // Skip the result operand.
    if (Idx == 0)
      continue;
    if (Idx == RootFirstOpIdx)
      MIB2 = MIB2.addReg(RegA, getKillRegState(KillA));
    else if (Idx == RootSecondOpIdx)
      MIB2 = MIB2.addReg(NewVR, getKillRegState(KillNewVR));
    else
      MIB2 = MIB2.add(MO);
  }
  MIB2.copyImplicitOps(Root);

  // Propagate FP flags from the original instructions.
  // But clear poison-generating flags because those may not be valid now.
  // TODO: There should be a helper function for copying only fast-math-flags.
  uint32_t IntersectedFlags = Root.getFlags() & Prev.getFlags();
  MIB1->setFlags(IntersectedFlags);
  MIB1->clearFlag(MachineInstr::MIFlag::NoSWrap);
  MIB1->clearFlag(MachineInstr::MIFlag::NoUWrap);
  MIB1->clearFlag(MachineInstr::MIFlag::IsExact);

  MIB2->setFlags(IntersectedFlags);
  MIB2->clearFlag(MachineInstr::MIFlag::NoSWrap);
  MIB2->clearFlag(MachineInstr::MIFlag::NoUWrap);
  MIB2->clearFlag(MachineInstr::MIFlag::IsExact);

  setSpecialOperandAttr(Root, Prev, *MIB1, *MIB2);

  // Record new instructions for insertion and old instructions for deletion.
  InsInstrs.push_back(MIB1);
  InsInstrs.push_back(MIB2);
  DelInstrs.push_back(&Prev);
  DelInstrs.push_back(&Root);

  // We transformed:
  // B = A op X (Prev)
  // C = B op Y (Root)
  // Into:
  // B = X op Y (MIB1)
  // C = A op B (MIB2)
  // C has the same value as before, B doesn't; as such, keep the debug number
  // of C but not of B.
  if (unsigned OldRootNum = Root.peekDebugInstrNum())
    MIB2.getInstr()->setDebugInstrNum(OldRootNum);
}

void TargetInstrInfo::genAlternativeCodeSequence(
    MachineInstr &Root, unsigned Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstIdxForVirtReg) const {
  MachineRegisterInfo &MRI = Root.getMF()->getRegInfo();

  // Select the previous instruction in the sequence based on the input pattern.
  std::array<unsigned, 5> OperandIndices;
  getReassociateOperandIndices(Root, Pattern, OperandIndices);
  MachineInstr *Prev =
      MRI.getUniqueVRegDef(Root.getOperand(OperandIndices[0]).getReg());

  // Don't reassociate if Prev and Root are in different blocks.
  if (Prev->getParent() != Root.getParent())
    return;

  reassociateOps(Root, *Prev, Pattern, InsInstrs, DelInstrs, OperandIndices,
                 InstIdxForVirtReg);
}

MachineTraceStrategy TargetInstrInfo::getMachineCombinerTraceStrategy() const {
  return MachineTraceStrategy::TS_MinInstrCount;
}

bool TargetInstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Remat clients assume operand 0 is the defined register.
  if (!MI.getNumOperands() || !MI.getOperand(0).isReg())
    return false;
  Register DefReg = MI.getOperand(0).getReg();

  // A sub-register definition can only be rematerialized if the instruction
  // doesn't read the other parts of the register.  Otherwise it is really a
  // read-modify-write operation on the full virtual register which cannot be
  // moved safely.
  if (DefReg.isVirtual() && MI.getOperand(0).getSubReg() &&
      MI.readsVirtualRegister(DefReg))
    return false;

  // A load from a fixed stack slot can be rematerialized. This may be
  // redundant with subsequent checks, but it's target-independent,
  // simple, and a common case.
  int FrameIdx = 0;
  if (isLoadFromStackSlot(MI, FrameIdx) &&
      MF.getFrameInfo().isImmutableObjectIndex(FrameIdx))
    return true;

  // Avoid instructions obviously unsafe for remat.
  if (MI.isNotDuplicable() || MI.mayStore() || MI.mayRaiseFPException() ||
      MI.hasUnmodeledSideEffects())
    return false;

  // Don't remat inline asm. We have no idea how expensive it is
  // even if it's side effect free.
  if (MI.isInlineAsm())
    return false;

  // Avoid instructions which load from potentially varying memory.
  if (MI.mayLoad() && !MI.isDereferenceableInvariantLoad())
    return false;

  // If any of the registers accessed are non-constant, conservatively assume
  // the instruction is not rematerializable.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg()) continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;

    // Check for a well-behaved physical register.
    if (Reg.isPhysical()) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        if (!MRI.isConstantPhysReg(Reg))
          return false;
      } else {
        // A physreg def. We can't remat it.
        return false;
      }
      continue;
    }

    // Only allow one virtual-register def.  There may be multiple defs of the
    // same virtual register, though.
    if (MO.isDef() && Reg != DefReg)
      return false;

    // Don't allow any virtual-register uses. Rematting an instruction with
    // virtual register uses would length the live ranges of the uses, which
    // is not necessarily a good idea, certainly not "trivial".
    if (MO.isUse())
      return false;
  }

  // Everything checked out.
  return true;
}

int TargetInstrInfo::getSPAdjust(const MachineInstr &MI) const {
  const MachineFunction *MF = MI.getMF();
  const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();
  bool StackGrowsDown =
    TFI->getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;

  unsigned FrameSetupOpcode = getCallFrameSetupOpcode();
  unsigned FrameDestroyOpcode = getCallFrameDestroyOpcode();

  if (!isFrameInstr(MI))
    return 0;

  int SPAdj = TFI->alignSPAdjust(getFrameSize(MI));

  if ((!StackGrowsDown && MI.getOpcode() == FrameSetupOpcode) ||
      (StackGrowsDown && MI.getOpcode() == FrameDestroyOpcode))
    SPAdj = -SPAdj;

  return SPAdj;
}

/// isSchedulingBoundary - Test if the given instruction should be
/// considered a scheduling boundary. This primarily includes labels
/// and terminators.
bool TargetInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                           const MachineBasicBlock *MBB,
                                           const MachineFunction &MF) const {
  // Terminators and labels can't be scheduled around.
  if (MI.isTerminator() || MI.isPosition())
    return true;

  // INLINEASM_BR can jump to another block
  if (MI.getOpcode() == TargetOpcode::INLINEASM_BR)
    return true;

  // Don't attempt to schedule around any instruction that defines
  // a stack-oriented pointer, as it's unlikely to be profitable. This
  // saves compile time, because it doesn't require every single
  // stack slot reference to depend on the instruction that does the
  // modification.
  const TargetLowering &TLI = *MF.getSubtarget().getTargetLowering();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  return MI.modifiesRegister(TLI.getStackPointerRegisterToSaveRestore(), TRI);
}

// Provide a global flag for disabling the PreRA hazard recognizer that targets
// may choose to honor.
bool TargetInstrInfo::usePreRAHazardRecognizer() const {
  return !DisableHazardRecognizer;
}

// Default implementation of CreateTargetRAHazardRecognizer.
ScheduleHazardRecognizer *TargetInstrInfo::
CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                             const ScheduleDAG *DAG) const {
  // Dummy hazard recognizer allows all instructions to issue.
  return new ScheduleHazardRecognizer();
}

// Default implementation of CreateTargetMIHazardRecognizer.
ScheduleHazardRecognizer *TargetInstrInfo::CreateTargetMIHazardRecognizer(
    const InstrItineraryData *II, const ScheduleDAGMI *DAG) const {
  return new ScoreboardHazardRecognizer(II, DAG, "machine-scheduler");
}

// Default implementation of CreateTargetPostRAHazardRecognizer.
ScheduleHazardRecognizer *TargetInstrInfo::
CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                   const ScheduleDAG *DAG) const {
  return new ScoreboardHazardRecognizer(II, DAG, "post-RA-sched");
}

// Default implementation of getMemOperandWithOffset.
bool TargetInstrInfo::getMemOperandWithOffset(
    const MachineInstr &MI, const MachineOperand *&BaseOp, int64_t &Offset,
    bool &OffsetIsScalable, const TargetRegisterInfo *TRI) const {
  SmallVector<const MachineOperand *, 4> BaseOps;
  LocationSize Width = 0;
  if (!getMemOperandsWithOffsetWidth(MI, BaseOps, Offset, OffsetIsScalable,
                                     Width, TRI) ||
      BaseOps.size() != 1)
    return false;
  BaseOp = BaseOps.front();
  return true;
}

//===----------------------------------------------------------------------===//
//  SelectionDAG latency interface.
//===----------------------------------------------------------------------===//

std::optional<unsigned>
TargetInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                   SDNode *DefNode, unsigned DefIdx,
                                   SDNode *UseNode, unsigned UseIdx) const {
  if (!ItinData || ItinData->isEmpty())
    return std::nullopt;

  if (!DefNode->isMachineOpcode())
    return std::nullopt;

  unsigned DefClass = get(DefNode->getMachineOpcode()).getSchedClass();
  if (!UseNode->isMachineOpcode())
    return ItinData->getOperandCycle(DefClass, DefIdx);
  unsigned UseClass = get(UseNode->getMachineOpcode()).getSchedClass();
  return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);
}

unsigned TargetInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                          SDNode *N) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  if (!N->isMachineOpcode())
    return 1;

  return ItinData->getStageLatency(get(N->getMachineOpcode()).getSchedClass());
}

//===----------------------------------------------------------------------===//
//  MachineInstr latency interface.
//===----------------------------------------------------------------------===//

unsigned TargetInstrInfo::getNumMicroOps(const InstrItineraryData *ItinData,
                                         const MachineInstr &MI) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  unsigned Class = MI.getDesc().getSchedClass();
  int UOps = ItinData->Itineraries[Class].NumMicroOps;
  if (UOps >= 0)
    return UOps;

  // The # of u-ops is dynamically determined. The specific target should
  // override this function to return the right number.
  return 1;
}

/// Return the default expected latency for a def based on it's opcode.
unsigned TargetInstrInfo::defaultDefLatency(const MCSchedModel &SchedModel,
                                            const MachineInstr &DefMI) const {
  if (DefMI.isTransient())
    return 0;
  if (DefMI.mayLoad())
    return SchedModel.LoadLatency;
  if (isHighLatencyDef(DefMI.getOpcode()))
    return SchedModel.HighLatency;
  return 1;
}

unsigned TargetInstrInfo::getPredicationCost(const MachineInstr &) const {
  return 0;
}

unsigned TargetInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                          const MachineInstr &MI,
                                          unsigned *PredCost) const {
  // Default to one cycle for no itinerary. However, an "empty" itinerary may
  // still have a MinLatency property, which getStageLatency checks.
  if (!ItinData)
    return MI.mayLoad() ? 2 : 1;

  return ItinData->getStageLatency(MI.getDesc().getSchedClass());
}

bool TargetInstrInfo::hasLowDefLatency(const TargetSchedModel &SchedModel,
                                       const MachineInstr &DefMI,
                                       unsigned DefIdx) const {
  const InstrItineraryData *ItinData = SchedModel.getInstrItineraries();
  if (!ItinData || ItinData->isEmpty())
    return false;

  unsigned DefClass = DefMI.getDesc().getSchedClass();
  std::optional<unsigned> DefCycle =
      ItinData->getOperandCycle(DefClass, DefIdx);
  return DefCycle && DefCycle <= 1U;
}

bool TargetInstrInfo::isFunctionSafeToSplit(const MachineFunction &MF) const {
  // TODO: We don't split functions where a section attribute has been set
  // since the split part may not be placed in a contiguous region. It may also
  // be more beneficial to augment the linker to ensure contiguous layout of
  // split functions within the same section as specified by the attribute.
  if (MF.getFunction().hasSection())
    return false;

  // We don't want to proceed further for cold functions
  // or functions of unknown hotness. Lukewarm functions have no prefix.
  std::optional<StringRef> SectionPrefix = MF.getFunction().getSectionPrefix();
  if (SectionPrefix &&
      (*SectionPrefix == "unlikely" || *SectionPrefix == "unknown")) {
    return false;
  }

  return true;
}

std::optional<ParamLoadedValue>
TargetInstrInfo::describeLoadedValue(const MachineInstr &MI,
                                     Register Reg) const {
  const MachineFunction *MF = MI.getMF();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  DIExpression *Expr = DIExpression::get(MF->getFunction().getContext(), {});
  int64_t Offset;
  bool OffsetIsScalable;

  // To simplify the sub-register handling, verify that we only need to
  // consider physical registers.
  assert(MF->getProperties().hasProperty(
      MachineFunctionProperties::Property::NoVRegs));

  if (auto DestSrc = isCopyInstr(MI)) {
    Register DestReg = DestSrc->Destination->getReg();

    // If the copy destination is the forwarding reg, describe the forwarding
    // reg using the copy source as the backup location. Example:
    //
    //   x0 = MOV x7
    //   call callee(x0)      ; x0 described as x7
    if (Reg == DestReg)
      return ParamLoadedValue(*DestSrc->Source, Expr);

    // If the target's hook couldn't describe this copy, give up.
    return std::nullopt;
  } else if (auto RegImm = isAddImmediate(MI, Reg)) {
    Register SrcReg = RegImm->Reg;
    Offset = RegImm->Imm;
    Expr = DIExpression::prepend(Expr, DIExpression::ApplyOffset, Offset);
    return ParamLoadedValue(MachineOperand::CreateReg(SrcReg, false), Expr);
  } else if (MI.hasOneMemOperand()) {
    // Only describe memory which provably does not escape the function. As
    // described in llvm.org/PR43343, escaped memory may be clobbered by the
    // callee (or by another thread).
    const auto &TII = MF->getSubtarget().getInstrInfo();
    const MachineFrameInfo &MFI = MF->getFrameInfo();
    const MachineMemOperand *MMO = MI.memoperands()[0];
    const PseudoSourceValue *PSV = MMO->getPseudoValue();

    // If the address points to "special" memory (e.g. a spill slot), it's
    // sufficient to check that it isn't aliased by any high-level IR value.
    if (!PSV || PSV->mayAlias(&MFI))
      return std::nullopt;

    const MachineOperand *BaseOp;
    if (!TII->getMemOperandWithOffset(MI, BaseOp, Offset, OffsetIsScalable,
                                      TRI))
      return std::nullopt;

    // FIXME: Scalable offsets are not yet handled in the offset code below.
    if (OffsetIsScalable)
      return std::nullopt;

    // TODO: Can currently only handle mem instructions with a single define.
    // An example from the x86 target:
    //    ...
    //    DIV64m $rsp, 1, $noreg, 24, $noreg, implicit-def dead $rax, implicit-def $rdx
    //    ...
    //
    if (MI.getNumExplicitDefs() != 1)
      return std::nullopt;

    // TODO: In what way do we need to take Reg into consideration here?

    SmallVector<uint64_t, 8> Ops;
    DIExpression::appendOffset(Ops, Offset);
    Ops.push_back(dwarf::DW_OP_deref_size);
    Ops.push_back(MMO->getSize().hasValue() ? MMO->getSize().getValue()
                                            : ~UINT64_C(0));
    Expr = DIExpression::prependOpcodes(Expr, Ops);
    return ParamLoadedValue(*BaseOp, Expr);
  }

  return std::nullopt;
}

// Get the call frame size just before MI.
unsigned TargetInstrInfo::getCallFrameSizeAt(MachineInstr &MI) const {
  // Search backwards from MI for the most recent call frame instruction.
  MachineBasicBlock *MBB = MI.getParent();
  for (auto &AdjI : reverse(make_range(MBB->instr_begin(), MI.getIterator()))) {
    if (AdjI.getOpcode() == getCallFrameSetupOpcode())
      return getFrameTotalSize(AdjI);
    if (AdjI.getOpcode() == getCallFrameDestroyOpcode())
      return 0;
  }

  // If none was found, use the call frame size from the start of the basic
  // block.
  return MBB->getCallFrameSize();
}

/// Both DefMI and UseMI must be valid.  By default, call directly to the
/// itinerary. This may be overriden by the target.
std::optional<unsigned> TargetInstrInfo::getOperandLatency(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI,
    unsigned DefIdx, const MachineInstr &UseMI, unsigned UseIdx) const {
  unsigned DefClass = DefMI.getDesc().getSchedClass();
  unsigned UseClass = UseMI.getDesc().getSchedClass();
  return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);
}

bool TargetInstrInfo::getRegSequenceInputs(
    const MachineInstr &MI, unsigned DefIdx,
    SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const {
  assert((MI.isRegSequence() ||
          MI.isRegSequenceLike()) && "Instruction do not have the proper type");

  if (!MI.isRegSequence())
    return getRegSequenceLikeInputs(MI, DefIdx, InputRegs);

  // We are looking at:
  // Def = REG_SEQUENCE v0, sub0, v1, sub1, ...
  assert(DefIdx == 0 && "REG_SEQUENCE only has one def");
  for (unsigned OpIdx = 1, EndOpIdx = MI.getNumOperands(); OpIdx != EndOpIdx;
       OpIdx += 2) {
    const MachineOperand &MOReg = MI.getOperand(OpIdx);
    if (MOReg.isUndef())
      continue;
    const MachineOperand &MOSubIdx = MI.getOperand(OpIdx + 1);
    assert(MOSubIdx.isImm() &&
           "One of the subindex of the reg_sequence is not an immediate");
    // Record Reg:SubReg, SubIdx.
    InputRegs.push_back(RegSubRegPairAndIdx(MOReg.getReg(), MOReg.getSubReg(),
                                            (unsigned)MOSubIdx.getImm()));
  }
  return true;
}

bool TargetInstrInfo::getExtractSubregInputs(
    const MachineInstr &MI, unsigned DefIdx,
    RegSubRegPairAndIdx &InputReg) const {
  assert((MI.isExtractSubreg() ||
      MI.isExtractSubregLike()) && "Instruction do not have the proper type");

  if (!MI.isExtractSubreg())
    return getExtractSubregLikeInputs(MI, DefIdx, InputReg);

  // We are looking at:
  // Def = EXTRACT_SUBREG v0.sub1, sub0.
  assert(DefIdx == 0 && "EXTRACT_SUBREG only has one def");
  const MachineOperand &MOReg = MI.getOperand(1);
  if (MOReg.isUndef())
    return false;
  const MachineOperand &MOSubIdx = MI.getOperand(2);
  assert(MOSubIdx.isImm() &&
         "The subindex of the extract_subreg is not an immediate");

  InputReg.Reg = MOReg.getReg();
  InputReg.SubReg = MOReg.getSubReg();
  InputReg.SubIdx = (unsigned)MOSubIdx.getImm();
  return true;
}

bool TargetInstrInfo::getInsertSubregInputs(
    const MachineInstr &MI, unsigned DefIdx,
    RegSubRegPair &BaseReg, RegSubRegPairAndIdx &InsertedReg) const {
  assert((MI.isInsertSubreg() ||
      MI.isInsertSubregLike()) && "Instruction do not have the proper type");

  if (!MI.isInsertSubreg())
    return getInsertSubregLikeInputs(MI, DefIdx, BaseReg, InsertedReg);

  // We are looking at:
  // Def = INSERT_SEQUENCE v0, v1, sub0.
  assert(DefIdx == 0 && "INSERT_SUBREG only has one def");
  const MachineOperand &MOBaseReg = MI.getOperand(1);
  const MachineOperand &MOInsertedReg = MI.getOperand(2);
  if (MOInsertedReg.isUndef())
    return false;
  const MachineOperand &MOSubIdx = MI.getOperand(3);
  assert(MOSubIdx.isImm() &&
         "One of the subindex of the reg_sequence is not an immediate");
  BaseReg.Reg = MOBaseReg.getReg();
  BaseReg.SubReg = MOBaseReg.getSubReg();

  InsertedReg.Reg = MOInsertedReg.getReg();
  InsertedReg.SubReg = MOInsertedReg.getSubReg();
  InsertedReg.SubIdx = (unsigned)MOSubIdx.getImm();
  return true;
}

// Returns a MIRPrinter comment for this machine operand.
std::string TargetInstrInfo::createMIROperandComment(
    const MachineInstr &MI, const MachineOperand &Op, unsigned OpIdx,
    const TargetRegisterInfo *TRI) const {

  if (!MI.isInlineAsm())
    return "";

  std::string Flags;
  raw_string_ostream OS(Flags);

  if (OpIdx == InlineAsm::MIOp_ExtraInfo) {
    // Print HasSideEffects, MayLoad, MayStore, IsAlignStack
    unsigned ExtraInfo = Op.getImm();
    bool First = true;
    for (StringRef Info : InlineAsm::getExtraInfoNames(ExtraInfo)) {
      if (!First)
        OS << " ";
      First = false;
      OS << Info;
    }

    return Flags;
  }

  int FlagIdx = MI.findInlineAsmFlagIdx(OpIdx);
  if (FlagIdx < 0 || (unsigned)FlagIdx != OpIdx)
    return "";

  assert(Op.isImm() && "Expected flag operand to be an immediate");
  // Pretty print the inline asm operand descriptor.
  unsigned Flag = Op.getImm();
  const InlineAsm::Flag F(Flag);
  OS << F.getKindName();

  unsigned RCID;
  if (!F.isImmKind() && !F.isMemKind() && F.hasRegClassConstraint(RCID)) {
    if (TRI) {
      OS << ':' << TRI->getRegClassName(TRI->getRegClass(RCID));
    } else
      OS << ":RC" << RCID;
  }

  if (F.isMemKind()) {
    InlineAsm::ConstraintCode MCID = F.getMemoryConstraintID();
    OS << ":" << InlineAsm::getMemConstraintName(MCID);
  }

  unsigned TiedTo;
  if (F.isUseOperandTiedToDef(TiedTo))
    OS << " tiedto:$" << TiedTo;

  if ((F.isRegDefKind() || F.isRegDefEarlyClobberKind() || F.isRegUseKind()) &&
      F.getRegMayBeFolded())
    OS << " foldable";

  return Flags;
}

TargetInstrInfo::PipelinerLoopInfo::~PipelinerLoopInfo() = default;

void TargetInstrInfo::mergeOutliningCandidateAttributes(
    Function &F, std::vector<outliner::Candidate> &Candidates) const {
  // Include target features from an arbitrary candidate for the outlined
  // function. This makes sure the outlined function knows what kinds of
  // instructions are going into it. This is fine, since all parent functions
  // must necessarily support the instructions that are in the outlined region.
  outliner::Candidate &FirstCand = Candidates.front();
  const Function &ParentFn = FirstCand.getMF()->getFunction();
  if (ParentFn.hasFnAttribute("target-features"))
    F.addFnAttr(ParentFn.getFnAttribute("target-features"));
  if (ParentFn.hasFnAttribute("target-cpu"))
    F.addFnAttr(ParentFn.getFnAttribute("target-cpu"));

  // Set nounwind, so we don't generate eh_frame.
  if (llvm::all_of(Candidates, [](const outliner::Candidate &C) {
        return C.getMF()->getFunction().hasFnAttribute(Attribute::NoUnwind);
      }))
    F.addFnAttr(Attribute::NoUnwind);
}

outliner::InstrType TargetInstrInfo::getOutliningType(
    MachineBasicBlock::iterator &MIT, unsigned Flags) const {
  MachineInstr &MI = *MIT;

  // NOTE: MI.isMetaInstruction() will match CFI_INSTRUCTION, but some targets
  // have support for outlining those. Special-case that here.
  if (MI.isCFIInstruction())
    // Just go right to the target implementation.
    return getOutliningTypeImpl(MIT, Flags);

  // Be conservative about inline assembly.
  if (MI.isInlineAsm())
    return outliner::InstrType::Illegal;

  // Labels generally can't safely be outlined.
  if (MI.isLabel())
    return outliner::InstrType::Illegal;

  // Don't let debug instructions impact analysis.
  if (MI.isDebugInstr())
    return outliner::InstrType::Invisible;

  // Some other special cases.
  switch (MI.getOpcode()) {
    case TargetOpcode::IMPLICIT_DEF:
    case TargetOpcode::KILL:
    case TargetOpcode::LIFETIME_START:
    case TargetOpcode::LIFETIME_END:
      return outliner::InstrType::Invisible;
    default:
      break;
  }

  // Is this a terminator for a basic block?
  if (MI.isTerminator()) {
    // If this is a branch to another block, we can't outline it.
    if (!MI.getParent()->succ_empty())
      return outliner::InstrType::Illegal;

    // Don't outline if the branch is not unconditional.
    if (isPredicated(MI))
      return outliner::InstrType::Illegal;
  }

  // Make sure none of the operands of this instruction do anything that
  // might break if they're moved outside their current function.
  // This includes MachineBasicBlock references, BlockAddressses,
  // Constant pool indices and jump table indices.
  //
  // A quick note on MO_TargetIndex:
  // This doesn't seem to be used in any of the architectures that the
  // MachineOutliner supports, but it was still filtered out in all of them.
  // There was one exception (RISC-V), but MO_TargetIndex also isn't used there.
  // As such, this check is removed both here and in the target-specific
  // implementations. Instead, we assert to make sure this doesn't
  // catch anyone off-guard somewhere down the line.
  for (const MachineOperand &MOP : MI.operands()) {
    // If you hit this assertion, please remove it and adjust
    // `getOutliningTypeImpl` for your target appropriately if necessary.
    // Adding the assertion back to other supported architectures
    // would be nice too :)
    assert(!MOP.isTargetIndex() && "This isn't used quite yet!");

    // CFI instructions should already have been filtered out at this point.
    assert(!MOP.isCFIIndex() && "CFI instructions handled elsewhere!");

    // PrologEpilogInserter should've already run at this point.
    assert(!MOP.isFI() && "FrameIndex instructions should be gone by now!");

    if (MOP.isMBB() || MOP.isBlockAddress() || MOP.isCPI() || MOP.isJTI())
      return outliner::InstrType::Illegal;
  }

  // If we don't know, delegate to the target-specific hook.
  return getOutliningTypeImpl(MIT, Flags);
}

bool TargetInstrInfo::isMBBSafeToOutlineFrom(MachineBasicBlock &MBB,
                                             unsigned &Flags) const {
  // Some instrumentations create special TargetOpcode at the start which
  // expands to special code sequences which must be present.
  auto First = MBB.getFirstNonDebugInstr();
  if (First == MBB.end())
    return true;

  if (First->getOpcode() == TargetOpcode::FENTRY_CALL ||
      First->getOpcode() == TargetOpcode::PATCHABLE_FUNCTION_ENTER)
    return false;

  // Some instrumentations create special pseudo-instructions at or just before
  // the end that must be present.
  auto Last = MBB.getLastNonDebugInstr();
  if (Last->getOpcode() == TargetOpcode::PATCHABLE_RET ||
      Last->getOpcode() == TargetOpcode::PATCHABLE_TAIL_CALL)
    return false;

  if (Last != First && Last->isReturn()) {
    --Last;
    if (Last->getOpcode() == TargetOpcode::PATCHABLE_FUNCTION_EXIT ||
        Last->getOpcode() == TargetOpcode::PATCHABLE_TAIL_CALL)
      return false;
  }
  return true;
}
