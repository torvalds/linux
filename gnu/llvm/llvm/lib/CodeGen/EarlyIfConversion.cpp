//===-- EarlyIfConversion.cpp - If-conversion on SSA form machine code ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Early if-conversion is for out-of-order CPUs that don't have a lot of
// predicable instructions. The goal is to eliminate conditional branches that
// may mispredict.
//
// Instructions from both sides of the branch are executed specutatively, and a
// cmov instruction selects the result.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "early-ifcvt"

// Absolute maximum number of instructions allowed per speculated block.
// This bypasses all other heuristics, so it should be set fairly high.
static cl::opt<unsigned>
BlockInstrLimit("early-ifcvt-limit", cl::init(30), cl::Hidden,
  cl::desc("Maximum number of instructions per speculated block."));

// Stress testing mode - disable heuristics.
static cl::opt<bool> Stress("stress-early-ifcvt", cl::Hidden,
  cl::desc("Turn all knobs to 11"));

STATISTIC(NumDiamondsSeen,  "Number of diamonds");
STATISTIC(NumDiamondsConv,  "Number of diamonds converted");
STATISTIC(NumTrianglesSeen, "Number of triangles");
STATISTIC(NumTrianglesConv, "Number of triangles converted");

//===----------------------------------------------------------------------===//
//                                 SSAIfConv
//===----------------------------------------------------------------------===//
//
// The SSAIfConv class performs if-conversion on SSA form machine code after
// determining if it is possible. The class contains no heuristics; external
// code should be used to determine when if-conversion is a good idea.
//
// SSAIfConv can convert both triangles and diamonds:
//
//   Triangle: Head              Diamond: Head
//              | \                       /  \_
//              |  \                     /    |
//              |  [TF]BB              FBB    TBB
//              |  /                     \    /
//              | /                       \  /
//             Tail                       Tail
//
// Instructions in the conditional blocks TBB and/or FBB are spliced into the
// Head block, and phis in the Tail block are converted to select instructions.
//
namespace {
class SSAIfConv {
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;

public:
  /// The block containing the conditional branch.
  MachineBasicBlock *Head;

  /// The block containing phis after the if-then-else.
  MachineBasicBlock *Tail;

  /// The 'true' conditional block as determined by analyzeBranch.
  MachineBasicBlock *TBB;

  /// The 'false' conditional block as determined by analyzeBranch.
  MachineBasicBlock *FBB;

  /// isTriangle - When there is no 'else' block, either TBB or FBB will be
  /// equal to Tail.
  bool isTriangle() const { return TBB == Tail || FBB == Tail; }

  /// Returns the Tail predecessor for the True side.
  MachineBasicBlock *getTPred() const { return TBB == Tail ? Head : TBB; }

  /// Returns the Tail predecessor for the  False side.
  MachineBasicBlock *getFPred() const { return FBB == Tail ? Head : FBB; }

  /// Information about each phi in the Tail block.
  struct PHIInfo {
    MachineInstr *PHI;
    unsigned TReg = 0, FReg = 0;
    // Latencies from Cond+Branch, TReg, and FReg to DstReg.
    int CondCycles = 0, TCycles = 0, FCycles = 0;

    PHIInfo(MachineInstr *phi) : PHI(phi) {}
  };

  SmallVector<PHIInfo, 8> PHIs;

  /// The branch condition determined by analyzeBranch.
  SmallVector<MachineOperand, 4> Cond;

private:
  /// Instructions in Head that define values used by the conditional blocks.
  /// The hoisted instructions must be inserted after these instructions.
  SmallPtrSet<MachineInstr*, 8> InsertAfter;

  /// Register units clobbered by the conditional blocks.
  BitVector ClobberedRegUnits;

  // Scratch pad for findInsertionPoint.
  SparseSet<unsigned> LiveRegUnits;

  /// Insertion point in Head for speculatively executed instructions form TBB
  /// and FBB.
  MachineBasicBlock::iterator InsertionPoint;

  /// Return true if all non-terminator instructions in MBB can be safely
  /// speculated.
  bool canSpeculateInstrs(MachineBasicBlock *MBB);

  /// Return true if all non-terminator instructions in MBB can be safely
  /// predicated.
  bool canPredicateInstrs(MachineBasicBlock *MBB);

  /// Scan through instruction dependencies and update InsertAfter array.
  /// Return false if any dependency is incompatible with if conversion.
  bool InstrDependenciesAllowIfConv(MachineInstr *I);

  /// Predicate all instructions of the basic block with current condition
  /// except for terminators. Reverse the condition if ReversePredicate is set.
  void PredicateBlock(MachineBasicBlock *MBB, bool ReversePredicate);

  /// Find a valid insertion point in Head.
  bool findInsertionPoint();

  /// Replace PHI instructions in Tail with selects.
  void replacePHIInstrs();

  /// Insert selects and rewrite PHI operands to use them.
  void rewritePHIOperands();

public:
  /// runOnMachineFunction - Initialize per-function data structures.
  void runOnMachineFunction(MachineFunction &MF) {
    TII = MF.getSubtarget().getInstrInfo();
    TRI = MF.getSubtarget().getRegisterInfo();
    MRI = &MF.getRegInfo();
    LiveRegUnits.clear();
    LiveRegUnits.setUniverse(TRI->getNumRegUnits());
    ClobberedRegUnits.clear();
    ClobberedRegUnits.resize(TRI->getNumRegUnits());
  }

  /// canConvertIf - If the sub-CFG headed by MBB can be if-converted,
  /// initialize the internal state, and return true.
  /// If predicate is set try to predicate the block otherwise try to
  /// speculatively execute it.
  bool canConvertIf(MachineBasicBlock *MBB, bool Predicate = false);

