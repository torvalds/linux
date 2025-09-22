//===-- llvm/CodeGen/MachineBasicBlock.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions for a basic block.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cmath>
using namespace llvm;

#define DEBUG_TYPE "codegen"

static cl::opt<bool> PrintSlotIndexes(
    "print-slotindexes",
    cl::desc("When printing machine IR, annotate instructions and blocks with "
             "SlotIndexes when available"),
    cl::init(true), cl::Hidden);

MachineBasicBlock::MachineBasicBlock(MachineFunction &MF, const BasicBlock *B)
    : BB(B), Number(-1), xParent(&MF) {
  Insts.Parent = this;
  if (B)
    IrrLoopHeaderWeight = B->getIrrLoopHeaderWeight();
}

MachineBasicBlock::~MachineBasicBlock() = default;

/// Return the MCSymbol for this basic block.
MCSymbol *MachineBasicBlock::getSymbol() const {
  if (!CachedMCSymbol) {
    const MachineFunction *MF = getParent();
    MCContext &Ctx = MF->getContext();

    // We emit a non-temporary symbol -- with a descriptive name -- if it begins
    // a section (with basic block sections). Otherwise we fall back to use temp
    // label.
    if (MF->hasBBSections() && isBeginSection()) {
      SmallString<5> Suffix;
      if (SectionID == MBBSectionID::ColdSectionID) {
        Suffix += ".cold";
      } else if (SectionID == MBBSectionID::ExceptionSectionID) {
        Suffix += ".eh";
      } else {
        // For symbols that represent basic block sections, we add ".__part." to
        // allow tools like symbolizers to know that this represents a part of
        // the original function.
        Suffix = (Suffix + Twine(".__part.") + Twine(SectionID.Number)).str();
      }
      CachedMCSymbol = Ctx.getOrCreateSymbol(MF->getName() + Suffix);
    } else {
      // If the block occurs as label in inline assembly, parsing the assembly
      // needs an actual label name => set AlwaysEmit in these cases.
      CachedMCSymbol = Ctx.createBlockSymbol(
          "BB" + Twine(MF->getFunctionNumber()) + "_" + Twine(getNumber()),
          /*AlwaysEmit=*/hasLabelMustBeEmitted());
    }
  }
  return CachedMCSymbol;
}

MCSymbol *MachineBasicBlock::getEHCatchretSymbol() const {
  if (!CachedEHCatchretMCSymbol) {
    const MachineFunction *MF = getParent();
    SmallString<128> SymbolName;
    raw_svector_ostream(SymbolName)
        << "$ehgcr_" << MF->getFunctionNumber() << '_' << getNumber();
    CachedEHCatchretMCSymbol = MF->getContext().getOrCreateSymbol(SymbolName);
  }
  return CachedEHCatchretMCSymbol;
}

MCSymbol *MachineBasicBlock::getEndSymbol() const {
  if (!CachedEndMCSymbol) {
    const MachineFunction *MF = getParent();
    MCContext &Ctx = MF->getContext();
    CachedEndMCSymbol = Ctx.createBlockSymbol(
        "BB_END" + Twine(MF->getFunctionNumber()) + "_" + Twine(getNumber()),
        /*AlwaysEmit=*/false);
  }
  return CachedEndMCSymbol;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const MachineBasicBlock &MBB) {
  MBB.print(OS);
  return OS;
}

Printable llvm::printMBBReference(const MachineBasicBlock &MBB) {
  return Printable([&MBB](raw_ostream &OS) { return MBB.printAsOperand(OS); });
}

/// When an MBB is added to an MF, we need to update the parent pointer of the
/// MBB, the MBB numbering, and any instructions in the MBB to be on the right
/// operand list for registers.
///
/// MBBs start out as #-1. When a MBB is added to a MachineFunction, it
/// gets the next available unique MBB number. If it is removed from a
/// MachineFunction, it goes back to being #-1.
void ilist_callback_traits<MachineBasicBlock>::addNodeToList(
    MachineBasicBlock *N) {
  MachineFunction &MF = *N->getParent();
  N->Number = MF.addToMBBNumbering(N);

  // Make sure the instructions have their operands in the reginfo lists.
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  for (MachineInstr &MI : N->instrs())
    MI.addRegOperandsToUseLists(RegInfo);
}

void ilist_callback_traits<MachineBasicBlock>::removeNodeFromList(
    MachineBasicBlock *N) {
  N->getParent()->removeFromMBBNumbering(N->Number);
  N->Number = -1;
}

/// When we add an instruction to a basic block list, we update its parent
/// pointer and add its operands from reg use/def lists if appropriate.
void ilist_traits<MachineInstr>::addNodeToList(MachineInstr *N) {
  assert(!N->getParent() && "machine instruction already in a basic block");
  N->setParent(Parent);

  // Add the instruction's register operands to their corresponding
  // use/def lists.
  MachineFunction *MF = Parent->getParent();
  N->addRegOperandsToUseLists(MF->getRegInfo());
  MF->handleInsertion(*N);
}

/// When we remove an instruction from a basic block list, we update its parent
/// pointer and remove its operands from reg use/def lists if appropriate.
void ilist_traits<MachineInstr>::removeNodeFromList(MachineInstr *N) {
  assert(N->getParent() && "machine instruction not in a basic block");

  // Remove from the use/def lists.
  if (MachineFunction *MF = N->getMF()) {
    MF->handleRemoval(*N);
    N->removeRegOperandsFromUseLists(MF->getRegInfo());
  }

  N->setParent(nullptr);
}

/// When moving a range of instructions from one MBB list to another, we need to
/// update the parent pointers and the use/def lists.
void ilist_traits<MachineInstr>::transferNodesFromList(ilist_traits &FromList,
                                                       instr_iterator First,
                                                       instr_iterator Last) {
  assert(Parent->getParent() == FromList.Parent->getParent() &&
         "cannot transfer MachineInstrs between MachineFunctions");

  // If it's within the same BB, there's nothing to do.
  if (this == &FromList)
    return;

  assert(Parent != FromList.Parent && "Two lists have the same parent?");

  // If splicing between two blocks within the same function, just update the
  // parent pointers.
  for (; First != Last; ++First)
    First->setParent(Parent);
}

void ilist_traits<MachineInstr>::deleteNode(MachineInstr *MI) {
  assert(!MI->getParent() && "MI is still in a block!");
  Parent->getParent()->deleteMachineInstr(MI);
}

MachineBasicBlock::iterator MachineBasicBlock::getFirstNonPHI() {
  instr_iterator I = instr_begin(), E = instr_end();
  while (I != E && I->isPHI())
    ++I;
  assert((I == E || !I->isInsideBundle()) &&
         "First non-phi MI cannot be inside a bundle!");
  return I;
}

MachineBasicBlock::iterator
MachineBasicBlock::SkipPHIsAndLabels(MachineBasicBlock::iterator I) {
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();

  iterator E = end();
  while (I != E && (I->isPHI() || I->isPosition() ||
                    TII->isBasicBlockPrologue(*I)))
    ++I;
  // FIXME: This needs to change if we wish to bundle labels
  // inside the bundle.
  assert((I == E || !I->isInsideBundle()) &&
         "First non-phi / non-label instruction is inside a bundle!");
  return I;
}

MachineBasicBlock::iterator
MachineBasicBlock::SkipPHIsLabelsAndDebug(MachineBasicBlock::iterator I,
                                          Register Reg, bool SkipPseudoOp) {
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();

  iterator E = end();
  while (I != E && (I->isPHI() || I->isPosition() || I->isDebugInstr() ||
                    (SkipPseudoOp && I->isPseudoProbe()) ||
                    TII->isBasicBlockPrologue(*I, Reg)))
    ++I;
  // FIXME: This needs to change if we wish to bundle labels / dbg_values
  // inside the bundle.
  assert((I == E || !I->isInsideBundle()) &&
         "First non-phi / non-label / non-debug "
         "instruction is inside a bundle!");
  return I;
}

MachineBasicBlock::iterator MachineBasicBlock::getFirstTerminator() {
  iterator B = begin(), E = end(), I = E;
  while (I != B && ((--I)->isTerminator() || I->isDebugInstr()))
    ; /*noop */
  while (I != E && !I->isTerminator())
    ++I;
  return I;
}

