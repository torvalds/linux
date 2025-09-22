//===- BranchFolding.cpp - Fold machine code branch instructions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass forwards branches to unconditional branches to make them branch
// directly to the target block.  This pass often results in dead MBB's, which
// it then removes.
//
// Note that this pass must be run after register allocation, it cannot handle
// SSA form. It also must handle virtual registers for targets that emit virtual
// ISA (e.g. NVPTX).
//
//===----------------------------------------------------------------------===//

#include "BranchFolding.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MBFIWrapper.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <numeric>

using namespace llvm;

#define DEBUG_TYPE "branch-folder"

STATISTIC(NumDeadBlocks, "Number of dead blocks removed");
STATISTIC(NumBranchOpts, "Number of branches optimized");
STATISTIC(NumTailMerge , "Number of block tails merged");
STATISTIC(NumHoist     , "Number of times common instructions are hoisted");
STATISTIC(NumTailCalls,  "Number of tail calls optimized");

static cl::opt<cl::boolOrDefault> FlagEnableTailMerge("enable-tail-merge",
                              cl::init(cl::BOU_UNSET), cl::Hidden);

// Throttle for huge numbers of predecessors (compile speed problems)
static cl::opt<unsigned>
TailMergeThreshold("tail-merge-threshold",
          cl::desc("Max number of predecessors to consider tail merging"),
          cl::init(150), cl::Hidden);

// Heuristic for tail merging (and, inversely, tail duplication).
static cl::opt<unsigned>
TailMergeSize("tail-merge-size",
              cl::desc("Min number of instructions to consider tail merging"),
              cl::init(3), cl::Hidden);

namespace {

  /// BranchFolderPass - Wrap branch folder in a machine function pass.
  class BranchFolderPass : public MachineFunctionPass {
  public:
    static char ID;

    explicit BranchFolderPass(): MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
      AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
      AU.addRequired<ProfileSummaryInfoWrapperPass>();
      AU.addRequired<TargetPassConfig>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoPHIs);
    }
  };

} // end anonymous namespace

char BranchFolderPass::ID = 0;

char &llvm::BranchFolderPassID = BranchFolderPass::ID;

INITIALIZE_PASS(BranchFolderPass, DEBUG_TYPE,
                "Control Flow Optimizer", false, false)

bool BranchFolderPass::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TargetPassConfig *PassConfig = &getAnalysis<TargetPassConfig>();
  // TailMerge can create jump into if branches that make CFG irreducible for
  // HW that requires structurized CFG.
  bool EnableTailMerge = !MF.getTarget().requiresStructuredCFG() &&
                         PassConfig->getEnableTailMerge();
  MBFIWrapper MBBFreqInfo(
      getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI());
  BranchFolder Folder(
      EnableTailMerge, /*CommonHoist=*/true, MBBFreqInfo,
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI(),
      &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI());
  return Folder.OptimizeFunction(MF, MF.getSubtarget().getInstrInfo(),
                                 MF.getSubtarget().getRegisterInfo());
}

BranchFolder::BranchFolder(bool DefaultEnableTailMerge, bool CommonHoist,
                           MBFIWrapper &FreqInfo,
                           const MachineBranchProbabilityInfo &ProbInfo,
                           ProfileSummaryInfo *PSI, unsigned MinTailLength)
    : EnableHoistCommonCode(CommonHoist), MinCommonTailLength(MinTailLength),
      MBBFreqInfo(FreqInfo), MBPI(ProbInfo), PSI(PSI) {
  switch (FlagEnableTailMerge) {
  case cl::BOU_UNSET:
    EnableTailMerge = DefaultEnableTailMerge;
    break;
  case cl::BOU_TRUE: EnableTailMerge = true; break;
  case cl::BOU_FALSE: EnableTailMerge = false; break;
  }
}

void BranchFolder::RemoveDeadBlock(MachineBasicBlock *MBB) {
  assert(MBB->pred_empty() && "MBB must be dead!");
  LLVM_DEBUG(dbgs() << "\nRemoving MBB: " << *MBB);

  MachineFunction *MF = MBB->getParent();
  // drop all successors.
  while (!MBB->succ_empty())
    MBB->removeSuccessor(MBB->succ_end()-1);

  // Avoid matching if this pointer gets reused.
  TriedMerging.erase(MBB);

  // Update call site info.
  for (const MachineInstr &MI : *MBB)
    if (MI.shouldUpdateCallSiteInfo())
      MF->eraseCallSiteInfo(&MI);

  // Remove the block.
  MF->erase(MBB);
  EHScopeMembership.erase(MBB);
  if (MLI)
    MLI->removeBlock(MBB);
}

bool BranchFolder::OptimizeFunction(MachineFunction &MF,
                                    const TargetInstrInfo *tii,
                                    const TargetRegisterInfo *tri,
                                    MachineLoopInfo *mli, bool AfterPlacement) {
  if (!tii) return false;

  TriedMerging.clear();

  MachineRegisterInfo &MRI = MF.getRegInfo();
  AfterBlockPlacement = AfterPlacement;
  TII = tii;
  TRI = tri;
  MLI = mli;
  this->MRI = &MRI;

  if (MinCommonTailLength == 0) {
    MinCommonTailLength = TailMergeSize.getNumOccurrences() > 0
                              ? TailMergeSize
                              : TII->getTailMergeSize(MF);
  }

  UpdateLiveIns = MRI.tracksLiveness() && TRI->trackLivenessAfterRegAlloc(MF);
  if (!UpdateLiveIns)
    MRI.invalidateLiveness();

  bool MadeChange = false;

  // Recalculate EH scope membership.
  EHScopeMembership = getEHScopeMembership(MF);

  bool MadeChangeThisIteration = true;
  while (MadeChangeThisIteration) {
    MadeChangeThisIteration    = TailMergeBlocks(MF);
    // No need to clean up if tail merging does not change anything after the
    // block placement.
    if (!AfterBlockPlacement || MadeChangeThisIteration)
      MadeChangeThisIteration |= OptimizeBranches(MF);
    if (EnableHoistCommonCode)
      MadeChangeThisIteration |= HoistCommonCode(MF);
    MadeChange |= MadeChangeThisIteration;
  }

  // See if any jump tables have become dead as the code generator
  // did its thing.
  MachineJumpTableInfo *JTI = MF.getJumpTableInfo();
  if (!JTI)
    return MadeChange;

  // Walk the function to find jump tables that are live.
  BitVector JTIsLive(JTI->getJumpTables().size());
  for (const MachineBasicBlock &BB : MF) {
    for (const MachineInstr &I : BB)
      for (const MachineOperand &Op : I.operands()) {
        if (!Op.isJTI()) continue;

        // Remember that this JT is live.
        JTIsLive.set(Op.getIndex());
      }
  }

  // Finally, remove dead jump tables.  This happens when the
  // indirect jump was unreachable (and thus deleted).
  for (unsigned i = 0, e = JTIsLive.size(); i != e; ++i)
    if (!JTIsLive.test(i)) {
      JTI->RemoveJumpTable(i);
      MadeChange = true;
    }

  return MadeChange;
}

//===----------------------------------------------------------------------===//
//  Tail Merging of Blocks
//===----------------------------------------------------------------------===//

/// HashMachineInstr - Compute a hash value for MI and its operands.
static unsigned HashMachineInstr(const MachineInstr &MI) {
  unsigned Hash = MI.getOpcode();
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &Op = MI.getOperand(i);

    // Merge in bits from the operand if easy. We can't use MachineOperand's
    // hash_code here because it's not deterministic and we sort by hash value
    // later.
    unsigned OperandHash = 0;
    switch (Op.getType()) {
    case MachineOperand::MO_Register:
      OperandHash = Op.getReg();
      break;
    case MachineOperand::MO_Immediate:
      OperandHash = Op.getImm();
      break;
    case MachineOperand::MO_MachineBasicBlock:
      OperandHash = Op.getMBB()->getNumber();
      break;
    case MachineOperand::MO_FrameIndex:
    case MachineOperand::MO_ConstantPoolIndex:
    case MachineOperand::MO_JumpTableIndex:
      OperandHash = Op.getIndex();
      break;
    case MachineOperand::MO_GlobalAddress:
    case MachineOperand::MO_ExternalSymbol:
      // Global address / external symbol are too hard, don't bother, but do
      // pull in the offset.
      OperandHash = Op.getOffset();
      break;
    default:
      break;
    }

    Hash += ((OperandHash << 3) | Op.getType()) << (i & 31);
  }
  return Hash;
}

/// HashEndOfMBB - Hash the last instruction in the MBB.
static unsigned HashEndOfMBB(const MachineBasicBlock &MBB) {
  MachineBasicBlock::const_iterator I = MBB.getLastNonDebugInstr(false);
  if (I == MBB.end())
    return 0;

  return HashMachineInstr(*I);
}

/// Whether MI should be counted as an instruction when calculating common tail.
static bool countsAsInstruction(const MachineInstr &MI) {
  return !(MI.isDebugInstr() || MI.isCFIInstruction());
}

/// Iterate backwards from the given iterator \p I, towards the beginning of the
/// block. If a MI satisfying 'countsAsInstruction' is found, return an iterator
/// pointing to that MI. If no such MI is found, return the end iterator.
static MachineBasicBlock::iterator
skipBackwardPastNonInstructions(MachineBasicBlock::iterator I,
                                MachineBasicBlock *MBB) {
  while (I != MBB->begin()) {
    --I;
    if (countsAsInstruction(*I))
      return I;
  }
  return MBB->end();
}

