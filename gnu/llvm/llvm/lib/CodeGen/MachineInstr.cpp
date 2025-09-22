//===- lib/CodeGen/MachineInstr.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Methods common to all machine instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Operator.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>

using namespace llvm;

static const MachineFunction *getMFIfAvailable(const MachineInstr &MI) {
  if (const MachineBasicBlock *MBB = MI.getParent())
    if (const MachineFunction *MF = MBB->getParent())
      return MF;
  return nullptr;
}

// Try to crawl up to the machine function and get TRI and IntrinsicInfo from
// it.
static void tryToGetTargetInfo(const MachineInstr &MI,
                               const TargetRegisterInfo *&TRI,
                               const MachineRegisterInfo *&MRI,
                               const TargetIntrinsicInfo *&IntrinsicInfo,
                               const TargetInstrInfo *&TII) {

  if (const MachineFunction *MF = getMFIfAvailable(MI)) {
    TRI = MF->getSubtarget().getRegisterInfo();
    MRI = &MF->getRegInfo();
    IntrinsicInfo = MF->getTarget().getIntrinsicInfo();
    TII = MF->getSubtarget().getInstrInfo();
  }
}

void MachineInstr::addImplicitDefUseOperands(MachineFunction &MF) {
  for (MCPhysReg ImpDef : MCID->implicit_defs())
    addOperand(MF, MachineOperand::CreateReg(ImpDef, true, true));
  for (MCPhysReg ImpUse : MCID->implicit_uses())
    addOperand(MF, MachineOperand::CreateReg(ImpUse, false, true));
}

/// MachineInstr ctor - This constructor creates a MachineInstr and adds the
/// implicit operands. It reserves space for the number of operands specified by
/// the MCInstrDesc.
MachineInstr::MachineInstr(MachineFunction &MF, const MCInstrDesc &TID,
                           DebugLoc DL, bool NoImp)
    : MCID(&TID), NumOperands(0), Flags(0), AsmPrinterFlags(0),
      DbgLoc(std::move(DL)), DebugInstrNum(0), Opcode(TID.Opcode) {
  assert(DbgLoc.hasTrivialDestructor() && "Expected trivial destructor");

  // Reserve space for the expected number of operands.
  if (unsigned NumOps = MCID->getNumOperands() + MCID->implicit_defs().size() +
                        MCID->implicit_uses().size()) {
    CapOperands = OperandCapacity::get(NumOps);
    Operands = MF.allocateOperandArray(CapOperands);
  }

  if (!NoImp)
    addImplicitDefUseOperands(MF);
}

/// MachineInstr ctor - Copies MachineInstr arg exactly.
/// Does not copy the number from debug instruction numbering, to preserve
/// uniqueness.
MachineInstr::MachineInstr(MachineFunction &MF, const MachineInstr &MI)
    : MCID(&MI.getDesc()), NumOperands(0), Flags(0), AsmPrinterFlags(0),
      Info(MI.Info), DbgLoc(MI.getDebugLoc()), DebugInstrNum(0),
      Opcode(MI.getOpcode()) {
  assert(DbgLoc.hasTrivialDestructor() && "Expected trivial destructor");

  CapOperands = OperandCapacity::get(MI.getNumOperands());
  Operands = MF.allocateOperandArray(CapOperands);

  // Copy operands.
  for (const MachineOperand &MO : MI.operands())
    addOperand(MF, MO);

  // Replicate ties between the operands, which addOperand was not
  // able to do reliably.
  for (unsigned i = 0, e = getNumOperands(); i < e; ++i) {
    MachineOperand &NewMO = getOperand(i);
    const MachineOperand &OrigMO = MI.getOperand(i);
    NewMO.TiedTo = OrigMO.TiedTo;
  }

  // Copy all the sensible flags.
  setFlags(MI.Flags);
}

void MachineInstr::setDesc(const MCInstrDesc &TID) {
  if (getParent())
    getMF()->handleChangeDesc(*this, TID);
  MCID = &TID;
  Opcode = TID.Opcode;
}

void MachineInstr::moveBefore(MachineInstr *MovePos) {
  MovePos->getParent()->splice(MovePos, getParent(), getIterator());
}

/// getRegInfo - If this instruction is embedded into a MachineFunction,
/// return the MachineRegisterInfo object for the current function, otherwise
/// return null.
MachineRegisterInfo *MachineInstr::getRegInfo() {
  if (MachineBasicBlock *MBB = getParent())
    return &MBB->getParent()->getRegInfo();
  return nullptr;
}

const MachineRegisterInfo *MachineInstr::getRegInfo() const {
  if (const MachineBasicBlock *MBB = getParent())
    return &MBB->getParent()->getRegInfo();
  return nullptr;
}

void MachineInstr::removeRegOperandsFromUseLists(MachineRegisterInfo &MRI) {
  for (MachineOperand &MO : operands())
    if (MO.isReg())
      MRI.removeRegOperandFromUseList(&MO);
}

void MachineInstr::addRegOperandsToUseLists(MachineRegisterInfo &MRI) {
  for (MachineOperand &MO : operands())
    if (MO.isReg())
      MRI.addRegOperandToUseList(&MO);
}

void MachineInstr::addOperand(const MachineOperand &Op) {
  MachineBasicBlock *MBB = getParent();
  assert(MBB && "Use MachineInstrBuilder to add operands to dangling instrs");
  MachineFunction *MF = MBB->getParent();
  assert(MF && "Use MachineInstrBuilder to add operands to dangling instrs");
  addOperand(*MF, Op);
}

/// Move NumOps MachineOperands from Src to Dst, with support for overlapping
/// ranges. If MRI is non-null also update use-def chains.
static void moveOperands(MachineOperand *Dst, MachineOperand *Src,
                         unsigned NumOps, MachineRegisterInfo *MRI) {
  if (MRI)
    return MRI->moveOperands(Dst, Src, NumOps);
  // MachineOperand is a trivially copyable type so we can just use memmove.
  assert(Dst && Src && "Unknown operands");
  std::memmove(Dst, Src, NumOps * sizeof(MachineOperand));
}

/// addOperand - Add the specified operand to the instruction.  If it is an
/// implicit operand, it is added to the end of the operand list.  If it is
/// an explicit operand it is added at the end of the explicit operand list
/// (before the first implicit operand).
void MachineInstr::addOperand(MachineFunction &MF, const MachineOperand &Op) {
  assert(isUInt<LLVM_MI_NUMOPERANDS_BITS>(NumOperands + 1) &&
         "Cannot add more operands.");
  assert(MCID && "Cannot add operands before providing an instr descriptor");

  // Check if we're adding one of our existing operands.
  if (&Op >= Operands && &Op < Operands + NumOperands) {
    // This is unusual: MI->addOperand(MI->getOperand(i)).
    // If adding Op requires reallocating or moving existing operands around,
    // the Op reference could go stale. Support it by copying Op.
    MachineOperand CopyOp(Op);
    return addOperand(MF, CopyOp);
  }

  // Find the insert location for the new operand.  Implicit registers go at
  // the end, everything else goes before the implicit regs.
  //
  // FIXME: Allow mixed explicit and implicit operands on inline asm.
  // InstrEmitter::EmitSpecialNode() is marking inline asm clobbers as
  // implicit-defs, but they must not be moved around.  See the FIXME in
  // InstrEmitter.cpp.
  unsigned OpNo = getNumOperands();
  bool isImpReg = Op.isReg() && Op.isImplicit();
  if (!isImpReg && !isInlineAsm()) {
    while (OpNo && Operands[OpNo-1].isReg() && Operands[OpNo-1].isImplicit()) {
      --OpNo;
      assert(!Operands[OpNo].isTied() && "Cannot move tied operands");
    }
  }

  // OpNo now points as the desired insertion point.  Unless this is a variadic
  // instruction, only implicit regs are allowed beyond MCID->getNumOperands().
  // RegMask operands go between the explicit and implicit operands.
  MachineRegisterInfo *MRI = getRegInfo();

  // Determine if the Operands array needs to be reallocated.
  // Save the old capacity and operand array.
  OperandCapacity OldCap = CapOperands;
  MachineOperand *OldOperands = Operands;
  if (!OldOperands || OldCap.getSize() == getNumOperands()) {
    CapOperands = OldOperands ? OldCap.getNext() : OldCap.get(1);
    Operands = MF.allocateOperandArray(CapOperands);
    // Move the operands before the insertion point.
    if (OpNo)
      moveOperands(Operands, OldOperands, OpNo, MRI);
  }

  // Move the operands following the insertion point.
  if (OpNo != NumOperands)
    moveOperands(Operands + OpNo + 1, OldOperands + OpNo, NumOperands - OpNo,
                 MRI);
  ++NumOperands;

  // Deallocate the old operand array.
  if (OldOperands != Operands && OldOperands)
    MF.deallocateOperandArray(OldCap, OldOperands);

  // Copy Op into place. It still needs to be inserted into the MRI use lists.
  MachineOperand *NewMO = new (Operands + OpNo) MachineOperand(Op);
  NewMO->ParentMI = this;

  // When adding a register operand, tell MRI about it.
  if (NewMO->isReg()) {
    // Ensure isOnRegUseList() returns false, regardless of Op's status.
    NewMO->Contents.Reg.Prev = nullptr;
    // Ignore existing ties. This is not a property that can be copied.
    NewMO->TiedTo = 0;
    // Add the new operand to MRI, but only for instructions in an MBB.
    if (MRI)
      MRI->addRegOperandToUseList(NewMO);
    // The MCID operand information isn't accurate until we start adding
    // explicit operands. The implicit operands are added first, then the
    // explicits are inserted before them.
    if (!isImpReg) {
      // Tie uses to defs as indicated in MCInstrDesc.
      if (NewMO->isUse()) {
        int DefIdx = MCID->getOperandConstraint(OpNo, MCOI::TIED_TO);
        if (DefIdx != -1)
          tieOperands(DefIdx, OpNo);
      }
      // If the register operand is flagged as early, mark the operand as such.
      if (MCID->getOperandConstraint(OpNo, MCOI::EARLY_CLOBBER) != -1)
        NewMO->setIsEarlyClobber(true);
    }
    // Ensure debug instructions set debug flag on register uses.
    if (NewMO->isUse() && isDebugInstr())
      NewMO->setIsDebug();
  }
}

void MachineInstr::removeOperand(unsigned OpNo) {
  assert(OpNo < getNumOperands() && "Invalid operand number");
  untieRegOperand(OpNo);

#ifndef NDEBUG
  // Moving tied operands would break the ties.
  for (unsigned i = OpNo + 1, e = getNumOperands(); i != e; ++i)
    if (Operands[i].isReg())
      assert(!Operands[i].isTied() && "Cannot move tied operands");
#endif

  MachineRegisterInfo *MRI = getRegInfo();
  if (MRI && Operands[OpNo].isReg())
    MRI->removeRegOperandFromUseList(Operands + OpNo);

  // Don't call the MachineOperand destructor. A lot of this code depends on
  // MachineOperand having a trivial destructor anyway, and adding a call here
  // wouldn't make it 'destructor-correct'.

  if (unsigned N = NumOperands - 1 - OpNo)
    moveOperands(Operands + OpNo, Operands + OpNo + 1, N, MRI);
  --NumOperands;
}

void MachineInstr::setExtraInfo(MachineFunction &MF,
                                ArrayRef<MachineMemOperand *> MMOs,
                                MCSymbol *PreInstrSymbol,
                                MCSymbol *PostInstrSymbol,
                                MDNode *HeapAllocMarker, MDNode *PCSections,
                                uint32_t CFIType, MDNode *MMRAs) {
  bool HasPreInstrSymbol = PreInstrSymbol != nullptr;
  bool HasPostInstrSymbol = PostInstrSymbol != nullptr;
  bool HasHeapAllocMarker = HeapAllocMarker != nullptr;
  bool HasPCSections = PCSections != nullptr;
  bool HasCFIType = CFIType != 0;
  bool HasMMRAs = MMRAs != nullptr;
  int NumPointers = MMOs.size() + HasPreInstrSymbol + HasPostInstrSymbol +
                    HasHeapAllocMarker + HasPCSections + HasCFIType + HasMMRAs;

  // Drop all extra info if there is none.
  if (NumPointers <= 0) {
    Info.clear();
    return;
  }

  // If more than one pointer, then store out of line. Store heap alloc markers
  // out of line because PointerSumType cannot hold more than 4 tag types with
  // 32-bit pointers.
  // FIXME: Maybe we should make the symbols in the extra info mutable?
  else if (NumPointers > 1 || HasMMRAs || HasHeapAllocMarker || HasPCSections ||
           HasCFIType) {
    Info.set<EIIK_OutOfLine>(
        MF.createMIExtraInfo(MMOs, PreInstrSymbol, PostInstrSymbol,
                             HeapAllocMarker, PCSections, CFIType, MMRAs));
    return;
  }

  // Otherwise store the single pointer inline.
  if (HasPreInstrSymbol)
    Info.set<EIIK_PreInstrSymbol>(PreInstrSymbol);
  else if (HasPostInstrSymbol)
    Info.set<EIIK_PostInstrSymbol>(PostInstrSymbol);
  else
    Info.set<EIIK_MMO>(MMOs[0]);
}