MachineBasicBlock::instr_iterator MachineBasicBlock::getFirstInstrTerminator() {
  instr_iterator B = instr_begin(), E = instr_end(), I = E;
  while (I != B && ((--I)->isTerminator() || I->isDebugInstr()))
    ; /*noop */
  while (I != E && !I->isTerminator())
    ++I;
  return I;
}

MachineBasicBlock::iterator MachineBasicBlock::getFirstTerminatorForward() {
  return find_if(instrs(), [](auto &II) { return II.isTerminator(); });
}

MachineBasicBlock::iterator
MachineBasicBlock::getFirstNonDebugInstr(bool SkipPseudoOp) {
  // Skip over begin-of-block dbg_value instructions.
  return skipDebugInstructionsForward(begin(), end(), SkipPseudoOp);
}

MachineBasicBlock::iterator
MachineBasicBlock::getLastNonDebugInstr(bool SkipPseudoOp) {
  // Skip over end-of-block dbg_value instructions.
  instr_iterator B = instr_begin(), I = instr_end();
  while (I != B) {
    --I;
    // Return instruction that starts a bundle.
    if (I->isDebugInstr() || I->isInsideBundle())
      continue;
    if (SkipPseudoOp && I->isPseudoProbe())
      continue;
    return I;
  }
  // The block is all debug values.
  return end();
}

bool MachineBasicBlock::hasEHPadSuccessor() const {
  for (const MachineBasicBlock *Succ : successors())
    if (Succ->isEHPad())
      return true;
  return false;
}