/// Given two machine basic blocks, return the number of instructions they
/// actually have in common together at their end. If a common tail is found (at
/// least by one instruction), then iterators for the first shared instruction
/// in each block are returned as well.
///
/// Non-instructions according to countsAsInstruction are ignored.
static unsigned ComputeCommonTailLength(MachineBasicBlock *MBB1,
                                        MachineBasicBlock *MBB2,
                                        MachineBasicBlock::iterator &I1,
                                        MachineBasicBlock::iterator &I2) {
  MachineBasicBlock::iterator MBBI1 = MBB1->end();
  MachineBasicBlock::iterator MBBI2 = MBB2->end();

  unsigned TailLen = 0;
  while (true) {
    MBBI1 = skipBackwardPastNonInstructions(MBBI1, MBB1);
    MBBI2 = skipBackwardPastNonInstructions(MBBI2, MBB2);
    if (MBBI1 == MBB1->end() || MBBI2 == MBB2->end())
      break;
    if (!MBBI1->isIdenticalTo(*MBBI2) ||
        // FIXME: This check is dubious. It's used to get around a problem where
        // people incorrectly expect inline asm directives to remain in the same
        // relative order. This is untenable because normal compiler
        // optimizations (like this one) may reorder and/or merge these
        // directives.
        MBBI1->isInlineAsm()) {
      break;
    }
    if (MBBI1->getFlag(MachineInstr::NoMerge) ||
        MBBI2->getFlag(MachineInstr::NoMerge))
      break;
    ++TailLen;
    I1 = MBBI1;
    I2 = MBBI2;
  }

  return TailLen;
}

void BranchFolder::replaceTailWithBranchTo(MachineBasicBlock::iterator OldInst,
                                           MachineBasicBlock &NewDest) {
  if (UpdateLiveIns) {
    // OldInst should always point to an instruction.
    MachineBasicBlock &OldMBB = *OldInst->getParent();
    LiveRegs.clear();
    LiveRegs.addLiveOuts(OldMBB);
    // Move backward to the place where will insert the jump.
    MachineBasicBlock::iterator I = OldMBB.end();
    do {
      --I;
      LiveRegs.stepBackward(*I);
    } while (I != OldInst);

    // Merging the tails may have switched some undef operand to non-undef ones.
    // Add IMPLICIT_DEFS into OldMBB as necessary to have a definition of the
    // register.
    for (MachineBasicBlock::RegisterMaskPair P : NewDest.liveins()) {
      // We computed the liveins with computeLiveIn earlier and should only see
      // full registers:
      assert(P.LaneMask == LaneBitmask::getAll() &&
             "Can only handle full register.");
      MCPhysReg Reg = P.PhysReg;
      if (!LiveRegs.available(*MRI, Reg))
        continue;
      DebugLoc DL;
      BuildMI(OldMBB, OldInst, DL, TII->get(TargetOpcode::IMPLICIT_DEF), Reg);
    }
  }

  TII->ReplaceTailWithBranchTo(OldInst, &NewDest);
  ++NumTailMerge;
}

MachineBasicBlock *BranchFolder::SplitMBBAt(MachineBasicBlock &CurMBB,
                                            MachineBasicBlock::iterator BBI1,
                                            const BasicBlock *BB) {
  if (!TII->isLegalToSplitMBBAt(CurMBB, BBI1))
    return nullptr;

  MachineFunction &MF = *CurMBB.getParent();

  // Create the fall-through block.
  MachineFunction::iterator MBBI = CurMBB.getIterator();
  MachineBasicBlock *NewMBB = MF.CreateMachineBasicBlock(BB);
  CurMBB.getParent()->insert(++MBBI, NewMBB);

  // Move all the successors of this block to the specified block.
  NewMBB->transferSuccessors(&CurMBB);

  // Add an edge from CurMBB to NewMBB for the fall-through.
  CurMBB.addSuccessor(NewMBB);

  // Splice the code over.
  NewMBB->splice(NewMBB->end(), &CurMBB, BBI1, CurMBB.end());

  // NewMBB belongs to the same loop as CurMBB.
  if (MLI)
    if (MachineLoop *ML = MLI->getLoopFor(&CurMBB))
      ML->addBasicBlockToLoop(NewMBB, *MLI);

  // NewMBB inherits CurMBB's block frequency.
  MBBFreqInfo.setBlockFreq(NewMBB, MBBFreqInfo.getBlockFreq(&CurMBB));

  if (UpdateLiveIns)
    computeAndAddLiveIns(LiveRegs, *NewMBB);

  // Add the new block to the EH scope.
  const auto &EHScopeI = EHScopeMembership.find(&CurMBB);
  if (EHScopeI != EHScopeMembership.end()) {
    auto n = EHScopeI->second;
    EHScopeMembership[NewMBB] = n;
  }

  return NewMBB;
}

/// EstimateRuntime - Make a rough estimate for how long it will take to run
/// the specified code.
static unsigned EstimateRuntime(MachineBasicBlock::iterator I,
                                MachineBasicBlock::iterator E) {
  unsigned Time = 0;
  for (; I != E; ++I) {
    if (!countsAsInstruction(*I))
      continue;
    if (I->isCall())
      Time += 10;
    else if (I->mayLoadOrStore())
      Time += 2;
    else
      ++Time;
  }
  return Time;
}

// CurMBB needs to add an unconditional branch to SuccMBB (we removed these
// branches temporarily for tail merging).  In the case where CurMBB ends
// with a conditional branch to the next block, optimize by reversing the
// test and conditionally branching to SuccMBB instead.
static void FixTail(MachineBasicBlock *CurMBB, MachineBasicBlock *SuccBB,
                    const TargetInstrInfo *TII, const DebugLoc &BranchDL) {
  MachineFunction *MF = CurMBB->getParent();
  MachineFunction::iterator I = std::next(MachineFunction::iterator(CurMBB));
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  DebugLoc dl = CurMBB->findBranchDebugLoc();
  if (!dl)
    dl = BranchDL;
  if (I != MF->end() && !TII->analyzeBranch(*CurMBB, TBB, FBB, Cond, true)) {
    MachineBasicBlock *NextBB = &*I;
    if (TBB == NextBB && !Cond.empty() && !FBB) {
      if (!TII->reverseBranchCondition(Cond)) {
        TII->removeBranch(*CurMBB);
        TII->insertBranch(*CurMBB, SuccBB, nullptr, Cond, dl);
        return;
      }
    }
  }
  TII->insertBranch(*CurMBB, SuccBB, nullptr,
                    SmallVector<MachineOperand, 0>(), dl);
}

bool
BranchFolder::MergePotentialsElt::operator<(const MergePotentialsElt &o) const {
  if (getHash() < o.getHash())
    return true;
  if (getHash() > o.getHash())
    return false;
  if (getBlock()->getNumber() < o.getBlock()->getNumber())
    return true;
  if (getBlock()->getNumber() > o.getBlock()->getNumber())
    return false;
  return false;
}

/// CountTerminators - Count the number of terminators in the given
/// block and set I to the position of the first non-terminator, if there
/// is one, or MBB->end() otherwise.
static unsigned CountTerminators(MachineBasicBlock *MBB,
                                 MachineBasicBlock::iterator &I) {
  I = MBB->end();
  unsigned NumTerms = 0;
  while (true) {
    if (I == MBB->begin()) {
      I = MBB->end();
      break;
    }
    --I;
    if (!I->isTerminator()) break;
    ++NumTerms;
  }
  return NumTerms;
}

/// A no successor, non-return block probably ends in unreachable and is cold.
/// Also consider a block that ends in an indirect branch to be a return block,
/// since many targets use plain indirect branches to return.
static bool blockEndsInUnreachable(const MachineBasicBlock *MBB) {
  if (!MBB->succ_empty())
    return false;
  if (MBB->empty())
    return true;
  return !(MBB->back().isReturn() || MBB->back().isIndirectBranch());
}

