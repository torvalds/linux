//===-- CoalesceBranches.cpp - Coalesce blocks with the same condition ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Coalesce basic blocks guarded by the same branch condition into a single
/// basic block.
///
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-branch-coalescing"

STATISTIC(NumBlocksCoalesced, "Number of blocks coalesced");
STATISTIC(NumPHINotMoved, "Number of PHI Nodes that cannot be merged");
STATISTIC(NumBlocksNotCoalesced, "Number of blocks not coalesced");

//===----------------------------------------------------------------------===//
//                               PPCBranchCoalescing
//===----------------------------------------------------------------------===//
///
/// Improve scheduling by coalescing branches that depend on the same condition.
/// This pass looks for blocks that are guarded by the same branch condition
/// and attempts to merge the blocks together. Such opportunities arise from
/// the expansion of select statements in the IR.
///
/// This pass does not handle implicit operands on branch statements. In order
/// to run on targets that use implicit operands, changes need to be made in the
/// canCoalesceBranch and canMerge methods.
///
/// Example: the following LLVM IR
///
///     %test = icmp eq i32 %x 0
///     %tmp1 = select i1 %test, double %a, double 2.000000e-03
///     %tmp2 = select i1 %test, double %b, double 5.000000e-03
///
/// expands to the following machine code:
///
/// %bb.0: derived from LLVM BB %entry
///    liveins: %f1 %f3 %x6
///        <SNIP1>
///        %0 = COPY %f1; F8RC:%0
///        %5 = CMPLWI killed %4, 0; CRRC:%5 GPRC:%4
///        %8 = LXSDX %zero8, killed %7, implicit %rm;
///                    mem:LD8[ConstantPool] F8RC:%8 G8RC:%7
///        BCC 76, %5, <%bb.2>; CRRC:%5
///    Successors according to CFG: %bb.1(?%) %bb.2(?%)
///
/// %bb.1: derived from LLVM BB %entry
///    Predecessors according to CFG: %bb.0
///    Successors according to CFG: %bb.2(?%)
///
/// %bb.2: derived from LLVM BB %entry
///    Predecessors according to CFG: %bb.0 %bb.1
///        %9 = PHI %8, <%bb.1>, %0, <%bb.0>;
///                    F8RC:%9,%8,%0
///        <SNIP2>
///        BCC 76, %5, <%bb.4>; CRRC:%5
///    Successors according to CFG: %bb.3(?%) %bb.4(?%)
///
/// %bb.3: derived from LLVM BB %entry
///    Predecessors according to CFG: %bb.2
///    Successors according to CFG: %bb.4(?%)
///
/// %bb.4: derived from LLVM BB %entry
///    Predecessors according to CFG: %bb.2 %bb.3
///        %13 = PHI %12, <%bb.3>, %2, <%bb.2>;
///                     F8RC:%13,%12,%2
///        <SNIP3>
///        BLR8 implicit %lr8, implicit %rm, implicit %f1
///
/// When this pattern is detected, branch coalescing will try to collapse
/// it by moving code in %bb.2 to %bb.0 and/or %bb.4 and removing %bb.3.
///
/// If all conditions are meet, IR should collapse to:
///
/// %bb.0: derived from LLVM BB %entry
///    liveins: %f1 %f3 %x6
///        <SNIP1>
///        %0 = COPY %f1; F8RC:%0
///        %5 = CMPLWI killed %4, 0; CRRC:%5 GPRC:%4
///        %8 = LXSDX %zero8, killed %7, implicit %rm;
///                     mem:LD8[ConstantPool] F8RC:%8 G8RC:%7
///        <SNIP2>
///        BCC 76, %5, <%bb.4>; CRRC:%5
///    Successors according to CFG: %bb.1(0x2aaaaaaa / 0x80000000 = 33.33%)
///      %bb.4(0x55555554 / 0x80000000 = 66.67%)
///
/// %bb.1: derived from LLVM BB %entry
///    Predecessors according to CFG: %bb.0
///    Successors according to CFG: %bb.4(0x40000000 / 0x80000000 = 50.00%)
///
/// %bb.4: derived from LLVM BB %entry
///    Predecessors according to CFG: %bb.0 %bb.1
///        %9 = PHI %8, <%bb.1>, %0, <%bb.0>;
///                    F8RC:%9,%8,%0
///        %13 = PHI %12, <%bb.1>, %2, <%bb.0>;
///                     F8RC:%13,%12,%2
///        <SNIP3>
///        BLR8 implicit %lr8, implicit %rm, implicit %f1
///
/// Branch Coalescing does not split blocks, it moves everything in the same
/// direction ensuring it does not break use/definition semantics.
///
/// PHI nodes and its corresponding use instructions are moved to its successor
/// block if there are no uses within the successor block PHI nodes.  PHI
/// node ordering cannot be assumed.
///
/// Non-PHI can be moved up to the predecessor basic block or down to the
/// successor basic block following any PHI instructions. Whether it moves
/// up or down depends on whether the register(s) defined in the instructions
/// are used in current block or in any PHI instructions at the beginning of
/// the successor block.