void MachineInstr::dropMemRefs(MachineFunction &MF) {
  if (memoperands_empty())
    return;

  setExtraInfo(MF, {}, getPreInstrSymbol(), getPostInstrSymbol(),
               getHeapAllocMarker(), getPCSections(), getCFIType(),
               getMMRAMetadata());
}

void MachineInstr::setMemRefs(MachineFunction &MF,
                              ArrayRef<MachineMemOperand *> MMOs) {
  if (MMOs.empty()) {
    dropMemRefs(MF);
    return;
  }

  setExtraInfo(MF, MMOs, getPreInstrSymbol(), getPostInstrSymbol(),
               getHeapAllocMarker(), getPCSections(), getCFIType(),
               getMMRAMetadata());
}

void MachineInstr::addMemOperand(MachineFunction &MF,
                                 MachineMemOperand *MO) {
  SmallVector<MachineMemOperand *, 2> MMOs;
  MMOs.append(memoperands_begin(), memoperands_end());
  MMOs.push_back(MO);
  setMemRefs(MF, MMOs);
}

void MachineInstr::cloneMemRefs(MachineFunction &MF, const MachineInstr &MI) {
  if (this == &MI)
    // Nothing to do for a self-clone!
    return;

  assert(&MF == MI.getMF() &&
         "Invalid machine functions when cloning memory refrences!");
  // See if we can just steal the extra info already allocated for the
  // instruction. We can do this whenever the pre- and post-instruction symbols
  // are the same (including null).
  if (getPreInstrSymbol() == MI.getPreInstrSymbol() &&
      getPostInstrSymbol() == MI.getPostInstrSymbol() &&
      getHeapAllocMarker() == MI.getHeapAllocMarker() &&
      getPCSections() == MI.getPCSections() && getMMRAMetadata() &&
      MI.getMMRAMetadata()) {
    Info = MI.Info;
    return;
  }

  // Otherwise, fall back on a copy-based clone.
  setMemRefs(MF, MI.memoperands());
}

/// Check to see if the MMOs pointed to by the two MemRefs arrays are
/// identical.
static bool hasIdenticalMMOs(ArrayRef<MachineMemOperand *> LHS,
                             ArrayRef<MachineMemOperand *> RHS) {
  if (LHS.size() != RHS.size())
    return false;

  auto LHSPointees = make_pointee_range(LHS);
  auto RHSPointees = make_pointee_range(RHS);
  return std::equal(LHSPointees.begin(), LHSPointees.end(),
                    RHSPointees.begin());
}

void MachineInstr::cloneMergedMemRefs(MachineFunction &MF,
                                      ArrayRef<const MachineInstr *> MIs) {
  // Try handling easy numbers of MIs with simpler mechanisms.
  if (MIs.empty()) {
    dropMemRefs(MF);
    return;
  }
  if (MIs.size() == 1) {
    cloneMemRefs(MF, *MIs[0]);
    return;
  }
  // Because an empty memoperands list provides *no* information and must be
  // handled conservatively (assuming the instruction can do anything), the only
  // way to merge with it is to drop all other memoperands.
  if (MIs[0]->memoperands_empty()) {
    dropMemRefs(MF);
    return;
  }

  // Handle the general case.
  SmallVector<MachineMemOperand *, 2> MergedMMOs;
  // Start with the first instruction.
  assert(&MF == MIs[0]->getMF() &&
         "Invalid machine functions when cloning memory references!");
  MergedMMOs.append(MIs[0]->memoperands_begin(), MIs[0]->memoperands_end());
  // Now walk all the other instructions and accumulate any different MMOs.
  for (const MachineInstr &MI : make_pointee_range(MIs.slice(1))) {
    assert(&MF == MI.getMF() &&
           "Invalid machine functions when cloning memory references!");

    // Skip MIs with identical operands to the first. This is a somewhat
    // arbitrary hack but will catch common cases without being quadratic.
    // TODO: We could fully implement merge semantics here if needed.
    if (hasIdenticalMMOs(MIs[0]->memoperands(), MI.memoperands()))
      continue;

    // Because an empty memoperands list provides *no* information and must be
    // handled conservatively (assuming the instruction can do anything), the
    // only way to merge with it is to drop all other memoperands.
    if (MI.memoperands_empty()) {
      dropMemRefs(MF);
      return;
    }

    // Otherwise accumulate these into our temporary buffer of the merged state.
    MergedMMOs.append(MI.memoperands_begin(), MI.memoperands_end());
  }

  setMemRefs(MF, MergedMMOs);
}

void MachineInstr::setPreInstrSymbol(MachineFunction &MF, MCSymbol *Symbol) {
  // Do nothing if old and new symbols are the same.
  if (Symbol == getPreInstrSymbol())
    return;

  // If there was only one symbol and we're removing it, just clear info.
  if (!Symbol && Info.is<EIIK_PreInstrSymbol>()) {
    Info.clear();
    return;
  }

  setExtraInfo(MF, memoperands(), Symbol, getPostInstrSymbol(),
               getHeapAllocMarker(), getPCSections(), getCFIType(),
               getMMRAMetadata());
}

void MachineInstr::setPostInstrSymbol(MachineFunction &MF, MCSymbol *Symbol) {
  // Do nothing if old and new symbols are the same.
  if (Symbol == getPostInstrSymbol())
    return;

  // If there was only one symbol and we're removing it, just clear info.
  if (!Symbol && Info.is<EIIK_PostInstrSymbol>()) {
    Info.clear();
    return;
  }

  setExtraInfo(MF, memoperands(), getPreInstrSymbol(), Symbol,
               getHeapAllocMarker(), getPCSections(), getCFIType(),
               getMMRAMetadata());
}

void MachineInstr::setHeapAllocMarker(MachineFunction &MF, MDNode *Marker) {
  // Do nothing if old and new symbols are the same.
  if (Marker == getHeapAllocMarker())
    return;

  setExtraInfo(MF, memoperands(), getPreInstrSymbol(), getPostInstrSymbol(),
               Marker, getPCSections(), getCFIType(), getMMRAMetadata());
}

void MachineInstr::setPCSections(MachineFunction &MF, MDNode *PCSections) {
  // Do nothing if old and new symbols are the same.
  if (PCSections == getPCSections())
    return;

  setExtraInfo(MF, memoperands(), getPreInstrSymbol(), getPostInstrSymbol(),
               getHeapAllocMarker(), PCSections, getCFIType(),
               getMMRAMetadata());
}

void MachineInstr::setCFIType(MachineFunction &MF, uint32_t Type) {
  // Do nothing if old and new types are the same.
  if (Type == getCFIType())
    return;

  setExtraInfo(MF, memoperands(), getPreInstrSymbol(), getPostInstrSymbol(),
               getHeapAllocMarker(), getPCSections(), Type, getMMRAMetadata());
}

void MachineInstr::setMMRAMetadata(MachineFunction &MF, MDNode *MMRAs) {
  // Do nothing if old and new symbols are the same.
  if (MMRAs == getMMRAMetadata())
    return;

  setExtraInfo(MF, memoperands(), getPreInstrSymbol(), getPostInstrSymbol(),
               getHeapAllocMarker(), getPCSections(), getCFIType(), MMRAs);
}

void MachineInstr::cloneInstrSymbols(MachineFunction &MF,
                                     const MachineInstr &MI) {
  if (this == &MI)
    // Nothing to do for a self-clone!
    return;

  assert(&MF == MI.getMF() &&
         "Invalid machine functions when cloning instruction symbols!");

  setPreInstrSymbol(MF, MI.getPreInstrSymbol());
  setPostInstrSymbol(MF, MI.getPostInstrSymbol());
  setHeapAllocMarker(MF, MI.getHeapAllocMarker());
  setPCSections(MF, MI.getPCSections());
  setMMRAMetadata(MF, MI.getMMRAMetadata());
}

uint32_t MachineInstr::mergeFlagsWith(const MachineInstr &Other) const {
  // For now, the just return the union of the flags. If the flags get more
  // complicated over time, we might need more logic here.
  return getFlags() | Other.getFlags();
}

uint32_t MachineInstr::copyFlagsFromInstruction(const Instruction &I) {
  uint32_t MIFlags = 0;
  // Copy the wrapping flags.
  if (const OverflowingBinaryOperator *OB =
          dyn_cast<OverflowingBinaryOperator>(&I)) {
    if (OB->hasNoSignedWrap())
      MIFlags |= MachineInstr::MIFlag::NoSWrap;
    if (OB->hasNoUnsignedWrap())
      MIFlags |= MachineInstr::MIFlag::NoUWrap;
  } else if (const TruncInst *TI = dyn_cast<TruncInst>(&I)) {
    if (TI->hasNoSignedWrap())
      MIFlags |= MachineInstr::MIFlag::NoSWrap;
    if (TI->hasNoUnsignedWrap())
      MIFlags |= MachineInstr::MIFlag::NoUWrap;
  } else if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&I)) {
    if (GEP->hasNoUnsignedSignedWrap())
      MIFlags |= MachineInstr::MIFlag::NoUSWrap;
    if (GEP->hasNoUnsignedWrap())
      MIFlags |= MachineInstr::MIFlag::NoUWrap;
  }

  // Copy the nonneg flag.
  if (const PossiblyNonNegInst *PNI = dyn_cast<PossiblyNonNegInst>(&I)) {
    if (PNI->hasNonNeg())
      MIFlags |= MachineInstr::MIFlag::NonNeg;
    // Copy the disjoint flag.
  } else if (const PossiblyDisjointInst *PD =
                 dyn_cast<PossiblyDisjointInst>(&I)) {
    if (PD->isDisjoint())
      MIFlags |= MachineInstr::MIFlag::Disjoint;
  }

  // Copy the exact flag.
  if (const PossiblyExactOperator *PE = dyn_cast<PossiblyExactOperator>(&I))
    if (PE->isExact())
      MIFlags |= MachineInstr::MIFlag::IsExact;

  // Copy the fast-math flags.
  if (const FPMathOperator *FP = dyn_cast<FPMathOperator>(&I)) {
    const FastMathFlags Flags = FP->getFastMathFlags();
    if (Flags.noNaNs())
      MIFlags |= MachineInstr::MIFlag::FmNoNans;
    if (Flags.noInfs())
      MIFlags |= MachineInstr::MIFlag::FmNoInfs;
    if (Flags.noSignedZeros())
      MIFlags |= MachineInstr::MIFlag::FmNsz;
    if (Flags.allowReciprocal())
      MIFlags |= MachineInstr::MIFlag::FmArcp;
    if (Flags.allowContract())
      MIFlags |= MachineInstr::MIFlag::FmContract;
    if (Flags.approxFunc())
      MIFlags |= MachineInstr::MIFlag::FmAfn;
    if (Flags.allowReassoc())
      MIFlags |= MachineInstr::MIFlag::FmReassoc;
  }

  if (I.getMetadata(LLVMContext::MD_unpredictable))
    MIFlags |= MachineInstr::MIFlag::Unpredictable;

  return MIFlags;
}

void MachineInstr::copyIRFlags(const Instruction &I) {
  Flags = copyFlagsFromInstruction(I);
}

bool MachineInstr::hasPropertyInBundle(uint64_t Mask, QueryType Type) const {
  assert(!isBundledWithPred() && "Must be called on bundle header");
  for (MachineBasicBlock::const_instr_iterator MII = getIterator();; ++MII) {
    if (MII->getDesc().getFlags() & Mask) {
      if (Type == AnyInBundle)
        return true;
    } else {
      if (Type == AllInBundle && !MII->isBundle())
        return false;
    }
    // This was the last instruction in the bundle.
    if (!MII->isBundledWithSucc())
      return Type == AllInBundle;
  }
}