/// ProfitableToMerge - Check if two machine basic blocks have a common tail
/// and decide if it would be profitable to merge those tails.  Return the
/// length of the common tail and iterators to the first common instruction
/// in each block.
/// MBB1, MBB2      The blocks to check
/// MinCommonTailLength  Minimum size of tail block to be merged.
/// CommonTailLen   Out parameter to record the size of the shared tail between
///                 MBB1 and MBB2
/// I1, I2          Iterator references that will be changed to point to the first
///                 instruction in the common tail shared by MBB1,MBB2
/// SuccBB          A common successor of MBB1, MBB2 which are in a canonical form
///                 relative to SuccBB
/// PredBB          The layout predecessor of SuccBB, if any.
/// EHScopeMembership  map from block to EH scope #.
/// AfterPlacement  True if we are merging blocks after layout. Stricter
///                 thresholds apply to prevent undoing tail-duplication.
static bool
ProfitableToMerge(MachineBasicBlock *MBB1, MachineBasicBlock *MBB2,
                  unsigned MinCommonTailLength, unsigned &CommonTailLen,
                  MachineBasicBlock::iterator &I1,
                  MachineBasicBlock::iterator &I2, MachineBasicBlock *SuccBB,
                  MachineBasicBlock *PredBB,
                  DenseMap<const MachineBasicBlock *, int> &EHScopeMembership,
                  bool AfterPlacement,
                  MBFIWrapper &MBBFreqInfo,
                  ProfileSummaryInfo *PSI) {
  // It is never profitable to tail-merge blocks from two different EH scopes.
  if (!EHScopeMembership.empty()) {
    auto EHScope1 = EHScopeMembership.find(MBB1);
    assert(EHScope1 != EHScopeMembership.end());
    auto EHScope2 = EHScopeMembership.find(MBB2);
    assert(EHScope2 != EHScopeMembership.end());
    if (EHScope1->second != EHScope2->second)
      return false;
  }

  CommonTailLen = ComputeCommonTailLength(MBB1, MBB2, I1, I2);
  if (CommonTailLen == 0)
    return false;
  LLVM_DEBUG(dbgs() << "Common tail length of " << printMBBReference(*MBB1)
                    << " and " << printMBBReference(*MBB2) << " is "
                    << CommonTailLen << '\n');

  // Move the iterators to the beginning of the MBB if we only got debug
  // instructions before the tail. This is to avoid splitting a block when we
  // only got debug instructions before the tail (to be invariant on -g).
  if (skipDebugInstructionsForward(MBB1->begin(), MBB1->end(), false) == I1)
    I1 = MBB1->begin();
  if (skipDebugInstructionsForward(MBB2->begin(), MBB2->end(), false) == I2)
    I2 = MBB2->begin();

  bool FullBlockTail1 = I1 == MBB1->begin();
  bool FullBlockTail2 = I2 == MBB2->begin();

  // It's almost always profitable to merge any number of non-terminator
  // instructions with the block that falls through into the common successor.
  // This is true only for a single successor. For multiple successors, we are
  // trading a conditional branch for an unconditional one.
  // TODO: Re-visit successor size for non-layout tail merging.
  if ((MBB1 == PredBB || MBB2 == PredBB) &&
      (!AfterPlacement || MBB1->succ_size() == 1)) {
    MachineBasicBlock::iterator I;
    unsigned NumTerms = CountTerminators(MBB1 == PredBB ? MBB2 : MBB1, I);
    if (CommonTailLen > NumTerms)
      return true;
  }

  // If these are identical non-return blocks with no successors, merge them.
  // Such blocks are typically cold calls to noreturn functions like abort, and
  // are unlikely to become a fallthrough target after machine block placement.
  // Tail merging these blocks is unlikely to create additional unconditional
  // branches, and will reduce the size of this cold code.
  if (FullBlockTail1 && FullBlockTail2 &&
      blockEndsInUnreachable(MBB1) && blockEndsInUnreachable(MBB2))
    return true;

  // If one of the blocks can be completely merged and happens to be in
  // a position where the other could fall through into it, merge any number
  // of instructions, because it can be done without a branch.
  // TODO: If the blocks are not adjacent, move one of them so that they are?
  if (MBB1->isLayoutSuccessor(MBB2) && FullBlockTail2)
    return true;
  if (MBB2->isLayoutSuccessor(MBB1) && FullBlockTail1)
    return true;

  // If both blocks are identical and end in a branch, merge them unless they
  // both have a fallthrough predecessor and successor.
  // We can only do this after block placement because it depends on whether
  // there are fallthroughs, and we don't know until after layout.
  if (AfterPlacement && FullBlockTail1 && FullBlockTail2) {
    auto BothFallThrough = [](MachineBasicBlock *MBB) {
      if (!MBB->succ_empty() && !MBB->canFallThrough())
        return false;
      MachineFunction::iterator I(MBB);
      MachineFunction *MF = MBB->getParent();
      return (MBB != &*MF->begin()) && std::prev(I)->canFallThrough();
    };
    if (!BothFallThrough(MBB1) || !BothFallThrough(MBB2))
      return true;
  }

  // If both blocks have an unconditional branch temporarily stripped out,
  // count that as an additional common instruction for the following
  // heuristics. This heuristic is only accurate for single-succ blocks, so to
  // make sure that during layout merging and duplicating don't crash, we check
  // for that when merging during layout.
  unsigned EffectiveTailLen = CommonTailLen;
  if (SuccBB && MBB1 != PredBB && MBB2 != PredBB &&
      (MBB1->succ_size() == 1 || !AfterPlacement) &&
      !MBB1->back().isBarrier() &&
      !MBB2->back().isBarrier())
    ++EffectiveTailLen;

  // Check if the common tail is long enough to be worthwhile.
  if (EffectiveTailLen >= MinCommonTailLength)
    return true;

  // If we are optimizing for code size, 2 instructions in common is enough if
  // we don't have to split a block.  At worst we will be introducing 1 new
  // branch instruction, which is likely to be smaller than the 2
  // instructions that would be deleted in the merge.
  MachineFunction *MF = MBB1->getParent();
  bool OptForSize =
      MF->getFunction().hasOptSize() ||
      (llvm::shouldOptimizeForSize(MBB1, PSI, &MBBFreqInfo) &&
       llvm::shouldOptimizeForSize(MBB2, PSI, &MBBFreqInfo));
  return EffectiveTailLen >= 2 && OptForSize &&
         (FullBlockTail1 || FullBlockTail2);
}

unsigned BranchFolder::ComputeSameTails(unsigned CurHash,
                                        unsigned MinCommonTailLength,
                                        MachineBasicBlock *SuccBB,
                                        MachineBasicBlock *PredBB) {
  unsigned maxCommonTailLength = 0U;
  SameTails.clear();
  MachineBasicBlock::iterator TrialBBI1, TrialBBI2;
  MPIterator HighestMPIter = std::prev(MergePotentials.end());
  for (MPIterator CurMPIter = std::prev(MergePotentials.end()),
                  B = MergePotentials.begin();
       CurMPIter != B && CurMPIter->getHash() == CurHash; --CurMPIter) {
    for (MPIterator I = std::prev(CurMPIter); I->getHash() == CurHash; --I) {
      unsigned CommonTailLen;
      if (ProfitableToMerge(CurMPIter->getBlock(), I->getBlock(),
                            MinCommonTailLength,
                            CommonTailLen, TrialBBI1, TrialBBI2,
                            SuccBB, PredBB,
                            EHScopeMembership,
                            AfterBlockPlacement, MBBFreqInfo, PSI)) {
        if (CommonTailLen > maxCommonTailLength) {
          SameTails.clear();
          maxCommonTailLength = CommonTailLen;
          HighestMPIter = CurMPIter;
          SameTails.push_back(SameTailElt(CurMPIter, TrialBBI1));
        }
        if (HighestMPIter == CurMPIter &&
            CommonTailLen == maxCommonTailLength)
          SameTails.push_back(SameTailElt(I, TrialBBI2));
      }
      if (I == B)
        break;
    }
  }
  return maxCommonTailLength;
}

void BranchFolder::RemoveBlocksWithHash(unsigned CurHash,
                                        MachineBasicBlock *SuccBB,
                                        MachineBasicBlock *PredBB,
                                        const DebugLoc &BranchDL) {
  MPIterator CurMPIter, B;
  for (CurMPIter = std::prev(MergePotentials.end()),
      B = MergePotentials.begin();
       CurMPIter->getHash() == CurHash; --CurMPIter) {
    // Put the unconditional branch back, if we need one.
    MachineBasicBlock *CurMBB = CurMPIter->getBlock();
    if (SuccBB && CurMBB != PredBB)
      FixTail(CurMBB, SuccBB, TII, BranchDL);
    if (CurMPIter == B)
      break;
  }
  if (CurMPIter->getHash() != CurHash)
    CurMPIter++;
  MergePotentials.erase(CurMPIter, MergePotentials.end());
}

bool BranchFolder::CreateCommonTailOnlyBlock(MachineBasicBlock *&PredBB,
                                             MachineBasicBlock *SuccBB,
                                             unsigned maxCommonTailLength,
                                             unsigned &commonTailIndex) {
  commonTailIndex = 0;
  unsigned TimeEstimate = ~0U;
  for (unsigned i = 0, e = SameTails.size(); i != e; ++i) {
    // Use PredBB if possible; that doesn't require a new branch.
    if (SameTails[i].getBlock() == PredBB) {
      commonTailIndex = i;
      break;
    }
    // Otherwise, make a (fairly bogus) choice based on estimate of
    // how long it will take the various blocks to execute.
    unsigned t = EstimateRuntime(SameTails[i].getBlock()->begin(),
                                 SameTails[i].getTailStartPos());
    if (t <= TimeEstimate) {
      TimeEstimate = t;
      commonTailIndex = i;
    }
  }

  MachineBasicBlock::iterator BBI =
    SameTails[commonTailIndex].getTailStartPos();
  MachineBasicBlock *MBB = SameTails[commonTailIndex].getBlock();

  LLVM_DEBUG(dbgs() << "\nSplitting " << printMBBReference(*MBB) << ", size "
                    << maxCommonTailLength);

  // If the split block unconditionally falls-thru to SuccBB, it will be
  // merged. In control flow terms it should then take SuccBB's name. e.g. If
  // SuccBB is an inner loop, the common tail is still part of the inner loop.
  const BasicBlock *BB = (SuccBB && MBB->succ_size() == 1) ?
    SuccBB->getBasicBlock() : MBB->getBasicBlock();
  MachineBasicBlock *newMBB = SplitMBBAt(*MBB, BBI, BB);
  if (!newMBB) {
    LLVM_DEBUG(dbgs() << "... failed!");
    return false;
  }

  SameTails[commonTailIndex].setBlock(newMBB);
  SameTails[commonTailIndex].setTailStartPos(newMBB->begin());

  // If we split PredBB, newMBB is the new predecessor.
  if (PredBB == MBB)
    PredBB = newMBB;

  return true;
}

static void
mergeOperations(MachineBasicBlock::iterator MBBIStartPos,
                MachineBasicBlock &MBBCommon) {
  MachineBasicBlock *MBB = MBBIStartPos->getParent();
  // Note CommonTailLen does not necessarily matches the size of
  // the common BB nor all its instructions because of debug
  // instructions differences.
  unsigned CommonTailLen = 0;
  for (auto E = MBB->end(); MBBIStartPos != E; ++MBBIStartPos)
    ++CommonTailLen;

  MachineBasicBlock::reverse_iterator MBBI = MBB->rbegin();
  MachineBasicBlock::reverse_iterator MBBIE = MBB->rend();
  MachineBasicBlock::reverse_iterator MBBICommon = MBBCommon.rbegin();
  MachineBasicBlock::reverse_iterator MBBIECommon = MBBCommon.rend();

  while (CommonTailLen--) {
    assert(MBBI != MBBIE && "Reached BB end within common tail length!");
    (void)MBBIE;

    if (!countsAsInstruction(*MBBI)) {
      ++MBBI;
      continue;
    }

    while ((MBBICommon != MBBIECommon) && !countsAsInstruction(*MBBICommon))
      ++MBBICommon;

    assert(MBBICommon != MBBIECommon &&
           "Reached BB end within common tail length!");
    assert(MBBICommon->isIdenticalTo(*MBBI) && "Expected matching MIIs!");

    // Merge MMOs from memory operations in the common block.
    if (MBBICommon->mayLoadOrStore())
      MBBICommon->cloneMergedMemRefs(*MBB->getParent(), {&*MBBICommon, &*MBBI});
    // Drop undef flags if they aren't present in all merged instructions.
    for (unsigned I = 0, E = MBBICommon->getNumOperands(); I != E; ++I) {
      MachineOperand &MO = MBBICommon->getOperand(I);
      if (MO.isReg() && MO.isUndef()) {
        const MachineOperand &OtherMO = MBBI->getOperand(I);
        if (!OtherMO.isUndef())
          MO.setIsUndef(false);
      }
    }

    ++MBBI;
    ++MBBICommon;
  }
}