bool MachineBasicBlock::isEntryBlock() const {
  return getParent()->begin() == getIterator();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineBasicBlock::dump() const {
  print(dbgs());
}
#endif

bool MachineBasicBlock::mayHaveInlineAsmBr() const {
  for (const MachineBasicBlock *Succ : successors()) {
    if (Succ->isInlineAsmBrIndirectTarget())
      return true;
  }
  return false;
}

bool MachineBasicBlock::isLegalToHoistInto() const {
  if (isReturnBlock() || hasEHPadSuccessor() || mayHaveInlineAsmBr())
    return false;
  return true;
}

bool MachineBasicBlock::hasName() const {
  if (const BasicBlock *LBB = getBasicBlock())
    return LBB->hasName();
  return false;
}

StringRef MachineBasicBlock::getName() const {
  if (const BasicBlock *LBB = getBasicBlock())
    return LBB->getName();
  else
    return StringRef("", 0);
}

/// Return a hopefully unique identifier for this block.
std::string MachineBasicBlock::getFullName() const {
  std::string Name;
  if (getParent())
    Name = (getParent()->getName() + ":").str();
  if (getBasicBlock())
    Name += getBasicBlock()->getName();
  else
    Name += ("BB" + Twine(getNumber())).str();
  return Name;
}

void MachineBasicBlock::print(raw_ostream &OS, const SlotIndexes *Indexes,
                              bool IsStandalone) const {
  const MachineFunction *MF = getParent();
  if (!MF) {
    OS << "Can't print out MachineBasicBlock because parent MachineFunction"
       << " is null\n";
    return;
  }
  const Function &F = MF->getFunction();
  const Module *M = F.getParent();
  ModuleSlotTracker MST(M);
  MST.incorporateFunction(F);
  print(OS, MST, Indexes, IsStandalone);
}

void MachineBasicBlock::print(raw_ostream &OS, ModuleSlotTracker &MST,
                              const SlotIndexes *Indexes,
                              bool IsStandalone) const {
  const MachineFunction *MF = getParent();
  if (!MF) {
    OS << "Can't print out MachineBasicBlock because parent MachineFunction"
       << " is null\n";
    return;
  }

  if (Indexes && PrintSlotIndexes)
    OS << Indexes->getMBBStartIdx(this) << '\t';

  printName(OS, PrintNameIr | PrintNameAttributes, &MST);
  OS << ":\n";

  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo &TII = *getParent()->getSubtarget().getInstrInfo();
  bool HasLineAttributes = false;

  // Print the preds of this block according to the CFG.
  if (!pred_empty() && IsStandalone) {
    if (Indexes) OS << '\t';
    // Don't indent(2), align with previous line attributes.
    OS << "; predecessors: ";
    ListSeparator LS;
    for (auto *Pred : predecessors())
      OS << LS << printMBBReference(*Pred);
    OS << '\n';
    HasLineAttributes = true;
  }

  if (!succ_empty()) {
    if (Indexes) OS << '\t';
    // Print the successors
    OS.indent(2) << "successors: ";
    ListSeparator LS;
    for (auto I = succ_begin(), E = succ_end(); I != E; ++I) {
      OS << LS << printMBBReference(**I);
      if (!Probs.empty())
        OS << '('
           << format("0x%08" PRIx32, getSuccProbability(I).getNumerator())
           << ')';
    }
    if (!Probs.empty() && IsStandalone) {
      // Print human readable probabilities as comments.
      OS << "; ";
      ListSeparator LS;
      for (auto I = succ_begin(), E = succ_end(); I != E; ++I) {
        const BranchProbability &BP = getSuccProbability(I);
        OS << LS << printMBBReference(**I) << '('
           << format("%.2f%%",
                     rint(((double)BP.getNumerator() / BP.getDenominator()) *
                          100.0 * 100.0) /
                         100.0)
           << ')';
      }
    }

    OS << '\n';
    HasLineAttributes = true;
  }

  if (!livein_empty() && MRI.tracksLiveness()) {
    if (Indexes) OS << '\t';
    OS.indent(2) << "liveins: ";

    ListSeparator LS;
    for (const auto &LI : liveins()) {
      OS << LS << printReg(LI.PhysReg, TRI);
      if (!LI.LaneMask.all())
        OS << ":0x" << PrintLaneMask(LI.LaneMask);
    }
    HasLineAttributes = true;
  }

  if (HasLineAttributes)
    OS << '\n';

  bool IsInBundle = false;
  for (const MachineInstr &MI : instrs()) {
    if (Indexes && PrintSlotIndexes) {
      if (Indexes->hasIndex(MI))
        OS << Indexes->getInstructionIndex(MI);
      OS << '\t';
    }

    if (IsInBundle && !MI.isInsideBundle()) {
      OS.indent(2) << "}\n";
      IsInBundle = false;
    }

    OS.indent(IsInBundle ? 4 : 2);
    MI.print(OS, MST, IsStandalone, /*SkipOpers=*/false, /*SkipDebugLoc=*/false,
             /*AddNewLine=*/false, &TII);

    if (!IsInBundle && MI.getFlag(MachineInstr::BundledSucc)) {
      OS << " {";
      IsInBundle = true;
    }
    OS << '\n';
  }

  if (IsInBundle)
    OS.indent(2) << "}\n";

  if (IrrLoopHeaderWeight && IsStandalone) {
    if (Indexes) OS << '\t';
    OS.indent(2) << "; Irreducible loop header weight: " << *IrrLoopHeaderWeight
                 << '\n';
  }
}

/// Print the basic block's name as:
///
///    bb.{number}[.{ir-name}] [(attributes...)]
///
/// The {ir-name} is only printed when the \ref PrintNameIr flag is passed
/// (which is the default). If the IR block has no name, it is identified
/// numerically using the attribute syntax as "(%ir-block.{ir-slot})".
///
/// When the \ref PrintNameAttributes flag is passed, additional attributes
/// of the block are printed when set.
///
/// \param printNameFlags Combination of \ref PrintNameFlag flags indicating
///                       the parts to print.
/// \param moduleSlotTracker Optional ModuleSlotTracker. This method will
///                          incorporate its own tracker when necessary to
///                          determine the block's IR name.
void MachineBasicBlock::printName(raw_ostream &os, unsigned printNameFlags,
                                  ModuleSlotTracker *moduleSlotTracker) const {
  os << "bb." << getNumber();
  bool hasAttributes = false;

  auto PrintBBRef = [&](const BasicBlock *bb) {
    os << "%ir-block.";
    if (bb->hasName()) {
      os << bb->getName();
    } else {
      int slot = -1;

      if (moduleSlotTracker) {
        slot = moduleSlotTracker->getLocalSlot(bb);
      } else if (bb->getParent()) {
        ModuleSlotTracker tmpTracker(bb->getModule(), false);
        tmpTracker.incorporateFunction(*bb->getParent());
        slot = tmpTracker.getLocalSlot(bb);
      }

      if (slot == -1)
        os << "<ir-block badref>";
      else
        os << slot;
    }
  };

  if (printNameFlags & PrintNameIr) {
    if (const auto *bb = getBasicBlock()) {
      if (bb->hasName()) {
        os << '.' << bb->getName();
      } else {
        hasAttributes = true;
        os << " (";
        PrintBBRef(bb);
      }
    }
  }

  if (printNameFlags & PrintNameAttributes) {
    if (isMachineBlockAddressTaken()) {
      os << (hasAttributes ? ", " : " (");
      os << "machine-block-address-taken";
      hasAttributes = true;
    }
    if (isIRBlockAddressTaken()) {
      os << (hasAttributes ? ", " : " (");
      os << "ir-block-address-taken ";
      PrintBBRef(getAddressTakenIRBlock());
      hasAttributes = true;
    }
    if (isEHPad()) {
      os << (hasAttributes ? ", " : " (");
      os << "landing-pad";
      hasAttributes = true;
    }
    if (isInlineAsmBrIndirectTarget()) {
      os << (hasAttributes ? ", " : " (");
      os << "inlineasm-br-indirect-target";
      hasAttributes = true;
    }
    if (isEHFuncletEntry()) {
      os << (hasAttributes ? ", " : " (");
      os << "ehfunclet-entry";
      hasAttributes = true;
    }
    if (getAlignment() != Align(1)) {
      os << (hasAttributes ? ", " : " (");
      os << "align " << getAlignment().value();
      hasAttributes = true;
    }
    if (getSectionID() != MBBSectionID(0)) {
      os << (hasAttributes ? ", " : " (");
      os << "bbsections ";
      switch (getSectionID().Type) {
      case MBBSectionID::SectionType::Exception:
        os << "Exception";
        break;
      case MBBSectionID::SectionType::Cold:
        os << "Cold";
        break;
      default:
        os << getSectionID().Number;
      }
      hasAttributes = true;
    }
    if (getBBID().has_value()) {
      os << (hasAttributes ? ", " : " (");
      os << "bb_id " << getBBID()->BaseID;
      if (getBBID()->CloneID != 0)
        os << " " << getBBID()->CloneID;
      hasAttributes = true;
    }
    if (CallFrameSize != 0) {
      os << (hasAttributes ? ", " : " (");
      os << "call-frame-size " << CallFrameSize;
      hasAttributes = true;
    }
  }

  if (hasAttributes)
    os << ')';
}

void MachineBasicBlock::printAsOperand(raw_ostream &OS,
                                       bool /*PrintType*/) const {
  OS << '%';
  printName(OS, 0);
}

void MachineBasicBlock::removeLiveIn(MCPhysReg Reg, LaneBitmask LaneMask) {
  LiveInVector::iterator I = find_if(
      LiveIns, [Reg](const RegisterMaskPair &LI) { return LI.PhysReg == Reg; });
  if (I == LiveIns.end())
    return;

  I->LaneMask &= ~LaneMask;
  if (I->LaneMask.none())
    LiveIns.erase(I);
}

MachineBasicBlock::livein_iterator
MachineBasicBlock::removeLiveIn(MachineBasicBlock::livein_iterator I) {
  // Get non-const version of iterator.
  LiveInVector::iterator LI = LiveIns.begin() + (I - LiveIns.begin());
  return LiveIns.erase(LI);
}

bool MachineBasicBlock::isLiveIn(MCPhysReg Reg, LaneBitmask LaneMask) const {
  livein_iterator I = find_if(
      LiveIns, [Reg](const RegisterMaskPair &LI) { return LI.PhysReg == Reg; });
  return I != livein_end() && (I->LaneMask & LaneMask).any();
}

void MachineBasicBlock::sortUniqueLiveIns() {
  llvm::sort(LiveIns,
             [](const RegisterMaskPair &LI0, const RegisterMaskPair &LI1) {
               return LI0.PhysReg < LI1.PhysReg;
             });
  // Liveins are sorted by physreg now we can merge their lanemasks.
  LiveInVector::const_iterator I = LiveIns.begin();
  LiveInVector::const_iterator J;
  LiveInVector::iterator Out = LiveIns.begin();
  for (; I != LiveIns.end(); ++Out, I = J) {
    MCRegister PhysReg = I->PhysReg;
    LaneBitmask LaneMask = I->LaneMask;
    for (J = std::next(I); J != LiveIns.end() && J->PhysReg == PhysReg; ++J)
      LaneMask |= J->LaneMask;
    Out->PhysReg = PhysReg;
    Out->LaneMask = LaneMask;
  }
  LiveIns.erase(Out, LiveIns.end());
}

Register
MachineBasicBlock::addLiveIn(MCRegister PhysReg, const TargetRegisterClass *RC) {
  assert(getParent() && "MBB must be inserted in function");
  assert(Register::isPhysicalRegister(PhysReg) && "Expected physreg");
  assert(RC && "Register class is required");
  assert((isEHPad() || this == &getParent()->front()) &&
         "Only the entry block and landing pads can have physreg live ins");

  bool LiveIn = isLiveIn(PhysReg);
  iterator I = SkipPHIsAndLabels(begin()), E = end();
  MachineRegisterInfo &MRI = getParent()->getRegInfo();
  const TargetInstrInfo &TII = *getParent()->getSubtarget().getInstrInfo();

  // Look for an existing copy.
  if (LiveIn)
    for (;I != E && I->isCopy(); ++I)
      if (I->getOperand(1).getReg() == PhysReg) {
        Register VirtReg = I->getOperand(0).getReg();
        if (!MRI.constrainRegClass(VirtReg, RC))
          llvm_unreachable("Incompatible live-in register class.");
        return VirtReg;
      }

  // No luck, create a virtual register.
  Register VirtReg = MRI.createVirtualRegister(RC);
  BuildMI(*this, I, DebugLoc(), TII.get(TargetOpcode::COPY), VirtReg)
    .addReg(PhysReg, RegState::Kill);
  if (!LiveIn)
    addLiveIn(PhysReg);
  return VirtReg;
}

void MachineBasicBlock::moveBefore(MachineBasicBlock *NewAfter) {
  getParent()->splice(NewAfter->getIterator(), getIterator());
}

void MachineBasicBlock::moveAfter(MachineBasicBlock *NewBefore) {
  getParent()->splice(++NewBefore->getIterator(), getIterator());
}

static int findJumpTableIndex(const MachineBasicBlock &MBB) {
  MachineBasicBlock::const_iterator TerminatorI = MBB.getFirstTerminator();
  if (TerminatorI == MBB.end())
    return -1;
  const MachineInstr &Terminator = *TerminatorI;
  const TargetInstrInfo *TII = MBB.getParent()->getSubtarget().getInstrInfo();
  return TII->getJumpTableIndex(Terminator);
}

void MachineBasicBlock::updateTerminator(
    MachineBasicBlock *PreviousLayoutSuccessor) {
  LLVM_DEBUG(dbgs() << "Updating terminators on " << printMBBReference(*this)
                    << "\n");

  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();
  // A block with no successors has no concerns with fall-through edges.
  if (this->succ_empty())
    return;

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  DebugLoc DL = findBranchDebugLoc();
  bool B = TII->analyzeBranch(*this, TBB, FBB, Cond);
  (void) B;
  assert(!B && "UpdateTerminators requires analyzable predecessors!");
  if (Cond.empty()) {
    if (TBB) {
      // The block has an unconditional branch. If its successor is now its
      // layout successor, delete the branch.
      if (isLayoutSuccessor(TBB))
        TII->removeBranch(*this);
    } else {
      // The block has an unconditional fallthrough, or the end of the block is
      // unreachable.

      // Unfortunately, whether the end of the block is unreachable is not
      // immediately obvious; we must fall back to checking the successor list,
      // and assuming that if the passed in block is in the succesor list and
      // not an EHPad, it must be the intended target.
      if (!PreviousLayoutSuccessor || !isSuccessor(PreviousLayoutSuccessor) ||
          PreviousLayoutSuccessor->isEHPad())
        return;

      // If the unconditional successor block is not the current layout
      // successor, insert a branch to jump to it.
      if (!isLayoutSuccessor(PreviousLayoutSuccessor))
        TII->insertBranch(*this, PreviousLayoutSuccessor, nullptr, Cond, DL);
    }
    return;
  }

  if (FBB) {
    // The block has a non-fallthrough conditional branch. If one of its
    // successors is its layout successor, rewrite it to a fallthrough
    // conditional branch.
    if (isLayoutSuccessor(TBB)) {
      if (TII->reverseBranchCondition(Cond))
        return;
      TII->removeBranch(*this);
      TII->insertBranch(*this, FBB, nullptr, Cond, DL);
    } else if (isLayoutSuccessor(FBB)) {
      TII->removeBranch(*this);
      TII->insertBranch(*this, TBB, nullptr, Cond, DL);
    }
    return;
  }

  // We now know we're going to fallthrough to PreviousLayoutSuccessor.
  assert(PreviousLayoutSuccessor);
  assert(!PreviousLayoutSuccessor->isEHPad());
  assert(isSuccessor(PreviousLayoutSuccessor));

  if (PreviousLayoutSuccessor == TBB) {
    // We had a fallthrough to the same basic block as the conditional jump
    // targets.  Remove the conditional jump, leaving an unconditional
    // fallthrough or an unconditional jump.
    TII->removeBranch(*this);
    if (!isLayoutSuccessor(TBB)) {
      Cond.clear();
      TII->insertBranch(*this, TBB, nullptr, Cond, DL);
    }
    return;
  }

  // The block has a fallthrough conditional branch.
  if (isLayoutSuccessor(TBB)) {
    if (TII->reverseBranchCondition(Cond)) {
      // We can't reverse the condition, add an unconditional branch.
      Cond.clear();
      TII->insertBranch(*this, PreviousLayoutSuccessor, nullptr, Cond, DL);
      return;
    }
    TII->removeBranch(*this);
    TII->insertBranch(*this, PreviousLayoutSuccessor, nullptr, Cond, DL);
  } else if (!isLayoutSuccessor(PreviousLayoutSuccessor)) {
    TII->removeBranch(*this);
    TII->insertBranch(*this, TBB, PreviousLayoutSuccessor, Cond, DL);
  }
}

void MachineBasicBlock::validateSuccProbs() const {
#ifndef NDEBUG
  int64_t Sum = 0;
  for (auto Prob : Probs)
    Sum += Prob.getNumerator();
  // Due to precision issue, we assume that the sum of probabilities is one if
  // the difference between the sum of their numerators and the denominator is
  // no greater than the number of successors.
  assert((uint64_t)std::abs(Sum - BranchProbability::getDenominator()) <=
             Probs.size() &&
         "The sum of successors's probabilities exceeds one.");
#endif // NDEBUG
}

void MachineBasicBlock::addSuccessor(MachineBasicBlock *Succ,
                                     BranchProbability Prob) {
  // Probability list is either empty (if successor list isn't empty, this means
  // disabled optimization) or has the same size as successor list.
  if (!(Probs.empty() && !Successors.empty()))
    Probs.push_back(Prob);
  Successors.push_back(Succ);
  Succ->addPredecessor(this);
}

void MachineBasicBlock::addSuccessorWithoutProb(MachineBasicBlock *Succ) {
  // We need to make sure probability list is either empty or has the same size
  // of successor list. When this function is called, we can safely delete all
  // probability in the list.
  Probs.clear();
  Successors.push_back(Succ);
  Succ->addPredecessor(this);
}

void MachineBasicBlock::splitSuccessor(MachineBasicBlock *Old,
                                       MachineBasicBlock *New,
                                       bool NormalizeSuccProbs) {
  succ_iterator OldI = llvm::find(successors(), Old);
  assert(OldI != succ_end() && "Old is not a successor of this block!");
  assert(!llvm::is_contained(successors(), New) &&
         "New is already a successor of this block!");

  // Add a new successor with equal probability as the original one. Note
  // that we directly copy the probability using the iterator rather than
  // getting a potentially synthetic probability computed when unknown. This
  // preserves the probabilities as-is and then we can renormalize them and
  // query them effectively afterward.
  addSuccessor(New, Probs.empty() ? BranchProbability::getUnknown()
                                  : *getProbabilityIterator(OldI));
  if (NormalizeSuccProbs)
    normalizeSuccProbs();
}

void MachineBasicBlock::removeSuccessor(MachineBasicBlock *Succ,
                                        bool NormalizeSuccProbs) {
  succ_iterator I = find(Successors, Succ);
  removeSuccessor(I, NormalizeSuccProbs);
}

MachineBasicBlock::succ_iterator
MachineBasicBlock::removeSuccessor(succ_iterator I, bool NormalizeSuccProbs) {
  assert(I != Successors.end() && "Not a current successor!");

  // If probability list is empty it means we don't use it (disabled
  // optimization).
  if (!Probs.empty()) {
    probability_iterator WI = getProbabilityIterator(I);
    Probs.erase(WI);
    if (NormalizeSuccProbs)
      normalizeSuccProbs();
  }

  (*I)->removePredecessor(this);
  return Successors.erase(I);
}

void MachineBasicBlock::replaceSuccessor(MachineBasicBlock *Old,
                                         MachineBasicBlock *New) {
  if (Old == New)
    return;

  succ_iterator E = succ_end();
  succ_iterator NewI = E;
  succ_iterator OldI = E;
  for (succ_iterator I = succ_begin(); I != E; ++I) {
    if (*I == Old) {
      OldI = I;
      if (NewI != E)
        break;
    }
    if (*I == New) {
      NewI = I;
      if (OldI != E)
        break;
    }
  }
  assert(OldI != E && "Old is not a successor of this block");

  // If New isn't already a successor, let it take Old's place.
  if (NewI == E) {
    Old->removePredecessor(this);
    New->addPredecessor(this);
    *OldI = New;
    return;
  }

  // New is already a successor.
  // Update its probability instead of adding a duplicate edge.
  if (!Probs.empty()) {
    auto ProbIter = getProbabilityIterator(NewI);
    if (!ProbIter->isUnknown())
      *ProbIter += *getProbabilityIterator(OldI);
  }
  removeSuccessor(OldI);
}

void MachineBasicBlock::copySuccessor(const MachineBasicBlock *Orig,
                                      succ_iterator I) {
  if (!Orig->Probs.empty())
    addSuccessor(*I, Orig->getSuccProbability(I));
  else
    addSuccessorWithoutProb(*I);
}

void MachineBasicBlock::addPredecessor(MachineBasicBlock *Pred) {
  Predecessors.push_back(Pred);
}

void MachineBasicBlock::removePredecessor(MachineBasicBlock *Pred) {
  pred_iterator I = find(Predecessors, Pred);
  assert(I != Predecessors.end() && "Pred is not a predecessor of this block!");
  Predecessors.erase(I);
}

void MachineBasicBlock::transferSuccessors(MachineBasicBlock *FromMBB) {
  if (this == FromMBB)
    return;

  while (!FromMBB->succ_empty()) {
    MachineBasicBlock *Succ = *FromMBB->succ_begin();

    // If probability list is empty it means we don't use it (disabled
    // optimization).
    if (!FromMBB->Probs.empty()) {
      auto Prob = *FromMBB->Probs.begin();
      addSuccessor(Succ, Prob);
    } else
      addSuccessorWithoutProb(Succ);

    FromMBB->removeSuccessor(Succ);
  }
}

void
MachineBasicBlock::transferSuccessorsAndUpdatePHIs(MachineBasicBlock *FromMBB) {
  if (this == FromMBB)
    return;

  while (!FromMBB->succ_empty()) {
    MachineBasicBlock *Succ = *FromMBB->succ_begin();
    if (!FromMBB->Probs.empty()) {
      auto Prob = *FromMBB->Probs.begin();
      addSuccessor(Succ, Prob);
    } else
      addSuccessorWithoutProb(Succ);
    FromMBB->removeSuccessor(Succ);

    // Fix up any PHI nodes in the successor.
    Succ->replacePhiUsesWith(FromMBB, this);
  }
  normalizeSuccProbs();
}

bool MachineBasicBlock::isPredecessor(const MachineBasicBlock *MBB) const {
  return is_contained(predecessors(), MBB);
}

bool MachineBasicBlock::isSuccessor(const MachineBasicBlock *MBB) const {
  return is_contained(successors(), MBB);
}

bool MachineBasicBlock::isLayoutSuccessor(const MachineBasicBlock *MBB) const {
  MachineFunction::const_iterator I(this);
  return std::next(I) == MachineFunction::const_iterator(MBB);
}

const MachineBasicBlock *MachineBasicBlock::getSingleSuccessor() const {
  return Successors.size() == 1 ? Successors[0] : nullptr;
}

const MachineBasicBlock *MachineBasicBlock::getSinglePredecessor() const {
  return Predecessors.size() == 1 ? Predecessors[0] : nullptr;
}

MachineBasicBlock *MachineBasicBlock::getFallThrough(bool JumpToFallThrough) {
  MachineFunction::iterator Fallthrough = getIterator();
  ++Fallthrough;
  // If FallthroughBlock is off the end of the function, it can't fall through.
  if (Fallthrough == getParent()->end())
    return nullptr;

  // If FallthroughBlock isn't a successor, no fallthrough is possible.
  if (!isSuccessor(&*Fallthrough))
    return nullptr;

  // Analyze the branches, if any, at the end of the block.
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();
  if (TII->analyzeBranch(*this, TBB, FBB, Cond)) {
    // If we couldn't analyze the branch, examine the last instruction.
    // If the block doesn't end in a known control barrier, assume fallthrough
    // is possible. The isPredicated check is needed because this code can be
    // called during IfConversion, where an instruction which is normally a
    // Barrier is predicated and thus no longer an actual control barrier.
    return (empty() || !back().isBarrier() || TII->isPredicated(back()))
               ? &*Fallthrough
               : nullptr;
  }

  // If there is no branch, control always falls through.
  if (!TBB) return &*Fallthrough;

  // If there is some explicit branch to the fallthrough block, it can obviously
  // reach, even though the branch should get folded to fall through implicitly.
  if (JumpToFallThrough && (MachineFunction::iterator(TBB) == Fallthrough ||
                            MachineFunction::iterator(FBB) == Fallthrough))
    return &*Fallthrough;

  // If it's an unconditional branch to some block not the fall through, it
  // doesn't fall through.
  if (Cond.empty()) return nullptr;

  // Otherwise, if it is conditional and has no explicit false block, it falls
  // through.
  return (FBB == nullptr) ? &*Fallthrough : nullptr;
}

bool MachineBasicBlock::canFallThrough() {
  return getFallThrough() != nullptr;
}

MachineBasicBlock *MachineBasicBlock::splitAt(MachineInstr &MI,
                                              bool UpdateLiveIns,
                                              LiveIntervals *LIS) {
  MachineBasicBlock::iterator SplitPoint(&MI);
  ++SplitPoint;

  if (SplitPoint == end()) {
    // Don't bother with a new block.
    return this;
  }

  MachineFunction *MF = getParent();

  LivePhysRegs LiveRegs;
  if (UpdateLiveIns) {
    // Make sure we add any physregs we define in the block as liveins to the
    // new block.
    MachineBasicBlock::iterator Prev(&MI);
    LiveRegs.init(*MF->getSubtarget().getRegisterInfo());
    LiveRegs.addLiveOuts(*this);
    for (auto I = rbegin(), E = Prev.getReverse(); I != E; ++I)
      LiveRegs.stepBackward(*I);
  }

  MachineBasicBlock *SplitBB = MF->CreateMachineBasicBlock(getBasicBlock());

  MF->insert(++MachineFunction::iterator(this), SplitBB);
  SplitBB->splice(SplitBB->begin(), this, SplitPoint, end());

  SplitBB->transferSuccessorsAndUpdatePHIs(this);
  addSuccessor(SplitBB);

  if (UpdateLiveIns)
    addLiveIns(*SplitBB, LiveRegs);

  if (LIS)
    LIS->insertMBBInMaps(SplitBB);

  return SplitBB;
}

// Returns `true` if there are possibly other users of the jump table at
// `JumpTableIndex` except for the ones in `IgnoreMBB`.
static bool jumpTableHasOtherUses(const MachineFunction &MF,
                                  const MachineBasicBlock &IgnoreMBB,
                                  int JumpTableIndex) {
  assert(JumpTableIndex >= 0 && "need valid index");
  const MachineJumpTableInfo &MJTI = *MF.getJumpTableInfo();
  const MachineJumpTableEntry &MJTE = MJTI.getJumpTables()[JumpTableIndex];
  // Take any basic block from the table; every user of the jump table must
  // show up in the predecessor list.
  const MachineBasicBlock *MBB = nullptr;
  for (MachineBasicBlock *B : MJTE.MBBs) {
    if (B != nullptr) {
      MBB = B;
      break;
    }
  }
  if (MBB == nullptr)
    return true; // can't rule out other users if there isn't any block.
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  for (MachineBasicBlock *Pred : MBB->predecessors()) {
    if (Pred == &IgnoreMBB)
      continue;
    MachineBasicBlock *DummyT = nullptr;
    MachineBasicBlock *DummyF = nullptr;
    Cond.clear();
    if (!TII.analyzeBranch(*Pred, DummyT, DummyF, Cond,
                           /*AllowModify=*/false)) {
      // analyzable direct jump
      continue;
    }
    int PredJTI = findJumpTableIndex(*Pred);
    if (PredJTI >= 0) {
      if (PredJTI == JumpTableIndex)
        return true;
      continue;
    }
    // Be conservative for unanalyzable jumps.
    return true;
  }
  return false;
}

class SlotIndexUpdateDelegate : public MachineFunction::Delegate {
private:
  MachineFunction &MF;
  SlotIndexes *Indexes;
  SmallSetVector<MachineInstr *, 2> Insertions;

public:
  SlotIndexUpdateDelegate(MachineFunction &MF, SlotIndexes *Indexes)
      : MF(MF), Indexes(Indexes) {
    MF.setDelegate(this);
  }

  ~SlotIndexUpdateDelegate() {
    MF.resetDelegate(this);
    for (auto MI : Insertions)
      Indexes->insertMachineInstrInMaps(*MI);
  }

  void MF_HandleInsertion(MachineInstr &MI) override {
    // This is called before MI is inserted into block so defer index update.
    if (Indexes)
      Insertions.insert(&MI);
  }

  void MF_HandleRemoval(MachineInstr &MI) override {
    if (Indexes && !Insertions.remove(&MI))
      Indexes->removeMachineInstrFromMaps(MI);
  }
};

#define GET_RESULT(RESULT, GETTER, INFIX)                                      \
  [MF, P, MFAM]() {                                                            \
    if (P) {                                                                   \
      auto *Wrapper = P->getAnalysisIfAvailable<RESULT##INFIX##WrapperPass>(); \
      return Wrapper ? &Wrapper->GETTER() : nullptr;                           \
    }                                                                          \
    return MFAM->getCachedResult<RESULT##Analysis>(*MF);                       \
  }()

MachineBasicBlock *MachineBasicBlock::SplitCriticalEdge(
    MachineBasicBlock *Succ, Pass *P, MachineFunctionAnalysisManager *MFAM,
    std::vector<SparseBitVector<>> *LiveInSets) {
  assert((P || MFAM) && "Need a way to get analysis results!");
  if (!canSplitCriticalEdge(Succ))
    return nullptr;

  MachineFunction *MF = getParent();
  MachineBasicBlock *PrevFallthrough = getNextNode();

  MachineBasicBlock *NMBB = MF->CreateMachineBasicBlock();
  NMBB->setCallFrameSize(Succ->getCallFrameSize());

  // Is there an indirect jump with jump table?
  bool ChangedIndirectJump = false;
  int JTI = findJumpTableIndex(*this);
  if (JTI >= 0) {
    MachineJumpTableInfo &MJTI = *MF->getJumpTableInfo();
    MJTI.ReplaceMBBInJumpTable(JTI, Succ, NMBB);
    ChangedIndirectJump = true;
  }

  MF->insert(std::next(MachineFunction::iterator(this)), NMBB);
  LLVM_DEBUG(dbgs() << "Splitting critical edge: " << printMBBReference(*this)
                    << " -- " << printMBBReference(*NMBB) << " -- "
                    << printMBBReference(*Succ) << '\n');

  LiveIntervals *LIS = GET_RESULT(LiveIntervals, getLIS, );
  SlotIndexes *Indexes = GET_RESULT(SlotIndexes, getSI, );
  if (LIS)
    LIS->insertMBBInMaps(NMBB);
  else if (Indexes)
    Indexes->insertMBBInMaps(NMBB);

  // On some targets like Mips, branches may kill virtual registers. Make sure
  // that LiveVariables is properly updated after updateTerminator replaces the
  // terminators.
  LiveVariables *LV = GET_RESULT(LiveVariables, getLV, );

  // Collect a list of virtual registers killed by the terminators.
  SmallVector<Register, 4> KilledRegs;
  if (LV)
    for (MachineInstr &MI :
         llvm::make_range(getFirstInstrTerminator(), instr_end())) {
      for (MachineOperand &MO : MI.all_uses()) {
        if (MO.getReg() == 0 || !MO.isKill() || MO.isUndef())
          continue;
        Register Reg = MO.getReg();
        if (Reg.isPhysical() || LV->getVarInfo(Reg).removeKill(MI)) {
          KilledRegs.push_back(Reg);
          LLVM_DEBUG(dbgs() << "Removing terminator kill: " << MI);
          MO.setIsKill(false);
        }
      }
    }

  SmallVector<Register, 4> UsedRegs;
  if (LIS) {
    for (MachineInstr &MI :
         llvm::make_range(getFirstInstrTerminator(), instr_end())) {
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || MO.getReg() == 0)
          continue;

        Register Reg = MO.getReg();
        if (!is_contained(UsedRegs, Reg))
          UsedRegs.push_back(Reg);
      }
    }
  }

  ReplaceUsesOfBlockWith(Succ, NMBB);

  // Since we replaced all uses of Succ with NMBB, that should also be treated
  // as the fallthrough successor
  if (Succ == PrevFallthrough)
    PrevFallthrough = NMBB;

  if (!ChangedIndirectJump) {
    SlotIndexUpdateDelegate SlotUpdater(*MF, Indexes);
    updateTerminator(PrevFallthrough);
  }

  // Insert unconditional "jump Succ" instruction in NMBB if necessary.
  NMBB->addSuccessor(Succ);
  if (!NMBB->isLayoutSuccessor(Succ)) {
    SlotIndexUpdateDelegate SlotUpdater(*MF, Indexes);
    SmallVector<MachineOperand, 4> Cond;
    const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();

    // In original 'this' BB, there must be a branch instruction targeting at
    // Succ. We can not find it out since currently getBranchDestBlock was not
    // implemented for all targets. However, if the merged DL has column or line
    // number, the scope and non-zero column and line number is same with that
    // branch instruction so we can safely use it.
    DebugLoc DL, MergedDL = findBranchDebugLoc();
    if (MergedDL && (MergedDL.getLine() || MergedDL.getCol()))
      DL = MergedDL;
    TII->insertBranch(*NMBB, Succ, nullptr, Cond, DL);
  }

  // Fix PHI nodes in Succ so they refer to NMBB instead of this.
  Succ->replacePhiUsesWith(this, NMBB);

  // Inherit live-ins from the successor
  for (const auto &LI : Succ->liveins())
    NMBB->addLiveIn(LI);

  // Update LiveVariables.
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  if (LV) {
    // Restore kills of virtual registers that were killed by the terminators.
    while (!KilledRegs.empty()) {
      Register Reg = KilledRegs.pop_back_val();
      for (instr_iterator I = instr_end(), E = instr_begin(); I != E;) {
        if (!(--I)->addRegisterKilled(Reg, TRI, /* AddIfNotFound= */ false))
          continue;
        if (Reg.isVirtual())
          LV->getVarInfo(Reg).Kills.push_back(&*I);
        LLVM_DEBUG(dbgs() << "Restored terminator kill: " << *I);
        break;
      }
    }
    // Update relevant live-through information.
    if (LiveInSets != nullptr)
      LV->addNewBlock(NMBB, this, Succ, *LiveInSets);
    else
      LV->addNewBlock(NMBB, this, Succ);
  }

  if (LIS) {
    // After splitting the edge and updating SlotIndexes, live intervals may be
    // in one of two situations, depending on whether this block was the last in
    // the function. If the original block was the last in the function, all
    // live intervals will end prior to the beginning of the new split block. If
    // the original block was not at the end of the function, all live intervals
    // will extend to the end of the new split block.

    bool isLastMBB =
      std::next(MachineFunction::iterator(NMBB)) == getParent()->end();

    SlotIndex StartIndex = Indexes->getMBBEndIdx(this);
    SlotIndex PrevIndex = StartIndex.getPrevSlot();
    SlotIndex EndIndex = Indexes->getMBBEndIdx(NMBB);

    // Find the registers used from NMBB in PHIs in Succ.
    SmallSet<Register, 8> PHISrcRegs;
    for (MachineBasicBlock::instr_iterator
         I = Succ->instr_begin(), E = Succ->instr_end();
         I != E && I->isPHI(); ++I) {
      for (unsigned ni = 1, ne = I->getNumOperands(); ni != ne; ni += 2) {
        if (I->getOperand(ni+1).getMBB() == NMBB) {
          MachineOperand &MO = I->getOperand(ni);
          Register Reg = MO.getReg();
          PHISrcRegs.insert(Reg);
          if (MO.isUndef())
            continue;

          LiveInterval &LI = LIS->getInterval(Reg);
          VNInfo *VNI = LI.getVNInfoAt(PrevIndex);
          assert(VNI &&
                 "PHI sources should be live out of their predecessors.");
          LI.addSegment(LiveInterval::Segment(StartIndex, EndIndex, VNI));
          for (auto &SR : LI.subranges())
            SR.addSegment(LiveInterval::Segment(StartIndex, EndIndex, VNI));
        }
      }
    }

    MachineRegisterInfo *MRI = &getParent()->getRegInfo();
    for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
      Register Reg = Register::index2VirtReg(i);
      if (PHISrcRegs.count(Reg) || !LIS->hasInterval(Reg))
        continue;

      LiveInterval &LI = LIS->getInterval(Reg);
      if (!LI.liveAt(PrevIndex))
        continue;

      bool isLiveOut = LI.liveAt(LIS->getMBBStartIdx(Succ));
      if (isLiveOut && isLastMBB) {
        VNInfo *VNI = LI.getVNInfoAt(PrevIndex);
        assert(VNI && "LiveInterval should have VNInfo where it is live.");
        LI.addSegment(LiveInterval::Segment(StartIndex, EndIndex, VNI));
        // Update subranges with live values
        for (auto &SR : LI.subranges()) {
          VNInfo *VNI = SR.getVNInfoAt(PrevIndex);
          if (VNI)
            SR.addSegment(LiveInterval::Segment(StartIndex, EndIndex, VNI));
        }
      } else if (!isLiveOut && !isLastMBB) {
        LI.removeSegment(StartIndex, EndIndex);
        for (auto &SR : LI.subranges())
          SR.removeSegment(StartIndex, EndIndex);
      }
    }

    // Update all intervals for registers whose uses may have been modified by
    // updateTerminator().
    LIS->repairIntervalsInRange(this, getFirstTerminator(), end(), UsedRegs);
  }

  if (auto *MDT = GET_RESULT(MachineDominatorTree, getDomTree, ))
    MDT->recordSplitCriticalEdge(this, Succ, NMBB);

  if (MachineLoopInfo *MLI = GET_RESULT(MachineLoop, getLI, Info))
    if (MachineLoop *TIL = MLI->getLoopFor(this)) {
      // If one or the other blocks were not in a loop, the new block is not
      // either, and thus LI doesn't need to be updated.
      if (MachineLoop *DestLoop = MLI->getLoopFor(Succ)) {
        if (TIL == DestLoop) {
          // Both in the same loop, the NMBB joins loop.
          DestLoop->addBasicBlockToLoop(NMBB, *MLI);
        } else if (TIL->contains(DestLoop)) {
          // Edge from an outer loop to an inner loop.  Add to the outer loop.
          TIL->addBasicBlockToLoop(NMBB, *MLI);
        } else if (DestLoop->contains(TIL)) {
          // Edge from an inner loop to an outer loop.  Add to the outer loop.
          DestLoop->addBasicBlockToLoop(NMBB, *MLI);
        } else {
          // Edge from two loops with no containment relation.  Because these
          // are natural loops, we know that the destination block must be the
          // header of its loop (adding a branch into a loop elsewhere would
          // create an irreducible loop).
          assert(DestLoop->getHeader() == Succ &&
                 "Should not create irreducible loops!");
          if (MachineLoop *P = DestLoop->getParentLoop())
            P->addBasicBlockToLoop(NMBB, *MLI);
        }
      }
    }

  return NMBB;
}

