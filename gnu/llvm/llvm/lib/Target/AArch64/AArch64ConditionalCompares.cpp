//===-- AArch64ConditionalCompares.cpp --- CCMP formation for AArch64 -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AArch64ConditionalCompares pass which reduces
// branching and code size by using the conditional compare instructions CCMP,
// CCMN, and FCMP.
//
// The CFG transformations for forming conditional compares are very similar to
// if-conversion, and this pass should run immediately before the early
// if-conversion pass.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-ccmp"

// Absolute maximum number of instructions allowed per speculated block.
// This bypasses all other heuristics, so it should be set fairly high.
static cl::opt<unsigned> BlockInstrLimit(
    "aarch64-ccmp-limit", cl::init(30), cl::Hidden,
    cl::desc("Maximum number of instructions per speculated block."));

// Stress testing mode - disable heuristics.
static cl::opt<bool> Stress("aarch64-stress-ccmp", cl::Hidden,
                            cl::desc("Turn all knobs to 11"));

STATISTIC(NumConsidered, "Number of ccmps considered");
STATISTIC(NumPhiRejs, "Number of ccmps rejected (PHI)");
STATISTIC(NumPhysRejs, "Number of ccmps rejected (Physregs)");
STATISTIC(NumPhi2Rejs, "Number of ccmps rejected (PHI2)");
STATISTIC(NumHeadBranchRejs, "Number of ccmps rejected (Head branch)");
STATISTIC(NumCmpBranchRejs, "Number of ccmps rejected (CmpBB branch)");
STATISTIC(NumCmpTermRejs, "Number of ccmps rejected (CmpBB is cbz...)");
STATISTIC(NumImmRangeRejs, "Number of ccmps rejected (Imm out of range)");
STATISTIC(NumLiveDstRejs, "Number of ccmps rejected (Cmp dest live)");
STATISTIC(NumMultNZCVUses, "Number of ccmps rejected (NZCV used)");
STATISTIC(NumUnknNZCVDefs, "Number of ccmps rejected (NZCV def unknown)");

STATISTIC(NumSpeculateRejs, "Number of ccmps rejected (Can't speculate)");

STATISTIC(NumConverted, "Number of ccmp instructions created");
STATISTIC(NumCompBranches, "Number of cbz/cbnz branches converted");

//===----------------------------------------------------------------------===//
//                                 SSACCmpConv
//===----------------------------------------------------------------------===//
//
// The SSACCmpConv class performs ccmp-conversion on SSA form machine code
// after determining if it is possible. The class contains no heuristics;
// external code should be used to determine when ccmp-conversion is a good
// idea.
//
// CCmp-formation works on a CFG representing chained conditions, typically
// from C's short-circuit || and && operators:
//
//   From:         Head            To:         Head
//                 / |                         CmpBB
//                /  |                         / |
//               |  CmpBB                     /  |
//               |  / |                    Tail  |
//               | /  |                      |   |
//              Tail  |                      |   |
//                |   |                      |   |
//               ... ...                    ... ...
//
// The Head block is terminated by a br.cond instruction, and the CmpBB block
// contains compare + br.cond. Tail must be a successor of both.
//
// The cmp-conversion turns the compare instruction in CmpBB into a conditional
// compare, and merges CmpBB into Head, speculatively executing its
// instructions. The AArch64 conditional compare instructions have an immediate
// operand that specifies the NZCV flag values when the condition is false and
// the compare isn't executed. This makes it possible to chain compares with
// different condition codes.
//
// Example:
//
//    if (a == 5 || b == 17)
//      foo();
//
//    Head:
//       cmp  w0, #5
//       b.eq Tail
//    CmpBB:
//       cmp  w1, #17
//       b.eq Tail
//    ...
//    Tail:
//      bl _foo
//
//  Becomes:
//
//    Head:
//       cmp  w0, #5
//       ccmp w1, #17, 4, ne  ; 4 = nZcv
//       b.eq Tail
//    ...
//    Tail:
//      bl _foo
//
// The ccmp condition code is the one that would cause the Head terminator to
// branch to CmpBB.
//
// FIXME: It should also be possible to speculate a block on the critical edge
// between Head and Tail, just like if-converting a diamond.
//
// FIXME: Handle PHIs in Tail by turning them into selects (if-conversion).

namespace {
class SSACCmpConv {
  MachineFunction *MF;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;
  const MachineBranchProbabilityInfo *MBPI;

public:
  /// The first block containing a conditional branch, dominating everything
  /// else.
  MachineBasicBlock *Head;

  /// The block containing cmp+br.cond with a successor shared with Head.
  MachineBasicBlock *CmpBB;