  /// convertIf - If-convert the last block passed to canConvertIf(), assuming
  /// it is possible. Add any erased blocks to RemovedBlocks.
  void convertIf(SmallVectorImpl<MachineBasicBlock *> &RemovedBlocks,
                 bool Predicate = false);
};
} // end anonymous namespace


/// canSpeculateInstrs - Returns true if all the instructions in MBB can safely
/// be speculated. The terminators are not considered.
///
/// If instructions use any values that are defined in the head basic block,
/// the defining instructions are added to InsertAfter.
///
/// Any clobbered regunits are added to ClobberedRegUnits.
///
bool SSAIfConv::canSpeculateInstrs(MachineBasicBlock *MBB) {
  // Reject any live-in physregs. It's probably CPSR/EFLAGS, and very hard to
  // get right.
  if (!MBB->livein_empty()) {
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has live-ins.\n");
    return false;
  }

  unsigned InstrCount = 0;

  // Check all instructions, except the terminators. It is assumed that
  // terminators never have side effects or define any used register values.
  for (MachineInstr &MI :
       llvm::make_range(MBB->begin(), MBB->getFirstTerminator())) {
    if (MI.isDebugInstr())
      continue;

    if (++InstrCount > BlockInstrLimit && !Stress) {
      LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has more than "
                        << BlockInstrLimit << " instructions.\n");
      return false;
    }

    // There shouldn't normally be any phis in a single-predecessor block.
    if (MI.isPHI()) {
      LLVM_DEBUG(dbgs() << "Can't hoist: " << MI);
      return false;
    }

    // Don't speculate loads. Note that it may be possible and desirable to
    // speculate GOT or constant pool loads that are guaranteed not to trap,
    // but we don't support that for now.
    if (MI.mayLoad()) {
      LLVM_DEBUG(dbgs() << "Won't speculate load: " << MI);
      return false;
    }

    // We never speculate stores, so an AA pointer isn't necessary.
    bool DontMoveAcrossStore = true;
    if (!MI.isSafeToMove(nullptr, DontMoveAcrossStore)) {
      LLVM_DEBUG(dbgs() << "Can't speculate: " << MI);
      return false;
    }

    // Check for any dependencies on Head instructions.
    if (!InstrDependenciesAllowIfConv(&MI))
      return false;
  }
  return true;
}

/// Check that there is no dependencies preventing if conversion.
///
/// If instruction uses any values that are defined in the head basic block,
/// the defining instructions are added to InsertAfter.
bool SSAIfConv::InstrDependenciesAllowIfConv(MachineInstr *I) {
  for (const MachineOperand &MO : I->operands()) {
    if (MO.isRegMask()) {
      LLVM_DEBUG(dbgs() << "Won't speculate regmask: " << *I);
      return false;
    }
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();

    // Remember clobbered regunits.
    if (MO.isDef() && Reg.isPhysical())
      for (MCRegUnit Unit : TRI->regunits(Reg.asMCReg()))
        ClobberedRegUnits.set(Unit);

    if (!MO.readsReg() || !Reg.isVirtual())
      continue;
    MachineInstr *DefMI = MRI->getVRegDef(Reg);
    if (!DefMI || DefMI->getParent() != Head)
      continue;
    if (InsertAfter.insert(DefMI).second)
      LLVM_DEBUG(dbgs() << printMBBReference(*I->getParent()) << " depends on "
                        << *DefMI);
    if (DefMI->isTerminator()) {
      LLVM_DEBUG(dbgs() << "Can't insert instructions below terminator.\n");
      return false;
    }
  }
  return true;
}

/// canPredicateInstrs - Returns true if all the instructions in MBB can safely
/// be predicates. The terminators are not considered.
///
/// If instructions use any values that are defined in the head basic block,
/// the defining instructions are added to InsertAfter.
///
/// Any clobbered regunits are added to ClobberedRegUnits.
///
bool SSAIfConv::canPredicateInstrs(MachineBasicBlock *MBB) {
  // Reject any live-in physregs. It's probably CPSR/EFLAGS, and very hard to
  // get right.
  if (!MBB->livein_empty()) {
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has live-ins.\n");
    return false;
  }

  unsigned InstrCount = 0;

  // Check all instructions, except the terminators. It is assumed that
  // terminators never have side effects or define any used register values.
  for (MachineBasicBlock::iterator I = MBB->begin(),
                                   E = MBB->getFirstTerminator();
       I != E; ++I) {
    if (I->isDebugInstr())
      continue;

    if (++InstrCount > BlockInstrLimit && !Stress) {
      LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has more than "
                        << BlockInstrLimit << " instructions.\n");
      return false;
    }

    // There shouldn't normally be any phis in a single-predecessor block.
    if (I->isPHI()) {
      LLVM_DEBUG(dbgs() << "Can't predicate: " << *I);
      return false;
    }

    // Check that instruction is predicable
    if (!TII->isPredicable(*I)) {
      LLVM_DEBUG(dbgs() << "Isn't predicable: " << *I);
      return false;
    }

    // Check that instruction is not already predicated.
    if (TII->isPredicated(*I) && !TII->canPredicatePredicatedInstr(*I)) {
      LLVM_DEBUG(dbgs() << "Is already predicated: " << *I);
      return false;
    }

    // Check for any dependencies on Head instructions.
    if (!InstrDependenciesAllowIfConv(&(*I)))
      return false;
  }
  return true;
}

// Apply predicate to all instructions in the machine block.
void SSAIfConv::PredicateBlock(MachineBasicBlock *MBB, bool ReversePredicate) {
  auto Condition = Cond;
  if (ReversePredicate) {
    bool CanRevCond = !TII->reverseBranchCondition(Condition);
    assert(CanRevCond && "Reversed predicate is not supported");
    (void)CanRevCond;
  }
  // Terminators don't need to be predicated as they will be removed.
  for (MachineBasicBlock::iterator I = MBB->begin(),
                                   E = MBB->getFirstTerminator();
       I != E; ++I) {
    if (I->isDebugInstr())
      continue;
    TII->PredicateInstruction(*I, Condition);
  }
}