bool MachineInstr::isIdenticalTo(const MachineInstr &Other,
                                 MICheckType Check) const {
  // If opcodes or number of operands are not the same then the two
  // instructions are obviously not identical.
  if (Other.getOpcode() != getOpcode() ||
      Other.getNumOperands() != getNumOperands())
    return false;

  if (isBundle()) {
    // We have passed the test above that both instructions have the same
    // opcode, so we know that both instructions are bundles here. Let's compare
    // MIs inside the bundle.
    assert(Other.isBundle() && "Expected that both instructions are bundles.");
    MachineBasicBlock::const_instr_iterator I1 = getIterator();
    MachineBasicBlock::const_instr_iterator I2 = Other.getIterator();
    // Loop until we analysed the last intruction inside at least one of the
    // bundles.
    while (I1->isBundledWithSucc() && I2->isBundledWithSucc()) {
      ++I1;
      ++I2;
      if (!I1->isIdenticalTo(*I2, Check))
        return false;
    }
    // If we've reached the end of just one of the two bundles, but not both,
    // the instructions are not identical.
    if (I1->isBundledWithSucc() || I2->isBundledWithSucc())
      return false;
  }

  // Check operands to make sure they match.
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    const MachineOperand &OMO = Other.getOperand(i);
    if (!MO.isReg()) {
      if (!MO.isIdenticalTo(OMO))
        return false;
      continue;
    }

    // Clients may or may not want to ignore defs when testing for equality.
    // For example, machine CSE pass only cares about finding common
    // subexpressions, so it's safe to ignore virtual register defs.
    if (MO.isDef()) {
      if (Check == IgnoreDefs)
        continue;
      else if (Check == IgnoreVRegDefs) {
        if (!MO.getReg().isVirtual() || !OMO.getReg().isVirtual())
          if (!MO.isIdenticalTo(OMO))
            return false;
      } else {
        if (!MO.isIdenticalTo(OMO))
          return false;
        if (Check == CheckKillDead && MO.isDead() != OMO.isDead())
          return false;
      }
    } else {
      if (!MO.isIdenticalTo(OMO))
        return false;
      if (Check == CheckKillDead && MO.isKill() != OMO.isKill())
        return false;
    }
  }
  // If DebugLoc does not match then two debug instructions are not identical.
  if (isDebugInstr())
    if (getDebugLoc() && Other.getDebugLoc() &&
        getDebugLoc() != Other.getDebugLoc())
      return false;
  // If pre- or post-instruction symbols do not match then the two instructions
  // are not identical.
  if (getPreInstrSymbol() != Other.getPreInstrSymbol() ||
      getPostInstrSymbol() != Other.getPostInstrSymbol())
    return false;
  // Call instructions with different CFI types are not identical.
  if (isCall() && getCFIType() != Other.getCFIType())
    return false;

  return true;
}

bool MachineInstr::isEquivalentDbgInstr(const MachineInstr &Other) const {
  if (!isDebugValueLike() || !Other.isDebugValueLike())
    return false;
  if (getDebugLoc() != Other.getDebugLoc())
    return false;
  if (getDebugVariable() != Other.getDebugVariable())
    return false;
  if (getNumDebugOperands() != Other.getNumDebugOperands())
    return false;
  for (unsigned OpIdx = 0; OpIdx < getNumDebugOperands(); ++OpIdx)
    if (!getDebugOperand(OpIdx).isIdenticalTo(Other.getDebugOperand(OpIdx)))
      return false;
  if (!DIExpression::isEqualExpression(
          getDebugExpression(), isIndirectDebugValue(),
          Other.getDebugExpression(), Other.isIndirectDebugValue()))
    return false;
  return true;
}

const MachineFunction *MachineInstr::getMF() const {
  return getParent()->getParent();
}

MachineInstr *MachineInstr::removeFromParent() {
  assert(getParent() && "Not embedded in a basic block!");
  return getParent()->remove(this);
}

MachineInstr *MachineInstr::removeFromBundle() {
  assert(getParent() && "Not embedded in a basic block!");
  return getParent()->remove_instr(this);
}

void MachineInstr::eraseFromParent() {
  assert(getParent() && "Not embedded in a basic block!");
  getParent()->erase(this);
}

void MachineInstr::eraseFromBundle() {
  assert(getParent() && "Not embedded in a basic block!");
  getParent()->erase_instr(this);
}

bool MachineInstr::isCandidateForCallSiteEntry(QueryType Type) const {
  if (!isCall(Type))
    return false;
  switch (getOpcode()) {
  case TargetOpcode::PATCHPOINT:
  case TargetOpcode::STACKMAP:
  case TargetOpcode::STATEPOINT:
  case TargetOpcode::FENTRY_CALL:
    return false;
  }
  return true;
}

bool MachineInstr::shouldUpdateCallSiteInfo() const {
  if (isBundle())
    return isCandidateForCallSiteEntry(MachineInstr::AnyInBundle);
  return isCandidateForCallSiteEntry();
}

unsigned MachineInstr::getNumExplicitOperands() const {
  unsigned NumOperands = MCID->getNumOperands();
  if (!MCID->isVariadic())
    return NumOperands;

  for (unsigned I = NumOperands, E = getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = getOperand(I);
    // The operands must always be in the following order:
    // - explicit reg defs,
    // - other explicit operands (reg uses, immediates, etc.),
    // - implicit reg defs
    // - implicit reg uses
    if (MO.isReg() && MO.isImplicit())
      break;
    ++NumOperands;
  }
  return NumOperands;
}

unsigned MachineInstr::getNumExplicitDefs() const {
  unsigned NumDefs = MCID->getNumDefs();
  if (!MCID->isVariadic())
    return NumDefs;

  for (unsigned I = NumDefs, E = getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = getOperand(I);
    if (!MO.isReg() || !MO.isDef() || MO.isImplicit())
      break;
    ++NumDefs;
  }
  return NumDefs;
}

void MachineInstr::bundleWithPred() {
  assert(!isBundledWithPred() && "MI is already bundled with its predecessor");
  setFlag(BundledPred);
  MachineBasicBlock::instr_iterator Pred = getIterator();
  --Pred;
  assert(!Pred->isBundledWithSucc() && "Inconsistent bundle flags");
  Pred->setFlag(BundledSucc);
}

void MachineInstr::bundleWithSucc() {
  assert(!isBundledWithSucc() && "MI is already bundled with its successor");
  setFlag(BundledSucc);
  MachineBasicBlock::instr_iterator Succ = getIterator();
  ++Succ;
  assert(!Succ->isBundledWithPred() && "Inconsistent bundle flags");
  Succ->setFlag(BundledPred);
}

void MachineInstr::unbundleFromPred() {
  assert(isBundledWithPred() && "MI isn't bundled with its predecessor");
  clearFlag(BundledPred);
  MachineBasicBlock::instr_iterator Pred = getIterator();
  --Pred;
  assert(Pred->isBundledWithSucc() && "Inconsistent bundle flags");
  Pred->clearFlag(BundledSucc);
}

void MachineInstr::unbundleFromSucc() {
  assert(isBundledWithSucc() && "MI isn't bundled with its successor");
  clearFlag(BundledSucc);
  MachineBasicBlock::instr_iterator Succ = getIterator();
  ++Succ;
  assert(Succ->isBundledWithPred() && "Inconsistent bundle flags");
  Succ->clearFlag(BundledPred);
}

bool MachineInstr::isStackAligningInlineAsm() const {
  if (isInlineAsm()) {
    unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
    if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
      return true;
  }
  return false;
}

InlineAsm::AsmDialect MachineInstr::getInlineAsmDialect() const {
  assert(isInlineAsm() && "getInlineAsmDialect() only works for inline asms!");
  unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
  return InlineAsm::AsmDialect((ExtraInfo & InlineAsm::Extra_AsmDialect) != 0);
}

int MachineInstr::findInlineAsmFlagIdx(unsigned OpIdx,
                                       unsigned *GroupNo) const {
  assert(isInlineAsm() && "Expected an inline asm instruction");
  assert(OpIdx < getNumOperands() && "OpIdx out of range");

  // Ignore queries about the initial operands.
  if (OpIdx < InlineAsm::MIOp_FirstOperand)
    return -1;

  unsigned Group = 0;
  unsigned NumOps;
  for (unsigned i = InlineAsm::MIOp_FirstOperand, e = getNumOperands(); i < e;
       i += NumOps) {
    const MachineOperand &FlagMO = getOperand(i);
    // If we reach the implicit register operands, stop looking.
    if (!FlagMO.isImm())
      return -1;
    const InlineAsm::Flag F(FlagMO.getImm());
    NumOps = 1 + F.getNumOperandRegisters();
    if (i + NumOps > OpIdx) {
      if (GroupNo)
        *GroupNo = Group;
      return i;
    }
    ++Group;
  }
  return -1;
}

const DILabel *MachineInstr::getDebugLabel() const {
  assert(isDebugLabel() && "not a DBG_LABEL");
  return cast<DILabel>(getOperand(0).getMetadata());
}

const MachineOperand &MachineInstr::getDebugVariableOp() const {
  assert((isDebugValueLike()) && "not a DBG_VALUE*");
  unsigned VariableOp = isNonListDebugValue() ? 2 : 0;
  return getOperand(VariableOp);
}

MachineOperand &MachineInstr::getDebugVariableOp() {
  assert((isDebugValueLike()) && "not a DBG_VALUE*");
  unsigned VariableOp = isNonListDebugValue() ? 2 : 0;
  return getOperand(VariableOp);
}

const DILocalVariable *MachineInstr::getDebugVariable() const {
  return cast<DILocalVariable>(getDebugVariableOp().getMetadata());
}

const MachineOperand &MachineInstr::getDebugExpressionOp() const {
  assert((isDebugValueLike()) && "not a DBG_VALUE*");
  unsigned ExpressionOp = isNonListDebugValue() ? 3 : 1;
  return getOperand(ExpressionOp);
}

MachineOperand &MachineInstr::getDebugExpressionOp() {
  assert((isDebugValueLike()) && "not a DBG_VALUE*");
  unsigned ExpressionOp = isNonListDebugValue() ? 3 : 1;
  return getOperand(ExpressionOp);
}

const DIExpression *MachineInstr::getDebugExpression() const {
  return cast<DIExpression>(getDebugExpressionOp().getMetadata());
}

bool MachineInstr::isDebugEntryValue() const {
  return isDebugValue() && getDebugExpression()->isEntryValue();
}

const TargetRegisterClass*
MachineInstr::getRegClassConstraint(unsigned OpIdx,
                                    const TargetInstrInfo *TII,
                                    const TargetRegisterInfo *TRI) const {
  assert(getParent() && "Can't have an MBB reference here!");
  assert(getMF() && "Can't have an MF reference here!");
  const MachineFunction &MF = *getMF();

  // Most opcodes have fixed constraints in their MCInstrDesc.
  if (!isInlineAsm())
    return TII->getRegClass(getDesc(), OpIdx, TRI, MF);

  if (!getOperand(OpIdx).isReg())
    return nullptr;

  // For tied uses on inline asm, get the constraint from the def.
  unsigned DefIdx;
  if (getOperand(OpIdx).isUse() && isRegTiedToDefOperand(OpIdx, &DefIdx))
    OpIdx = DefIdx;

  // Inline asm stores register class constraints in the flag word.
  int FlagIdx = findInlineAsmFlagIdx(OpIdx);
  if (FlagIdx < 0)
    return nullptr;

  const InlineAsm::Flag F(getOperand(FlagIdx).getImm());
  unsigned RCID;
  if ((F.isRegUseKind() || F.isRegDefKind() || F.isRegDefEarlyClobberKind()) &&
      F.hasRegClassConstraint(RCID))
    return TRI->getRegClass(RCID);

  // Assume that all registers in a memory operand are pointers.
  if (F.isMemKind())
    return TRI->getPointerRegClass(MF);

  return nullptr;
}