bool MachineBasicBlock::canSplitCriticalEdge(
    const MachineBasicBlock *Succ) const {
  // Splitting the critical edge to a landing pad block is non-trivial. Don't do
  // it in this generic function.
  if (Succ->isEHPad())
    return false;

  // Splitting the critical edge to a callbr's indirect block isn't advised.
  // Don't do it in this generic function.
  if (Succ->isInlineAsmBrIndirectTarget())
    return false;

  const MachineFunction *MF = getParent();
  // Performance might be harmed on HW that implements branching using exec mask
  // where both sides of the branches are always executed.
  if (MF->getTarget().requiresStructuredCFG())
    return false;

  // Do we have an Indirect jump with a jumptable that we can rewrite?
  int JTI = findJumpTableIndex(*this);
  if (JTI >= 0 && !jumpTableHasOtherUses(*MF, *this, JTI))
    return true;

  // We may need to update this's terminator, but we can't do that if
  // analyzeBranch fails.
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  // AnalyzeBanch should modify this, since we did not allow modification.
  if (TII->analyzeBranch(*const_cast<MachineBasicBlock *>(this), TBB, FBB, Cond,
                         /*AllowModify*/ false))
    return false;

  // Avoid bugpoint weirdness: A block may end with a conditional branch but
  // jumps to the same MBB is either case. We have duplicate CFG edges in that
  // case that we can't handle. Since this never happens in properly optimized
  // code, just skip those edges.
  if (TBB && TBB == FBB) {
    LLVM_DEBUG(dbgs() << "Won't split critical edge after degenerate "
                      << printMBBReference(*this) << '\n');
    return false;
  }
  return true;
}