/// Find an insertion point in Head for the speculated instructions. The
/// insertion point must be:
///
/// 1. Before any terminators.
/// 2. After any instructions in InsertAfter.
/// 3. Not have any clobbered regunits live.
///
/// This function sets InsertionPoint and returns true when successful, it
/// returns false if no valid insertion point could be found.
///
bool SSAIfConv::findInsertionPoint() {
  // Keep track of live regunits before the current position.
  // Only track RegUnits that are also in ClobberedRegUnits.
  LiveRegUnits.clear();
  SmallVector<MCRegister, 8> Reads;
  MachineBasicBlock::iterator FirstTerm = Head->getFirstTerminator();
  MachineBasicBlock::iterator I = Head->end();
  MachineBasicBlock::iterator B = Head->begin();
  while (I != B) {
    --I;
    // Some of the conditional code depends in I.
    if (InsertAfter.count(&*I)) {
      LLVM_DEBUG(dbgs() << "Can't insert code after " << *I);
      return false;
    }

    // Update live regunits.
    for (const MachineOperand &MO : I->operands()) {
      // We're ignoring regmask operands. That is conservatively correct.
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (!Reg.isPhysical())
        continue;
      // I clobbers Reg, so it isn't live before I.
      if (MO.isDef())
        for (MCRegUnit Unit : TRI->regunits(Reg.asMCReg()))
          LiveRegUnits.erase(Unit);
      // Unless I reads Reg.
      if (MO.readsReg())
        Reads.push_back(Reg.asMCReg());
    }
    // Anything read by I is live before I.
    while (!Reads.empty())
      for (MCRegUnit Unit : TRI->regunits(Reads.pop_back_val()))
        if (ClobberedRegUnits.test(Unit))
          LiveRegUnits.insert(Unit);

    // We can't insert before a terminator.
    if (I != FirstTerm && I->isTerminator())
      continue;

    // Some of the clobbered registers are live before I, not a valid insertion
    // point.
    if (!LiveRegUnits.empty()) {
      LLVM_DEBUG({
        dbgs() << "Would clobber";
        for (unsigned LRU : LiveRegUnits)
          dbgs() << ' ' << printRegUnit(LRU, TRI);
        dbgs() << " live before " << *I;
      });
      continue;
    }

    // This is a valid insertion point.
    InsertionPoint = I;
    LLVM_DEBUG(dbgs() << "Can insert before " << *I);
    return true;
  }
  LLVM_DEBUG(dbgs() << "No legal insertion point found.\n");
  return false;
}



/// canConvertIf - analyze the sub-cfg rooted in MBB, and return true if it is
/// a potential candidate for if-conversion. Fill out the internal state.
///
bool SSAIfConv::canConvertIf(MachineBasicBlock *MBB, bool Predicate) {
  Head = MBB;
  TBB = FBB = Tail = nullptr;

  if (Head->succ_size() != 2)
    return false;
  MachineBasicBlock *Succ0 = Head->succ_begin()[0];
  MachineBasicBlock *Succ1 = Head->succ_begin()[1];

  // Canonicalize so Succ0 has MBB as its single predecessor.
  if (Succ0->pred_size() != 1)
    std::swap(Succ0, Succ1);

  if (Succ0->pred_size() != 1 || Succ0->succ_size() != 1)
    return false;

  Tail = Succ0->succ_begin()[0];

  // This is not a triangle.
  if (Tail != Succ1) {
    // Check for a diamond. We won't deal with any critical edges.
    if (Succ1->pred_size() != 1 || Succ1->succ_size() != 1 ||
        Succ1->succ_begin()[0] != Tail)
      return false;
    LLVM_DEBUG(dbgs() << "\nDiamond: " << printMBBReference(*Head) << " -> "
                      << printMBBReference(*Succ0) << "/"
                      << printMBBReference(*Succ1) << " -> "
                      << printMBBReference(*Tail) << '\n');

    // Live-in physregs are tricky to get right when speculating code.
    if (!Tail->livein_empty()) {
      LLVM_DEBUG(dbgs() << "Tail has live-ins.\n");
      return false;
    }
  } else {
    LLVM_DEBUG(dbgs() << "\nTriangle: " << printMBBReference(*Head) << " -> "
                      << printMBBReference(*Succ0) << " -> "
                      << printMBBReference(*Tail) << '\n');
  }

  // This is a triangle or a diamond.
  // Skip if we cannot predicate and there are no phis skip as there must be
  // side effects that can only be handled with predication.
  if (!Predicate && (Tail->empty() || !Tail->front().isPHI())) {
    LLVM_DEBUG(dbgs() << "No phis in tail.\n");
    return false;
  }

  // The branch we're looking to eliminate must be analyzable.
  Cond.clear();
  if (TII->analyzeBranch(*Head, TBB, FBB, Cond)) {
    LLVM_DEBUG(dbgs() << "Branch not analyzable.\n");
    return false;
  }

  // This is weird, probably some sort of degenerate CFG.
  if (!TBB) {
    LLVM_DEBUG(dbgs() << "analyzeBranch didn't find conditional branch.\n");
    return false;
  }

  // Make sure the analyzed branch is conditional; one of the successors
  // could be a landing pad. (Empty landing pads can be generated on Windows.)
  if (Cond.empty()) {
    LLVM_DEBUG(dbgs() << "analyzeBranch found an unconditional branch.\n");
    return false;
  }

  // analyzeBranch doesn't set FBB on a fall-through branch.
  // Make sure it is always set.
  FBB = TBB == Succ0 ? Succ1 : Succ0;

  // Any phis in the tail block must be convertible to selects.
  PHIs.clear();
  MachineBasicBlock *TPred = getTPred();
  MachineBasicBlock *FPred = getFPred();
  for (MachineBasicBlock::iterator I = Tail->begin(), E = Tail->end();
       I != E && I->isPHI(); ++I) {
    PHIs.push_back(&*I);
    PHIInfo &PI = PHIs.back();
    // Find PHI operands corresponding to TPred and FPred.
    for (unsigned i = 1; i != PI.PHI->getNumOperands(); i += 2) {
      if (PI.PHI->getOperand(i+1).getMBB() == TPred)
        PI.TReg = PI.PHI->getOperand(i).getReg();
      if (PI.PHI->getOperand(i+1).getMBB() == FPred)
        PI.FReg = PI.PHI->getOperand(i).getReg();
    }
    assert(Register::isVirtualRegister(PI.TReg) && "Bad PHI");
    assert(Register::isVirtualRegister(PI.FReg) && "Bad PHI");

    // Get target information.
    if (!TII->canInsertSelect(*Head, Cond, PI.PHI->getOperand(0).getReg(),
                              PI.TReg, PI.FReg, PI.CondCycles, PI.TCycles,
                              PI.FCycles)) {
      LLVM_DEBUG(dbgs() << "Can't convert: " << *PI.PHI);
      return false;
    }
  }

  // Check that the conditional instructions can be speculated.
  InsertAfter.clear();
  ClobberedRegUnits.reset();
  if (Predicate) {
    if (TBB != Tail && !canPredicateInstrs(TBB))
      return false;
    if (FBB != Tail && !canPredicateInstrs(FBB))
      return false;
  } else {
    if (TBB != Tail && !canSpeculateInstrs(TBB))
      return false;
    if (FBB != Tail && !canSpeculateInstrs(FBB))
      return false;
  }

  // Try to find a valid insertion point for the speculated instructions in the
  // head basic block.
  if (!findInsertionPoint())
    return false;

  if (isTriangle())
    ++NumTrianglesSeen;
  else
    ++NumDiamondsSeen;
  return true;
}