void BranchFolder::mergeCommonTails(unsigned commonTailIndex) {
  MachineBasicBlock *MBB = SameTails[commonTailIndex].getBlock();

  std::vector<MachineBasicBlock::iterator> NextCommonInsts(SameTails.size());
  for (unsigned int i = 0 ; i != SameTails.size() ; ++i) {
    if (i != commonTailIndex) {
      NextCommonInsts[i] = SameTails[i].getTailStartPos();
      mergeOperations(SameTails[i].getTailStartPos(), *MBB);
    } else {
      assert(SameTails[i].getTailStartPos() == MBB->begin() &&
          "MBB is not a common tail only block");
    }
  }

  for (auto &MI : *MBB) {
    if (!countsAsInstruction(MI))
      continue;
    DebugLoc DL = MI.getDebugLoc();
    for (unsigned int i = 0 ; i < NextCommonInsts.size() ; i++) {
      if (i == commonTailIndex)
        continue;

      auto &Pos = NextCommonInsts[i];
      assert(Pos != SameTails[i].getBlock()->end() &&
          "Reached BB end within common tail");
      while (!countsAsInstruction(*Pos)) {
        ++Pos;
        assert(Pos != SameTails[i].getBlock()->end() &&
            "Reached BB end within common tail");
      }
      assert(MI.isIdenticalTo(*Pos) && "Expected matching MIIs!");
      DL = DILocation::getMergedLocation(DL, Pos->getDebugLoc());
      NextCommonInsts[i] = ++Pos;
    }
    MI.setDebugLoc(DL);
  }

  if (UpdateLiveIns) {
    LivePhysRegs NewLiveIns(*TRI);
    computeLiveIns(NewLiveIns, *MBB);
    LiveRegs.init(*TRI);

    // The flag merging may lead to some register uses no longer using the
    // <undef> flag, add IMPLICIT_DEFs in the predecessors as necessary.
    for (MachineBasicBlock *Pred : MBB->predecessors()) {
      LiveRegs.clear();
      LiveRegs.addLiveOuts(*Pred);
      MachineBasicBlock::iterator InsertBefore = Pred->getFirstTerminator();
      for (Register Reg : NewLiveIns) {
        if (!LiveRegs.available(*MRI, Reg))
          continue;

        // Skip the register if we are about to add one of its super registers.
        // TODO: Common this up with the same logic in addLineIns().
        if (any_of(TRI->superregs(Reg), [&](MCPhysReg SReg) {
              return NewLiveIns.contains(SReg) && !MRI->isReserved(SReg);
            }))
          continue;

        DebugLoc DL;
        BuildMI(*Pred, InsertBefore, DL, TII->get(TargetOpcode::IMPLICIT_DEF),
                Reg);
      }
    }

    MBB->clearLiveIns();
    addLiveIns(*MBB, NewLiveIns);
  }
}

// See if any of the blocks in MergePotentials (which all have SuccBB as a
// successor, or all have no successor if it is null) can be tail-merged.
// If there is a successor, any blocks in MergePotentials that are not
// tail-merged and are not immediately before Succ must have an unconditional
// branch to Succ added (but the predecessor/successor lists need no
// adjustment). The lone predecessor of Succ that falls through into Succ,
// if any, is given in PredBB.
// MinCommonTailLength - Except for the special cases below, tail-merge if
// there are at least this many instructions in common.
bool BranchFolder::TryTailMergeBlocks(MachineBasicBlock *SuccBB,
                                      MachineBasicBlock *PredBB,
                                      unsigned MinCommonTailLength) {
  bool MadeChange = false;

  LLVM_DEBUG(
      dbgs() << "\nTryTailMergeBlocks: ";
      for (unsigned i = 0, e = MergePotentials.size(); i != e; ++i) dbgs()
      << printMBBReference(*MergePotentials[i].getBlock())
      << (i == e - 1 ? "" : ", ");
      dbgs() << "\n"; if (SuccBB) {
        dbgs() << "  with successor " << printMBBReference(*SuccBB) << '\n';
        if (PredBB)
          dbgs() << "  which has fall-through from "
                 << printMBBReference(*PredBB) << "\n";
      } dbgs() << "Looking for common tails of at least "
               << MinCommonTailLength << " instruction"
               << (MinCommonTailLength == 1 ? "" : "s") << '\n';);

  // Sort by hash value so that blocks with identical end sequences sort
  // together.
  array_pod_sort(MergePotentials.begin(), MergePotentials.end());

  // Walk through equivalence sets looking for actual exact matches.
  while (MergePotentials.size() > 1) {
    unsigned CurHash = MergePotentials.back().getHash();
    const DebugLoc &BranchDL = MergePotentials.back().getBranchDebugLoc();

    // Build SameTails, identifying the set of blocks with this hash code
    // and with the maximum number of instructions in common.
    unsigned maxCommonTailLength = ComputeSameTails(CurHash,
                                                    MinCommonTailLength,
                                                    SuccBB, PredBB);

    // If we didn't find any pair that has at least MinCommonTailLength
    // instructions in common, remove all blocks with this hash code and retry.
    if (SameTails.empty()) {
      RemoveBlocksWithHash(CurHash, SuccBB, PredBB, BranchDL);
      continue;
    }

    // If one of the blocks is the entire common tail (and is not the entry
    // block/an EH pad, which we can't jump to), we can treat all blocks with
    // this same tail at once.  Use PredBB if that is one of the possibilities,
    // as that will not introduce any extra branches.
    MachineBasicBlock *EntryBB =
        &MergePotentials.front().getBlock()->getParent()->front();
    unsigned commonTailIndex = SameTails.size();
    // If there are two blocks, check to see if one can be made to fall through
    // into the other.
    if (SameTails.size() == 2 &&
        SameTails[0].getBlock()->isLayoutSuccessor(SameTails[1].getBlock()) &&
        SameTails[1].tailIsWholeBlock() && !SameTails[1].getBlock()->isEHPad())
      commonTailIndex = 1;
    else if (SameTails.size() == 2 &&
             SameTails[1].getBlock()->isLayoutSuccessor(
                 SameTails[0].getBlock()) &&
             SameTails[0].tailIsWholeBlock() &&
             !SameTails[0].getBlock()->isEHPad())
      commonTailIndex = 0;
    else {
      // Otherwise just pick one, favoring the fall-through predecessor if
      // there is one.
      for (unsigned i = 0, e = SameTails.size(); i != e; ++i) {
        MachineBasicBlock *MBB = SameTails[i].getBlock();
        if ((MBB == EntryBB || MBB->isEHPad()) &&
            SameTails[i].tailIsWholeBlock())
          continue;
        if (MBB == PredBB) {
          commonTailIndex = i;
          break;
        }
        if (SameTails[i].tailIsWholeBlock())
          commonTailIndex = i;
      }
    }

    if (commonTailIndex == SameTails.size() ||
        (SameTails[commonTailIndex].getBlock() == PredBB &&
         !SameTails[commonTailIndex].tailIsWholeBlock())) {
      // None of the blocks consist entirely of the common tail.
      // Split a block so that one does.
      if (!CreateCommonTailOnlyBlock(PredBB, SuccBB,
                                     maxCommonTailLength, commonTailIndex)) {
        RemoveBlocksWithHash(CurHash, SuccBB, PredBB, BranchDL);
        continue;
      }
    }

    MachineBasicBlock *MBB = SameTails[commonTailIndex].getBlock();

    // Recompute common tail MBB's edge weights and block frequency.
    setCommonTailEdgeWeights(*MBB);

    // Merge debug locations, MMOs and undef flags across identical instructions
    // for common tail.
    mergeCommonTails(commonTailIndex);

    // MBB is common tail.  Adjust all other BB's to jump to this one.
    // Traversal must be forwards so erases work.
    LLVM_DEBUG(dbgs() << "\nUsing common tail in " << printMBBReference(*MBB)
                      << " for ");
    for (unsigned int i=0, e = SameTails.size(); i != e; ++i) {
      if (commonTailIndex == i)
        continue;
      LLVM_DEBUG(dbgs() << printMBBReference(*SameTails[i].getBlock())
                        << (i == e - 1 ? "" : ", "));
      // Hack the end off BB i, making it jump to BB commonTailIndex instead.
      replaceTailWithBranchTo(SameTails[i].getTailStartPos(), *MBB);
      // BB i is no longer a predecessor of SuccBB; remove it from the worklist.
      MergePotentials.erase(SameTails[i].getMPIter());
    }
    LLVM_DEBUG(dbgs() << "\n");
    // We leave commonTailIndex in the worklist in case there are other blocks
    // that match it with a smaller number of instructions.
    MadeChange = true;
  }
  return MadeChange;
}