const TargetRegisterClass *MachineInstr::getRegClassConstraintEffectForVReg(
    Register Reg, const TargetRegisterClass *CurRC, const TargetInstrInfo *TII,
    const TargetRegisterInfo *TRI, bool ExploreBundle) const {
  // Check every operands inside the bundle if we have
  // been asked to.
  if (ExploreBundle)
    for (ConstMIBundleOperands OpndIt(*this); OpndIt.isValid() && CurRC;
         ++OpndIt)
      CurRC = OpndIt->getParent()->getRegClassConstraintEffectForVRegImpl(
          OpndIt.getOperandNo(), Reg, CurRC, TII, TRI);
  else
    // Otherwise, just check the current operands.
    for (unsigned i = 0, e = NumOperands; i < e && CurRC; ++i)
      CurRC = getRegClassConstraintEffectForVRegImpl(i, Reg, CurRC, TII, TRI);
  return CurRC;
}

const TargetRegisterClass *MachineInstr::getRegClassConstraintEffectForVRegImpl(
    unsigned OpIdx, Register Reg, const TargetRegisterClass *CurRC,
    const TargetInstrInfo *TII, const TargetRegisterInfo *TRI) const {
  assert(CurRC && "Invalid initial register class");
  // Check if Reg is constrained by some of its use/def from MI.
  const MachineOperand &MO = getOperand(OpIdx);
  if (!MO.isReg() || MO.getReg() != Reg)
    return CurRC;
  // If yes, accumulate the constraints through the operand.
  return getRegClassConstraintEffect(OpIdx, CurRC, TII, TRI);
}

const TargetRegisterClass *MachineInstr::getRegClassConstraintEffect(
    unsigned OpIdx, const TargetRegisterClass *CurRC,
    const TargetInstrInfo *TII, const TargetRegisterInfo *TRI) const {
  const TargetRegisterClass *OpRC = getRegClassConstraint(OpIdx, TII, TRI);
  const MachineOperand &MO = getOperand(OpIdx);
  assert(MO.isReg() &&
         "Cannot get register constraints for non-register operand");
  assert(CurRC && "Invalid initial register class");
  if (unsigned SubIdx = MO.getSubReg()) {
    if (OpRC)
      CurRC = TRI->getMatchingSuperRegClass(CurRC, OpRC, SubIdx);
    else
      CurRC = TRI->getSubClassWithSubReg(CurRC, SubIdx);
  } else if (OpRC)
    CurRC = TRI->getCommonSubClass(CurRC, OpRC);
  return CurRC;
}

/// Return the number of instructions inside the MI bundle, not counting the
/// header instruction.
unsigned MachineInstr::getBundleSize() const {
  MachineBasicBlock::const_instr_iterator I = getIterator();
  unsigned Size = 0;
  while (I->isBundledWithSucc()) {
    ++Size;
    ++I;
  }
  return Size;
}

/// Returns true if the MachineInstr has an implicit-use operand of exactly
/// the given register (not considering sub/super-registers).
bool MachineInstr::hasRegisterImplicitUseOperand(Register Reg) const {
  for (const MachineOperand &MO : operands()) {
    if (MO.isReg() && MO.isUse() && MO.isImplicit() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

/// findRegisterUseOperandIdx() - Returns the MachineOperand that is a use of
/// the specific register or -1 if it is not found. It further tightens
/// the search criteria to a use that kills the register if isKill is true.
int MachineInstr::findRegisterUseOperandIdx(Register Reg,
                                            const TargetRegisterInfo *TRI,
                                            bool isKill) const {
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || !MO.isUse())
      continue;
    Register MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MOReg == Reg || (TRI && Reg && MOReg && TRI->regsOverlap(MOReg, Reg)))
      if (!isKill || MO.isKill())
        return i;
  }
  return -1;
}

/// readsWritesVirtualRegister - Return a pair of bools (reads, writes)
/// indicating if this instruction reads or writes Reg. This also considers
/// partial defines.
std::pair<bool,bool>
MachineInstr::readsWritesVirtualRegister(Register Reg,
                                         SmallVectorImpl<unsigned> *Ops) const {
  bool PartDef = false; // Partial redefine.
  bool FullDef = false; // Full define.
  bool Use = false;

  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || MO.getReg() != Reg)
      continue;
    if (Ops)
      Ops->push_back(i);
    if (MO.isUse())
      Use |= !MO.isUndef();
    else if (MO.getSubReg() && !MO.isUndef())
      // A partial def undef doesn't count as reading the register.
      PartDef = true;
    else
      FullDef = true;
  }
  // A partial redefine uses Reg unless there is also a full define.
  return std::make_pair(Use || (PartDef && !FullDef), PartDef || FullDef);
}

/// findRegisterDefOperandIdx() - Returns the operand index that is a def of
/// the specified register or -1 if it is not found. If isDead is true, defs
/// that are not dead are skipped. If TargetRegisterInfo is non-null, then it
/// also checks if there is a def of a super-register.
int MachineInstr::findRegisterDefOperandIdx(Register Reg,
                                            const TargetRegisterInfo *TRI,
                                            bool isDead, bool Overlap) const {
  bool isPhys = Reg.isPhysical();
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    // Accept regmask operands when Overlap is set.
    // Ignore them when looking for a specific def operand (Overlap == false).
    if (isPhys && Overlap && MO.isRegMask() && MO.clobbersPhysReg(Reg))
      return i;
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register MOReg = MO.getReg();
    bool Found = (MOReg == Reg);
    if (!Found && TRI && isPhys && MOReg.isPhysical()) {
      if (Overlap)
        Found = TRI->regsOverlap(MOReg, Reg);
      else
        Found = TRI->isSubRegister(MOReg, Reg);
    }
    if (Found && (!isDead || MO.isDead()))
      return i;
  }
  return -1;
}

/// findFirstPredOperandIdx() - Find the index of the first operand in the
/// operand list that is used to represent the predicate. It returns -1 if
/// none is found.
int MachineInstr::findFirstPredOperandIdx() const {
  // Don't call MCID.findFirstPredOperandIdx() because this variant
  // is sometimes called on an instruction that's not yet complete, and
  // so the number of operands is less than the MCID indicates. In
  // particular, the PTX target does this.
  const MCInstrDesc &MCID = getDesc();
  if (MCID.isPredicable()) {
    for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
      if (MCID.operands()[i].isPredicate())
        return i;
  }

  return -1;
}

// MachineOperand::TiedTo is 4 bits wide.
const unsigned TiedMax = 15;

/// tieOperands - Mark operands at DefIdx and UseIdx as tied to each other.
///
/// Use and def operands can be tied together, indicated by a non-zero TiedTo
/// field. TiedTo can have these values:
///
/// 0:              Operand is not tied to anything.
/// 1 to TiedMax-1: Tied to getOperand(TiedTo-1).
/// TiedMax:        Tied to an operand >= TiedMax-1.
///
/// The tied def must be one of the first TiedMax operands on a normal
/// instruction. INLINEASM instructions allow more tied defs.
///
void MachineInstr::tieOperands(unsigned DefIdx, unsigned UseIdx) {
  MachineOperand &DefMO = getOperand(DefIdx);
  MachineOperand &UseMO = getOperand(UseIdx);
  assert(DefMO.isDef() && "DefIdx must be a def operand");
  assert(UseMO.isUse() && "UseIdx must be a use operand");
  assert(!DefMO.isTied() && "Def is already tied to another use");
  assert(!UseMO.isTied() && "Use is already tied to another def");

  if (DefIdx < TiedMax)
    UseMO.TiedTo = DefIdx + 1;
  else {
    // Inline asm can use the group descriptors to find tied operands,
    // statepoint tied operands are trivial to match (1-1 reg def with reg use),
    // but on normal instruction, the tied def must be within the first TiedMax
    // operands.
    assert((isInlineAsm() || getOpcode() == TargetOpcode::STATEPOINT) &&
           "DefIdx out of range");
    UseMO.TiedTo = TiedMax;
  }

  // UseIdx can be out of range, we'll search for it in findTiedOperandIdx().
  DefMO.TiedTo = std::min(UseIdx + 1, TiedMax);
}

/// Given the index of a tied register operand, find the operand it is tied to.
/// Defs are tied to uses and vice versa. Returns the index of the tied operand
/// which must exist.
unsigned MachineInstr::findTiedOperandIdx(unsigned OpIdx) const {
  const MachineOperand &MO = getOperand(OpIdx);
  assert(MO.isTied() && "Operand isn't tied");

  // Normally TiedTo is in range.
  if (MO.TiedTo < TiedMax)
    return MO.TiedTo - 1;

  // Uses on normal instructions can be out of range.
  if (!isInlineAsm() && getOpcode() != TargetOpcode::STATEPOINT) {
    // Normal tied defs must be in the 0..TiedMax-1 range.
    if (MO.isUse())
      return TiedMax - 1;
    // MO is a def. Search for the tied use.
    for (unsigned i = TiedMax - 1, e = getNumOperands(); i != e; ++i) {
      const MachineOperand &UseMO = getOperand(i);
      if (UseMO.isReg() && UseMO.isUse() && UseMO.TiedTo == OpIdx + 1)
        return i;
    }
    llvm_unreachable("Can't find tied use");
  }

  if (getOpcode() == TargetOpcode::STATEPOINT) {
    // In STATEPOINT defs correspond 1-1 to GC pointer operands passed
    // on registers.
    StatepointOpers SO(this);
    unsigned CurUseIdx = SO.getFirstGCPtrIdx();
    assert(CurUseIdx != -1U && "only gc pointer statepoint operands can be tied");
    unsigned NumDefs = getNumDefs();
    for (unsigned CurDefIdx = 0; CurDefIdx < NumDefs; ++CurDefIdx) {
      while (!getOperand(CurUseIdx).isReg())
        CurUseIdx = StackMaps::getNextMetaArgIdx(this, CurUseIdx);
      if (OpIdx == CurDefIdx)
        return CurUseIdx;
      if (OpIdx == CurUseIdx)
        return CurDefIdx;
      CurUseIdx = StackMaps::getNextMetaArgIdx(this, CurUseIdx);
    }
    llvm_unreachable("Can't find tied use");
  }

  // Now deal with inline asm by parsing the operand group descriptor flags.
  // Find the beginning of each operand group.
  SmallVector<unsigned, 8> GroupIdx;
  unsigned OpIdxGroup = ~0u;
  unsigned NumOps;
  for (unsigned i = InlineAsm::MIOp_FirstOperand, e = getNumOperands(); i < e;
       i += NumOps) {
    const MachineOperand &FlagMO = getOperand(i);
    assert(FlagMO.isImm() && "Invalid tied operand on inline asm");
    unsigned CurGroup = GroupIdx.size();
    GroupIdx.push_back(i);
    const InlineAsm::Flag F(FlagMO.getImm());
    NumOps = 1 + F.getNumOperandRegisters();
    // OpIdx belongs to this operand group.
    if (OpIdx > i && OpIdx < i + NumOps)
      OpIdxGroup = CurGroup;
    unsigned TiedGroup;
    if (!F.isUseOperandTiedToDef(TiedGroup))
      continue;
    // Operands in this group are tied to operands in TiedGroup which must be
    // earlier. Find the number of operands between the two groups.
    unsigned Delta = i - GroupIdx[TiedGroup];

    // OpIdx is a use tied to TiedGroup.
    if (OpIdxGroup == CurGroup)
      return OpIdx - Delta;

    // OpIdx is a def tied to this use group.
    if (OpIdxGroup == TiedGroup)
      return OpIdx + Delta;
  }
  llvm_unreachable("Invalid tied operand on inline asm");
}

/// clearKillInfo - Clears kill flags on all operands.
///
void MachineInstr::clearKillInfo() {
  for (MachineOperand &MO : operands()) {
    if (MO.isReg() && MO.isUse())
      MO.setIsKill(false);
  }
}

void MachineInstr::substituteRegister(Register FromReg, Register ToReg,
                                      unsigned SubIdx,
                                      const TargetRegisterInfo &RegInfo) {
  if (ToReg.isPhysical()) {
    if (SubIdx)
      ToReg = RegInfo.getSubReg(ToReg, SubIdx);
    for (MachineOperand &MO : operands()) {
      if (!MO.isReg() || MO.getReg() != FromReg)
        continue;
      MO.substPhysReg(ToReg, RegInfo);
    }
  } else {
    for (MachineOperand &MO : operands()) {
      if (!MO.isReg() || MO.getReg() != FromReg)
        continue;
      MO.substVirtReg(ToReg, SubIdx, RegInfo);
    }
  }
}