  /// The common successor for Head and CmpBB.
  MachineBasicBlock *Tail;

  /// The compare instruction in CmpBB that can be converted to a ccmp.
  MachineInstr *CmpMI;

private:
  /// The branch condition in Head as determined by analyzeBranch.
  SmallVector<MachineOperand, 4> HeadCond;

  /// The condition code that makes Head branch to CmpBB.
  AArch64CC::CondCode HeadCmpBBCC;

  /// The branch condition in CmpBB.
  SmallVector<MachineOperand, 4> CmpBBCond;

  /// The condition code that makes CmpBB branch to Tail.
  AArch64CC::CondCode CmpBBTailCC;

  /// Check if the Tail PHIs are trivially convertible.
  bool trivialTailPHIs();

  /// Remove CmpBB from the Tail PHIs.
  void updateTailPHIs();

  /// Check if an operand defining DstReg is dead.
  bool isDeadDef(unsigned DstReg);

  /// Find the compare instruction in MBB that controls the conditional branch.
  /// Return NULL if a convertible instruction can't be found.
  MachineInstr *findConvertibleCompare(MachineBasicBlock *MBB);

  /// Return true if all non-terminator instructions in MBB can be safely
  /// speculated.
  bool canSpeculateInstrs(MachineBasicBlock *MBB, const MachineInstr *CmpMI);

public:
  /// runOnMachineFunction - Initialize per-function data structures.
  void runOnMachineFunction(MachineFunction &MF,
                            const MachineBranchProbabilityInfo *MBPI) {
    this->MF = &MF;
    this->MBPI = MBPI;
    TII = MF.getSubtarget().getInstrInfo();
    TRI = MF.getSubtarget().getRegisterInfo();
    MRI = &MF.getRegInfo();
  }

  /// If the sub-CFG headed by MBB can be cmp-converted, initialize the
  /// internal state, and return true.
  bool canConvert(MachineBasicBlock *MBB);

  /// Cmo-convert the last block passed to canConvertCmp(), assuming
  /// it is possible. Add any erased blocks to RemovedBlocks.
  void convert(SmallVectorImpl<MachineBasicBlock *> &RemovedBlocks);

  /// Return the expected code size delta if the conversion into a
  /// conditional compare is performed.
  int expectedCodeSizeDelta() const;
};
} // end anonymous namespace

// Check that all PHIs in Tail are selecting the same value from Head and CmpBB.
// This means that no if-conversion is required when merging CmpBB into Head.
bool SSACCmpConv::trivialTailPHIs() {
  for (auto &I : *Tail) {
    if (!I.isPHI())
      break;
    unsigned HeadReg = 0, CmpBBReg = 0;
    // PHI operands come in (VReg, MBB) pairs.
    for (unsigned oi = 1, oe = I.getNumOperands(); oi != oe; oi += 2) {
      MachineBasicBlock *MBB = I.getOperand(oi + 1).getMBB();
      Register Reg = I.getOperand(oi).getReg();
      if (MBB == Head) {
        assert((!HeadReg || HeadReg == Reg) && "Inconsistent PHI operands");
        HeadReg = Reg;
      }
      if (MBB == CmpBB) {
        assert((!CmpBBReg || CmpBBReg == Reg) && "Inconsistent PHI operands");
        CmpBBReg = Reg;
      }
    }
    if (HeadReg != CmpBBReg)
      return false;
  }
  return true;
}

// Assuming that trivialTailPHIs() is true, update the Tail PHIs by simply
// removing the CmpBB operands. The Head operands will be identical.
void SSACCmpConv::updateTailPHIs() {
  for (auto &I : *Tail) {
    if (!I.isPHI())
      break;
    // I is a PHI. It can have multiple entries for CmpBB.
    for (unsigned oi = I.getNumOperands(); oi > 2; oi -= 2) {
      // PHI operands are (Reg, MBB) at (oi-2, oi-1).
      if (I.getOperand(oi - 1).getMBB() == CmpBB) {
        I.removeOperand(oi - 1);
        I.removeOperand(oi - 2);
      }
    }
  }
}

// This pass runs before the AArch64DeadRegisterDefinitions pass, so compares
// are still writing virtual registers without any uses.
bool SSACCmpConv::isDeadDef(unsigned DstReg) {
  // Writes to the zero register are dead.
  if (DstReg == AArch64::WZR || DstReg == AArch64::XZR)
    return true;
  if (!Register::isVirtualRegister(DstReg))
    return false;
  // A virtual register def without any uses will be marked dead later, and
  // eventually replaced by the zero register.
  return MRI->use_nodbg_empty(DstReg);
}