bool BranchFolder::TailMergeBlocks(MachineFunction &MF) {
  bool MadeChange = false;
  if (!EnableTailMerge)
    return MadeChange;

  // First find blocks with no successors.
  // Block placement may create new tail merging opportunities for these blocks.
  MergePotentials.clear();
  for (MachineBasicBlock &MBB : MF) {
    if (MergePotentials.size() == TailMergeThreshold)
      break;
    if (!TriedMerging.count(&MBB) && MBB.succ_empty())
      MergePotentials.push_back(MergePotentialsElt(HashEndOfMBB(MBB), &MBB,
                                                   MBB.findBranchDebugLoc()));
  }

  // If this is a large problem, avoid visiting the same basic blocks
  // multiple times.
  if (MergePotentials.size() == TailMergeThreshold)
    for (const MergePotentialsElt &Elt : MergePotentials)
      TriedMerging.insert(Elt.getBlock());

  // See if we can do any tail merging on those.
  if (MergePotentials.size() >= 2)
    MadeChange |= TryTailMergeBlocks(nullptr, nullptr, MinCommonTailLength);

  // Look at blocks (IBB) with multiple predecessors (PBB).
  // We change each predecessor to a canonical form, by
  // (1) temporarily removing any unconditional branch from the predecessor
  // to IBB, and
  // (2) alter conditional branches so they branch to the other block
  // not IBB; this may require adding back an unconditional branch to IBB
  // later, where there wasn't one coming in.  E.g.
  //   Bcc IBB
  //   fallthrough to QBB
  // here becomes
  //   Bncc QBB
  // with a conceptual B to IBB after that, which never actually exists.
  // With those changes, we see whether the predecessors' tails match,
  // and merge them if so.  We change things out of canonical form and
  // back to the way they were later in the process.  (OptimizeBranches
  // would undo some of this, but we can't use it, because we'd get into
  // a compile-time infinite loop repeatedly doing and undoing the same
  // transformations.)

  for (MachineFunction::iterator I = std::next(MF.begin()), E = MF.end();
       I != E; ++I) {
    if (I->pred_size() < 2) continue;
    SmallPtrSet<MachineBasicBlock *, 8> UniquePreds;
    MachineBasicBlock *IBB = &*I;
    MachineBasicBlock *PredBB = &*std::prev(I);
    MergePotentials.clear();
    MachineLoop *ML;

    // Bail if merging after placement and IBB is the loop header because
    // -- If merging predecessors that belong to the same loop as IBB, the
    // common tail of merged predecessors may become the loop top if block
    // placement is called again and the predecessors may branch to this common
    // tail and require more branches. This can be relaxed if
    // MachineBlockPlacement::findBestLoopTop is more flexible.
    // --If merging predecessors that do not belong to the same loop as IBB, the
    // loop info of IBB's loop and the other loops may be affected. Calling the
    // block placement again may make big change to the layout and eliminate the
    // reason to do tail merging here.
    if (AfterBlockPlacement && MLI) {
      ML = MLI->getLoopFor(IBB);
      if (ML && IBB == ML->getHeader())
        continue;
    }

    for (MachineBasicBlock *PBB : I->predecessors()) {
      if (MergePotentials.size() == TailMergeThreshold)
        break;

      if (TriedMerging.count(PBB))
        continue;

      // Skip blocks that loop to themselves, can't tail merge these.
      if (PBB == IBB)
        continue;

      // Visit each predecessor only once.
      if (!UniquePreds.insert(PBB).second)
        continue;

      // Skip blocks which may jump to a landing pad or jump from an asm blob.
      // Can't tail merge these.
      if (PBB->hasEHPadSuccessor() || PBB->mayHaveInlineAsmBr())
        continue;

      // After block placement, only consider predecessors that belong to the
      // same loop as IBB.  The reason is the same as above when skipping loop
      // header.
      if (AfterBlockPlacement && MLI)
        if (ML != MLI->getLoopFor(PBB))
          continue;

      MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
      SmallVector<MachineOperand, 4> Cond;
      if (!TII->analyzeBranch(*PBB, TBB, FBB, Cond, true)) {
        // Failing case: IBB is the target of a cbr, and we cannot reverse the
        // branch.
        SmallVector<MachineOperand, 4> NewCond(Cond);
        if (!Cond.empty() && TBB == IBB) {
          if (TII->reverseBranchCondition(NewCond))
            continue;
          // This is the QBB case described above
          if (!FBB) {
            auto Next = ++PBB->getIterator();
            if (Next != MF.end())
              FBB = &*Next;
          }
        }

        // Remove the unconditional branch at the end, if any.
        DebugLoc dl = PBB->findBranchDebugLoc();
        if (TBB && (Cond.empty() || FBB)) {
          TII->removeBranch(*PBB);
          if (!Cond.empty())
            // reinsert conditional branch only, for now
            TII->insertBranch(*PBB, (TBB == IBB) ? FBB : TBB, nullptr,
                              NewCond, dl);
        }

        MergePotentials.push_back(
            MergePotentialsElt(HashEndOfMBB(*PBB), PBB, dl));
      }
    }

    // If this is a large problem, avoid visiting the same basic blocks multiple
    // times.
    if (MergePotentials.size() == TailMergeThreshold)
      for (MergePotentialsElt &Elt : MergePotentials)
        TriedMerging.insert(Elt.getBlock());

    if (MergePotentials.size() >= 2)
      MadeChange |= TryTailMergeBlocks(IBB, PredBB, MinCommonTailLength);

    // Reinsert an unconditional branch if needed. The 1 below can occur as a
    // result of removing blocks in TryTailMergeBlocks.
    PredBB = &*std::prev(I); // this may have been changed in TryTailMergeBlocks
    if (MergePotentials.size() == 1 &&
        MergePotentials.begin()->getBlock() != PredBB)
      FixTail(MergePotentials.begin()->getBlock(), IBB, TII,
              MergePotentials.begin()->getBranchDebugLoc());
  }

  return MadeChange;
}

void BranchFolder::setCommonTailEdgeWeights(MachineBasicBlock &TailMBB) {
  SmallVector<BlockFrequency, 2> EdgeFreqLs(TailMBB.succ_size());
  BlockFrequency AccumulatedMBBFreq;

  // Aggregate edge frequency of successor edge j:
  //  edgeFreq(j) = sum (freq(bb) * edgeProb(bb, j)),
  //  where bb is a basic block that is in SameTails.
  for (const auto &Src : SameTails) {
    const MachineBasicBlock *SrcMBB = Src.getBlock();
    BlockFrequency BlockFreq = MBBFreqInfo.getBlockFreq(SrcMBB);
    AccumulatedMBBFreq += BlockFreq;

    // It is not necessary to recompute edge weights if TailBB has less than two
    // successors.
    if (TailMBB.succ_size() <= 1)
      continue;

    auto EdgeFreq = EdgeFreqLs.begin();

    for (auto SuccI = TailMBB.succ_begin(), SuccE = TailMBB.succ_end();
         SuccI != SuccE; ++SuccI, ++EdgeFreq)
      *EdgeFreq += BlockFreq * MBPI.getEdgeProbability(SrcMBB, *SuccI);
  }

  MBBFreqInfo.setBlockFreq(&TailMBB, AccumulatedMBBFreq);

  if (TailMBB.succ_size() <= 1)
    return;

  auto SumEdgeFreq =
      std::accumulate(EdgeFreqLs.begin(), EdgeFreqLs.end(), BlockFrequency(0))
          .getFrequency();
  auto EdgeFreq = EdgeFreqLs.begin();

  if (SumEdgeFreq > 0) {
    for (auto SuccI = TailMBB.succ_begin(), SuccE = TailMBB.succ_end();
         SuccI != SuccE; ++SuccI, ++EdgeFreq) {
      auto Prob = BranchProbability::getBranchProbability(
          EdgeFreq->getFrequency(), SumEdgeFreq);
      TailMBB.setSuccProbability(SuccI, Prob);
    }
  }
}

//===----------------------------------------------------------------------===//
//  Branch Optimization
//===----------------------------------------------------------------------===//

bool BranchFolder::OptimizeBranches(MachineFunction &MF) {
  bool MadeChange = false;

  // Make sure blocks are numbered in order
  MF.RenumberBlocks();
  // Renumbering blocks alters EH scope membership, recalculate it.
  EHScopeMembership = getEHScopeMembership(MF);

  for (MachineBasicBlock &MBB :
       llvm::make_early_inc_range(llvm::drop_begin(MF))) {
    MadeChange |= OptimizeBlock(&MBB);

    // If it is dead, remove it.
    if (MBB.pred_empty() && !MBB.isMachineBlockAddressTaken()) {
      RemoveDeadBlock(&MBB);
      MadeChange = true;
      ++NumDeadBlocks;
    }
  }

  return MadeChange;
}

// Blocks should be considered empty if they contain only debug info;
// else the debug info would affect codegen.
static bool IsEmptyBlock(MachineBasicBlock *MBB) {
  return MBB->getFirstNonDebugInstr(true) == MBB->end();
}

// Blocks with only debug info and branches should be considered the same
// as blocks with only branches.
static bool IsBranchOnlyBlock(MachineBasicBlock *MBB) {
  MachineBasicBlock::iterator I = MBB->getFirstNonDebugInstr();
  assert(I != MBB->end() && "empty block!");
  return I->isBranch();
}

/// IsBetterFallthrough - Return true if it would be clearly better to
/// fall-through to MBB1 than to fall through into MBB2.  This has to return
/// a strict ordering, returning true for both (MBB1,MBB2) and (MBB2,MBB1) will
/// result in infinite loops.
static bool IsBetterFallthrough(MachineBasicBlock *MBB1,
                                MachineBasicBlock *MBB2) {
  assert(MBB1 && MBB2 && "Unknown MachineBasicBlock");

  // Right now, we use a simple heuristic.  If MBB2 ends with a call, and
  // MBB1 doesn't, we prefer to fall through into MBB1.  This allows us to
  // optimize branches that branch to either a return block or an assert block
  // into a fallthrough to the return.
  MachineBasicBlock::iterator MBB1I = MBB1->getLastNonDebugInstr();
  MachineBasicBlock::iterator MBB2I = MBB2->getLastNonDebugInstr();
  if (MBB1I == MBB1->end() || MBB2I == MBB2->end())
    return false;

  // If there is a clear successor ordering we make sure that one block
  // will fall through to the next
  if (MBB1->isSuccessor(MBB2)) return true;
  if (MBB2->isSuccessor(MBB1)) return false;

  return MBB2I->isCall() && !MBB1I->isCall();
}

/// getBranchDebugLoc - Find and return, if any, the DebugLoc of the branch
/// instructions on the block.
static DebugLoc getBranchDebugLoc(MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I != MBB.end() && I->isBranch())
    return I->getDebugLoc();
  return DebugLoc();
}

static void copyDebugInfoToPredecessor(const TargetInstrInfo *TII,
                                       MachineBasicBlock &MBB,
                                       MachineBasicBlock &PredMBB) {
  auto InsertBefore = PredMBB.getFirstTerminator();
  for (MachineInstr &MI : MBB.instrs())
    if (MI.isDebugInstr()) {
      TII->duplicate(PredMBB, InsertBefore, MI);
      LLVM_DEBUG(dbgs() << "Copied debug entity from empty block to pred: "
                        << MI);
    }
}

static void copyDebugInfoToSuccessor(const TargetInstrInfo *TII,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock &SuccMBB) {
  auto InsertBefore = SuccMBB.SkipPHIsAndLabels(SuccMBB.begin());
  for (MachineInstr &MI : MBB.instrs())
    if (MI.isDebugInstr()) {
      TII->duplicate(SuccMBB, InsertBefore, MI);
      LLVM_DEBUG(dbgs() << "Copied debug entity from empty block to succ: "
                        << MI);
    }
}