/// isSafeToMove - Return true if it is safe to move this instruction. If
/// SawStore is set to true, it means that there is a store (or call) between
/// the instruction's location and its intended destination.
bool MachineInstr::isSafeToMove(AAResults *AA, bool &SawStore) const {
  // Ignore stuff that we obviously can't move.
  //
  // Treat volatile loads as stores. This is not strictly necessary for
  // volatiles, but it is required for atomic loads. It is not allowed to move
  // a load across an atomic load with Ordering > Monotonic.
  if (mayStore() || isCall() || isPHI() ||
      (mayLoad() && hasOrderedMemoryRef())) {
    SawStore = true;
    return false;
  }

  if (isPosition() || isDebugInstr() || isTerminator() ||
      mayRaiseFPException() || hasUnmodeledSideEffects() ||
      isJumpTableDebugInfo())
    return false;

  // See if this instruction does a load.  If so, we have to guarantee that the
  // loaded value doesn't change between the load and the its intended
  // destination. The check for isInvariantLoad gives the target the chance to
  // classify the load as always returning a constant, e.g. a constant pool
  // load.
  if (mayLoad() && !isDereferenceableInvariantLoad())
    // Otherwise, this is a real load.  If there is a store between the load and
    // end of block, we can't move it.
    return !SawStore;

  return true;
}

static bool MemOperandsHaveAlias(const MachineFrameInfo &MFI, AAResults *AA,
                                 bool UseTBAA, const MachineMemOperand *MMOa,
                                 const MachineMemOperand *MMOb) {
  // The following interface to AA is fashioned after DAGCombiner::isAlias and
  // operates with MachineMemOperand offset with some important assumptions:
  //   - LLVM fundamentally assumes flat address spaces.
  //   - MachineOperand offset can *only* result from legalization and cannot
  //     affect queries other than the trivial case of overlap checking.
  //   - These offsets never wrap and never step outside of allocated objects.
  //   - There should never be any negative offsets here.
  //
  // FIXME: Modify API to hide this math from "user"
  // Even before we go to AA we can reason locally about some memory objects. It
  // can save compile time, and possibly catch some corner cases not currently
  // covered.

  int64_t OffsetA = MMOa->getOffset();
  int64_t OffsetB = MMOb->getOffset();
  int64_t MinOffset = std::min(OffsetA, OffsetB);

  LocationSize WidthA = MMOa->getSize();
  LocationSize WidthB = MMOb->getSize();
  bool KnownWidthA = WidthA.hasValue();
  bool KnownWidthB = WidthB.hasValue();
  bool BothMMONonScalable = !WidthA.isScalable() && !WidthB.isScalable();

  const Value *ValA = MMOa->getValue();
  const Value *ValB = MMOb->getValue();
  bool SameVal = (ValA && ValB && (ValA == ValB));
  if (!SameVal) {
    const PseudoSourceValue *PSVa = MMOa->getPseudoValue();
    const PseudoSourceValue *PSVb = MMOb->getPseudoValue();
    if (PSVa && ValB && !PSVa->mayAlias(&MFI))
      return false;
    if (PSVb && ValA && !PSVb->mayAlias(&MFI))
      return false;
    if (PSVa && PSVb && (PSVa == PSVb))
      SameVal = true;
  }

  if (SameVal && BothMMONonScalable) {
    if (!KnownWidthA || !KnownWidthB)
      return true;
    int64_t MaxOffset = std::max(OffsetA, OffsetB);
    int64_t LowWidth = (MinOffset == OffsetA)
                           ? WidthA.getValue().getKnownMinValue()
                           : WidthB.getValue().getKnownMinValue();
    return (MinOffset + LowWidth > MaxOffset);
  }

  if (!AA)
    return true;

  if (!ValA || !ValB)
    return true;

  assert((OffsetA >= 0) && "Negative MachineMemOperand offset");
  assert((OffsetB >= 0) && "Negative MachineMemOperand offset");

  // If Scalable Location Size has non-zero offset, Width + Offset does not work
  // at the moment
  if ((WidthA.isScalable() && OffsetA > 0) ||
      (WidthB.isScalable() && OffsetB > 0))
    return true;

  int64_t OverlapA =
      KnownWidthA ? WidthA.getValue().getKnownMinValue() + OffsetA - MinOffset
                  : MemoryLocation::UnknownSize;
  int64_t OverlapB =
      KnownWidthB ? WidthB.getValue().getKnownMinValue() + OffsetB - MinOffset
                  : MemoryLocation::UnknownSize;

  LocationSize LocA = (WidthA.isScalable() || !KnownWidthA)
                          ? WidthA
                          : LocationSize::precise(OverlapA);
  LocationSize LocB = (WidthB.isScalable() || !KnownWidthB)
                          ? WidthB
                          : LocationSize::precise(OverlapB);

  return !AA->isNoAlias(
      MemoryLocation(ValA, LocA, UseTBAA ? MMOa->getAAInfo() : AAMDNodes()),
      MemoryLocation(ValB, LocB, UseTBAA ? MMOb->getAAInfo() : AAMDNodes()));
}

bool MachineInstr::mayAlias(AAResults *AA, const MachineInstr &Other,
                            bool UseTBAA) const {
  const MachineFunction *MF = getMF();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();

  // Exclude call instruction which may alter the memory but can not be handled
  // by this function.
  if (isCall() || Other.isCall())
    return true;

  // If neither instruction stores to memory, they can't alias in any
  // meaningful way, even if they read from the same address.
  if (!mayStore() && !Other.mayStore())
    return false;

  // Both instructions must be memory operations to be able to alias.
  if (!mayLoadOrStore() || !Other.mayLoadOrStore())
    return false;

  // Let the target decide if memory accesses cannot possibly overlap.
  if (TII->areMemAccessesTriviallyDisjoint(*this, Other))
    return false;

  // Memory operations without memory operands may access anything. Be
  // conservative and assume `MayAlias`.
  if (memoperands_empty() || Other.memoperands_empty())
    return true;

  // Skip if there are too many memory operands.
  auto NumChecks = getNumMemOperands() * Other.getNumMemOperands();
  if (NumChecks > TII->getMemOperandAACheckLimit())
    return true;

  // Check each pair of memory operands from both instructions, which can't
  // alias only if all pairs won't alias.
  for (auto *MMOa : memoperands())
    for (auto *MMOb : Other.memoperands())
      if (MemOperandsHaveAlias(MFI, AA, UseTBAA, MMOa, MMOb))
        return true;

  return false;
}

/// hasOrderedMemoryRef - Return true if this instruction may have an ordered
/// or volatile memory reference, or if the information describing the memory
/// reference is not available. Return false if it is known to have no ordered
/// memory references.
bool MachineInstr::hasOrderedMemoryRef() const {
  // An instruction known never to access memory won't have a volatile access.
  if (!mayStore() &&
      !mayLoad() &&
      !isCall() &&
      !hasUnmodeledSideEffects())
    return false;

  // Otherwise, if the instruction has no memory reference information,
  // conservatively assume it wasn't preserved.
  if (memoperands_empty())
    return true;

  // Check if any of our memory operands are ordered.
  return llvm::any_of(memoperands(), [](const MachineMemOperand *MMO) {
    return !MMO->isUnordered();
  });
}

/// isDereferenceableInvariantLoad - Return true if this instruction will never
/// trap and is loading from a location whose value is invariant across a run of
/// this function.
bool MachineInstr::isDereferenceableInvariantLoad() const {
  // If the instruction doesn't load at all, it isn't an invariant load.
  if (!mayLoad())
    return false;

  // If the instruction has lost its memoperands, conservatively assume that
  // it may not be an invariant load.
  if (memoperands_empty())
    return false;

  const MachineFrameInfo &MFI = getParent()->getParent()->getFrameInfo();

  for (MachineMemOperand *MMO : memoperands()) {
    if (!MMO->isUnordered())
      // If the memory operand has ordering side effects, we can't move the
      // instruction.  Such an instruction is technically an invariant load,
      // but the caller code would need updated to expect that.
      return false;
    if (MMO->isStore()) return false;
    if (MMO->isInvariant() && MMO->isDereferenceable())
      continue;

    // A load from a constant PseudoSourceValue is invariant.
    if (const PseudoSourceValue *PSV = MMO->getPseudoValue()) {
      if (PSV->isConstant(&MFI))
        continue;
    }

    // Otherwise assume conservatively.
    return false;
  }

  // Everything checks out.
  return true;
}

/// isConstantValuePHI - If the specified instruction is a PHI that always
/// merges together the same virtual register, return the register, otherwise
/// return 0.
unsigned MachineInstr::isConstantValuePHI() const {
  if (!isPHI())
    return 0;
  assert(getNumOperands() >= 3 &&
         "It's illegal to have a PHI without source operands");

  Register Reg = getOperand(1).getReg();
  for (unsigned i = 3, e = getNumOperands(); i < e; i += 2)
    if (getOperand(i).getReg() != Reg)
      return 0;
  return Reg;
}

bool MachineInstr::hasUnmodeledSideEffects() const {
  if (hasProperty(MCID::UnmodeledSideEffects))
    return true;
  if (isInlineAsm()) {
    unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
    if (ExtraInfo & InlineAsm::Extra_HasSideEffects)
      return true;
  }

  return false;
}

bool MachineInstr::isLoadFoldBarrier() const {
  return mayStore() || isCall() ||
         (hasUnmodeledSideEffects() && !isPseudoProbe());
}

/// allDefsAreDead - Return true if all the defs of this instruction are dead.
///
bool MachineInstr::allDefsAreDead() const {
  for (const MachineOperand &MO : operands()) {
    if (!MO.isReg() || MO.isUse())
      continue;
    if (!MO.isDead())
      return false;
  }
  return true;
}

bool MachineInstr::allImplicitDefsAreDead() const {
  for (const MachineOperand &MO : implicit_operands()) {
    if (!MO.isReg() || MO.isUse())
      continue;
    if (!MO.isDead())
      return false;
  }
  return true;
}

/// copyImplicitOps - Copy implicit register operands from specified
/// instruction to this instruction.
void MachineInstr::copyImplicitOps(MachineFunction &MF,
                                   const MachineInstr &MI) {
  for (const MachineOperand &MO :
       llvm::drop_begin(MI.operands(), MI.getDesc().getNumOperands()))
    if ((MO.isReg() && MO.isImplicit()) || MO.isRegMask())
      addOperand(MF, MO);
}

bool MachineInstr::hasComplexRegisterTies() const {
  const MCInstrDesc &MCID = getDesc();
  if (MCID.Opcode == TargetOpcode::STATEPOINT)
    return true;
  for (unsigned I = 0, E = getNumOperands(); I < E; ++I) {
    const auto &Operand = getOperand(I);
    if (!Operand.isReg() || Operand.isDef())
      // Ignore the defined registers as MCID marks only the uses as tied.
      continue;
    int ExpectedTiedIdx = MCID.getOperandConstraint(I, MCOI::TIED_TO);
    int TiedIdx = Operand.isTied() ? int(findTiedOperandIdx(I)) : -1;
    if (ExpectedTiedIdx != TiedIdx)
      return true;
  }
  return false;
}