// Parse a condition code returned by analyzeBranch, and compute the CondCode
// corresponding to TBB.
// Return
static bool parseCond(ArrayRef<MachineOperand> Cond, AArch64CC::CondCode &CC) {
  // A normal br.cond simply has the condition code.
  if (Cond[0].getImm() != -1) {
    assert(Cond.size() == 1 && "Unknown Cond array format");
    CC = (AArch64CC::CondCode)(int)Cond[0].getImm();
    return true;
  }
  // For tbz and cbz instruction, the opcode is next.
  switch (Cond[1].getImm()) {
  default:
    // This includes tbz / tbnz branches which can't be converted to
    // ccmp + br.cond.
    return false;
  case AArch64::CBZW:
  case AArch64::CBZX:
    assert(Cond.size() == 3 && "Unknown Cond array format");
    CC = AArch64CC::EQ;
    return true;
  case AArch64::CBNZW:
  case AArch64::CBNZX:
    assert(Cond.size() == 3 && "Unknown Cond array format");
    CC = AArch64CC::NE;
    return true;
  }
}

MachineInstr *SSACCmpConv::findConvertibleCompare(MachineBasicBlock *MBB) {
  MachineBasicBlock::iterator I = MBB->getFirstTerminator();
  if (I == MBB->end())
    return nullptr;
  // The terminator must be controlled by the flags.
  if (!I->readsRegister(AArch64::NZCV, /*TRI=*/nullptr)) {
    switch (I->getOpcode()) {
    case AArch64::CBZW:
    case AArch64::CBZX:
    case AArch64::CBNZW:
    case AArch64::CBNZX:
      // These can be converted into a ccmp against #0.
      return &*I;
    }
    ++NumCmpTermRejs;
    LLVM_DEBUG(dbgs() << "Flags not used by terminator: " << *I);
    return nullptr;
  }

  // Now find the instruction controlling the terminator.
  for (MachineBasicBlock::iterator B = MBB->begin(); I != B;) {
    I = prev_nodbg(I, MBB->begin());
    assert(!I->isTerminator() && "Spurious terminator");
    switch (I->getOpcode()) {
    // cmp is an alias for subs with a dead destination register.
    case AArch64::SUBSWri:
    case AArch64::SUBSXri:
    // cmn is an alias for adds with a dead destination register.
    case AArch64::ADDSWri:
    case AArch64::ADDSXri:
      // Check that the immediate operand is within range, ccmp wants a uimm5.
      // Rd = SUBSri Rn, imm, shift
      if (I->getOperand(3).getImm() || !isUInt<5>(I->getOperand(2).getImm())) {
        LLVM_DEBUG(dbgs() << "Immediate out of range for ccmp: " << *I);
        ++NumImmRangeRejs;
        return nullptr;
      }
      [[fallthrough]];
    case AArch64::SUBSWrr:
    case AArch64::SUBSXrr:
    case AArch64::ADDSWrr:
    case AArch64::ADDSXrr:
      if (isDeadDef(I->getOperand(0).getReg()))
        return &*I;
      LLVM_DEBUG(dbgs() << "Can't convert compare with live destination: "
                        << *I);
      ++NumLiveDstRejs;
      return nullptr;
    case AArch64::FCMPSrr:
    case AArch64::FCMPDrr:
    case AArch64::FCMPESrr:
    case AArch64::FCMPEDrr:
      return &*I;
    }

    // Check for flag reads and clobbers.
    PhysRegInfo PRI = AnalyzePhysRegInBundle(*I, AArch64::NZCV, TRI);

    if (PRI.Read) {
      // The ccmp doesn't produce exactly the same flags as the original
      // compare, so reject the transform if there are uses of the flags
      // besides the terminators.
      LLVM_DEBUG(dbgs() << "Can't create ccmp with multiple uses: " << *I);
      ++NumMultNZCVUses;
      return nullptr;
    }

    if (PRI.Defined || PRI.Clobbered) {
      LLVM_DEBUG(dbgs() << "Not convertible compare: " << *I);
      ++NumUnknNZCVDefs;
      return nullptr;
    }
  }
  LLVM_DEBUG(dbgs() << "Flags not defined in " << printMBBReference(*MBB)
                    << '\n');
  return nullptr;
}