// Try to salvage DBG_VALUE instructions from an otherwise empty block. If such
// a basic block is removed we would lose the debug information unless we have
// copied the information to a predecessor/successor.
//
// TODO: This function only handles some simple cases. An alternative would be
// to run a heavier analysis, such as the LiveDebugValues pass, before we do
// branch folding.
static void salvageDebugInfoFromEmptyBlock(const TargetInstrInfo *TII,
                                           MachineBasicBlock &MBB) {
  assert(IsEmptyBlock(&MBB) && "Expected an empty block (except debug info).");
  // If this MBB is the only predecessor of a successor it is legal to copy
  // DBG_VALUE instructions to the beginning of the successor.
  for (MachineBasicBlock *SuccBB : MBB.successors())
    if (SuccBB->pred_size() == 1)
      copyDebugInfoToSuccessor(TII, MBB, *SuccBB);
  // If this MBB is the only successor of a predecessor it is legal to copy the
  // DBG_VALUE instructions to the end of the predecessor (just before the
  // terminators, assuming that the terminator isn't affecting the DBG_VALUE).
  for (MachineBasicBlock *PredBB : MBB.predecessors())
    if (PredBB->succ_size() == 1)
      copyDebugInfoToPredecessor(TII, MBB, *PredBB);
}

bool BranchFolder::OptimizeBlock(MachineBasicBlock *MBB) {
  bool MadeChange = false;
  MachineFunction &MF = *MBB->getParent();
ReoptimizeBlock:

  MachineFunction::iterator FallThrough = MBB->getIterator();
  ++FallThrough;

  // Make sure MBB and FallThrough belong to the same EH scope.
  bool SameEHScope = true;
  if (!EHScopeMembership.empty() && FallThrough != MF.end()) {
    auto MBBEHScope = EHScopeMembership.find(MBB);
    assert(MBBEHScope != EHScopeMembership.end());
    auto FallThroughEHScope = EHScopeMembership.find(&*FallThrough);
    assert(FallThroughEHScope != EHScopeMembership.end());
    SameEHScope = MBBEHScope->second == FallThroughEHScope->second;
  }

  // Analyze the branch in the current block. As a side-effect, this may cause
  // the block to become empty.
  MachineBasicBlock *CurTBB = nullptr, *CurFBB = nullptr;
  SmallVector<MachineOperand, 4> CurCond;
  bool CurUnAnalyzable =
      TII->analyzeBranch(*MBB, CurTBB, CurFBB, CurCond, true);

  // If this block is empty, make everyone use its fall-through, not the block
  // explicitly.  Landing pads should not do this since the landing-pad table
  // points to this block.  Blocks with their addresses taken shouldn't be
  // optimized away.
  if (IsEmptyBlock(MBB) && !MBB->isEHPad() && !MBB->hasAddressTaken() &&
      SameEHScope) {
    salvageDebugInfoFromEmptyBlock(TII, *MBB);
    // Dead block?  Leave for cleanup later.
    if (MBB->pred_empty()) return MadeChange;

    if (FallThrough == MF.end()) {
      // TODO: Simplify preds to not branch here if possible!
    } else if (FallThrough->isEHPad()) {
      // Don't rewrite to a landing pad fallthough.  That could lead to the case
      // where a BB jumps to more than one landing pad.
      // TODO: Is it ever worth rewriting predecessors which don't already
      // jump to a landing pad, and so can safely jump to the fallthrough?
    } else if (MBB->isSuccessor(&*FallThrough)) {
      // Rewrite all predecessors of the old block to go to the fallthrough
      // instead.
      while (!MBB->pred_empty()) {
        MachineBasicBlock *Pred = *(MBB->pred_end()-1);
        Pred->ReplaceUsesOfBlockWith(MBB, &*FallThrough);
      }
      // Add rest successors of MBB to successors of FallThrough. Those
      // successors are not directly reachable via MBB, so it should be
      // landing-pad.
      for (auto SI = MBB->succ_begin(), SE = MBB->succ_end(); SI != SE; ++SI)
        if (*SI != &*FallThrough && !FallThrough->isSuccessor(*SI)) {
          assert((*SI)->isEHPad() && "Bad CFG");
          FallThrough->copySuccessor(MBB, SI);
        }
      // If MBB was the target of a jump table, update jump tables to go to the
      // fallthrough instead.
      if (MachineJumpTableInfo *MJTI = MF.getJumpTableInfo())
        MJTI->ReplaceMBBInJumpTables(MBB, &*FallThrough);
      MadeChange = true;
    }
    return MadeChange;
  }

  // Check to see if we can simplify the terminator of the block before this
  // one.
  MachineBasicBlock &PrevBB = *std::prev(MachineFunction::iterator(MBB));

  MachineBasicBlock *PriorTBB = nullptr, *PriorFBB = nullptr;
  SmallVector<MachineOperand, 4> PriorCond;
  bool PriorUnAnalyzable =
      TII->analyzeBranch(PrevBB, PriorTBB, PriorFBB, PriorCond, true);
  if (!PriorUnAnalyzable) {
    // If the previous branch is conditional and both conditions go to the same
    // destination, remove the branch, replacing it with an unconditional one or
    // a fall-through.
    if (PriorTBB && PriorTBB == PriorFBB) {
      DebugLoc dl = getBranchDebugLoc(PrevBB);
      TII->removeBranch(PrevBB);
      PriorCond.clear();
      if (PriorTBB != MBB)
        TII->insertBranch(PrevBB, PriorTBB, nullptr, PriorCond, dl);
      MadeChange = true;
      ++NumBranchOpts;
      goto ReoptimizeBlock;
    }

    // If the previous block unconditionally falls through to this block and
    // this block has no other predecessors, move the contents of this block
    // into the prior block. This doesn't usually happen when SimplifyCFG
    // has been used, but it can happen if tail merging splits a fall-through
    // predecessor of a block.
    // This has to check PrevBB->succ_size() because EH edges are ignored by
    // analyzeBranch.
    if (PriorCond.empty() && !PriorTBB && MBB->pred_size() == 1 &&
        PrevBB.succ_size() == 1 && PrevBB.isSuccessor(MBB) &&
        !MBB->hasAddressTaken() && !MBB->isEHPad()) {
      LLVM_DEBUG(dbgs() << "\nMerging into block: " << PrevBB
                        << "From MBB: " << *MBB);
      // Remove redundant DBG_VALUEs first.
      if (!PrevBB.empty()) {
        MachineBasicBlock::iterator PrevBBIter = PrevBB.end();
        --PrevBBIter;
        MachineBasicBlock::iterator MBBIter = MBB->begin();
        // Check if DBG_VALUE at the end of PrevBB is identical to the
        // DBG_VALUE at the beginning of MBB.
        while (PrevBBIter != PrevBB.begin() && MBBIter != MBB->end()
               && PrevBBIter->isDebugInstr() && MBBIter->isDebugInstr()) {
          if (!MBBIter->isIdenticalTo(*PrevBBIter))
            break;
          MachineInstr &DuplicateDbg = *MBBIter;
          ++MBBIter; -- PrevBBIter;
          DuplicateDbg.eraseFromParent();
        }
      }
      PrevBB.splice(PrevBB.end(), MBB, MBB->begin(), MBB->end());
      PrevBB.removeSuccessor(PrevBB.succ_begin());
      assert(PrevBB.succ_empty());
      PrevBB.transferSuccessors(MBB);
      MadeChange = true;
      return MadeChange;
    }

    // If the previous branch *only* branches to *this* block (conditional or
    // not) remove the branch.
    if (PriorTBB == MBB && !PriorFBB) {
      TII->removeBranch(PrevBB);
      MadeChange = true;
      ++NumBranchOpts;
      goto ReoptimizeBlock;
    }

    // If the prior block branches somewhere else on the condition and here if
    // the condition is false, remove the uncond second branch.
    if (PriorFBB == MBB) {
      DebugLoc dl = getBranchDebugLoc(PrevBB);
      TII->removeBranch(PrevBB);
      TII->insertBranch(PrevBB, PriorTBB, nullptr, PriorCond, dl);
      MadeChange = true;
      ++NumBranchOpts;
      goto ReoptimizeBlock;
    }

    // If the prior block branches here on true and somewhere else on false, and
    // if the branch condition is reversible, reverse the branch to create a
    // fall-through.
    if (PriorTBB == MBB) {
      SmallVector<MachineOperand, 4> NewPriorCond(PriorCond);
      if (!TII->reverseBranchCondition(NewPriorCond)) {
        DebugLoc dl = getBranchDebugLoc(PrevBB);
        TII->removeBranch(PrevBB);
        TII->insertBranch(PrevBB, PriorFBB, nullptr, NewPriorCond, dl);
        MadeChange = true;
        ++NumBranchOpts;
        goto ReoptimizeBlock;
      }
    }

    // If this block has no successors (e.g. it is a return block or ends with
    // a call to a no-return function like abort or __cxa_throw) and if the pred
    // falls through into this block, and if it would otherwise fall through
    // into the block after this, move this block to the end of the function.
    //
    // We consider it more likely that execution will stay in the function (e.g.
    // due to loops) than it is to exit it.  This asserts in loops etc, moving
    // the assert condition out of the loop body.
    if (MBB->succ_empty() && !PriorCond.empty() && !PriorFBB &&
        MachineFunction::iterator(PriorTBB) == FallThrough &&
        !MBB->canFallThrough()) {
      bool DoTransform = true;

      // We have to be careful that the succs of PredBB aren't both no-successor
      // blocks.  If neither have successors and if PredBB is the second from
      // last block in the function, we'd just keep swapping the two blocks for
      // last.  Only do the swap if one is clearly better to fall through than
      // the other.
      if (FallThrough == --MF.end() &&
          !IsBetterFallthrough(PriorTBB, MBB))
        DoTransform = false;

      if (DoTransform) {
        // Reverse the branch so we will fall through on the previous true cond.
        SmallVector<MachineOperand, 4> NewPriorCond(PriorCond);
        if (!TII->reverseBranchCondition(NewPriorCond)) {
          LLVM_DEBUG(dbgs() << "\nMoving MBB: " << *MBB
                            << "To make fallthrough to: " << *PriorTBB << "\n");

          DebugLoc dl = getBranchDebugLoc(PrevBB);
          TII->removeBranch(PrevBB);
          TII->insertBranch(PrevBB, MBB, nullptr, NewPriorCond, dl);

          // Move this block to the end of the function.
          MBB->moveAfter(&MF.back());
          MadeChange = true;
          ++NumBranchOpts;
          return MadeChange;
        }
      }
    }
  }

  if (!IsEmptyBlock(MBB)) {
    MachineInstr &TailCall = *MBB->getFirstNonDebugInstr();
    if (TII->isUnconditionalTailCall(TailCall)) {
      SmallVector<MachineBasicBlock *> PredsChanged;
      for (auto &Pred : MBB->predecessors()) {
        MachineBasicBlock *PredTBB = nullptr, *PredFBB = nullptr;
        SmallVector<MachineOperand, 4> PredCond;
        bool PredAnalyzable =
            !TII->analyzeBranch(*Pred, PredTBB, PredFBB, PredCond, true);

        // Only eliminate if MBB == TBB (Taken Basic Block)
        if (PredAnalyzable && !PredCond.empty() && PredTBB == MBB &&
            PredTBB != PredFBB) {
          // The predecessor has a conditional branch to this block which
          // consists of only a tail call. Try to fold the tail call into the
          // conditional branch.
          if (TII->canMakeTailCallConditional(PredCond, TailCall)) {
            // TODO: It would be nice if analyzeBranch() could provide a pointer
            // to the branch instruction so replaceBranchWithTailCall() doesn't
            // have to search for it.
            TII->replaceBranchWithTailCall(*Pred, PredCond, TailCall);
            PredsChanged.push_back(Pred);
          }
        }
        // If the predecessor is falling through to this block, we could reverse
        // the branch condition and fold the tail call into that. However, after
        // that we might have to re-arrange the CFG to fall through to the other
        // block and there is a high risk of regressing code size rather than
        // improving it.
      }
      if (!PredsChanged.empty()) {
        NumTailCalls += PredsChanged.size();
        for (auto &Pred : PredsChanged)
          Pred->removeSuccessor(MBB);

        return true;
      }
    }
  }

  if (!CurUnAnalyzable) {
    // If this is a two-way branch, and the FBB branches to this block, reverse
    // the condition so the single-basic-block loop is faster.  Instead of:
    //    Loop: xxx; jcc Out; jmp Loop
    // we want:
    //    Loop: xxx; jncc Loop; jmp Out
    if (CurTBB && CurFBB && CurFBB == MBB && CurTBB != MBB) {
      SmallVector<MachineOperand, 4> NewCond(CurCond);
      if (!TII->reverseBranchCondition(NewCond)) {
        DebugLoc dl = getBranchDebugLoc(*MBB);
        TII->removeBranch(*MBB);
        TII->insertBranch(*MBB, CurFBB, CurTBB, NewCond, dl);
        MadeChange = true;
        ++NumBranchOpts;
        goto ReoptimizeBlock;
      }
    }

    // If this branch is the only thing in its block, see if we can forward
    // other blocks across it.
    if (CurTBB && CurCond.empty() && !CurFBB &&
        IsBranchOnlyBlock(MBB) && CurTBB != MBB &&
        !MBB->hasAddressTaken() && !MBB->isEHPad()) {
      DebugLoc dl = getBranchDebugLoc(*MBB);
      // This block may contain just an unconditional branch.  Because there can
      // be 'non-branch terminators' in the block, try removing the branch and
      // then seeing if the block is empty.
      TII->removeBranch(*MBB);
      // If the only things remaining in the block are debug info, remove these
      // as well, so this will behave the same as an empty block in non-debug
      // mode.
      if (IsEmptyBlock(MBB)) {
        // Make the block empty, losing the debug info (we could probably
        // improve this in some cases.)
        MBB->erase(MBB->begin(), MBB->end());
      }
      // If this block is just an unconditional branch to CurTBB, we can
      // usually completely eliminate the block.  The only case we cannot
      // completely eliminate the block is when the block before this one
      // falls through into MBB and we can't understand the prior block's branch
      // condition.
      if (MBB->empty()) {
        bool PredHasNoFallThrough = !PrevBB.canFallThrough();
        if (PredHasNoFallThrough || !PriorUnAnalyzable ||
            !PrevBB.isSuccessor(MBB)) {
          // If the prior block falls through into us, turn it into an
          // explicit branch to us to make updates simpler.
          if (!PredHasNoFallThrough && PrevBB.isSuccessor(MBB) &&
              PriorTBB != MBB && PriorFBB != MBB) {
            if (!PriorTBB) {
              assert(PriorCond.empty() && !PriorFBB &&
                     "Bad branch analysis");
              PriorTBB = MBB;
            } else {
              assert(!PriorFBB && "Machine CFG out of date!");
              PriorFBB = MBB;
            }
            DebugLoc pdl = getBranchDebugLoc(PrevBB);
            TII->removeBranch(PrevBB);
            TII->insertBranch(PrevBB, PriorTBB, PriorFBB, PriorCond, pdl);
          }

          // Iterate through all the predecessors, revectoring each in-turn.
          size_t PI = 0;
          bool DidChange = false;
          bool HasBranchToSelf = false;
          while(PI != MBB->pred_size()) {
            MachineBasicBlock *PMBB = *(MBB->pred_begin() + PI);
            if (PMBB == MBB) {
              // If this block has an uncond branch to itself, leave it.
              ++PI;
              HasBranchToSelf = true;
            } else {
              DidChange = true;
              PMBB->ReplaceUsesOfBlockWith(MBB, CurTBB);
              // Add rest successors of MBB to successors of CurTBB. Those
              // successors are not directly reachable via MBB, so it should be
              // landing-pad.
              for (auto SI = MBB->succ_begin(), SE = MBB->succ_end(); SI != SE;
                   ++SI)
                if (*SI != CurTBB && !CurTBB->isSuccessor(*SI)) {
                  assert((*SI)->isEHPad() && "Bad CFG");
                  CurTBB->copySuccessor(MBB, SI);
                }
              // If this change resulted in PMBB ending in a conditional
              // branch where both conditions go to the same destination,
              // change this to an unconditional branch.
              MachineBasicBlock *NewCurTBB = nullptr, *NewCurFBB = nullptr;
              SmallVector<MachineOperand, 4> NewCurCond;
              bool NewCurUnAnalyzable = TII->analyzeBranch(
                  *PMBB, NewCurTBB, NewCurFBB, NewCurCond, true);
              if (!NewCurUnAnalyzable && NewCurTBB && NewCurTBB == NewCurFBB) {
                DebugLoc pdl = getBranchDebugLoc(*PMBB);
                TII->removeBranch(*PMBB);
                NewCurCond.clear();
                TII->insertBranch(*PMBB, NewCurTBB, nullptr, NewCurCond, pdl);
                MadeChange = true;
                ++NumBranchOpts;
              }
            }
          }

          // Change any jumptables to go to the new MBB.
          if (MachineJumpTableInfo *MJTI = MF.getJumpTableInfo())
            MJTI->ReplaceMBBInJumpTables(MBB, CurTBB);
          if (DidChange) {
            ++NumBranchOpts;
            MadeChange = true;
            if (!HasBranchToSelf) return MadeChange;
          }
        }
      }

      // Add the branch back if the block is more than just an uncond branch.
      TII->insertBranch(*MBB, CurTBB, nullptr, CurCond, dl);
    }
  }

  // If the prior block doesn't fall through into this block, and if this
  // block doesn't fall through into some other block, see if we can find a
  // place to move this block where a fall-through will happen.
  if (!PrevBB.canFallThrough()) {
    // Now we know that there was no fall-through into this block, check to
    // see if it has a fall-through into its successor.
    bool CurFallsThru = MBB->canFallThrough();

    if (!MBB->isEHPad()) {
      // Check all the predecessors of this block.  If one of them has no fall
      // throughs, and analyzeBranch thinks it _could_ fallthrough to this
      // block, move this block right after it.
      for (MachineBasicBlock *PredBB : MBB->predecessors()) {
        // Analyze the branch at the end of the pred.
        MachineBasicBlock *PredTBB = nullptr, *PredFBB = nullptr;
        SmallVector<MachineOperand, 4> PredCond;
        if (PredBB != MBB && !PredBB->canFallThrough() &&
            !TII->analyzeBranch(*PredBB, PredTBB, PredFBB, PredCond, true) &&
            (PredTBB == MBB || PredFBB == MBB) &&
            (!CurFallsThru || !CurTBB || !CurFBB) &&
            (!CurFallsThru || MBB->getNumber() >= PredBB->getNumber())) {
          // If the current block doesn't fall through, just move it.
          // If the current block can fall through and does not end with a
          // conditional branch, we need to append an unconditional jump to
          // the (current) next block.  To avoid a possible compile-time
          // infinite loop, move blocks only backward in this case.
          // Also, if there are already 2 branches here, we cannot add a third;
          // this means we have the case
          // Bcc next
          // B elsewhere
          // next:
          if (CurFallsThru) {
            MachineBasicBlock *NextBB = &*std::next(MBB->getIterator());
            CurCond.clear();
            TII->insertBranch(*MBB, NextBB, nullptr, CurCond, DebugLoc());
          }
          MBB->moveAfter(PredBB);
          MadeChange = true;
          goto ReoptimizeBlock;
        }
      }
    }

    if (!CurFallsThru) {
      // Check analyzable branch-successors to see if we can move this block
      // before one.
      if (!CurUnAnalyzable) {
        for (MachineBasicBlock *SuccBB : {CurFBB, CurTBB}) {
          if (!SuccBB)
            continue;
          // Analyze the branch at the end of the block before the succ.
          MachineFunction::iterator SuccPrev = --SuccBB->getIterator();

          // If this block doesn't already fall-through to that successor, and
          // if the succ doesn't already have a block that can fall through into
          // it, we can arrange for the fallthrough to happen.
          if (SuccBB != MBB && &*SuccPrev != MBB &&
              !SuccPrev->canFallThrough()) {
            MBB->moveBefore(SuccBB);
            MadeChange = true;
            goto ReoptimizeBlock;
          }
        }
      }

      // Okay, there is no really great place to put this block.  If, however,
      // the block before this one would be a fall-through if this block were
      // removed, move this block to the end of the function. There is no real
      // advantage in "falling through" to an EH block, so we don't want to
      // perform this transformation for that case.
      //
      // Also, Windows EH introduced the possibility of an arbitrary number of
      // successors to a given block.  The analyzeBranch call does not consider
      // exception handling and so we can get in a state where a block
      // containing a call is followed by multiple EH blocks that would be
      // rotated infinitely at the end of the function if the transformation
      // below were performed for EH "FallThrough" blocks.  Therefore, even if
      // that appears not to be happening anymore, we should assume that it is
      // possible and not remove the "!FallThrough()->isEHPad" condition below.
      MachineBasicBlock *PrevTBB = nullptr, *PrevFBB = nullptr;
      SmallVector<MachineOperand, 4> PrevCond;
      if (FallThrough != MF.end() &&
          !FallThrough->isEHPad() &&
          !TII->analyzeBranch(PrevBB, PrevTBB, PrevFBB, PrevCond, true) &&
          PrevBB.isSuccessor(&*FallThrough)) {
        MBB->moveAfter(&MF.back());
        MadeChange = true;
        return MadeChange;
      }
    }
  }

  return MadeChange;
}