/// \return true iff the two registers are known to have the same value.
static bool hasSameValue(const MachineRegisterInfo &MRI,
                         const TargetInstrInfo *TII, Register TReg,
                         Register FReg) {
  if (TReg == FReg)
    return true;

  if (!TReg.isVirtual() || !FReg.isVirtual())
    return false;

  const MachineInstr *TDef = MRI.getUniqueVRegDef(TReg);
  const MachineInstr *FDef = MRI.getUniqueVRegDef(FReg);
  if (!TDef || !FDef)
    return false;

  // If there are side-effects, all bets are off.
  if (TDef->hasUnmodeledSideEffects())
    return false;

  // If the instruction could modify memory, or there may be some intervening
  // store between the two, we can't consider them to be equal.
  if (TDef->mayLoadOrStore() && !TDef->isDereferenceableInvariantLoad())
    return false;

  // We also can't guarantee that they are the same if, for example, the
  // instructions are both a copy from a physical reg, because some other
  // instruction may have modified the value in that reg between the two
  // defining insts.
  if (any_of(TDef->uses(), [](const MachineOperand &MO) {
        return MO.isReg() && MO.getReg().isPhysical();
      }))
    return false;

  // Check whether the two defining instructions produce the same value(s).
  if (!TII->produceSameValue(*TDef, *FDef, &MRI))
    return false;

  // Further, check that the two defs come from corresponding operands.
  int TIdx = TDef->findRegisterDefOperandIdx(TReg, /*TRI=*/nullptr);
  int FIdx = FDef->findRegisterDefOperandIdx(FReg, /*TRI=*/nullptr);
  if (TIdx == -1 || FIdx == -1)
    return false;

  return TIdx == FIdx;
}

/// replacePHIInstrs - Completely replace PHI instructions with selects.
/// This is possible when the only Tail predecessors are the if-converted
/// blocks.
void SSAIfConv::replacePHIInstrs() {
  assert(Tail->pred_size() == 2 && "Cannot replace PHIs");
  MachineBasicBlock::iterator FirstTerm = Head->getFirstTerminator();
  assert(FirstTerm != Head->end() && "No terminators");
  DebugLoc HeadDL = FirstTerm->getDebugLoc();

  // Convert all PHIs to select instructions inserted before FirstTerm.
  for (PHIInfo &PI : PHIs) {
    LLVM_DEBUG(dbgs() << "If-converting " << *PI.PHI);
    Register DstReg = PI.PHI->getOperand(0).getReg();
    if (hasSameValue(*MRI, TII, PI.TReg, PI.FReg)) {
      // We do not need the select instruction if both incoming values are
      // equal, but we do need a COPY.
      BuildMI(*Head, FirstTerm, HeadDL, TII->get(TargetOpcode::COPY), DstReg)
          .addReg(PI.TReg);
    } else {
      TII->insertSelect(*Head, FirstTerm, HeadDL, DstReg, Cond, PI.TReg,
                        PI.FReg);
    }
    LLVM_DEBUG(dbgs() << "          --> " << *std::prev(FirstTerm));
    PI.PHI->eraseFromParent();
    PI.PHI = nullptr;
  }
}