LLT MachineInstr::getTypeToPrint(unsigned OpIdx, SmallBitVector &PrintedTypes,
                                 const MachineRegisterInfo &MRI) const {
  const MachineOperand &Op = getOperand(OpIdx);
  if (!Op.isReg())
    return LLT{};

  if (isVariadic() || OpIdx >= getNumExplicitOperands())
    return MRI.getType(Op.getReg());

  auto &OpInfo = getDesc().operands()[OpIdx];
  if (!OpInfo.isGenericType())
    return MRI.getType(Op.getReg());

  if (PrintedTypes[OpInfo.getGenericTypeIndex()])
    return LLT{};

  LLT TypeToPrint = MRI.getType(Op.getReg());
  // Don't mark the type index printed if it wasn't actually printed: maybe
  // another operand with the same type index has an actual type attached:
  if (TypeToPrint.isValid())
    PrintedTypes.set(OpInfo.getGenericTypeIndex());
  return TypeToPrint;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineInstr::dump() const {
  dbgs() << "  ";
  print(dbgs());
}

LLVM_DUMP_METHOD void MachineInstr::dumprImpl(
    const MachineRegisterInfo &MRI, unsigned Depth, unsigned MaxDepth,
    SmallPtrSetImpl<const MachineInstr *> &AlreadySeenInstrs) const {
  if (Depth >= MaxDepth)
    return;
  if (!AlreadySeenInstrs.insert(this).second)
    return;
  // PadToColumn always inserts at least one space.
  // Don't mess up the alignment if we don't want any space.
  if (Depth)
    fdbgs().PadToColumn(Depth * 2);
  print(fdbgs());
  for (const MachineOperand &MO : operands()) {
    if (!MO.isReg() || MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (Reg.isPhysical())
      continue;
    const MachineInstr *NewMI = MRI.getUniqueVRegDef(Reg);
    if (NewMI == nullptr)
      continue;
    NewMI->dumprImpl(MRI, Depth + 1, MaxDepth, AlreadySeenInstrs);
  }
}

LLVM_DUMP_METHOD void MachineInstr::dumpr(const MachineRegisterInfo &MRI,
                                          unsigned MaxDepth) const {
  SmallPtrSet<const MachineInstr *, 16> AlreadySeenInstrs;
  dumprImpl(MRI, 0, MaxDepth, AlreadySeenInstrs);
}
#endif

void MachineInstr::print(raw_ostream &OS, bool IsStandalone, bool SkipOpers,
                         bool SkipDebugLoc, bool AddNewLine,
                         const TargetInstrInfo *TII) const {
  const Module *M = nullptr;
  const Function *F = nullptr;
  if (const MachineFunction *MF = getMFIfAvailable(*this)) {
    F = &MF->getFunction();
    M = F->getParent();
    if (!TII)
      TII = MF->getSubtarget().getInstrInfo();
  }

  ModuleSlotTracker MST(M);
  if (F)
    MST.incorporateFunction(*F);
  print(OS, MST, IsStandalone, SkipOpers, SkipDebugLoc, AddNewLine, TII);
}

void MachineInstr::print(raw_ostream &OS, ModuleSlotTracker &MST,
                         bool IsStandalone, bool SkipOpers, bool SkipDebugLoc,
                         bool AddNewLine, const TargetInstrInfo *TII) const {
  // We can be a bit tidier if we know the MachineFunction.
  const TargetRegisterInfo *TRI = nullptr;
  const MachineRegisterInfo *MRI = nullptr;
  const TargetIntrinsicInfo *IntrinsicInfo = nullptr;
  tryToGetTargetInfo(*this, TRI, MRI, IntrinsicInfo, TII);

  if (isCFIInstruction())
    assert(getNumOperands() == 1 && "Expected 1 operand in CFI instruction");

  SmallBitVector PrintedTypes(8);
  bool ShouldPrintRegisterTies = IsStandalone || hasComplexRegisterTies();
  auto getTiedOperandIdx = [&](unsigned OpIdx) {
    if (!ShouldPrintRegisterTies)
      return 0U;
    const MachineOperand &MO = getOperand(OpIdx);
    if (MO.isReg() && MO.isTied() && !MO.isDef())
      return findTiedOperandIdx(OpIdx);
    return 0U;
  };
  unsigned StartOp = 0;
  unsigned e = getNumOperands();

  // Print explicitly defined operands on the left of an assignment syntax.
  while (StartOp < e) {
    const MachineOperand &MO = getOperand(StartOp);
    if (!MO.isReg() || !MO.isDef() || MO.isImplicit())
      break;

    if (StartOp != 0)
      OS << ", ";

    LLT TypeToPrint = MRI ? getTypeToPrint(StartOp, PrintedTypes, *MRI) : LLT{};
    unsigned TiedOperandIdx = getTiedOperandIdx(StartOp);
    MO.print(OS, MST, TypeToPrint, StartOp, /*PrintDef=*/false, IsStandalone,
             ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
    ++StartOp;
  }

  if (StartOp != 0)
    OS << " = ";

  if (getFlag(MachineInstr::FrameSetup))
    OS << "frame-setup ";
  if (getFlag(MachineInstr::FrameDestroy))
    OS << "frame-destroy ";
  if (getFlag(MachineInstr::FmNoNans))
    OS << "nnan ";
  if (getFlag(MachineInstr::FmNoInfs))
    OS << "ninf ";
  if (getFlag(MachineInstr::FmNsz))
    OS << "nsz ";
  if (getFlag(MachineInstr::FmArcp))
    OS << "arcp ";
  if (getFlag(MachineInstr::FmContract))
    OS << "contract ";
  if (getFlag(MachineInstr::FmAfn))
    OS << "afn ";
  if (getFlag(MachineInstr::FmReassoc))
    OS << "reassoc ";
  if (getFlag(MachineInstr::NoUWrap))
    OS << "nuw ";
  if (getFlag(MachineInstr::NoSWrap))
    OS << "nsw ";
  if (getFlag(MachineInstr::IsExact))
    OS << "exact ";
  if (getFlag(MachineInstr::NoFPExcept))
    OS << "nofpexcept ";
  if (getFlag(MachineInstr::NoMerge))
    OS << "nomerge ";
  if (getFlag(MachineInstr::NonNeg))
    OS << "nneg ";
  if (getFlag(MachineInstr::Disjoint))
    OS << "disjoint ";

  // Print the opcode name.
  if (TII)
    OS << TII->getName(getOpcode());
  else
    OS << "UNKNOWN";

  if (SkipOpers)
    return;

  // Print the rest of the operands.
  bool FirstOp = true;
  unsigned AsmDescOp = ~0u;
  unsigned AsmOpCount = 0;

  if (isInlineAsm() && e >= InlineAsm::MIOp_FirstOperand) {
    // Print asm string.
    OS << " ";
    const unsigned OpIdx = InlineAsm::MIOp_AsmString;
    LLT TypeToPrint = MRI ? getTypeToPrint(OpIdx, PrintedTypes, *MRI) : LLT{};
    unsigned TiedOperandIdx = getTiedOperandIdx(OpIdx);
    getOperand(OpIdx).print(OS, MST, TypeToPrint, OpIdx, /*PrintDef=*/true, IsStandalone,
                            ShouldPrintRegisterTies, TiedOperandIdx, TRI,
                            IntrinsicInfo);

    // Print HasSideEffects, MayLoad, MayStore, IsAlignStack
    unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
    if (ExtraInfo & InlineAsm::Extra_HasSideEffects)
      OS << " [sideeffect]";
    if (ExtraInfo & InlineAsm::Extra_MayLoad)
      OS << " [mayload]";
    if (ExtraInfo & InlineAsm::Extra_MayStore)
      OS << " [maystore]";
    if (ExtraInfo & InlineAsm::Extra_IsConvergent)
      OS << " [isconvergent]";
    if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
      OS << " [alignstack]";
    if (getInlineAsmDialect() == InlineAsm::AD_ATT)
      OS << " [attdialect]";
    if (getInlineAsmDialect() == InlineAsm::AD_Intel)
      OS << " [inteldialect]";

    StartOp = AsmDescOp = InlineAsm::MIOp_FirstOperand;
    FirstOp = false;
  }

  for (unsigned i = StartOp, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);

    if (FirstOp) FirstOp = false; else OS << ",";
    OS << " ";

    if (isDebugValueLike() && MO.isMetadata()) {
      // Pretty print DBG_VALUE* instructions.
      auto *DIV = dyn_cast<DILocalVariable>(MO.getMetadata());
      if (DIV && !DIV->getName().empty())
        OS << "!\"" << DIV->getName() << '\"';
      else {
        LLT TypeToPrint = MRI ? getTypeToPrint(i, PrintedTypes, *MRI) : LLT{};
        unsigned TiedOperandIdx = getTiedOperandIdx(i);
        MO.print(OS, MST, TypeToPrint, i, /*PrintDef=*/true, IsStandalone,
                 ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
      }
    } else if (isDebugLabel() && MO.isMetadata()) {
      // Pretty print DBG_LABEL instructions.
      auto *DIL = dyn_cast<DILabel>(MO.getMetadata());
      if (DIL && !DIL->getName().empty())
        OS << "\"" << DIL->getName() << '\"';
      else {
        LLT TypeToPrint = MRI ? getTypeToPrint(i, PrintedTypes, *MRI) : LLT{};
        unsigned TiedOperandIdx = getTiedOperandIdx(i);
        MO.print(OS, MST, TypeToPrint, i, /*PrintDef=*/true, IsStandalone,
                 ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
      }
    } else if (i == AsmDescOp && MO.isImm()) {
      // Pretty print the inline asm operand descriptor.
      OS << '$' << AsmOpCount++;
      unsigned Flag = MO.getImm();
      const InlineAsm::Flag F(Flag);
      OS << ":[";
      OS << F.getKindName();

      unsigned RCID;
      if (!F.isImmKind() && !F.isMemKind() && F.hasRegClassConstraint(RCID)) {
        if (TRI) {
          OS << ':' << TRI->getRegClassName(TRI->getRegClass(RCID));
        } else
          OS << ":RC" << RCID;
      }

      if (F.isMemKind()) {
        const InlineAsm::ConstraintCode MCID = F.getMemoryConstraintID();
        OS << ":" << InlineAsm::getMemConstraintName(MCID);
      }

      unsigned TiedTo;
      if (F.isUseOperandTiedToDef(TiedTo))
        OS << " tiedto:$" << TiedTo;

      if ((F.isRegDefKind() || F.isRegDefEarlyClobberKind() ||
           F.isRegUseKind()) &&
          F.getRegMayBeFolded()) {
        OS << " foldable";
      }

      OS << ']';

      // Compute the index of the next operand descriptor.
      AsmDescOp += 1 + F.getNumOperandRegisters();
    } else {
      LLT TypeToPrint = MRI ? getTypeToPrint(i, PrintedTypes, *MRI) : LLT{};
      unsigned TiedOperandIdx = getTiedOperandIdx(i);
      if (MO.isImm() && isOperandSubregIdx(i))
        MachineOperand::printSubRegIdx(OS, MO.getImm(), TRI);
      else
        MO.print(OS, MST, TypeToPrint, i, /*PrintDef=*/true, IsStandalone,
                 ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
    }
  }

  // Print any optional symbols attached to this instruction as-if they were
  // operands.
  if (MCSymbol *PreInstrSymbol = getPreInstrSymbol()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " pre-instr-symbol ";
    MachineOperand::printSymbol(OS, *PreInstrSymbol);
  }
  if (MCSymbol *PostInstrSymbol = getPostInstrSymbol()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " post-instr-symbol ";
    MachineOperand::printSymbol(OS, *PostInstrSymbol);
  }
  if (MDNode *HeapAllocMarker = getHeapAllocMarker()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " heap-alloc-marker ";
    HeapAllocMarker->printAsOperand(OS, MST);
  }
  if (MDNode *PCSections = getPCSections()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " pcsections ";
    PCSections->printAsOperand(OS, MST);
  }
  if (MDNode *MMRA = getMMRAMetadata()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " mmra ";
    MMRA->printAsOperand(OS, MST);
  }
  if (uint32_t CFIType = getCFIType()) {
    if (!FirstOp)
      OS << ',';
    OS << " cfi-type " << CFIType;
  }

  if (DebugInstrNum) {
    if (!FirstOp)
      OS << ",";
    OS << " debug-instr-number " << DebugInstrNum;
  }

  if (!SkipDebugLoc) {
    if (const DebugLoc &DL = getDebugLoc()) {
      if (!FirstOp)
        OS << ',';
      OS << " debug-location ";
      DL->printAsOperand(OS, MST);
    }
  }

  if (!memoperands_empty()) {
    SmallVector<StringRef, 0> SSNs;
    const LLVMContext *Context = nullptr;
    std::unique_ptr<LLVMContext> CtxPtr;
    const MachineFrameInfo *MFI = nullptr;
    if (const MachineFunction *MF = getMFIfAvailable(*this)) {
      MFI = &MF->getFrameInfo();
      Context = &MF->getFunction().getContext();
    } else {
      CtxPtr = std::make_unique<LLVMContext>();
      Context = CtxPtr.get();
    }

    OS << " :: ";
    bool NeedComma = false;
    for (const MachineMemOperand *Op : memoperands()) {
      if (NeedComma)
        OS << ", ";
      Op->print(OS, MST, SSNs, *Context, MFI, TII);
      NeedComma = true;
    }
  }

  if (SkipDebugLoc)
    return;

  bool HaveSemi = false;

  // Print debug location information.
  if (const DebugLoc &DL = getDebugLoc()) {
    if (!HaveSemi) {
      OS << ';';
      HaveSemi = true;
    }
    OS << ' ';
    DL.print(OS);
  }

  // Print extra comments for DEBUG_VALUE and friends if they are well-formed.
  if ((isNonListDebugValue() && getNumOperands() >= 4) ||
      (isDebugValueList() && getNumOperands() >= 2) ||
      (isDebugRef() && getNumOperands() >= 3)) {
    if (getDebugVariableOp().isMetadata()) {
      if (!HaveSemi) {
        OS << ";";
        HaveSemi = true;
      }
      auto *DV = getDebugVariable();
      OS << " line no:" << DV->getLine();
      if (isIndirectDebugValue())
        OS << " indirect";
    }
  }
  // TODO: DBG_LABEL

  if (AddNewLine)
    OS << '\n';
}

bool MachineInstr::addRegisterKilled(Register IncomingReg,
                                     const TargetRegisterInfo *RegInfo,
                                     bool AddIfNotFound) {
  bool isPhysReg = IncomingReg.isPhysical();
  bool hasAliases = isPhysReg &&
    MCRegAliasIterator(IncomingReg, RegInfo, false).isValid();
  bool Found = false;
  SmallVector<unsigned,4> DeadOps;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || !MO.isUse() || MO.isUndef())
      continue;

    // DEBUG_VALUE nodes do not contribute to code generation and should
    // always be ignored. Failure to do so may result in trying to modify
    // KILL flags on DEBUG_VALUE nodes.
    if (MO.isDebug())
      continue;

    Register Reg = MO.getReg();
    if (!Reg)
      continue;

    if (Reg == IncomingReg) {
      if (!Found) {
        if (MO.isKill())
          // The register is already marked kill.
          return true;
        if (isPhysReg && isRegTiedToDefOperand(i))
          // Two-address uses of physregs must not be marked kill.
          return true;
        MO.setIsKill();
        Found = true;
      }
    } else if (hasAliases && MO.isKill() && Reg.isPhysical()) {
      // A super-register kill already exists.
      if (RegInfo->isSuperRegister(IncomingReg, Reg))
        return true;
      if (RegInfo->isSubRegister(IncomingReg, Reg))
        DeadOps.push_back(i);
    }
  }

  // Trim unneeded kill operands.
  while (!DeadOps.empty()) {
    unsigned OpIdx = DeadOps.back();
    if (getOperand(OpIdx).isImplicit() &&
        (!isInlineAsm() || findInlineAsmFlagIdx(OpIdx) < 0))
      removeOperand(OpIdx);
    else
      getOperand(OpIdx).setIsKill(false);
    DeadOps.pop_back();
  }

  // If not found, this means an alias of one of the operands is killed. Add a
  // new implicit operand if required.
  if (!Found && AddIfNotFound) {
    addOperand(MachineOperand::CreateReg(IncomingReg,
                                         false /*IsDef*/,
                                         true  /*IsImp*/,
                                         true  /*IsKill*/));
    return true;
  }
  return Found;
}

void MachineInstr::clearRegisterKills(Register Reg,
                                      const TargetRegisterInfo *RegInfo) {
  if (!Reg.isPhysical())
    RegInfo = nullptr;
  for (MachineOperand &MO : operands()) {
    if (!MO.isReg() || !MO.isUse() || !MO.isKill())
      continue;
    Register OpReg = MO.getReg();
    if ((RegInfo && RegInfo->regsOverlap(Reg, OpReg)) || Reg == OpReg)
      MO.setIsKill(false);
  }
}

bool MachineInstr::addRegisterDead(Register Reg,
                                   const TargetRegisterInfo *RegInfo,
                                   bool AddIfNotFound) {
  bool isPhysReg = Reg.isPhysical();
  bool hasAliases = isPhysReg &&
    MCRegAliasIterator(Reg, RegInfo, false).isValid();
  bool Found = false;
  SmallVector<unsigned,4> DeadOps;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register MOReg = MO.getReg();
    if (!MOReg)
      continue;

    if (MOReg == Reg) {
      MO.setIsDead();
      Found = true;
    } else if (hasAliases && MO.isDead() && MOReg.isPhysical()) {
      // There exists a super-register that's marked dead.
      if (RegInfo->isSuperRegister(Reg, MOReg))
        return true;
      if (RegInfo->isSubRegister(Reg, MOReg))
        DeadOps.push_back(i);
    }
  }

  // Trim unneeded dead operands.
  while (!DeadOps.empty()) {
    unsigned OpIdx = DeadOps.back();
    if (getOperand(OpIdx).isImplicit() &&
        (!isInlineAsm() || findInlineAsmFlagIdx(OpIdx) < 0))
      removeOperand(OpIdx);
    else
      getOperand(OpIdx).setIsDead(false);
    DeadOps.pop_back();
  }

  // If not found, this means an alias of one of the operands is dead. Add a
  // new implicit operand if required.
  if (Found || !AddIfNotFound)
    return Found;

  addOperand(MachineOperand::CreateReg(Reg,
                                       true  /*IsDef*/,
                                       true  /*IsImp*/,
                                       false /*IsKill*/,
                                       true  /*IsDead*/));
  return true;
}

void MachineInstr::clearRegisterDeads(Register Reg) {
  for (MachineOperand &MO : operands()) {
    if (!MO.isReg() || !MO.isDef() || MO.getReg() != Reg)
      continue;
    MO.setIsDead(false);
  }
}

void MachineInstr::setRegisterDefReadUndef(Register Reg, bool IsUndef) {
  for (MachineOperand &MO : operands()) {
    if (!MO.isReg() || !MO.isDef() || MO.getReg() != Reg || MO.getSubReg() == 0)
      continue;
    MO.setIsUndef(IsUndef);
  }
}

void MachineInstr::addRegisterDefined(Register Reg,
                                      const TargetRegisterInfo *RegInfo) {
  if (Reg.isPhysical()) {
    MachineOperand *MO = findRegisterDefOperand(Reg, RegInfo, false, false);
    if (MO)
      return;
  } else {
    for (const MachineOperand &MO : operands()) {
      if (MO.isReg() && MO.getReg() == Reg && MO.isDef() &&
          MO.getSubReg() == 0)
        return;
    }
  }
  addOperand(MachineOperand::CreateReg(Reg,
                                       true  /*IsDef*/,
                                       true  /*IsImp*/));
}

void MachineInstr::setPhysRegsDeadExcept(ArrayRef<Register> UsedRegs,
                                         const TargetRegisterInfo &TRI) {
  bool HasRegMask = false;
  for (MachineOperand &MO : operands()) {
    if (MO.isRegMask()) {
      HasRegMask = true;
      continue;
    }
    if (!MO.isReg() || !MO.isDef()) continue;
    Register Reg = MO.getReg();
    if (!Reg.isPhysical())
      continue;
    // If there are no uses, including partial uses, the def is dead.
    if (llvm::none_of(UsedRegs,
                      [&](MCRegister Use) { return TRI.regsOverlap(Use, Reg); }))
      MO.setIsDead();
  }

  // This is a call with a register mask operand.
  // Mask clobbers are always dead, so add defs for the non-dead defines.
  if (HasRegMask)
    for (const Register &UsedReg : UsedRegs)
      addRegisterDefined(UsedReg, &TRI);
}

unsigned
MachineInstrExpressionTrait::getHashValue(const MachineInstr* const &MI) {
  // Build up a buffer of hash code components.
  SmallVector<size_t, 16> HashComponents;
  HashComponents.reserve(MI->getNumOperands() + 1);
  HashComponents.push_back(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg().isVirtual())
      continue;  // Skip virtual register defs.

    HashComponents.push_back(hash_value(MO));
  }
  return hash_combine_range(HashComponents.begin(), HashComponents.end());
}

void MachineInstr::emitError(StringRef Msg) const {
  // Find the source location cookie.
  uint64_t LocCookie = 0;
  const MDNode *LocMD = nullptr;
  for (unsigned i = getNumOperands(); i != 0; --i) {
    if (getOperand(i-1).isMetadata() &&
        (LocMD = getOperand(i-1).getMetadata()) &&
        LocMD->getNumOperands() != 0) {
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(LocMD->getOperand(0))) {
        LocCookie = CI->getZExtValue();
        break;
      }
    }
  }

  if (const MachineBasicBlock *MBB = getParent())
    if (const MachineFunction *MF = MBB->getParent())
      return MF->getFunction().getContext().emitError(LocCookie, Msg);
  report_fatal_error(Msg);
}