/// Determine if all the instructions in MBB can safely
/// be speculated. The terminators are not considered.
///
/// Only CmpMI is allowed to clobber the flags.
///
bool SSACCmpConv::canSpeculateInstrs(MachineBasicBlock *MBB,
                                     const MachineInstr *CmpMI) {
  // Reject any live-in physregs. It's probably NZCV/EFLAGS, and very hard to
  // get right.
  if (!MBB->livein_empty()) {
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has live-ins.\n");
    return false;
  }

  unsigned InstrCount = 0;

  // Check all instructions, except the terminators. It is assumed that
  // terminators never have side effects or define any used register values.
  for (auto &I : make_range(MBB->begin(), MBB->getFirstTerminator())) {
    if (I.isDebugInstr())
      continue;

    if (++InstrCount > BlockInstrLimit && !Stress) {
      LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has more than "
                        << BlockInstrLimit << " instructions.\n");
      return false;
    }

    // There shouldn't normally be any phis in a single-predecessor block.
    if (I.isPHI()) {
      LLVM_DEBUG(dbgs() << "Can't hoist: " << I);
      return false;
    }

    // Don't speculate loads. Note that it may be possible and desirable to
    // speculate GOT or constant pool loads that are guaranteed not to trap,
    // but we don't support that for now.
    if (I.mayLoad()) {
      LLVM_DEBUG(dbgs() << "Won't speculate load: " << I);
      return false;
    }

    // We never speculate stores, so an AA pointer isn't necessary.
    bool DontMoveAcrossStore = true;
    if (!I.isSafeToMove(nullptr, DontMoveAcrossStore)) {
      LLVM_DEBUG(dbgs() << "Can't speculate: " << I);
      return false;
    }

    // Only CmpMI is allowed to clobber the flags.
    if (&I != CmpMI && I.modifiesRegister(AArch64::NZCV, TRI)) {
      LLVM_DEBUG(dbgs() << "Clobbers flags: " << I);
      return false;
    }
  }
  return true;
}

/// Analyze the sub-cfg rooted in MBB, and return true if it is a potential
/// candidate for cmp-conversion. Fill out the internal state.
///
bool SSACCmpConv::canConvert(MachineBasicBlock *MBB) {
  Head = MBB;
  Tail = CmpBB = nullptr;

  if (Head->succ_size() != 2)
    return false;
  MachineBasicBlock *Succ0 = Head->succ_begin()[0];
  MachineBasicBlock *Succ1 = Head->succ_begin()[1];

  // CmpBB can only have a single predecessor. Tail is allowed many.
  if (Succ0->pred_size() != 1)
    std::swap(Succ0, Succ1);

  // Succ0 is our candidate for CmpBB.
  if (Succ0->pred_size() != 1 || Succ0->succ_size() != 2)
    return false;

  CmpBB = Succ0;
  Tail = Succ1;

  if (!CmpBB->isSuccessor(Tail))
    return false;

  // The CFG topology checks out.
  LLVM_DEBUG(dbgs() << "\nTriangle: " << printMBBReference(*Head) << " -> "
                    << printMBBReference(*CmpBB) << " -> "
                    << printMBBReference(*Tail) << '\n');
  ++NumConsidered;

  // Tail is allowed to have many predecessors, but we can't handle PHIs yet.
  //
  // FIXME: Real PHIs could be if-converted as long as the CmpBB values are
  // defined before The CmpBB cmp clobbers the flags. Alternatively, it should
  // always be safe to sink the ccmp down to immediately before the CmpBB
  // terminators.
  if (!trivialTailPHIs()) {
    LLVM_DEBUG(dbgs() << "Can't handle phis in Tail.\n");
    ++NumPhiRejs;
    return false;
  }

  if (!Tail->livein_empty()) {
    LLVM_DEBUG(dbgs() << "Can't handle live-in physregs in Tail.\n");
    ++NumPhysRejs;
    return false;
  }

  // CmpBB should never have PHIs since Head is its only predecessor.
  // FIXME: Clean them up if it happens.
  if (!CmpBB->empty() && CmpBB->front().isPHI()) {
    LLVM_DEBUG(dbgs() << "Can't handle phis in CmpBB.\n");
    ++NumPhi2Rejs;
    return false;
  }

  if (!CmpBB->livein_empty()) {
    LLVM_DEBUG(dbgs() << "Can't handle live-in physregs in CmpBB.\n");
    ++NumPhysRejs;
    return false;
  }

  // The branch we're looking to eliminate must be analyzable.
  HeadCond.clear();
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  if (TII->analyzeBranch(*Head, TBB, FBB, HeadCond)) {
    LLVM_DEBUG(dbgs() << "Head branch not analyzable.\n");
    ++NumHeadBranchRejs;
    return false;
  }

  // This is weird, probably some sort of degenerate CFG, or an edge to a
  // landing pad.
  if (!TBB || HeadCond.empty()) {
    LLVM_DEBUG(
        dbgs() << "analyzeBranch didn't find conditional branch in Head.\n");
    ++NumHeadBranchRejs;
    return false;
  }

  if (!parseCond(HeadCond, HeadCmpBBCC)) {
    LLVM_DEBUG(dbgs() << "Unsupported branch type on Head\n");
    ++NumHeadBranchRejs;
    return false;
  }

  // Make sure the branch direction is right.
  if (TBB != CmpBB) {
    assert(TBB == Tail && "Unexpected TBB");
    HeadCmpBBCC = AArch64CC::getInvertedCondCode(HeadCmpBBCC);
  }

  CmpBBCond.clear();
  TBB = FBB = nullptr;
  if (TII->analyzeBranch(*CmpBB, TBB, FBB, CmpBBCond)) {
    LLVM_DEBUG(dbgs() << "CmpBB branch not analyzable.\n");
    ++NumCmpBranchRejs;
    return false;
  }

  if (!TBB || CmpBBCond.empty()) {
    LLVM_DEBUG(
        dbgs() << "analyzeBranch didn't find conditional branch in CmpBB.\n");
    ++NumCmpBranchRejs;
    return false;
  }

  if (!parseCond(CmpBBCond, CmpBBTailCC)) {
    LLVM_DEBUG(dbgs() << "Unsupported branch type on CmpBB\n");
    ++NumCmpBranchRejs;
    return false;
  }

  if (TBB != Tail)
    CmpBBTailCC = AArch64CC::getInvertedCondCode(CmpBBTailCC);

  LLVM_DEBUG(dbgs() << "Head->CmpBB on "
                    << AArch64CC::getCondCodeName(HeadCmpBBCC)
                    << ", CmpBB->Tail on "
                    << AArch64CC::getCondCodeName(CmpBBTailCC) << '\n');

  CmpMI = findConvertibleCompare(CmpBB);
  if (!CmpMI)
    return false;

  if (!canSpeculateInstrs(CmpBB, CmpMI)) {
    ++NumSpeculateRejs;
    return false;
  }
  return true;
}