/// rewritePHIOperands - When there are additional Tail predecessors, insert
/// select instructions in Head and rewrite PHI operands to use the selects.
/// Keep the PHI instructions in Tail to handle the other predecessors.
void SSAIfConv::rewritePHIOperands() {
  MachineBasicBlock::iterator FirstTerm = Head->getFirstTerminator();
  assert(FirstTerm != Head->end() && "No terminators");
  DebugLoc HeadDL = FirstTerm->getDebugLoc();

  // Convert all PHIs to select instructions inserted before FirstTerm.
  for (PHIInfo &PI : PHIs) {
    unsigned DstReg = 0;

    LLVM_DEBUG(dbgs() << "If-converting " << *PI.PHI);
    if (hasSameValue(*MRI, TII, PI.TReg, PI.FReg)) {
      // We do not need the select instruction if both incoming values are
      // equal.
      DstReg = PI.TReg;
    } else {
      Register PHIDst = PI.PHI->getOperand(0).getReg();
      DstReg = MRI->createVirtualRegister(MRI->getRegClass(PHIDst));
      TII->insertSelect(*Head, FirstTerm, HeadDL,
                         DstReg, Cond, PI.TReg, PI.FReg);
      LLVM_DEBUG(dbgs() << "          --> " << *std::prev(FirstTerm));
    }

    // Rewrite PHI operands TPred -> (DstReg, Head), remove FPred.
    for (unsigned i = PI.PHI->getNumOperands(); i != 1; i -= 2) {
      MachineBasicBlock *MBB = PI.PHI->getOperand(i-1).getMBB();
      if (MBB == getTPred()) {
        PI.PHI->getOperand(i-1).setMBB(Head);
        PI.PHI->getOperand(i-2).setReg(DstReg);
      } else if (MBB == getFPred()) {
        PI.PHI->removeOperand(i-1);
        PI.PHI->removeOperand(i-2);
      }
    }
    LLVM_DEBUG(dbgs() << "          --> " << *PI.PHI);
  }
}

/// convertIf - Execute the if conversion after canConvertIf has determined the
/// feasibility.
///
/// Any basic blocks erased will be added to RemovedBlocks.
///
void SSAIfConv::convertIf(SmallVectorImpl<MachineBasicBlock *> &RemovedBlocks,
                          bool Predicate) {
  assert(Head && Tail && TBB && FBB && "Call canConvertIf first.");

  // Update statistics.
  if (isTriangle())
    ++NumTrianglesConv;
  else
    ++NumDiamondsConv;

  // Move all instructions into Head, except for the terminators.
  if (TBB != Tail) {
    if (Predicate)
      PredicateBlock(TBB, /*ReversePredicate=*/false);
    Head->splice(InsertionPoint, TBB, TBB->begin(), TBB->getFirstTerminator());
  }
  if (FBB != Tail) {
    if (Predicate)
      PredicateBlock(FBB, /*ReversePredicate=*/true);
    Head->splice(InsertionPoint, FBB, FBB->begin(), FBB->getFirstTerminator());
  }
  // Are there extra Tail predecessors?
  bool ExtraPreds = Tail->pred_size() != 2;
  if (ExtraPreds)
    rewritePHIOperands();
  else
    replacePHIInstrs();

  // Fix up the CFG, temporarily leave Head without any successors.
  Head->removeSuccessor(TBB);
  Head->removeSuccessor(FBB, true);
  if (TBB != Tail)
    TBB->removeSuccessor(Tail, true);
  if (FBB != Tail)
    FBB->removeSuccessor(Tail, true);

  // Fix up Head's terminators.
  // It should become a single branch or a fallthrough.
  DebugLoc HeadDL = Head->getFirstTerminator()->getDebugLoc();
  TII->removeBranch(*Head);

  // Erase the now empty conditional blocks. It is likely that Head can fall
  // through to Tail, and we can join the two blocks.
  if (TBB != Tail) {
    RemovedBlocks.push_back(TBB);
    TBB->eraseFromParent();
  }
  if (FBB != Tail) {
    RemovedBlocks.push_back(FBB);
    FBB->eraseFromParent();
  }

  assert(Head->succ_empty() && "Additional head successors?");
  if (!ExtraPreds && Head->isLayoutSuccessor(Tail)) {
    // Splice Tail onto the end of Head.
    LLVM_DEBUG(dbgs() << "Joining tail " << printMBBReference(*Tail)
                      << " into head " << printMBBReference(*Head) << '\n');
    Head->splice(Head->end(), Tail,
                     Tail->begin(), Tail->end());
    Head->transferSuccessorsAndUpdatePHIs(Tail);
    RemovedBlocks.push_back(Tail);
    Tail->eraseFromParent();
  } else {
    // We need a branch to Tail, let code placement work it out later.
    LLVM_DEBUG(dbgs() << "Converting to unconditional branch.\n");
    SmallVector<MachineOperand, 0> EmptyCond;
    TII->insertBranch(*Head, Tail, nullptr, EmptyCond, HeadDL);
    Head->addSuccessor(Tail);
  }
  LLVM_DEBUG(dbgs() << *Head);
}

//===----------------------------------------------------------------------===//
//                           EarlyIfConverter Pass
//===----------------------------------------------------------------------===//

namespace {
class EarlyIfConverter : public MachineFunctionPass {
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MCSchedModel SchedModel;
  MachineRegisterInfo *MRI = nullptr;
  MachineDominatorTree *DomTree = nullptr;
  MachineLoopInfo *Loops = nullptr;
  MachineTraceMetrics *Traces = nullptr;
  MachineTraceMetrics::Ensemble *MinInstr = nullptr;
  SSAIfConv IfConv;

public:
  static char ID;
  EarlyIfConverter() : MachineFunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "Early If-Conversion"; }

private:
  bool tryConvertIf(MachineBasicBlock*);
  void invalidateTraces();
  bool shouldConvertIf();
};
} // end anonymous namespace

char EarlyIfConverter::ID = 0;
char &llvm::EarlyIfConverterID = EarlyIfConverter::ID;

INITIALIZE_PASS_BEGIN(EarlyIfConverter, DEBUG_TYPE,
                      "Early If Converter", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineTraceMetrics)
INITIALIZE_PASS_END(EarlyIfConverter, DEBUG_TYPE,
                    "Early If Converter", false, false)

void EarlyIfConverter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addRequired<MachineTraceMetrics>();
  AU.addPreserved<MachineTraceMetrics>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