/// Prepare MI to be removed from its bundle. This fixes bundle flags on MI's
/// neighboring instructions so the bundle won't be broken by removing MI.
static void unbundleSingleMI(MachineInstr *MI) {
  // Removing the first instruction in a bundle.
  if (MI->isBundledWithSucc() && !MI->isBundledWithPred())
    MI->unbundleFromSucc();
  // Removing the last instruction in a bundle.
  if (MI->isBundledWithPred() && !MI->isBundledWithSucc())
    MI->unbundleFromPred();
  // If MI is not bundled, or if it is internal to a bundle, the neighbor flags
  // are already fine.
}

MachineBasicBlock::instr_iterator
MachineBasicBlock::erase(MachineBasicBlock::instr_iterator I) {
  unbundleSingleMI(&*I);
  return Insts.erase(I);
}

MachineInstr *MachineBasicBlock::remove_instr(MachineInstr *MI) {
  unbundleSingleMI(MI);
  MI->clearFlag(MachineInstr::BundledPred);
  MI->clearFlag(MachineInstr::BundledSucc);
  return Insts.remove(MI);
}

MachineBasicBlock::instr_iterator
MachineBasicBlock::insert(instr_iterator I, MachineInstr *MI) {
  assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
         "Cannot insert instruction with bundle flags");
  // Set the bundle flags when inserting inside a bundle.
  if (I != instr_end() && I->isBundledWithPred()) {
    MI->setFlag(MachineInstr::BundledPred);
    MI->setFlag(MachineInstr::BundledSucc);
  }
  return Insts.insert(I, MI);
}