MachineInstrBuilder llvm::BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                  const MCInstrDesc &MCID, bool IsIndirect,
                                  Register Reg, const MDNode *Variable,
                                  const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  auto MIB = BuildMI(MF, DL, MCID).addReg(Reg);
  if (IsIndirect)
    MIB.addImm(0U);
  else
    MIB.addReg(0U);
  return MIB.addMetadata(Variable).addMetadata(Expr);
}

MachineInstrBuilder llvm::BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                  const MCInstrDesc &MCID, bool IsIndirect,
                                  ArrayRef<MachineOperand> DebugOps,
                                  const MDNode *Variable, const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  if (MCID.Opcode == TargetOpcode::DBG_VALUE) {
    assert(DebugOps.size() == 1 &&
           "DBG_VALUE must contain exactly one debug operand");
    MachineOperand DebugOp = DebugOps[0];
    if (DebugOp.isReg())
      return BuildMI(MF, DL, MCID, IsIndirect, DebugOp.getReg(), Variable,
                     Expr);

    auto MIB = BuildMI(MF, DL, MCID).add(DebugOp);
    if (IsIndirect)
      MIB.addImm(0U);
    else
      MIB.addReg(0U);
    return MIB.addMetadata(Variable).addMetadata(Expr);
  }

  auto MIB = BuildMI(MF, DL, MCID);
  MIB.addMetadata(Variable).addMetadata(Expr);
  for (const MachineOperand &DebugOp : DebugOps)
    if (DebugOp.isReg())
      MIB.addReg(DebugOp.getReg());
    else
      MIB.add(DebugOp);
  return MIB;
}

MachineInstrBuilder llvm::BuildMI(MachineBasicBlock &BB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, const MCInstrDesc &MCID,
                                  bool IsIndirect, Register Reg,
                                  const MDNode *Variable, const MDNode *Expr) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = BuildMI(MF, DL, MCID, IsIndirect, Reg, Variable, Expr);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI);
}

MachineInstrBuilder llvm::BuildMI(MachineBasicBlock &BB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, const MCInstrDesc &MCID,
                                  bool IsIndirect,
                                  ArrayRef<MachineOperand> DebugOps,
                                  const MDNode *Variable, const MDNode *Expr) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI =
      BuildMI(MF, DL, MCID, IsIndirect, DebugOps, Variable, Expr);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, *MI);
}

/// Compute the new DIExpression to use with a DBG_VALUE for a spill slot.
/// This prepends DW_OP_deref when spilling an indirect DBG_VALUE.
static const DIExpression *
computeExprForSpill(const MachineInstr &MI,
                    SmallVectorImpl<const MachineOperand *> &SpilledOperands) {
  assert(MI.getDebugVariable()->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
         "Expected inlined-at fields to agree");

  const DIExpression *Expr = MI.getDebugExpression();
  if (MI.isIndirectDebugValue()) {
    assert(MI.getDebugOffset().getImm() == 0 &&
           "DBG_VALUE with nonzero offset");
    Expr = DIExpression::prepend(Expr, DIExpression::DerefBefore);
  } else if (MI.isDebugValueList()) {
    // We will replace the spilled register with a frame index, so
    // immediately deref all references to the spilled register.
    std::array<uint64_t, 1> Ops{{dwarf::DW_OP_deref}};
    for (const MachineOperand *Op : SpilledOperands) {
      unsigned OpIdx = MI.getDebugOperandIndex(Op);
      Expr = DIExpression::appendOpsToArg(Expr, Ops, OpIdx);
    }
  }
  return Expr;
}
static const DIExpression *computeExprForSpill(const MachineInstr &MI,
                                               Register SpillReg) {
  assert(MI.hasDebugOperandForReg(SpillReg) && "Spill Reg is not used in MI.");
  SmallVector<const MachineOperand *> SpillOperands;
  for (const MachineOperand &Op : MI.getDebugOperandsForReg(SpillReg))
    SpillOperands.push_back(&Op);
  return computeExprForSpill(MI, SpillOperands);
}