//===----------------------------------------------------------------------===//
//  Hoist Common Code
//===----------------------------------------------------------------------===//

bool BranchFolder::HoistCommonCode(MachineFunction &MF) {
  bool MadeChange = false;
  for (MachineBasicBlock &MBB : llvm::make_early_inc_range(MF))
    MadeChange |= HoistCommonCodeInSuccs(&MBB);

  return MadeChange;
}

/// findFalseBlock - BB has a fallthrough. Find its 'false' successor given
/// its 'true' successor.
static MachineBasicBlock *findFalseBlock(MachineBasicBlock *BB,
                                         MachineBasicBlock *TrueBB) {
  for (MachineBasicBlock *SuccBB : BB->successors())
    if (SuccBB != TrueBB)
      return SuccBB;
  return nullptr;
}

template <class Container>
static void addRegAndItsAliases(Register Reg, const TargetRegisterInfo *TRI,
                                Container &Set) {
  if (Reg.isPhysical()) {
    for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
      Set.insert(*AI);
  } else {
    Set.insert(Reg);
  }
}

/// findHoistingInsertPosAndDeps - Find the location to move common instructions
/// in successors to. The location is usually just before the terminator,
/// however if the terminator is a conditional branch and its previous
/// instruction is the flag setting instruction, the previous instruction is
/// the preferred location. This function also gathers uses and defs of the
/// instructions from the insertion point to the end of the block. The data is
/// used by HoistCommonCodeInSuccs to ensure safety.
static
MachineBasicBlock::iterator findHoistingInsertPosAndDeps(MachineBasicBlock *MBB,
                                                  const TargetInstrInfo *TII,
                                                  const TargetRegisterInfo *TRI,
                                                  SmallSet<Register, 4> &Uses,
                                                  SmallSet<Register, 4> &Defs) {
  MachineBasicBlock::iterator Loc = MBB->getFirstTerminator();
  if (!TII->isUnpredicatedTerminator(*Loc))
    return MBB->end();

  for (const MachineOperand &MO : Loc->operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (MO.isUse()) {
      addRegAndItsAliases(Reg, TRI, Uses);
    } else {
      if (!MO.isDead())
        // Don't try to hoist code in the rare case the terminator defines a
        // register that is later used.
        return MBB->end();

      // If the terminator defines a register, make sure we don't hoist
      // the instruction whose def might be clobbered by the terminator.
      addRegAndItsAliases(Reg, TRI, Defs);
    }
  }

  if (Uses.empty())
    return Loc;
  // If the terminator is the only instruction in the block and Uses is not
  // empty (or we would have returned above), we can still safely hoist
  // instructions just before the terminator as long as the Defs/Uses are not
  // violated (which is checked in HoistCommonCodeInSuccs).
  if (Loc == MBB->begin())
    return Loc;

  // The terminator is probably a conditional branch, try not to separate the
  // branch from condition setting instruction.
  MachineBasicBlock::iterator PI = prev_nodbg(Loc, MBB->begin());

  bool IsDef = false;
  for (const MachineOperand &MO : PI->operands()) {
    // If PI has a regmask operand, it is probably a call. Separate away.
    if (MO.isRegMask())
      return Loc;
    if (!MO.isReg() || MO.isUse())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (Uses.count(Reg)) {
      IsDef = true;
      break;
    }
  }
  if (!IsDef)
    // The condition setting instruction is not just before the conditional
    // branch.
    return Loc;

  // Be conservative, don't insert instruction above something that may have
  // side-effects. And since it's potentially bad to separate flag setting
  // instruction from the conditional branch, just abort the optimization
  // completely.
  // Also avoid moving code above predicated instruction since it's hard to
  // reason about register liveness with predicated instruction.
  bool DontMoveAcrossStore = true;
  if (!PI->isSafeToMove(nullptr, DontMoveAcrossStore) || TII->isPredicated(*PI))
    return MBB->end();

  // Find out what registers are live. Note this routine is ignoring other live
  // registers which are only used by instructions in successor blocks.
  for (const MachineOperand &MO : PI->operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (MO.isUse()) {
      addRegAndItsAliases(Reg, TRI, Uses);
    } else {
      if (Uses.erase(Reg)) {
        if (Reg.isPhysical()) {
          for (MCPhysReg SubReg : TRI->subregs(Reg))
            Uses.erase(SubReg); // Use sub-registers to be conservative
        }
      }
      addRegAndItsAliases(Reg, TRI, Defs);
    }
  }

  return PI;
}