void SSACCmpConv::convert(SmallVectorImpl<MachineBasicBlock *> &RemovedBlocks) {
  LLVM_DEBUG(dbgs() << "Merging " << printMBBReference(*CmpBB) << " into "
                    << printMBBReference(*Head) << ":\n"
                    << *CmpBB);

  // All CmpBB instructions are moved into Head, and CmpBB is deleted.
  // Update the CFG first.
  updateTailPHIs();

  // Save successor probabilties before removing CmpBB and Tail from their
  // parents.
  BranchProbability Head2CmpBB = MBPI->getEdgeProbability(Head, CmpBB);
  BranchProbability CmpBB2Tail = MBPI->getEdgeProbability(CmpBB, Tail);

  Head->removeSuccessor(CmpBB);
  CmpBB->removeSuccessor(Tail);

  // If Head and CmpBB had successor probabilties, udpate the probabilities to
  // reflect the ccmp-conversion.
  if (Head->hasSuccessorProbabilities() && CmpBB->hasSuccessorProbabilities()) {

    // Head is allowed two successors. We've removed CmpBB, so the remaining
    // successor is Tail. We need to increase the successor probability for
    // Tail to account for the CmpBB path we removed.
    //
    // Pr(Tail|Head) += Pr(CmpBB|Head) * Pr(Tail|CmpBB).
    assert(*Head->succ_begin() == Tail && "Head successor is not Tail");
    BranchProbability Head2Tail = MBPI->getEdgeProbability(Head, Tail);
    Head->setSuccProbability(Head->succ_begin(),
                             Head2Tail + Head2CmpBB * CmpBB2Tail);

    // We will transfer successors of CmpBB to Head in a moment without
    // normalizing the successor probabilities. Set the successor probabilites
    // before doing so.
    //
    // Pr(I|Head) = Pr(CmpBB|Head) * Pr(I|CmpBB).
    for (auto I = CmpBB->succ_begin(), E = CmpBB->succ_end(); I != E; ++I) {
      BranchProbability CmpBB2I = MBPI->getEdgeProbability(CmpBB, *I);
      CmpBB->setSuccProbability(I, Head2CmpBB * CmpBB2I);
    }
  }

  Head->transferSuccessorsAndUpdatePHIs(CmpBB);
  DebugLoc TermDL = Head->getFirstTerminator()->getDebugLoc();
  TII->removeBranch(*Head);

  // If the Head terminator was one of the cbz / tbz branches with built-in
  // compare, we need to insert an explicit compare instruction in its place.
  if (HeadCond[0].getImm() == -1) {
    ++NumCompBranches;
    unsigned Opc = 0;
    switch (HeadCond[1].getImm()) {
    case AArch64::CBZW:
    case AArch64::CBNZW:
      Opc = AArch64::SUBSWri;
      break;
    case AArch64::CBZX:
    case AArch64::CBNZX:
      Opc = AArch64::SUBSXri;
      break;
    default:
      llvm_unreachable("Cannot convert Head branch");
    }
    const MCInstrDesc &MCID = TII->get(Opc);
    // Create a dummy virtual register for the SUBS def.
    Register DestReg =
        MRI->createVirtualRegister(TII->getRegClass(MCID, 0, TRI, *MF));
    // Insert a SUBS Rn, #0 instruction instead of the cbz / cbnz.
    BuildMI(*Head, Head->end(), TermDL, MCID)
        .addReg(DestReg, RegState::Define | RegState::Dead)
        .add(HeadCond[2])
        .addImm(0)
        .addImm(0);
    // SUBS uses the GPR*sp register classes.
    MRI->constrainRegClass(HeadCond[2].getReg(),
                           TII->getRegClass(MCID, 1, TRI, *MF));
  }

  Head->splice(Head->end(), CmpBB, CmpBB->begin(), CmpBB->end());

  // Now replace CmpMI with a ccmp instruction that also considers the incoming
  // flags.
  unsigned Opc = 0;
  unsigned FirstOp = 1;   // First CmpMI operand to copy.
  bool isZBranch = false; // CmpMI is a cbz/cbnz instruction.
  switch (CmpMI->getOpcode()) {
  default:
    llvm_unreachable("Unknown compare opcode");
  case AArch64::SUBSWri:    Opc = AArch64::CCMPWi; break;
  case AArch64::SUBSWrr:    Opc = AArch64::CCMPWr; break;
  case AArch64::SUBSXri:    Opc = AArch64::CCMPXi; break;
  case AArch64::SUBSXrr:    Opc = AArch64::CCMPXr; break;
  case AArch64::ADDSWri:    Opc = AArch64::CCMNWi; break;
  case AArch64::ADDSWrr:    Opc = AArch64::CCMNWr; break;
  case AArch64::ADDSXri:    Opc = AArch64::CCMNXi; break;
  case AArch64::ADDSXrr:    Opc = AArch64::CCMNXr; break;
  case AArch64::FCMPSrr:    Opc = AArch64::FCCMPSrr; FirstOp = 0; break;
  case AArch64::FCMPDrr:    Opc = AArch64::FCCMPDrr; FirstOp = 0; break;
  case AArch64::FCMPESrr:   Opc = AArch64::FCCMPESrr; FirstOp = 0; break;
  case AArch64::FCMPEDrr:   Opc = AArch64::FCCMPEDrr; FirstOp = 0; break;
  case AArch64::CBZW:
  case AArch64::CBNZW:
    Opc = AArch64::CCMPWi;
    FirstOp = 0;
    isZBranch = true;
    break;
  case AArch64::CBZX:
  case AArch64::CBNZX:
    Opc = AArch64::CCMPXi;
    FirstOp = 0;
    isZBranch = true;
    break;
  }

  // The ccmp instruction should set the flags according to the comparison when
  // Head would have branched to CmpBB.
  // The NZCV immediate operand should provide flags for the case where Head
  // would have branched to Tail. These flags should cause the new Head
  // terminator to branch to tail.
  unsigned NZCV = AArch64CC::getNZCVToSatisfyCondCode(CmpBBTailCC);
  const MCInstrDesc &MCID = TII->get(Opc);
  MRI->constrainRegClass(CmpMI->getOperand(FirstOp).getReg(),
                         TII->getRegClass(MCID, 0, TRI, *MF));
  if (CmpMI->getOperand(FirstOp + 1).isReg())
    MRI->constrainRegClass(CmpMI->getOperand(FirstOp + 1).getReg(),
                           TII->getRegClass(MCID, 1, TRI, *MF));
  MachineInstrBuilder MIB = BuildMI(*Head, CmpMI, CmpMI->getDebugLoc(), MCID)
                                .add(CmpMI->getOperand(FirstOp)); // Register Rn
  if (isZBranch)
    MIB.addImm(0); // cbz/cbnz Rn -> ccmp Rn, #0
  else
    MIB.add(CmpMI->getOperand(FirstOp + 1)); // Register Rm / Immediate
  MIB.addImm(NZCV).addImm(HeadCmpBBCC);

  // If CmpMI was a terminator, we need a new conditional branch to replace it.
  // This now becomes a Head terminator.
  if (isZBranch) {
    bool isNZ = CmpMI->getOpcode() == AArch64::CBNZW ||
                CmpMI->getOpcode() == AArch64::CBNZX;
    BuildMI(*Head, CmpMI, CmpMI->getDebugLoc(), TII->get(AArch64::Bcc))
        .addImm(isNZ ? AArch64CC::NE : AArch64CC::EQ)
        .add(CmpMI->getOperand(1)); // Branch target.
  }
  CmpMI->eraseFromParent();
  Head->updateTerminator(CmpBB->getNextNode());

  RemovedBlocks.push_back(CmpBB);
  CmpBB->eraseFromParent();
  LLVM_DEBUG(dbgs() << "Result:\n" << *Head);
  ++NumConverted;
}