namespace {
/// Update the dominator tree after if-conversion erased some blocks.
void updateDomTree(MachineDominatorTree *DomTree, const SSAIfConv &IfConv,
                   ArrayRef<MachineBasicBlock *> Removed) {
  // convertIf can remove TBB, FBB, and Tail can be merged into Head.
  // TBB and FBB should not dominate any blocks.
  // Tail children should be transferred to Head.
  MachineDomTreeNode *HeadNode = DomTree->getNode(IfConv.Head);
  for (auto *B : Removed) {
    MachineDomTreeNode *Node = DomTree->getNode(B);
    assert(Node != HeadNode && "Cannot erase the head node");
    while (Node->getNumChildren()) {
      assert(Node->getBlock() == IfConv.Tail && "Unexpected children");
      DomTree->changeImmediateDominator(Node->back(), HeadNode);
    }
    DomTree->eraseNode(B);
  }
}

/// Update LoopInfo after if-conversion.
void updateLoops(MachineLoopInfo *Loops,
                 ArrayRef<MachineBasicBlock *> Removed) {
  // If-conversion doesn't change loop structure, and it doesn't mess with back
  // edges, so updating LoopInfo is simply removing the dead blocks.
  for (auto *B : Removed)
    Loops->removeBlock(B);
}
} // namespace

/// Invalidate MachineTraceMetrics before if-conversion.
void EarlyIfConverter::invalidateTraces() {
  Traces->verifyAnalysis();
  Traces->invalidate(IfConv.Head);
  Traces->invalidate(IfConv.Tail);
  Traces->invalidate(IfConv.TBB);
  Traces->invalidate(IfConv.FBB);
  Traces->verifyAnalysis();
}

// Adjust cycles with downward saturation.
static unsigned adjCycles(unsigned Cyc, int Delta) {
  if (Delta < 0 && Cyc + Delta > Cyc)
    return 0;
  return Cyc + Delta;
}

namespace {
/// Helper class to simplify emission of cycle counts into optimization remarks.
struct Cycles {
  const char *Key;
  unsigned Value;
};
template <typename Remark> Remark &operator<<(Remark &R, Cycles C) {
  return R << ore::NV(C.Key, C.Value) << (C.Value == 1 ? " cycle" : " cycles");
}
} // anonymous namespace