MachineInstr *llvm::buildDbgValueForSpill(MachineBasicBlock &BB,
                                          MachineBasicBlock::iterator I,
                                          const MachineInstr &Orig,
                                          int FrameIndex, Register SpillReg) {
  assert(!Orig.isDebugRef() &&
         "DBG_INSTR_REF should not reference a virtual register.");
  const DIExpression *Expr = computeExprForSpill(Orig, SpillReg);
  MachineInstrBuilder NewMI =
      BuildMI(BB, I, Orig.getDebugLoc(), Orig.getDesc());
  // Non-Variadic Operands: Location, Offset, Variable, Expression
  // Variadic Operands:     Variable, Expression, Locations...
  if (Orig.isNonListDebugValue())
    NewMI.addFrameIndex(FrameIndex).addImm(0U);
  NewMI.addMetadata(Orig.getDebugVariable()).addMetadata(Expr);
  if (Orig.isDebugValueList()) {
    for (const MachineOperand &Op : Orig.debug_operands())
      if (Op.isReg() && Op.getReg() == SpillReg)
        NewMI.addFrameIndex(FrameIndex);
      else
        NewMI.add(MachineOperand(Op));
  }
  return NewMI;
}
MachineInstr *llvm::buildDbgValueForSpill(
    MachineBasicBlock &BB, MachineBasicBlock::iterator I,
    const MachineInstr &Orig, int FrameIndex,
    SmallVectorImpl<const MachineOperand *> &SpilledOperands) {
  const DIExpression *Expr = computeExprForSpill(Orig, SpilledOperands);
  MachineInstrBuilder NewMI =
      BuildMI(BB, I, Orig.getDebugLoc(), Orig.getDesc());
  // Non-Variadic Operands: Location, Offset, Variable, Expression
  // Variadic Operands:     Variable, Expression, Locations...
  if (Orig.isNonListDebugValue())
    NewMI.addFrameIndex(FrameIndex).addImm(0U);
  NewMI.addMetadata(Orig.getDebugVariable()).addMetadata(Expr);
  if (Orig.isDebugValueList()) {
    for (const MachineOperand &Op : Orig.debug_operands())
      if (is_contained(SpilledOperands, &Op))
        NewMI.addFrameIndex(FrameIndex);
      else
        NewMI.add(MachineOperand(Op));
  }
  return NewMI;
}

void llvm::updateDbgValueForSpill(MachineInstr &Orig, int FrameIndex,
                                  Register Reg) {
  const DIExpression *Expr = computeExprForSpill(Orig, Reg);
  if (Orig.isNonListDebugValue())
    Orig.getDebugOffset().ChangeToImmediate(0U);
  for (MachineOperand &Op : Orig.getDebugOperandsForReg(Reg))
    Op.ChangeToFrameIndex(FrameIndex);
  Orig.getDebugExpressionOp().setMetadata(Expr);
}

void MachineInstr::collectDebugValues(
                                SmallVectorImpl<MachineInstr *> &DbgValues) {
  MachineInstr &MI = *this;
  if (!MI.getOperand(0).isReg())
    return;

  MachineBasicBlock::iterator DI = MI; ++DI;
  for (MachineBasicBlock::iterator DE = MI.getParent()->end();
       DI != DE; ++DI) {
    if (!DI->isDebugValue())
      return;
    if (DI->hasDebugOperandForReg(MI.getOperand(0).getReg()))
      DbgValues.push_back(&*DI);
  }
}

void MachineInstr::changeDebugValuesDefReg(Register Reg) {
  // Collect matching debug values.
  SmallVector<MachineInstr *, 2> DbgValues;

  if (!getOperand(0).isReg())
    return;

  Register DefReg = getOperand(0).getReg();
  auto *MRI = getRegInfo();
  for (auto &MO : MRI->use_operands(DefReg)) {
    auto *DI = MO.getParent();
    if (!DI->isDebugValue())
      continue;
    if (DI->hasDebugOperandForReg(DefReg)) {
      DbgValues.push_back(DI);
    }
  }

  // Propagate Reg to debug value instructions.
  for (auto *DBI : DbgValues)
    for (MachineOperand &Op : DBI->getDebugOperandsForReg(DefReg))
      Op.setReg(Reg);
}

using MMOList = SmallVector<const MachineMemOperand *, 2>;

static LocationSize getSpillSlotSize(const MMOList &Accesses,
                                     const MachineFrameInfo &MFI) {
  uint64_t Size = 0;
  for (const auto *A : Accesses) {
    if (MFI.isSpillSlotObjectIndex(
            cast<FixedStackPseudoSourceValue>(A->getPseudoValue())
                ->getFrameIndex())) {
      LocationSize S = A->getSize();
      if (!S.hasValue())
        return LocationSize::beforeOrAfterPointer();
      Size += S.getValue();
    }
  }
  return Size;
}

std::optional<LocationSize>
MachineInstr::getSpillSize(const TargetInstrInfo *TII) const {
  int FI;
  if (TII->isStoreToStackSlotPostFE(*this, FI)) {
    const MachineFrameInfo &MFI = getMF()->getFrameInfo();
    if (MFI.isSpillSlotObjectIndex(FI))
      return (*memoperands_begin())->getSize();
  }
  return std::nullopt;
}

std::optional<LocationSize>
MachineInstr::getFoldedSpillSize(const TargetInstrInfo *TII) const {
  MMOList Accesses;
  if (TII->hasStoreToStackSlot(*this, Accesses))
    return getSpillSlotSize(Accesses, getMF()->getFrameInfo());
  return std::nullopt;
}

std::optional<LocationSize>
MachineInstr::getRestoreSize(const TargetInstrInfo *TII) const {
  int FI;
  if (TII->isLoadFromStackSlotPostFE(*this, FI)) {
    const MachineFrameInfo &MFI = getMF()->getFrameInfo();
    if (MFI.isSpillSlotObjectIndex(FI))
      return (*memoperands_begin())->getSize();
  }
  return std::nullopt;
}

std::optional<LocationSize>
MachineInstr::getFoldedRestoreSize(const TargetInstrInfo *TII) const {
  MMOList Accesses;
  if (TII->hasLoadFromStackSlot(*this, Accesses))
    return getSpillSlotSize(Accesses, getMF()->getFrameInfo());
  return std::nullopt;
}

unsigned MachineInstr::getDebugInstrNum() {
  if (DebugInstrNum == 0)
    DebugInstrNum = getParent()->getParent()->getNewDebugInstrNum();
  return DebugInstrNum;
}

unsigned MachineInstr::getDebugInstrNum(MachineFunction &MF) {
  if (DebugInstrNum == 0)
    DebugInstrNum = MF.getNewDebugInstrNum();
  return DebugInstrNum;
}

std::tuple<LLT, LLT> MachineInstr::getFirst2LLTs() const {
  return std::tuple(getRegInfo()->getType(getOperand(0).getReg()),
                    getRegInfo()->getType(getOperand(1).getReg()));
}

std::tuple<LLT, LLT, LLT> MachineInstr::getFirst3LLTs() const {
  return std::tuple(getRegInfo()->getType(getOperand(0).getReg()),
                    getRegInfo()->getType(getOperand(1).getReg()),
                    getRegInfo()->getType(getOperand(2).getReg()));
}

std::tuple<LLT, LLT, LLT, LLT> MachineInstr::getFirst4LLTs() const {
  return std::tuple(getRegInfo()->getType(getOperand(0).getReg()),
                    getRegInfo()->getType(getOperand(1).getReg()),
                    getRegInfo()->getType(getOperand(2).getReg()),
                    getRegInfo()->getType(getOperand(3).getReg()));
}

std::tuple<LLT, LLT, LLT, LLT, LLT> MachineInstr::getFirst5LLTs() const {
  return std::tuple(getRegInfo()->getType(getOperand(0).getReg()),
                    getRegInfo()->getType(getOperand(1).getReg()),
                    getRegInfo()->getType(getOperand(2).getReg()),
                    getRegInfo()->getType(getOperand(3).getReg()),
                    getRegInfo()->getType(getOperand(4).getReg()));
}

std::tuple<Register, LLT, Register, LLT>
MachineInstr::getFirst2RegLLTs() const {
  Register Reg0 = getOperand(0).getReg();
  Register Reg1 = getOperand(1).getReg();
  return std::tuple(Reg0, getRegInfo()->getType(Reg0), Reg1,
                    getRegInfo()->getType(Reg1));
}

std::tuple<Register, LLT, Register, LLT, Register, LLT>
MachineInstr::getFirst3RegLLTs() const {
  Register Reg0 = getOperand(0).getReg();
  Register Reg1 = getOperand(1).getReg();
  Register Reg2 = getOperand(2).getReg();
  return std::tuple(Reg0, getRegInfo()->getType(Reg0), Reg1,
                    getRegInfo()->getType(Reg1), Reg2,
                    getRegInfo()->getType(Reg2));
}

std::tuple<Register, LLT, Register, LLT, Register, LLT, Register, LLT>
MachineInstr::getFirst4RegLLTs() const {
  Register Reg0 = getOperand(0).getReg();
  Register Reg1 = getOperand(1).getReg();
  Register Reg2 = getOperand(2).getReg();
  Register Reg3 = getOperand(3).getReg();
  return std::tuple(
      Reg0, getRegInfo()->getType(Reg0), Reg1, getRegInfo()->getType(Reg1),
      Reg2, getRegInfo()->getType(Reg2), Reg3, getRegInfo()->getType(Reg3));
}

std::tuple<Register, LLT, Register, LLT, Register, LLT, Register, LLT, Register,
           LLT>
MachineInstr::getFirst5RegLLTs() const {
  Register Reg0 = getOperand(0).getReg();
  Register Reg1 = getOperand(1).getReg();
  Register Reg2 = getOperand(2).getReg();
  Register Reg3 = getOperand(3).getReg();
  Register Reg4 = getOperand(4).getReg();
  return std::tuple(
      Reg0, getRegInfo()->getType(Reg0), Reg1, getRegInfo()->getType(Reg1),
      Reg2, getRegInfo()->getType(Reg2), Reg3, getRegInfo()->getType(Reg3),
      Reg4, getRegInfo()->getType(Reg4));
}

void MachineInstr::insert(mop_iterator InsertBefore,
                          ArrayRef<MachineOperand> Ops) {
  assert(InsertBefore != nullptr && "invalid iterator");
  assert(InsertBefore->getParent() == this &&
         "iterator points to operand of other inst");
  if (Ops.empty())
    return;

  // Do one pass to untie operands.
  SmallDenseMap<unsigned, unsigned> TiedOpIndices;
  for (const MachineOperand &MO : operands()) {
    if (MO.isReg() && MO.isTied()) {
      unsigned OpNo = getOperandNo(&MO);
      unsigned TiedTo = findTiedOperandIdx(OpNo);
      TiedOpIndices[OpNo] = TiedTo;
      untieRegOperand(OpNo);
    }
  }

  unsigned OpIdx = getOperandNo(InsertBefore);
  unsigned NumOperands = getNumOperands();
  unsigned OpsToMove = NumOperands - OpIdx;

  SmallVector<MachineOperand> MovingOps;
  MovingOps.reserve(OpsToMove);

  for (unsigned I = 0; I < OpsToMove; ++I) {
    MovingOps.emplace_back(getOperand(OpIdx));
    removeOperand(OpIdx);
  }
  for (const MachineOperand &MO : Ops)
    addOperand(MO);
  for (const MachineOperand &OpMoved : MovingOps)
    addOperand(OpMoved);

  // Re-tie operands.
  for (auto [Tie1, Tie2] : TiedOpIndices) {
    if (Tie1 >= OpIdx)
      Tie1 += Ops.size();
    if (Tie2 >= OpIdx)
      Tie2 += Ops.size();
    tieOperands(Tie1, Tie2);
  }
}

bool MachineInstr::mayFoldInlineAsmRegOp(unsigned OpId) const {
  assert(OpId && "expected non-zero operand id");
  assert(isInlineAsm() && "should only be used on inline asm");

  if (!getOperand(OpId).isReg())
    return false;

  const MachineOperand &MD = getOperand(OpId - 1);
  if (!MD.isImm())
    return false;

  InlineAsm::Flag F(MD.getImm());
  if (F.isRegUseKind() || F.isRegDefKind() || F.isRegDefEarlyClobberKind())
    return F.getRegMayBeFolded();
  return false;
}