/// This method unlinks 'this' from the containing function, and returns it, but
/// does not delete it.
MachineBasicBlock *MachineBasicBlock::removeFromParent() {
  assert(getParent() && "Not embedded in a function!");
  getParent()->remove(this);
  return this;
}

/// This method unlinks 'this' from the containing function, and deletes it.
void MachineBasicBlock::eraseFromParent() {
  assert(getParent() && "Not embedded in a function!");
  getParent()->erase(this);
}

/// Given a machine basic block that branched to 'Old', change the code and CFG
/// so that it branches to 'New' instead.
void MachineBasicBlock::ReplaceUsesOfBlockWith(MachineBasicBlock *Old,
                                               MachineBasicBlock *New) {
  assert(Old != New && "Cannot replace self with self!");

  MachineBasicBlock::instr_iterator I = instr_end();
  while (I != instr_begin()) {
    --I;
    if (!I->isTerminator()) break;

    // Scan the operands of this machine instruction, replacing any uses of Old
    // with New.
    for (MachineOperand &MO : I->operands())
      if (MO.isMBB() && MO.getMBB() == Old)
        MO.setMBB(New);
  }

  // Update the successor information.
  replaceSuccessor(Old, New);
}

void MachineBasicBlock::replacePhiUsesWith(MachineBasicBlock *Old,
                                           MachineBasicBlock *New) {
  for (MachineInstr &MI : phis())
    for (unsigned i = 2, e = MI.getNumOperands() + 1; i != e; i += 2) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.getMBB() == Old)
        MO.setMBB(New);
    }
}