/// Apply cost model and heuristics to the if-conversion in IfConv.
/// Return true if the conversion is a good idea.
///
bool EarlyIfConverter::shouldConvertIf() {
  // Stress testing mode disables all cost considerations.
  if (Stress)
    return true;

  // Do not try to if-convert if the condition has a high chance of being
  // predictable.
  MachineLoop *CurrentLoop = Loops->getLoopFor(IfConv.Head);
  // If the condition is in a loop, consider it predictable if the condition
  // itself or all its operands are loop-invariant. E.g. this considers a load
  // from a loop-invariant address predictable; we were unable to prove that it
  // doesn't alias any of the memory-writes in the loop, but it is likely to
  // read to same value multiple times.
  if (CurrentLoop && any_of(IfConv.Cond, [&](MachineOperand &MO) {
        if (!MO.isReg() || !MO.isUse())
          return false;
        Register Reg = MO.getReg();
        if (Register::isPhysicalRegister(Reg))
          return false;

        MachineInstr *Def = MRI->getVRegDef(Reg);
        return CurrentLoop->isLoopInvariant(*Def) ||
               all_of(Def->operands(), [&](MachineOperand &Op) {
                 if (Op.isImm())
                   return true;
                 if (!MO.isReg() || !MO.isUse())
                   return false;
                 Register Reg = MO.getReg();
                 if (Register::isPhysicalRegister(Reg))
                   return false;

                 MachineInstr *Def = MRI->getVRegDef(Reg);
                 return CurrentLoop->isLoopInvariant(*Def);
               });
      }))
    return false;

  if (!MinInstr)
    MinInstr = Traces->getEnsemble(MachineTraceStrategy::TS_MinInstrCount);

  MachineTraceMetrics::Trace TBBTrace = MinInstr->getTrace(IfConv.getTPred());
  MachineTraceMetrics::Trace FBBTrace = MinInstr->getTrace(IfConv.getFPred());
  LLVM_DEBUG(dbgs() << "TBB: " << TBBTrace << "FBB: " << FBBTrace);
  unsigned MinCrit = std::min(TBBTrace.getCriticalPath(),
                              FBBTrace.getCriticalPath());

  // Set a somewhat arbitrary limit on the critical path extension we accept.
  unsigned CritLimit = SchedModel.MispredictPenalty/2;

  MachineBasicBlock &MBB = *IfConv.Head;
  MachineOptimizationRemarkEmitter MORE(*MBB.getParent(), nullptr);

  // If-conversion only makes sense when there is unexploited ILP. Compute the
  // maximum-ILP resource length of the trace after if-conversion. Compare it
  // to the shortest critical path.
  SmallVector<const MachineBasicBlock*, 1> ExtraBlocks;
  if (IfConv.TBB != IfConv.Tail)
    ExtraBlocks.push_back(IfConv.TBB);
  unsigned ResLength = FBBTrace.getResourceLength(ExtraBlocks);
  LLVM_DEBUG(dbgs() << "Resource length " << ResLength
                    << ", minimal critical path " << MinCrit << '\n');
  if (ResLength > MinCrit + CritLimit) {
    LLVM_DEBUG(dbgs() << "Not enough available ILP.\n");
    MORE.emit([&]() {
      MachineOptimizationRemarkMissed R(DEBUG_TYPE, "IfConversion",
                                        MBB.findDebugLoc(MBB.back()), &MBB);
      R << "did not if-convert branch: the resulting critical path ("
        << Cycles{"ResLength", ResLength}
        << ") would extend the shorter leg's critical path ("
        << Cycles{"MinCrit", MinCrit} << ") by more than the threshold of "
        << Cycles{"CritLimit", CritLimit}
        << ", which cannot be hidden by available ILP.";
      return R;
    });
    return false;
  }

  // Assume that the depth of the first head terminator will also be the depth
  // of the select instruction inserted, as determined by the flag dependency.
  // TBB / FBB data dependencies may delay the select even more.
  MachineTraceMetrics::Trace HeadTrace = MinInstr->getTrace(IfConv.Head);
  unsigned BranchDepth =
      HeadTrace.getInstrCycles(*IfConv.Head->getFirstTerminator()).Depth;
  LLVM_DEBUG(dbgs() << "Branch depth: " << BranchDepth << '\n');

  // Look at all the tail phis, and compute the critical path extension caused
  // by inserting select instructions.
  MachineTraceMetrics::Trace TailTrace = MinInstr->getTrace(IfConv.Tail);
  struct CriticalPathInfo {
    unsigned Extra; // Count of extra cycles that the component adds.
    unsigned Depth; // Absolute depth of the component in cycles.
  };
  CriticalPathInfo Cond{};
  CriticalPathInfo TBlock{};
  CriticalPathInfo FBlock{};
  bool ShouldConvert = true;
  for (SSAIfConv::PHIInfo &PI : IfConv.PHIs) {
    unsigned Slack = TailTrace.getInstrSlack(*PI.PHI);
    unsigned MaxDepth = Slack + TailTrace.getInstrCycles(*PI.PHI).Depth;
    LLVM_DEBUG(dbgs() << "Slack " << Slack << ":\t" << *PI.PHI);

    // The condition is pulled into the critical path.
    unsigned CondDepth = adjCycles(BranchDepth, PI.CondCycles);
    if (CondDepth > MaxDepth) {
      unsigned Extra = CondDepth - MaxDepth;
      LLVM_DEBUG(dbgs() << "Condition adds " << Extra << " cycles.\n");
      if (Extra > Cond.Extra)
        Cond = {Extra, CondDepth};
      if (Extra > CritLimit) {
        LLVM_DEBUG(dbgs() << "Exceeds limit of " << CritLimit << '\n');
        ShouldConvert = false;
      }
    }

    // The TBB value is pulled into the critical path.
    unsigned TDepth = adjCycles(TBBTrace.getPHIDepth(*PI.PHI), PI.TCycles);
    if (TDepth > MaxDepth) {
      unsigned Extra = TDepth - MaxDepth;
      LLVM_DEBUG(dbgs() << "TBB data adds " << Extra << " cycles.\n");
      if (Extra > TBlock.Extra)
        TBlock = {Extra, TDepth};
      if (Extra > CritLimit) {
        LLVM_DEBUG(dbgs() << "Exceeds limit of " << CritLimit << '\n');
        ShouldConvert = false;
      }
    }

    // The FBB value is pulled into the critical path.
    unsigned FDepth = adjCycles(FBBTrace.getPHIDepth(*PI.PHI), PI.FCycles);
    if (FDepth > MaxDepth) {
      unsigned Extra = FDepth - MaxDepth;
      LLVM_DEBUG(dbgs() << "FBB data adds " << Extra << " cycles.\n");
      if (Extra > FBlock.Extra)
        FBlock = {Extra, FDepth};
      if (Extra > CritLimit) {
        LLVM_DEBUG(dbgs() << "Exceeds limit of " << CritLimit << '\n');
        ShouldConvert = false;
      }
    }
  }

  // Organize by "short" and "long" legs, since the diagnostics get confusing
  // when referring to the "true" and "false" sides of the branch, given that
  // those don't always correlate with what the user wrote in source-terms.
  const CriticalPathInfo Short = TBlock.Extra > FBlock.Extra ? FBlock : TBlock;
  const CriticalPathInfo Long = TBlock.Extra > FBlock.Extra ? TBlock : FBlock;

  if (ShouldConvert) {
    MORE.emit([&]() {
      MachineOptimizationRemark R(DEBUG_TYPE, "IfConversion",
                                  MBB.back().getDebugLoc(), &MBB);
      R << "performing if-conversion on branch: the condition adds "
        << Cycles{"CondCycles", Cond.Extra} << " to the critical path";
      if (Short.Extra > 0)
        R << ", and the short leg adds another "
          << Cycles{"ShortCycles", Short.Extra};
      if (Long.Extra > 0)
        R << ", and the long leg adds another "
          << Cycles{"LongCycles", Long.Extra};
      R << ", each staying under the threshold of "
        << Cycles{"CritLimit", CritLimit} << ".";
      return R;
    });
  } else {
    MORE.emit([&]() {
      MachineOptimizationRemarkMissed R(DEBUG_TYPE, "IfConversion",
                                        MBB.back().getDebugLoc(), &MBB);
      R << "did not if-convert branch: the condition would add "
        << Cycles{"CondCycles", Cond.Extra} << " to the critical path";
      if (Cond.Extra > CritLimit)
        R << " exceeding the limit of " << Cycles{"CritLimit", CritLimit};
      if (Short.Extra > 0) {
        R << ", and the short leg would add another "
          << Cycles{"ShortCycles", Short.Extra};
        if (Short.Extra > CritLimit)
          R << " exceeding the limit of " << Cycles{"CritLimit", CritLimit};
      }
      if (Long.Extra > 0) {
        R << ", and the long leg would add another "
          << Cycles{"LongCycles", Long.Extra};
        if (Long.Extra > CritLimit)
          R << " exceeding the limit of " << Cycles{"CritLimit", CritLimit};
      }
      R << ".";
      return R;
    });
  }

  return ShouldConvert;
}

/// Attempt repeated if-conversion on MBB, return true if successful.
///
bool EarlyIfConverter::tryConvertIf(MachineBasicBlock *MBB) {
  bool Changed = false;
  while (IfConv.canConvertIf(MBB) && shouldConvertIf()) {
    // If-convert MBB and update analyses.
    invalidateTraces();
    SmallVector<MachineBasicBlock*, 4> RemovedBlocks;
    IfConv.convertIf(RemovedBlocks);
    Changed = true;
    updateDomTree(DomTree, IfConv, RemovedBlocks);
    updateLoops(Loops, RemovedBlocks);
  }
  return Changed;
}