namespace {

class PPCBranchCoalescing : public MachineFunctionPass {
  struct CoalescingCandidateInfo {
    MachineBasicBlock *BranchBlock;       // Block containing the branch
    MachineBasicBlock *BranchTargetBlock; // Block branched to
    MachineBasicBlock *FallThroughBlock;  // Fall-through if branch not taken
    SmallVector<MachineOperand, 4> Cond;
    bool MustMoveDown;
    bool MustMoveUp;

    CoalescingCandidateInfo();
    void clear();
  };

  MachineDominatorTree *MDT;
  MachinePostDominatorTree *MPDT;
  const TargetInstrInfo *TII;
  MachineRegisterInfo *MRI;

  void initialize(MachineFunction &F);
  bool canCoalesceBranch(CoalescingCandidateInfo &Cand);
  bool identicalOperands(ArrayRef<MachineOperand> OperandList1,
                         ArrayRef<MachineOperand> OperandList2) const;
  bool validateCandidates(CoalescingCandidateInfo &SourceRegion,
                          CoalescingCandidateInfo &TargetRegion) const;

public:
  static char ID;

  PPCBranchCoalescing() : MachineFunctionPass(ID) {
    initializePPCBranchCoalescingPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "Branch Coalescing"; }

  bool mergeCandidates(CoalescingCandidateInfo &SourceRegion,
                       CoalescingCandidateInfo &TargetRegion);
  bool canMoveToBeginning(const MachineInstr &MI,
                          const MachineBasicBlock &MBB) const;
  bool canMoveToEnd(const MachineInstr &MI,
                    const MachineBasicBlock &MBB) const;
  bool canMerge(CoalescingCandidateInfo &SourceRegion,
                CoalescingCandidateInfo &TargetRegion) const;
  void moveAndUpdatePHIs(MachineBasicBlock *SourceRegionMBB,
                         MachineBasicBlock *TargetRegionMBB);
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // End anonymous namespace.

char PPCBranchCoalescing::ID = 0;
/// createPPCBranchCoalescingPass - returns an instance of the Branch Coalescing
/// Pass
FunctionPass *llvm::createPPCBranchCoalescingPass() {
  return new PPCBranchCoalescing();
}

INITIALIZE_PASS_BEGIN(PPCBranchCoalescing, DEBUG_TYPE,
                      "Branch Coalescing", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(PPCBranchCoalescing, DEBUG_TYPE, "Branch Coalescing",
                    false, false)

PPCBranchCoalescing::CoalescingCandidateInfo::CoalescingCandidateInfo()
    : BranchBlock(nullptr), BranchTargetBlock(nullptr),
      FallThroughBlock(nullptr), MustMoveDown(false), MustMoveUp(false) {}

void PPCBranchCoalescing::CoalescingCandidateInfo::clear() {
  BranchBlock = nullptr;
  BranchTargetBlock = nullptr;
  FallThroughBlock = nullptr;
  Cond.clear();
  MustMoveDown = false;
  MustMoveUp = false;
}

void PPCBranchCoalescing::initialize(MachineFunction &MF) {
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  TII = MF.getSubtarget().getInstrInfo();
  MRI = &MF.getRegInfo();
}

///
/// Analyze the branch statement to determine if it can be coalesced. This
/// method analyses the branch statement for the given candidate to determine
/// if it can be coalesced. If the branch can be coalesced, then the
/// BranchTargetBlock and the FallThroughBlock are recorded in the specified
/// Candidate.
///
///\param[in,out] Cand The coalescing candidate to analyze
///\return true if and only if the branch can be coalesced, false otherwise
///
bool PPCBranchCoalescing::canCoalesceBranch(CoalescingCandidateInfo &Cand) {
  LLVM_DEBUG(dbgs() << "Determine if branch block "
                    << Cand.BranchBlock->getNumber() << " can be coalesced:");
  MachineBasicBlock *FalseMBB = nullptr;

  if (TII->analyzeBranch(*Cand.BranchBlock, Cand.BranchTargetBlock, FalseMBB,
                         Cand.Cond)) {
    LLVM_DEBUG(dbgs() << "TII unable to Analyze Branch - skip\n");
    return false;
  }

  for (auto &I : Cand.BranchBlock->terminators()) {
    LLVM_DEBUG(dbgs() << "Looking at terminator : " << I << "\n");
    if (!I.isBranch())
      continue;

    // The analyzeBranch method does not include any implicit operands.
    // This is not an issue on PPC but must be handled on other targets.
    // For this pass to be made target-independent, the analyzeBranch API
    // need to be updated to support implicit operands and there would
    // need to be a way to verify that any implicit operands would not be
    // clobbered by merging blocks.  This would include identifying the
    // implicit operands as well as the basic block they are defined in.
    // This could be done by changing the analyzeBranch API to have it also
    // record and return the implicit operands and the blocks where they are
    // defined. Alternatively, the BranchCoalescing code would need to be
    // extended to identify the implicit operands.  The analysis in canMerge
    // must then be extended to prove that none of the implicit operands are
    // changed in the blocks that are combined during coalescing.
    if (I.getNumOperands() != I.getNumExplicitOperands()) {
      LLVM_DEBUG(dbgs() << "Terminator contains implicit operands - skip : "
                        << I << "\n");
      return false;
    }
  }

  if (Cand.BranchBlock->isEHPad() || Cand.BranchBlock->hasEHPadSuccessor()) {
    LLVM_DEBUG(dbgs() << "EH Pad - skip\n");
    return false;
  }

  if (Cand.BranchBlock->mayHaveInlineAsmBr()) {
    LLVM_DEBUG(dbgs() << "Inline Asm Br - skip\n");
    return false;
  }

  // For now only consider triangles (i.e, BranchTargetBlock is set,
  // FalseMBB is null, and BranchTargetBlock is a successor to BranchBlock)
  if (!Cand.BranchTargetBlock || FalseMBB ||
      !Cand.BranchBlock->isSuccessor(Cand.BranchTargetBlock)) {
    LLVM_DEBUG(dbgs() << "Does not form a triangle - skip\n");
    return false;
  }

  // Ensure there are only two successors
  if (Cand.BranchBlock->succ_size() != 2) {
    LLVM_DEBUG(dbgs() << "Does not have 2 successors - skip\n");
    return false;
  }

  // The block must be able to fall through.
  assert(Cand.BranchBlock->canFallThrough() &&
         "Expecting the block to fall through!");

  // We have already ensured there are exactly two successors to
  // BranchBlock and that BranchTargetBlock is a successor to BranchBlock.
  // Ensure the single fall though block is empty.
  MachineBasicBlock *Succ =
    (*Cand.BranchBlock->succ_begin() == Cand.BranchTargetBlock)
    ? *Cand.BranchBlock->succ_rbegin()
    : *Cand.BranchBlock->succ_begin();

  assert(Succ && "Expecting a valid fall-through block\n");

  if (!Succ->empty()) {
    LLVM_DEBUG(dbgs() << "Fall-through block contains code -- skip\n");
    return false;
  }

  if (!Succ->isSuccessor(Cand.BranchTargetBlock)) {
    LLVM_DEBUG(
        dbgs()
        << "Successor of fall through block is not branch taken block\n");
    return false;
  }

  Cand.FallThroughBlock = Succ;
  LLVM_DEBUG(dbgs() << "Valid Candidate\n");
  return true;
}

///
/// Determine if the two operand lists are identical
///
/// \param[in] OpList1 operand list
/// \param[in] OpList2 operand list
/// \return true if and only if the operands lists are identical
///
bool PPCBranchCoalescing::identicalOperands(
    ArrayRef<MachineOperand> OpList1, ArrayRef<MachineOperand> OpList2) const {

  if (OpList1.size() != OpList2.size()) {
    LLVM_DEBUG(dbgs() << "Operand list is different size\n");
    return false;
  }

  for (unsigned i = 0; i < OpList1.size(); ++i) {
    const MachineOperand &Op1 = OpList1[i];
    const MachineOperand &Op2 = OpList2[i];

    LLVM_DEBUG(dbgs() << "Op1: " << Op1 << "\n"
                      << "Op2: " << Op2 << "\n");

    if (Op1.isIdenticalTo(Op2)) {
      // filter out instructions with physical-register uses
      if (Op1.isReg() && Op1.getReg().isPhysical()
          // If the physical register is constant then we can assume the value
          // has not changed between uses.
          && !(Op1.isUse() && MRI->isConstantPhysReg(Op1.getReg()))) {
        LLVM_DEBUG(dbgs() << "The operands are not provably identical.\n");
        return false;
      }
      LLVM_DEBUG(dbgs() << "Op1 and Op2 are identical!\n");
      continue;
    }

    // If the operands are not identical, but are registers, check to see if the
    // definition of the register produces the same value. If they produce the
    // same value, consider them to be identical.
    if (Op1.isReg() && Op2.isReg() && Op1.getReg().isVirtual() &&
        Op2.getReg().isVirtual()) {
      MachineInstr *Op1Def = MRI->getVRegDef(Op1.getReg());
      MachineInstr *Op2Def = MRI->getVRegDef(Op2.getReg());
      if (TII->produceSameValue(*Op1Def, *Op2Def, MRI)) {
        LLVM_DEBUG(dbgs() << "Op1Def: " << *Op1Def << " and " << *Op2Def
                          << " produce the same value!\n");
      } else {
        LLVM_DEBUG(dbgs() << "Operands produce different values\n");
        return false;
      }
    } else {
      LLVM_DEBUG(dbgs() << "The operands are not provably identical.\n");
      return false;
    }
  }

  return true;
}

///
/// Moves ALL PHI instructions in SourceMBB to beginning of TargetMBB
/// and update them to refer to the new block.  PHI node ordering
/// cannot be assumed so it does not matter where the PHI instructions
/// are moved to in TargetMBB.
///
/// \param[in] SourceMBB block to move PHI instructions from
/// \param[in] TargetMBB block to move PHI instructions to
///
void PPCBranchCoalescing::moveAndUpdatePHIs(MachineBasicBlock *SourceMBB,
                                         MachineBasicBlock *TargetMBB) {

  MachineBasicBlock::iterator MI = SourceMBB->begin();
  MachineBasicBlock::iterator ME = SourceMBB->getFirstNonPHI();

  if (MI == ME) {
    LLVM_DEBUG(dbgs() << "SourceMBB contains no PHI instructions.\n");
    return;
  }

  // Update all PHI instructions in SourceMBB and move to top of TargetMBB
  for (MachineBasicBlock::iterator Iter = MI; Iter != ME; Iter++) {
    MachineInstr &PHIInst = *Iter;
    for (unsigned i = 2, e = PHIInst.getNumOperands() + 1; i != e; i += 2) {
      MachineOperand &MO = PHIInst.getOperand(i);
      if (MO.getMBB() == SourceMBB)
        MO.setMBB(TargetMBB);
    }
  }
  TargetMBB->splice(TargetMBB->begin(), SourceMBB, MI, ME);
}

///
/// This function checks if MI can be moved to the beginning of the TargetMBB
/// following PHI instructions. A MI instruction can be moved to beginning of
/// the TargetMBB if there are no uses of it within the TargetMBB PHI nodes.
///
/// \param[in] MI the machine instruction to move.
/// \param[in] TargetMBB the machine basic block to move to
/// \return true if it is safe to move MI to beginning of TargetMBB,
///         false otherwise.
///
bool PPCBranchCoalescing::canMoveToBeginning(const MachineInstr &MI,
                                          const MachineBasicBlock &TargetMBB
                                          ) const {

  LLVM_DEBUG(dbgs() << "Checking if " << MI << " can move to beginning of "
                    << TargetMBB.getNumber() << "\n");

  for (auto &Def : MI.defs()) { // Looking at Def
    for (auto &Use : MRI->use_instructions(Def.getReg())) {
      if (Use.isPHI() && Use.getParent() == &TargetMBB) {
        LLVM_DEBUG(dbgs() << "    *** used in a PHI -- cannot move ***\n");
        return false;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "  Safe to move to the beginning.\n");
  return true;
}

///
/// This function checks if MI can be moved to the end of the TargetMBB,
/// immediately before the first terminator.  A MI instruction can be moved
/// to then end of the TargetMBB if no PHI node defines what MI uses within
/// it's own MBB.
///
/// \param[in] MI the machine instruction to move.
/// \param[in] TargetMBB the machine basic block to move to
/// \return true if it is safe to move MI to end of TargetMBB,
///         false otherwise.
///
bool PPCBranchCoalescing::canMoveToEnd(const MachineInstr &MI,
                                    const MachineBasicBlock &TargetMBB
                                    ) const {

  LLVM_DEBUG(dbgs() << "Checking if " << MI << " can move to end of "
                    << TargetMBB.getNumber() << "\n");

  for (auto &Use : MI.uses()) {
    if (Use.isReg() && Use.getReg().isVirtual()) {
      MachineInstr *DefInst = MRI->getVRegDef(Use.getReg());
      if (DefInst->isPHI() && DefInst->getParent() == MI.getParent()) {
        LLVM_DEBUG(dbgs() << "    *** Cannot move this instruction ***\n");
        return false;
      } else {
        LLVM_DEBUG(
            dbgs() << "    *** def is in another block -- safe to move!\n");
      }
    }
  }

  LLVM_DEBUG(dbgs() << "  Safe to move to the end.\n");
  return true;
}

///
/// This method checks to ensure the two coalescing candidates follows the
/// expected pattern required for coalescing.
///
/// \param[in] SourceRegion The candidate to move statements from
/// \param[in] TargetRegion The candidate to move statements to
/// \return true if all instructions in SourceRegion.BranchBlock can be merged
/// into a block in TargetRegion; false otherwise.
///
bool PPCBranchCoalescing::validateCandidates(
    CoalescingCandidateInfo &SourceRegion,
    CoalescingCandidateInfo &TargetRegion) const {

  if (TargetRegion.BranchTargetBlock != SourceRegion.BranchBlock)
    llvm_unreachable("Expecting SourceRegion to immediately follow TargetRegion");
  else if (!MDT->dominates(TargetRegion.BranchBlock, SourceRegion.BranchBlock))
    llvm_unreachable("Expecting TargetRegion to dominate SourceRegion");
  else if (!MPDT->dominates(SourceRegion.BranchBlock, TargetRegion.BranchBlock))
    llvm_unreachable("Expecting SourceRegion to post-dominate TargetRegion");
  else if (!TargetRegion.FallThroughBlock->empty() ||
           !SourceRegion.FallThroughBlock->empty())
    llvm_unreachable("Expecting fall-through blocks to be empty");

  return true;
}

///
/// This method determines whether the two coalescing candidates can be merged.
/// In order to be merged, all instructions must be able to
///   1. Move to the beginning of the SourceRegion.BranchTargetBlock;
///   2. Move to the end of the TargetRegion.BranchBlock.
/// Merging involves moving the instructions in the
/// TargetRegion.BranchTargetBlock (also SourceRegion.BranchBlock).
///
/// This function first try to move instructions from the
/// TargetRegion.BranchTargetBlock down, to the beginning of the
/// SourceRegion.BranchTargetBlock. This is not possible if any register defined
/// in TargetRegion.BranchTargetBlock is used in a PHI node in the
/// SourceRegion.BranchTargetBlock. In this case, check whether the statement
/// can be moved up, to the end of the TargetRegion.BranchBlock (immediately
/// before the branch statement). If it cannot move, then these blocks cannot
/// be merged.
///
/// Note that there is no analysis for moving instructions past the fall-through
/// blocks because they are confirmed to be empty. An assert is thrown if they
/// are not.
///
/// \param[in] SourceRegion The candidate to move statements from
/// \param[in] TargetRegion The candidate to move statements to
/// \return true if all instructions in SourceRegion.BranchBlock can be merged
///         into a block in TargetRegion, false otherwise.
///
bool PPCBranchCoalescing::canMerge(CoalescingCandidateInfo &SourceRegion,
                                CoalescingCandidateInfo &TargetRegion) const {
  if (!validateCandidates(SourceRegion, TargetRegion))
    return false;

  // Walk through PHI nodes first and see if they force the merge into the
  // SourceRegion.BranchTargetBlock.
  for (MachineBasicBlock::iterator
           I = SourceRegion.BranchBlock->instr_begin(),
           E = SourceRegion.BranchBlock->getFirstNonPHI();
       I != E; ++I) {
    for (auto &Def : I->defs())
      for (auto &Use : MRI->use_instructions(Def.getReg())) {
        if (Use.isPHI() && Use.getParent() == SourceRegion.BranchTargetBlock) {
          LLVM_DEBUG(dbgs()
                     << "PHI " << *I
                     << " defines register used in another "
                        "PHI within branch target block -- can't merge\n");
          NumPHINotMoved++;
          return false;
        }
        if (Use.getParent() == SourceRegion.BranchBlock) {
          LLVM_DEBUG(dbgs() << "PHI " << *I
                            << " defines register used in this "
                               "block -- all must move down\n");
          SourceRegion.MustMoveDown = true;
        }
      }
  }

  // Walk through the MI to see if they should be merged into
  // TargetRegion.BranchBlock (up) or SourceRegion.BranchTargetBlock (down)
  for (MachineBasicBlock::iterator
           I = SourceRegion.BranchBlock->getFirstNonPHI(),
           E = SourceRegion.BranchBlock->end();
       I != E; ++I) {
    if (!canMoveToBeginning(*I, *SourceRegion.BranchTargetBlock)) {
      LLVM_DEBUG(dbgs() << "Instruction " << *I
                        << " cannot move down - must move up!\n");
      SourceRegion.MustMoveUp = true;
    }
    if (!canMoveToEnd(*I, *TargetRegion.BranchBlock)) {
      LLVM_DEBUG(dbgs() << "Instruction " << *I
                        << " cannot move up - must move down!\n");
      SourceRegion.MustMoveDown = true;
    }
  }

  return (SourceRegion.MustMoveUp && SourceRegion.MustMoveDown) ? false : true;
}

/// Merge the instructions from SourceRegion.BranchBlock,
/// SourceRegion.BranchTargetBlock, and SourceRegion.FallThroughBlock into
/// TargetRegion.BranchBlock, TargetRegion.BranchTargetBlock and
/// TargetRegion.FallThroughBlock respectively.
///
/// The successors for blocks in TargetRegion will be updated to use the
/// successors from blocks in SourceRegion. Finally, the blocks in SourceRegion
/// will be removed from the function.
///
/// A region consists of a BranchBlock, a FallThroughBlock, and a
/// BranchTargetBlock. Branch coalesce works on patterns where the
/// TargetRegion's BranchTargetBlock must also be the SourceRegions's
/// BranchBlock.
///
///  Before mergeCandidates:
///
///  +---------------------------+
///  |  TargetRegion.BranchBlock |
///  +---------------------------+
///     /        |
///    /   +--------------------------------+
///   |    |  TargetRegion.FallThroughBlock |
///    \   +--------------------------------+
///     \        |
///  +----------------------------------+
///  |  TargetRegion.BranchTargetBlock  |
///  |  SourceRegion.BranchBlock        |
///  +----------------------------------+
///     /        |
///    /   +--------------------------------+
///   |    |  SourceRegion.FallThroughBlock |
///    \   +--------------------------------+
///     \        |
///  +----------------------------------+
///  |  SourceRegion.BranchTargetBlock  |
///  +----------------------------------+
///
///  After mergeCandidates:
///
///  +-----------------------------+
///  |  TargetRegion.BranchBlock   |
///  |  SourceRegion.BranchBlock   |
///  +-----------------------------+
///     /        |
///    /   +---------------------------------+
///   |    |  TargetRegion.FallThroughBlock  |
///   |    |  SourceRegion.FallThroughBlock  |
///    \   +---------------------------------+
///     \        |
///  +----------------------------------+
///  |  SourceRegion.BranchTargetBlock  |
///  +----------------------------------+
///
/// \param[in] SourceRegion The candidate to move blocks from
/// \param[in] TargetRegion The candidate to move blocks to
///
bool PPCBranchCoalescing::mergeCandidates(CoalescingCandidateInfo &SourceRegion,
                                       CoalescingCandidateInfo &TargetRegion) {

  if (SourceRegion.MustMoveUp && SourceRegion.MustMoveDown) {
    llvm_unreachable("Cannot have both MustMoveDown and MustMoveUp set!");
    return false;
  }

  if (!validateCandidates(SourceRegion, TargetRegion))
    return false;

  // Start the merging process by first handling the BranchBlock.
  // Move any PHIs in SourceRegion.BranchBlock down to the branch-taken block
  moveAndUpdatePHIs(SourceRegion.BranchBlock, SourceRegion.BranchTargetBlock);

  // Move remaining instructions in SourceRegion.BranchBlock into
  // TargetRegion.BranchBlock
  MachineBasicBlock::iterator firstInstr =
      SourceRegion.BranchBlock->getFirstNonPHI();
  MachineBasicBlock::iterator lastInstr =
      SourceRegion.BranchBlock->getFirstTerminator();

  MachineBasicBlock *Source = SourceRegion.MustMoveDown
                                  ? SourceRegion.BranchTargetBlock
                                  : TargetRegion.BranchBlock;

  MachineBasicBlock::iterator Target =
      SourceRegion.MustMoveDown
          ? SourceRegion.BranchTargetBlock->getFirstNonPHI()
          : TargetRegion.BranchBlock->getFirstTerminator();

  Source->splice(Target, SourceRegion.BranchBlock, firstInstr, lastInstr);

  // Once PHI and instructions have been moved we need to clean up the
  // control flow.

  // Remove SourceRegion.FallThroughBlock before transferring successors of
  // SourceRegion.BranchBlock to TargetRegion.BranchBlock.
  SourceRegion.BranchBlock->removeSuccessor(SourceRegion.FallThroughBlock);
  TargetRegion.BranchBlock->transferSuccessorsAndUpdatePHIs(
      SourceRegion.BranchBlock);
  // Update branch in TargetRegion.BranchBlock to jump to
  // SourceRegion.BranchTargetBlock
  // In this case, TargetRegion.BranchTargetBlock == SourceRegion.BranchBlock.
  TargetRegion.BranchBlock->ReplaceUsesOfBlockWith(
      SourceRegion.BranchBlock, SourceRegion.BranchTargetBlock);
  // Remove the branch statement(s) in SourceRegion.BranchBlock
  MachineBasicBlock::iterator I =
      SourceRegion.BranchBlock->terminators().begin();
  while (I != SourceRegion.BranchBlock->terminators().end()) {
    MachineInstr &CurrInst = *I;
    ++I;
    if (CurrInst.isBranch())
      CurrInst.eraseFromParent();
  }

  // Fall-through block should be empty since this is part of the condition
  // to coalesce the branches.
  assert(TargetRegion.FallThroughBlock->empty() &&
         "FallThroughBlocks should be empty!");

  // Transfer successor information and move PHIs down to the
  // branch-taken block.
  TargetRegion.FallThroughBlock->transferSuccessorsAndUpdatePHIs(
      SourceRegion.FallThroughBlock);
  TargetRegion.FallThroughBlock->removeSuccessor(SourceRegion.BranchBlock);
  TargetRegion.FallThroughBlock->normalizeSuccProbs();

  // Remove the blocks from the function.
  assert(SourceRegion.BranchBlock->empty() &&
         "Expecting branch block to be empty!");
  SourceRegion.BranchBlock->eraseFromParent();

  assert(SourceRegion.FallThroughBlock->empty() &&
         "Expecting fall-through block to be empty!\n");
  SourceRegion.FallThroughBlock->eraseFromParent();

  NumBlocksCoalesced++;
  return true;
}

bool PPCBranchCoalescing::runOnMachineFunction(MachineFunction &MF) {

  if (skipFunction(MF.getFunction()) || MF.empty())
    return false;

  bool didSomething = false;

  LLVM_DEBUG(dbgs() << "******** Branch Coalescing ********\n");
  initialize(MF);

  LLVM_DEBUG(dbgs() << "Function: "; MF.dump(); dbgs() << "\n");

  CoalescingCandidateInfo Cand1, Cand2;
  // Walk over blocks and find candidates to merge
  // Continue trying to merge with the first candidate found, as long as merging
  // is successfull.
  for (MachineBasicBlock &MBB : MF) {
    bool MergedCandidates = false;
    do {
      MergedCandidates = false;
      Cand1.clear();
      Cand2.clear();

      Cand1.BranchBlock = &MBB;

      // If unable to coalesce the branch, then continue to next block
      if (!canCoalesceBranch(Cand1))
        break;

      Cand2.BranchBlock = Cand1.BranchTargetBlock;
      if (!canCoalesceBranch(Cand2))
        break;

      // The branch-taken block of the second candidate should post-dominate the
      // first candidate.
      assert(MPDT->dominates(Cand2.BranchTargetBlock, Cand1.BranchBlock) &&
             "Branch-taken block should post-dominate first candidate");

      if (!identicalOperands(Cand1.Cond, Cand2.Cond)) {
        LLVM_DEBUG(dbgs() << "Blocks " << Cand1.BranchBlock->getNumber()
                          << " and " << Cand2.BranchBlock->getNumber()
                          << " have different branches\n");
        break;
      }
      if (!canMerge(Cand2, Cand1)) {
        LLVM_DEBUG(dbgs() << "Cannot merge blocks "
                          << Cand1.BranchBlock->getNumber() << " and "
                          << Cand2.BranchBlock->getNumber() << "\n");
        NumBlocksNotCoalesced++;
        continue;
      }
      LLVM_DEBUG(dbgs() << "Merging blocks " << Cand1.BranchBlock->getNumber()
                        << " and " << Cand1.BranchTargetBlock->getNumber()
                        << "\n");
      MergedCandidates = mergeCandidates(Cand2, Cand1);
      if (MergedCandidates)
        didSomething = true;

      LLVM_DEBUG(dbgs() << "Function after merging: "; MF.dump();
                 dbgs() << "\n");
    } while (MergedCandidates);
  }

#ifndef NDEBUG
  // Verify MF is still valid after branch coalescing
  if (didSomething)
    MF.verify(nullptr, "Error in code produced by branch coalescing");
#endif // NDEBUG

  LLVM_DEBUG(dbgs() << "Finished Branch Coalescing\n");
  return didSomething;
}