/// Find the next valid DebugLoc starting at MBBI, skipping any debug
/// instructions.  Return UnknownLoc if there is none.
DebugLoc
MachineBasicBlock::findDebugLoc(instr_iterator MBBI) {
  // Skip debug declarations, we don't want a DebugLoc from them.
  MBBI = skipDebugInstructionsForward(MBBI, instr_end());
  if (MBBI != instr_end())
    return MBBI->getDebugLoc();
  return {};
}

DebugLoc MachineBasicBlock::rfindDebugLoc(reverse_instr_iterator MBBI) {
  if (MBBI == instr_rend())
    return findDebugLoc(instr_begin());
  // Skip debug declarations, we don't want a DebugLoc from them.
  MBBI = skipDebugInstructionsBackward(MBBI, instr_rbegin());
  if (!MBBI->isDebugInstr())
    return MBBI->getDebugLoc();
  return {};
}

/// Find the previous valid DebugLoc preceding MBBI, skipping any debug
/// instructions.  Return UnknownLoc if there is none.
DebugLoc MachineBasicBlock::findPrevDebugLoc(instr_iterator MBBI) {
  if (MBBI == instr_begin())
    return {};
  // Skip debug instructions, we don't want a DebugLoc from them.
  MBBI = prev_nodbg(MBBI, instr_begin());
  if (!MBBI->isDebugInstr())
    return MBBI->getDebugLoc();
  return {};
}

DebugLoc MachineBasicBlock::rfindPrevDebugLoc(reverse_instr_iterator MBBI) {
  if (MBBI == instr_rend())
    return {};
  // Skip debug declarations, we don't want a DebugLoc from them.
  MBBI = next_nodbg(MBBI, instr_rend());
  if (MBBI != instr_rend())
    return MBBI->getDebugLoc();
  return {};
}