bool BranchFolder::HoistCommonCodeInSuccs(MachineBasicBlock *MBB) {
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  if (TII->analyzeBranch(*MBB, TBB, FBB, Cond, true) || !TBB || Cond.empty())
    return false;

  if (!FBB) FBB = findFalseBlock(MBB, TBB);
  if (!FBB)
    // Malformed bcc? True and false blocks are the same?
    return false;

  // Restrict the optimization to cases where MBB is the only predecessor,
  // it is an obvious win.
  if (TBB->pred_size() > 1 || FBB->pred_size() > 1)
    return false;

  // Find a suitable position to hoist the common instructions to. Also figure
  // out which registers are used or defined by instructions from the insertion
  // point to the end of the block.
  SmallSet<Register, 4> Uses, Defs;
  MachineBasicBlock::iterator Loc =
    findHoistingInsertPosAndDeps(MBB, TII, TRI, Uses, Defs);
  if (Loc == MBB->end())
    return false;

  bool HasDups = false;
  SmallSet<Register, 4> ActiveDefsSet, AllDefsSet;
  MachineBasicBlock::iterator TIB = TBB->begin();
  MachineBasicBlock::iterator FIB = FBB->begin();
  MachineBasicBlock::iterator TIE = TBB->end();
  MachineBasicBlock::iterator FIE = FBB->end();
  while (TIB != TIE && FIB != FIE) {
    // Skip dbg_value instructions. These do not count.
    TIB = skipDebugInstructionsForward(TIB, TIE, false);
    FIB = skipDebugInstructionsForward(FIB, FIE, false);
    if (TIB == TIE || FIB == FIE)
      break;

    if (!TIB->isIdenticalTo(*FIB, MachineInstr::CheckKillDead))
      break;

    if (TII->isPredicated(*TIB))
      // Hard to reason about register liveness with predicated instruction.
      break;

    bool IsSafe = true;
    for (MachineOperand &MO : TIB->operands()) {
      // Don't attempt to hoist instructions with register masks.
      if (MO.isRegMask()) {
        IsSafe = false;
        break;
      }
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (!Reg)
        continue;
      if (MO.isDef()) {
        if (Uses.count(Reg)) {
          // Avoid clobbering a register that's used by the instruction at
          // the point of insertion.
          IsSafe = false;
          break;
        }

        if (Defs.count(Reg) && !MO.isDead()) {
          // Don't hoist the instruction if the def would be clobber by the
          // instruction at the point insertion. FIXME: This is overly
          // conservative. It should be possible to hoist the instructions
          // in BB2 in the following example:
          // BB1:
          // r1, eflag = op1 r2, r3
          // brcc eflag
          //
          // BB2:
          // r1 = op2, ...
          //    = op3, killed r1
          IsSafe = false;
          break;
        }
      } else if (!ActiveDefsSet.count(Reg)) {
        if (Defs.count(Reg)) {
          // Use is defined by the instruction at the point of insertion.
          IsSafe = false;
          break;
        }

        if (MO.isKill() && Uses.count(Reg))
          // Kills a register that's read by the instruction at the point of
          // insertion. Remove the kill marker.
          MO.setIsKill(false);
      }
    }
    if (!IsSafe)
      break;

    bool DontMoveAcrossStore = true;
    if (!TIB->isSafeToMove(nullptr, DontMoveAcrossStore))
      break;

    // Remove kills from ActiveDefsSet, these registers had short live ranges.
    for (const MachineOperand &MO : TIB->all_uses()) {
      if (!MO.isKill())
        continue;
      Register Reg = MO.getReg();
      if (!Reg)
        continue;
      if (!AllDefsSet.count(Reg)) {
        continue;
      }
      if (Reg.isPhysical()) {
        for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
          ActiveDefsSet.erase(*AI);
      } else {
        ActiveDefsSet.erase(Reg);
      }
    }

    // Track local defs so we can update liveins.
    for (const MachineOperand &MO : TIB->all_defs()) {
      if (MO.isDead())
        continue;
      Register Reg = MO.getReg();
      if (!Reg || Reg.isVirtual())
        continue;
      addRegAndItsAliases(Reg, TRI, ActiveDefsSet);
      addRegAndItsAliases(Reg, TRI, AllDefsSet);
    }

    HasDups = true;
    ++TIB;
    ++FIB;
  }

  if (!HasDups)
    return false;

  MBB->splice(Loc, TBB, TBB->begin(), TIB);
  FBB->erase(FBB->begin(), FIB);

  if (UpdateLiveIns)
    fullyRecomputeLiveIns({TBB, FBB});

  ++NumHoist;
  return true;
}