bool EarlyIfConverter::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** EARLY IF-CONVERSION **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  if (skipFunction(MF.getFunction()))
    return false;

  // Only run if conversion if the target wants it.
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  if (!STI.enableEarlyIfConversion())
    return false;

  TII = STI.getInstrInfo();
  TRI = STI.getRegisterInfo();
  SchedModel = STI.getSchedModel();
  MRI = &MF.getRegInfo();
  DomTree = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  Loops = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  Traces = &getAnalysis<MachineTraceMetrics>();
  MinInstr = nullptr;

  bool Changed = false;
  IfConv.runOnMachineFunction(MF);

  // Visit blocks in dominator tree post-order. The post-order enables nested
  // if-conversion in a single pass. The tryConvertIf() function may erase
  // blocks, but only blocks dominated by the head block. This makes it safe to
  // update the dominator tree while the post-order iterator is still active.
  for (auto *DomNode : post_order(DomTree))
    if (tryConvertIf(DomNode->getBlock()))
      Changed = true;

  return Changed;
}

//===----------------------------------------------------------------------===//
//                           EarlyIfPredicator Pass
//===----------------------------------------------------------------------===//

namespace {
class EarlyIfPredicator : public MachineFunctionPass {
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  TargetSchedModel SchedModel;
  MachineRegisterInfo *MRI = nullptr;
  MachineDominatorTree *DomTree = nullptr;
  MachineBranchProbabilityInfo *MBPI = nullptr;
  MachineLoopInfo *Loops = nullptr;
  SSAIfConv IfConv;

public:
  static char ID;
  EarlyIfPredicator() : MachineFunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "Early If-predicator"; }

protected:
  bool tryConvertIf(MachineBasicBlock *);
  bool shouldConvertIf();
};
} // end anonymous namespace

#undef DEBUG_TYPE
#define DEBUG_TYPE "early-if-predicator"

char EarlyIfPredicator::ID = 0;
char &llvm::EarlyIfPredicatorID = EarlyIfPredicator::ID;

INITIALIZE_PASS_BEGIN(EarlyIfPredicator, DEBUG_TYPE, "Early If Predicator",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(EarlyIfPredicator, DEBUG_TYPE, "Early If Predicator", false,
                    false)

void EarlyIfPredicator::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

/// Apply the target heuristic to decide if the transformation is profitable.
bool EarlyIfPredicator::shouldConvertIf() {
  auto TrueProbability = MBPI->getEdgeProbability(IfConv.Head, IfConv.TBB);
  if (IfConv.isTriangle()) {
    MachineBasicBlock &IfBlock =
        (IfConv.TBB == IfConv.Tail) ? *IfConv.FBB : *IfConv.TBB;

    unsigned ExtraPredCost = 0;
    unsigned Cycles = 0;
    for (MachineInstr &I : IfBlock) {
      unsigned NumCycles = SchedModel.computeInstrLatency(&I, false);
      if (NumCycles > 1)
        Cycles += NumCycles - 1;
      ExtraPredCost += TII->getPredicationCost(I);
    }

    return TII->isProfitableToIfCvt(IfBlock, Cycles, ExtraPredCost,
                                    TrueProbability);
  }
  unsigned TExtra = 0;
  unsigned FExtra = 0;
  unsigned TCycle = 0;
  unsigned FCycle = 0;
  for (MachineInstr &I : *IfConv.TBB) {
    unsigned NumCycles = SchedModel.computeInstrLatency(&I, false);
    if (NumCycles > 1)
      TCycle += NumCycles - 1;
    TExtra += TII->getPredicationCost(I);
  }
  for (MachineInstr &I : *IfConv.FBB) {
    unsigned NumCycles = SchedModel.computeInstrLatency(&I, false);
    if (NumCycles > 1)
      FCycle += NumCycles - 1;
    FExtra += TII->getPredicationCost(I);
  }
  return TII->isProfitableToIfCvt(*IfConv.TBB, TCycle, TExtra, *IfConv.FBB,
                                  FCycle, FExtra, TrueProbability);
}

/// Attempt repeated if-conversion on MBB, return true if successful.
///
bool EarlyIfPredicator::tryConvertIf(MachineBasicBlock *MBB) {
  bool Changed = false;
  while (IfConv.canConvertIf(MBB, /*Predicate*/ true) && shouldConvertIf()) {
    // If-convert MBB and update analyses.
    SmallVector<MachineBasicBlock *, 4> RemovedBlocks;
    IfConv.convertIf(RemovedBlocks, /*Predicate*/ true);
    Changed = true;
    updateDomTree(DomTree, IfConv, RemovedBlocks);
    updateLoops(Loops, RemovedBlocks);
  }
  return Changed;
}

bool EarlyIfPredicator::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** EARLY IF-PREDICATOR **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  if (skipFunction(MF.getFunction()))
    return false;

  const TargetSubtargetInfo &STI = MF.getSubtarget();
  TII = STI.getInstrInfo();
  TRI = STI.getRegisterInfo();
  MRI = &MF.getRegInfo();
  SchedModel.init(&STI);
  DomTree = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  Loops = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();

  bool Changed = false;
  IfConv.runOnMachineFunction(MF);

  // Visit blocks in dominator tree post-order. The post-order enables nested
  // if-conversion in a single pass. The tryConvertIf() function may erase
  // blocks, but only blocks dominated by the head block. This makes it safe to
  // update the dominator tree while the post-order iterator is still active.
  for (auto *DomNode : post_order(DomTree))
    if (tryConvertIf(DomNode->getBlock()))
      Changed = true;

  return Changed;
}