/// Find and return the merged DebugLoc of the branch instructions of the block.
/// Return UnknownLoc if there is none.
DebugLoc
MachineBasicBlock::findBranchDebugLoc() {
  DebugLoc DL;
  auto TI = getFirstTerminator();
  while (TI != end() && !TI->isBranch())
    ++TI;

  if (TI != end()) {
    DL = TI->getDebugLoc();
    for (++TI ; TI != end() ; ++TI)
      if (TI->isBranch())
        DL = DILocation::getMergedLocation(DL, TI->getDebugLoc());
  }
  return DL;
}

/// Return probability of the edge from this block to MBB.
BranchProbability
MachineBasicBlock::getSuccProbability(const_succ_iterator Succ) const {
  if (Probs.empty())
    return BranchProbability(1, succ_size());

  const auto &Prob = *getProbabilityIterator(Succ);
  if (Prob.isUnknown()) {
    // For unknown probabilities, collect the sum of all known ones, and evenly
    // ditribute the complemental of the sum to each unknown probability.
    unsigned KnownProbNum = 0;
    auto Sum = BranchProbability::getZero();
    for (const auto &P : Probs) {
      if (!P.isUnknown()) {
        Sum += P;
        KnownProbNum++;
      }
    }
    return Sum.getCompl() / (Probs.size() - KnownProbNum);
  } else
    return Prob;
}

/// Set successor probability of a given iterator.
void MachineBasicBlock::setSuccProbability(succ_iterator I,
                                           BranchProbability Prob) {
  assert(!Prob.isUnknown());
  if (Probs.empty())
    return;
  *getProbabilityIterator(I) = Prob;
}

/// Return probability iterator corresonding to the I successor iterator
MachineBasicBlock::const_probability_iterator
MachineBasicBlock::getProbabilityIterator(
    MachineBasicBlock::const_succ_iterator I) const {
  assert(Probs.size() == Successors.size() && "Async probability list!");
  const size_t index = std::distance(Successors.begin(), I);
  assert(index < Probs.size() && "Not a current successor!");
  return Probs.begin() + index;
}

/// Return probability iterator corresonding to the I successor iterator.
MachineBasicBlock::probability_iterator
MachineBasicBlock::getProbabilityIterator(MachineBasicBlock::succ_iterator I) {
  assert(Probs.size() == Successors.size() && "Async probability list!");
  const size_t index = std::distance(Successors.begin(), I);
  assert(index < Probs.size() && "Not a current successor!");
  return Probs.begin() + index;
}

/// Return whether (physical) register "Reg" has been <def>ined and not <kill>ed
/// as of just before "MI".
///
/// Search is localised to a neighborhood of
/// Neighborhood instructions before (searching for defs or kills) and N
/// instructions after (searching just for defs) MI.
MachineBasicBlock::LivenessQueryResult
MachineBasicBlock::computeRegisterLiveness(const TargetRegisterInfo *TRI,
                                           MCRegister Reg, const_iterator Before,
                                           unsigned Neighborhood) const {
  unsigned N = Neighborhood;

  // Try searching forwards from Before, looking for reads or defs.
  const_iterator I(Before);
  for (; I != end() && N > 0; ++I) {
    if (I->isDebugOrPseudoInstr())
      continue;

    --N;

    PhysRegInfo Info = AnalyzePhysRegInBundle(*I, Reg, TRI);

    // Register is live when we read it here.
    if (Info.Read)
      return LQR_Live;
    // Register is dead if we can fully overwrite or clobber it here.
    if (Info.FullyDefined || Info.Clobbered)
      return LQR_Dead;
  }

  // If we reached the end, it is safe to clobber Reg at the end of a block of
  // no successor has it live in.
  if (I == end()) {
    for (MachineBasicBlock *S : successors()) {
      for (const MachineBasicBlock::RegisterMaskPair &LI : S->liveins()) {
        if (TRI->regsOverlap(LI.PhysReg, Reg))
          return LQR_Live;
      }
    }

    return LQR_Dead;
  }


  N = Neighborhood;

  // Start by searching backwards from Before, looking for kills, reads or defs.
  I = const_iterator(Before);
  // If this is the first insn in the block, don't search backwards.
  if (I != begin()) {
    do {
      --I;

      if (I->isDebugOrPseudoInstr())
        continue;

      --N;

      PhysRegInfo Info = AnalyzePhysRegInBundle(*I, Reg, TRI);

      // Defs happen after uses so they take precedence if both are present.

      // Register is dead after a dead def of the full register.
      if (Info.DeadDef)
        return LQR_Dead;
      // Register is (at least partially) live after a def.
      if (Info.Defined) {
        if (!Info.PartialDeadDef)
          return LQR_Live;
        // As soon as we saw a partial definition (dead or not),
        // we cannot tell if the value is partial live without
        // tracking the lanemasks. We are not going to do this,
        // so fall back on the remaining of the analysis.
        break;
      }
      // Register is dead after a full kill or clobber and no def.
      if (Info.Killed || Info.Clobbered)
        return LQR_Dead;
      // Register must be live if we read it.
      if (Info.Read)
        return LQR_Live;

    } while (I != begin() && N > 0);
  }

  // If all the instructions before this in the block are debug instructions,
  // skip over them.
  while (I != begin() && std::prev(I)->isDebugOrPseudoInstr())
    --I;

  // Did we get to the start of the block?
  if (I == begin()) {
    // If so, the register's state is definitely defined by the live-in state.
    for (const MachineBasicBlock::RegisterMaskPair &LI : liveins())
      if (TRI->regsOverlap(LI.PhysReg, Reg))
        return LQR_Live;

    return LQR_Dead;
  }

  // At this point we have no idea of the liveness of the register.
  return LQR_Unknown;
}

const uint32_t *
MachineBasicBlock::getBeginClobberMask(const TargetRegisterInfo *TRI) const {
  // EH funclet entry does not preserve any registers.
  return isEHFuncletEntry() ? TRI->getNoPreservedMask() : nullptr;
}

const uint32_t *
MachineBasicBlock::getEndClobberMask(const TargetRegisterInfo *TRI) const {
  // If we see a return block with successors, this must be a funclet return,
  // which does not preserve any registers. If there are no successors, we don't
  // care what kind of return it is, putting a mask after it is a no-op.
  return isReturnBlock() && !succ_empty() ? TRI->getNoPreservedMask() : nullptr;
}

void MachineBasicBlock::clearLiveIns() {
  LiveIns.clear();
}

void MachineBasicBlock::clearLiveIns(
    std::vector<RegisterMaskPair> &OldLiveIns) {
  assert(OldLiveIns.empty() && "Vector must be empty");
  std::swap(LiveIns, OldLiveIns);
}

MachineBasicBlock::livein_iterator MachineBasicBlock::livein_begin() const {
  assert(getParent()->getProperties().hasProperty(
      MachineFunctionProperties::Property::TracksLiveness) &&
      "Liveness information is accurate");
  return LiveIns.begin();
}

MachineBasicBlock::liveout_iterator MachineBasicBlock::liveout_begin() const {
  const MachineFunction &MF = *getParent();
  assert(MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::TracksLiveness) &&
      "Liveness information is accurate");

  const TargetLowering &TLI = *MF.getSubtarget().getTargetLowering();
  MCPhysReg ExceptionPointer = 0, ExceptionSelector = 0;
  if (MF.getFunction().hasPersonalityFn()) {
    auto PersonalityFn = MF.getFunction().getPersonalityFn();
    ExceptionPointer = TLI.getExceptionPointerRegister(PersonalityFn);
    ExceptionSelector = TLI.getExceptionSelectorRegister(PersonalityFn);
  }

  return liveout_iterator(*this, ExceptionPointer, ExceptionSelector, false);
}

bool MachineBasicBlock::sizeWithoutDebugLargerThan(unsigned Limit) const {
  unsigned Cntr = 0;
  auto R = instructionsWithoutDebug(begin(), end());
  for (auto I = R.begin(), E = R.end(); I != E; ++I) {
    if (++Cntr > Limit)
      return true;
  }
  return false;
}

const MBBSectionID MBBSectionID::ColdSectionID(MBBSectionID::SectionType::Cold);
const MBBSectionID
    MBBSectionID::ExceptionSectionID(MBBSectionID::SectionType::Exception);