int SSACCmpConv::expectedCodeSizeDelta() const {
  int delta = 0;
  // If the Head terminator was one of the cbz / tbz branches with built-in
  // compare, we need to insert an explicit compare instruction in its place
  // plus a branch instruction.
  if (HeadCond[0].getImm() == -1) {
    switch (HeadCond[1].getImm()) {
    case AArch64::CBZW:
    case AArch64::CBNZW:
    case AArch64::CBZX:
    case AArch64::CBNZX:
      // Therefore delta += 1
      delta = 1;
      break;
    default:
      llvm_unreachable("Cannot convert Head branch");
    }
  }
  // If the Cmp terminator was one of the cbz / tbz branches with
  // built-in compare, it will be turned into a compare instruction
  // into Head, but we do not save any instruction.
  // Otherwise, we save the branch instruction.
  switch (CmpMI->getOpcode()) {
  default:
    --delta;
    break;
  case AArch64::CBZW:
  case AArch64::CBNZW:
  case AArch64::CBZX:
  case AArch64::CBNZX:
    break;
  }
  return delta;
}

//===----------------------------------------------------------------------===//
//                       AArch64ConditionalCompares Pass
//===----------------------------------------------------------------------===//

namespace {
class AArch64ConditionalCompares : public MachineFunctionPass {
  const MachineBranchProbabilityInfo *MBPI;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MCSchedModel SchedModel;
  // Does the proceeded function has Oz attribute.
  bool MinSize;
  MachineRegisterInfo *MRI;
  MachineDominatorTree *DomTree;
  MachineLoopInfo *Loops;
  MachineTraceMetrics *Traces;
  MachineTraceMetrics::Ensemble *MinInstr;
  SSACCmpConv CmpConv;

public:
  static char ID;
  AArch64ConditionalCompares() : MachineFunctionPass(ID) {
    initializeAArch64ConditionalComparesPass(*PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "AArch64 Conditional Compares";
  }

private:
  bool tryConvert(MachineBasicBlock *);
  void updateDomTree(ArrayRef<MachineBasicBlock *> Removed);
  void updateLoops(ArrayRef<MachineBasicBlock *> Removed);
  void invalidateTraces();
  bool shouldConvert();
};
} // end anonymous namespace

char AArch64ConditionalCompares::ID = 0;

INITIALIZE_PASS_BEGIN(AArch64ConditionalCompares, "aarch64-ccmp",
                      "AArch64 CCMP Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineTraceMetrics)
INITIALIZE_PASS_END(AArch64ConditionalCompares, "aarch64-ccmp",
                    "AArch64 CCMP Pass", false, false)

FunctionPass *llvm::createAArch64ConditionalCompares() {
  return new AArch64ConditionalCompares();
}

void AArch64ConditionalCompares::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addRequired<MachineTraceMetrics>();
  AU.addPreserved<MachineTraceMetrics>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

/// Update the dominator tree after if-conversion erased some blocks.
void AArch64ConditionalCompares::updateDomTree(
    ArrayRef<MachineBasicBlock *> Removed) {
  // convert() removes CmpBB which was previously dominated by Head.
  // CmpBB children should be transferred to Head.
  MachineDomTreeNode *HeadNode = DomTree->getNode(CmpConv.Head);
  for (MachineBasicBlock *RemovedMBB : Removed) {
    MachineDomTreeNode *Node = DomTree->getNode(RemovedMBB);
    assert(Node != HeadNode && "Cannot erase the head node");
    assert(Node->getIDom() == HeadNode && "CmpBB should be dominated by Head");
    while (Node->getNumChildren())
      DomTree->changeImmediateDominator(Node->back(), HeadNode);
    DomTree->eraseNode(RemovedMBB);
  }
}

/// Update LoopInfo after if-conversion.
void
AArch64ConditionalCompares::updateLoops(ArrayRef<MachineBasicBlock *> Removed) {
  if (!Loops)
    return;
  for (MachineBasicBlock *RemovedMBB : Removed)
    Loops->removeBlock(RemovedMBB);
}

/// Invalidate MachineTraceMetrics before if-conversion.
void AArch64ConditionalCompares::invalidateTraces() {
  Traces->invalidate(CmpConv.Head);
  Traces->invalidate(CmpConv.CmpBB);
}

/// Apply cost model and heuristics to the if-conversion in IfConv.
/// Return true if the conversion is a good idea.
///
bool AArch64ConditionalCompares::shouldConvert() {
  // Stress testing mode disables all cost considerations.
  if (Stress)
    return true;
  if (!MinInstr)
    MinInstr = Traces->getEnsemble(MachineTraceStrategy::TS_MinInstrCount);

  // Head dominates CmpBB, so it is always included in its trace.
  MachineTraceMetrics::Trace Trace = MinInstr->getTrace(CmpConv.CmpBB);

  // If code size is the main concern
  if (MinSize) {
    int CodeSizeDelta = CmpConv.expectedCodeSizeDelta();
    LLVM_DEBUG(dbgs() << "Code size delta:  " << CodeSizeDelta << '\n');
    // If we are minimizing the code size, do the conversion whatever
    // the cost is.
    if (CodeSizeDelta < 0)
      return true;
    if (CodeSizeDelta > 0) {
      LLVM_DEBUG(dbgs() << "Code size is increasing, give up on this one.\n");
      return false;
    }
    // CodeSizeDelta == 0, continue with the regular heuristics
  }

  // Heuristic: The compare conversion delays the execution of the branch
  // instruction because we must wait for the inputs to the second compare as
  // well. The branch has no dependent instructions, but delaying it increases
  // the cost of a misprediction.
  //
  // Set a limit on the delay we will accept.
  unsigned DelayLimit = SchedModel.MispredictPenalty * 3 / 4;

  // Instruction depths can be computed for all trace instructions above CmpBB.
  unsigned HeadDepth =
      Trace.getInstrCycles(*CmpConv.Head->getFirstTerminator()).Depth;
  unsigned CmpBBDepth =
      Trace.getInstrCycles(*CmpConv.CmpBB->getFirstTerminator()).Depth;
  LLVM_DEBUG(dbgs() << "Head depth:  " << HeadDepth
                    << "\nCmpBB depth: " << CmpBBDepth << '\n');
  if (CmpBBDepth > HeadDepth + DelayLimit) {
    LLVM_DEBUG(dbgs() << "Branch delay would be larger than " << DelayLimit
                      << " cycles.\n");
    return false;
  }

  // Check the resource depth at the bottom of CmpBB - these instructions will
  // be speculated.
  unsigned ResDepth = Trace.getResourceDepth(true);
  LLVM_DEBUG(dbgs() << "Resources:   " << ResDepth << '\n');

  // Heuristic: The speculatively executed instructions must all be able to
  // merge into the Head block. The Head critical path should dominate the
  // resource cost of the speculated instructions.
  if (ResDepth > HeadDepth) {
    LLVM_DEBUG(dbgs() << "Too many instructions to speculate.\n");
    return false;
  }
  return true;
}

bool AArch64ConditionalCompares::tryConvert(MachineBasicBlock *MBB) {
  bool Changed = false;
  while (CmpConv.canConvert(MBB) && shouldConvert()) {
    invalidateTraces();
    SmallVector<MachineBasicBlock *, 4> RemovedBlocks;
    CmpConv.convert(RemovedBlocks);
    Changed = true;
    updateDomTree(RemovedBlocks);
    updateLoops(RemovedBlocks);
  }
  return Changed;
}

bool AArch64ConditionalCompares::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** AArch64 Conditional Compares **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  if (skipFunction(MF.getFunction()))
    return false;

  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  SchedModel = MF.getSubtarget().getSchedModel();
  MRI = &MF.getRegInfo();
  DomTree = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  Loops = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  Traces = &getAnalysis<MachineTraceMetrics>();
  MinInstr = nullptr;
  MinSize = MF.getFunction().hasMinSize();

  bool Changed = false;
  CmpConv.runOnMachineFunction(MF, MBPI);

  // Visit blocks in dominator tree pre-order. The pre-order enables multiple
  // cmp-conversions from the same head block.
  // Note that updateDomTree() modifies the children of the DomTree node
  // currently being visited. The df_iterator supports that; it doesn't look at
  // child_begin() / child_end() until after a node has been visited.
  for (auto *I : depth_first(DomTree))
    if (tryConvert(I->getBlock()))
      Changed = true;

  return Changed;
}
