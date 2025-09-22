//===- BranchProbabilityInfo.cpp - Branch Probability Analysis ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "branch-prob"

static cl::opt<bool> PrintBranchProb(
    "print-bpi", cl::init(false), cl::Hidden,
    cl::desc("Print the branch probability info."));

cl::opt<std::string> PrintBranchProbFuncName(
    "print-bpi-func-name", cl::Hidden,
    cl::desc("The option to specify the name of the function "
             "whose branch probability info is printed."));

INITIALIZE_PASS_BEGIN(BranchProbabilityInfoWrapperPass, "branch-prob",
                      "Branch Probability Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(BranchProbabilityInfoWrapperPass, "branch-prob",
                    "Branch Probability Analysis", false, true)

BranchProbabilityInfoWrapperPass::BranchProbabilityInfoWrapperPass()
    : FunctionPass(ID) {
  initializeBranchProbabilityInfoWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

char BranchProbabilityInfoWrapperPass::ID = 0;

// Weights are for internal use only. They are used by heuristics to help to
// estimate edges' probability. Example:
//
// Using "Loop Branch Heuristics" we predict weights of edges for the
// block BB2.
//         ...
//          |
//          V
//         BB1<-+
//          |   |
//          |   | (Weight = 124)
//          V   |
//         BB2--+
//          |
//          | (Weight = 4)
//          V
//         BB3
//
// Probability of the edge BB2->BB1 = 124 / (124 + 4) = 0.96875
// Probability of the edge BB2->BB3 = 4 / (124 + 4) = 0.03125
static const uint32_t LBH_TAKEN_WEIGHT = 124;
static const uint32_t LBH_NONTAKEN_WEIGHT = 4;

/// Unreachable-terminating branch taken probability.
///
/// This is the probability for a branch being taken to a block that terminates
/// (eventually) in unreachable. These are predicted as unlikely as possible.
/// All reachable probability will proportionally share the remaining part.
static const BranchProbability UR_TAKEN_PROB = BranchProbability::getRaw(1);

/// Heuristics and lookup tables for non-loop branches:
/// Pointer Heuristics (PH)
static const uint32_t PH_TAKEN_WEIGHT = 20;
static const uint32_t PH_NONTAKEN_WEIGHT = 12;
static const BranchProbability
    PtrTakenProb(PH_TAKEN_WEIGHT, PH_TAKEN_WEIGHT + PH_NONTAKEN_WEIGHT);
static const BranchProbability
    PtrUntakenProb(PH_NONTAKEN_WEIGHT, PH_TAKEN_WEIGHT + PH_NONTAKEN_WEIGHT);

using ProbabilityList = SmallVector<BranchProbability>;
using ProbabilityTable = std::map<CmpInst::Predicate, ProbabilityList>;

/// Pointer comparisons:
static const ProbabilityTable PointerTable{
    {ICmpInst::ICMP_NE, {PtrTakenProb, PtrUntakenProb}}, /// p != q -> Likely
    {ICmpInst::ICMP_EQ, {PtrUntakenProb, PtrTakenProb}}, /// p == q -> Unlikely
};

/// Zero Heuristics (ZH)
static const uint32_t ZH_TAKEN_WEIGHT = 20;
static const uint32_t ZH_NONTAKEN_WEIGHT = 12;
static const BranchProbability
    ZeroTakenProb(ZH_TAKEN_WEIGHT, ZH_TAKEN_WEIGHT + ZH_NONTAKEN_WEIGHT);
static const BranchProbability
    ZeroUntakenProb(ZH_NONTAKEN_WEIGHT, ZH_TAKEN_WEIGHT + ZH_NONTAKEN_WEIGHT);

/// Integer compares with 0:
static const ProbabilityTable ICmpWithZeroTable{
    {CmpInst::ICMP_EQ, {ZeroUntakenProb, ZeroTakenProb}},  /// X == 0 -> Unlikely
    {CmpInst::ICMP_NE, {ZeroTakenProb, ZeroUntakenProb}},  /// X != 0 -> Likely
    {CmpInst::ICMP_SLT, {ZeroUntakenProb, ZeroTakenProb}}, /// X < 0  -> Unlikely
    {CmpInst::ICMP_SGT, {ZeroTakenProb, ZeroUntakenProb}}, /// X > 0  -> Likely
};

/// Integer compares with -1:
static const ProbabilityTable ICmpWithMinusOneTable{
    {CmpInst::ICMP_EQ, {ZeroUntakenProb, ZeroTakenProb}},  /// X == -1 -> Unlikely
    {CmpInst::ICMP_NE, {ZeroTakenProb, ZeroUntakenProb}},  /// X != -1 -> Likely
    // InstCombine canonicalizes X >= 0 into X > -1
    {CmpInst::ICMP_SGT, {ZeroTakenProb, ZeroUntakenProb}}, /// X >= 0  -> Likely
};

/// Integer compares with 1:
static const ProbabilityTable ICmpWithOneTable{
    // InstCombine canonicalizes X <= 0 into X < 1
    {CmpInst::ICMP_SLT, {ZeroUntakenProb, ZeroTakenProb}}, /// X <= 0 -> Unlikely
};

/// strcmp and similar functions return zero, negative, or positive, if the
/// first string is equal, less, or greater than the second. We consider it
/// likely that the strings are not equal, so a comparison with zero is
/// probably false, but also a comparison with any other number is also
/// probably false given that what exactly is returned for nonzero values is
/// not specified. Any kind of comparison other than equality we know
/// nothing about.
static const ProbabilityTable ICmpWithLibCallTable{
    {CmpInst::ICMP_EQ, {ZeroUntakenProb, ZeroTakenProb}},
    {CmpInst::ICMP_NE, {ZeroTakenProb, ZeroUntakenProb}},
};

// Floating-Point Heuristics (FPH)
static const uint32_t FPH_TAKEN_WEIGHT = 20;
static const uint32_t FPH_NONTAKEN_WEIGHT = 12;

/// This is the probability for an ordered floating point comparison.
static const uint32_t FPH_ORD_WEIGHT = 1024 * 1024 - 1;
/// This is the probability for an unordered floating point comparison, it means
/// one or two of the operands are NaN. Usually it is used to test for an
/// exceptional case, so the result is unlikely.
static const uint32_t FPH_UNO_WEIGHT = 1;

static const BranchProbability FPOrdTakenProb(FPH_ORD_WEIGHT,
                                              FPH_ORD_WEIGHT + FPH_UNO_WEIGHT);
static const BranchProbability
    FPOrdUntakenProb(FPH_UNO_WEIGHT, FPH_ORD_WEIGHT + FPH_UNO_WEIGHT);
static const BranchProbability
    FPTakenProb(FPH_TAKEN_WEIGHT, FPH_TAKEN_WEIGHT + FPH_NONTAKEN_WEIGHT);
static const BranchProbability
    FPUntakenProb(FPH_NONTAKEN_WEIGHT, FPH_TAKEN_WEIGHT + FPH_NONTAKEN_WEIGHT);

/// Floating-Point compares:
static const ProbabilityTable FCmpTable{
    {FCmpInst::FCMP_ORD, {FPOrdTakenProb, FPOrdUntakenProb}}, /// !isnan -> Likely
    {FCmpInst::FCMP_UNO, {FPOrdUntakenProb, FPOrdTakenProb}}, /// isnan -> Unlikely
};

/// Set of dedicated "absolute" execution weights for a block. These weights are
/// meaningful relative to each other and their derivatives only.
enum class BlockExecWeight : std::uint32_t {
  /// Special weight used for cases with exact zero probability.
  ZERO = 0x0,
  /// Minimal possible non zero weight.
  LOWEST_NON_ZERO = 0x1,
  /// Weight to an 'unreachable' block.
  UNREACHABLE = ZERO,
  /// Weight to a block containing non returning call.
  NORETURN = LOWEST_NON_ZERO,
  /// Weight to 'unwind' block of an invoke instruction.
  UNWIND = LOWEST_NON_ZERO,
  /// Weight to a 'cold' block. Cold blocks are the ones containing calls marked
  /// with attribute 'cold'.
  COLD = 0xffff,
  /// Default weight is used in cases when there is no dedicated execution
  /// weight set. It is not propagated through the domination line either.
  DEFAULT = 0xfffff
};

BranchProbabilityInfo::SccInfo::SccInfo(const Function &F) {
  // Record SCC numbers of blocks in the CFG to identify irreducible loops.
  // FIXME: We could only calculate this if the CFG is known to be irreducible
  // (perhaps cache this info in LoopInfo if we can easily calculate it there?).
  int SccNum = 0;
  for (scc_iterator<const Function *> It = scc_begin(&F); !It.isAtEnd();
       ++It, ++SccNum) {
    // Ignore single-block SCCs since they either aren't loops or LoopInfo will
    // catch them.
    const std::vector<const BasicBlock *> &Scc = *It;
    if (Scc.size() == 1)
      continue;

    LLVM_DEBUG(dbgs() << "BPI: SCC " << SccNum << ":");
    for (const auto *BB : Scc) {
      LLVM_DEBUG(dbgs() << " " << BB->getName());
      SccNums[BB] = SccNum;
      calculateSccBlockType(BB, SccNum);
    }
    LLVM_DEBUG(dbgs() << "\n");
  }
}

int BranchProbabilityInfo::SccInfo::getSCCNum(const BasicBlock *BB) const {
  auto SccIt = SccNums.find(BB);
  if (SccIt == SccNums.end())
    return -1;
  return SccIt->second;
}

void BranchProbabilityInfo::SccInfo::getSccEnterBlocks(
    int SccNum, SmallVectorImpl<BasicBlock *> &Enters) const {

  for (auto MapIt : SccBlocks[SccNum]) {
    const auto *BB = MapIt.first;
    if (isSCCHeader(BB, SccNum))
      for (const auto *Pred : predecessors(BB))
        if (getSCCNum(Pred) != SccNum)
          Enters.push_back(const_cast<BasicBlock *>(BB));
  }
}

void BranchProbabilityInfo::SccInfo::getSccExitBlocks(
    int SccNum, SmallVectorImpl<BasicBlock *> &Exits) const {
  for (auto MapIt : SccBlocks[SccNum]) {
    const auto *BB = MapIt.first;
    if (isSCCExitingBlock(BB, SccNum))
      for (const auto *Succ : successors(BB))
        if (getSCCNum(Succ) != SccNum)
          Exits.push_back(const_cast<BasicBlock *>(Succ));
  }
}

uint32_t BranchProbabilityInfo::SccInfo::getSccBlockType(const BasicBlock *BB,
                                                         int SccNum) const {
  assert(getSCCNum(BB) == SccNum);

  assert(SccBlocks.size() > static_cast<unsigned>(SccNum) && "Unknown SCC");
  const auto &SccBlockTypes = SccBlocks[SccNum];

  auto It = SccBlockTypes.find(BB);
  if (It != SccBlockTypes.end()) {
    return It->second;
  }
  return Inner;
}

void BranchProbabilityInfo::SccInfo::calculateSccBlockType(const BasicBlock *BB,
                                                           int SccNum) {
  assert(getSCCNum(BB) == SccNum);
  uint32_t BlockType = Inner;

  if (llvm::any_of(predecessors(BB), [&](const BasicBlock *Pred) {
        // Consider any block that is an entry point to the SCC as
        // a header.
        return getSCCNum(Pred) != SccNum;
      }))
    BlockType |= Header;

  if (llvm::any_of(successors(BB), [&](const BasicBlock *Succ) {
        return getSCCNum(Succ) != SccNum;
      }))
    BlockType |= Exiting;

  // Lazily compute the set of headers for a given SCC and cache the results
  // in the SccHeaderMap.
  if (SccBlocks.size() <= static_cast<unsigned>(SccNum))
    SccBlocks.resize(SccNum + 1);
  auto &SccBlockTypes = SccBlocks[SccNum];

  if (BlockType != Inner) {
    bool IsInserted;
    std::tie(std::ignore, IsInserted) =
        SccBlockTypes.insert(std::make_pair(BB, BlockType));
    assert(IsInserted && "Duplicated block in SCC");
  }
}

BranchProbabilityInfo::LoopBlock::LoopBlock(const BasicBlock *BB,
                                            const LoopInfo &LI,
                                            const SccInfo &SccI)
    : BB(BB) {
  LD.first = LI.getLoopFor(BB);
  if (!LD.first) {
    LD.second = SccI.getSCCNum(BB);
  }
}

bool BranchProbabilityInfo::isLoopEnteringEdge(const LoopEdge &Edge) const {
  const auto &SrcBlock = Edge.first;
  const auto &DstBlock = Edge.second;
  return (DstBlock.getLoop() &&
          !DstBlock.getLoop()->contains(SrcBlock.getLoop())) ||
         // Assume that SCCs can't be nested.
         (DstBlock.getSccNum() != -1 &&
          SrcBlock.getSccNum() != DstBlock.getSccNum());
}

bool BranchProbabilityInfo::isLoopExitingEdge(const LoopEdge &Edge) const {
  return isLoopEnteringEdge({Edge.second, Edge.first});
}

bool BranchProbabilityInfo::isLoopEnteringExitingEdge(
    const LoopEdge &Edge) const {
  return isLoopEnteringEdge(Edge) || isLoopExitingEdge(Edge);
}

bool BranchProbabilityInfo::isLoopBackEdge(const LoopEdge &Edge) const {
  const auto &SrcBlock = Edge.first;
  const auto &DstBlock = Edge.second;
  return SrcBlock.belongsToSameLoop(DstBlock) &&
         ((DstBlock.getLoop() &&
           DstBlock.getLoop()->getHeader() == DstBlock.getBlock()) ||
          (DstBlock.getSccNum() != -1 &&
           SccI->isSCCHeader(DstBlock.getBlock(), DstBlock.getSccNum())));
}

void BranchProbabilityInfo::getLoopEnterBlocks(
    const LoopBlock &LB, SmallVectorImpl<BasicBlock *> &Enters) const {
  if (LB.getLoop()) {
    auto *Header = LB.getLoop()->getHeader();
    Enters.append(pred_begin(Header), pred_end(Header));
  } else {
    assert(LB.getSccNum() != -1 && "LB doesn't belong to any loop?");
    SccI->getSccEnterBlocks(LB.getSccNum(), Enters);
  }
}

void BranchProbabilityInfo::getLoopExitBlocks(
    const LoopBlock &LB, SmallVectorImpl<BasicBlock *> &Exits) const {
  if (LB.getLoop()) {
    LB.getLoop()->getExitBlocks(Exits);
  } else {
    assert(LB.getSccNum() != -1 && "LB doesn't belong to any loop?");
    SccI->getSccExitBlocks(LB.getSccNum(), Exits);
  }
}

// Propagate existing explicit probabilities from either profile data or
// 'expect' intrinsic processing. Examine metadata against unreachable
// heuristic. The probability of the edge coming to unreachable block is
// set to min of metadata and unreachable heuristic.
bool BranchProbabilityInfo::calcMetadataWeights(const BasicBlock *BB) {
  const Instruction *TI = BB->getTerminator();
  assert(TI->getNumSuccessors() > 1 && "expected more than one successor!");
  if (!(isa<BranchInst>(TI) || isa<SwitchInst>(TI) || isa<IndirectBrInst>(TI) ||
        isa<InvokeInst>(TI) || isa<CallBrInst>(TI)))
    return false;

  MDNode *WeightsNode = getValidBranchWeightMDNode(*TI);
  if (!WeightsNode)
    return false;

  // Check that the number of successors is manageable.
  assert(TI->getNumSuccessors() < UINT32_MAX && "Too many successors");

  // Build up the final weights that will be used in a temporary buffer.
  // Compute the sum of all weights to later decide whether they need to
  // be scaled to fit in 32 bits.
  uint64_t WeightSum = 0;
  SmallVector<uint32_t, 2> Weights;
  SmallVector<unsigned, 2> UnreachableIdxs;
  SmallVector<unsigned, 2> ReachableIdxs;

  extractBranchWeights(WeightsNode, Weights);
  for (unsigned I = 0, E = Weights.size(); I != E; ++I) {
    WeightSum += Weights[I];
    const LoopBlock SrcLoopBB = getLoopBlock(BB);
    const LoopBlock DstLoopBB = getLoopBlock(TI->getSuccessor(I));
    auto EstimatedWeight = getEstimatedEdgeWeight({SrcLoopBB, DstLoopBB});
    if (EstimatedWeight &&
        *EstimatedWeight <= static_cast<uint32_t>(BlockExecWeight::UNREACHABLE))
      UnreachableIdxs.push_back(I);
    else
      ReachableIdxs.push_back(I);
  }
  assert(Weights.size() == TI->getNumSuccessors() && "Checked above");

  // If the sum of weights does not fit in 32 bits, scale every weight down
  // accordingly.
  uint64_t ScalingFactor =
      (WeightSum > UINT32_MAX) ? WeightSum / UINT32_MAX + 1 : 1;

  if (ScalingFactor > 1) {
    WeightSum = 0;
    for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I) {
      Weights[I] /= ScalingFactor;
      WeightSum += Weights[I];
    }
  }
  assert(WeightSum <= UINT32_MAX &&
         "Expected weights to scale down to 32 bits");

  if (WeightSum == 0 || ReachableIdxs.size() == 0) {
    for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I)
      Weights[I] = 1;
    WeightSum = TI->getNumSuccessors();
  }

  // Set the probability.
  SmallVector<BranchProbability, 2> BP;
  for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I)
    BP.push_back({ Weights[I], static_cast<uint32_t>(WeightSum) });

  // Examine the metadata against unreachable heuristic.
  // If the unreachable heuristic is more strong then we use it for this edge.
  if (UnreachableIdxs.size() == 0 || ReachableIdxs.size() == 0) {
    setEdgeProbability(BB, BP);
    return true;
  }

  auto UnreachableProb = UR_TAKEN_PROB;
  for (auto I : UnreachableIdxs)
    if (UnreachableProb < BP[I]) {
      BP[I] = UnreachableProb;
    }

  // Sum of all edge probabilities must be 1.0. If we modified the probability
  // of some edges then we must distribute the introduced difference over the
  // reachable blocks.
  //
  // Proportional distribution: the relation between probabilities of the
  // reachable edges is kept unchanged. That is for any reachable edges i and j:
  //   newBP[i] / newBP[j] == oldBP[i] / oldBP[j] =>
  //   newBP[i] / oldBP[i] == newBP[j] / oldBP[j] == K
  // Where K is independent of i,j.
  //   newBP[i] == oldBP[i] * K
  // We need to find K.
  // Make sum of all reachables of the left and right parts:
  //   sum_of_reachable(newBP) == K * sum_of_reachable(oldBP)
  // Sum of newBP must be equal to 1.0:
  //   sum_of_reachable(newBP) + sum_of_unreachable(newBP) == 1.0 =>
  //   sum_of_reachable(newBP) = 1.0 - sum_of_unreachable(newBP)
  // Where sum_of_unreachable(newBP) is what has been just changed.
  // Finally:
  //   K == sum_of_reachable(newBP) / sum_of_reachable(oldBP) =>
  //   K == (1.0 - sum_of_unreachable(newBP)) / sum_of_reachable(oldBP)
  BranchProbability NewUnreachableSum = BranchProbability::getZero();
  for (auto I : UnreachableIdxs)
    NewUnreachableSum += BP[I];

  BranchProbability NewReachableSum =
      BranchProbability::getOne() - NewUnreachableSum;

  BranchProbability OldReachableSum = BranchProbability::getZero();
  for (auto I : ReachableIdxs)
    OldReachableSum += BP[I];

  if (OldReachableSum != NewReachableSum) { // Anything to dsitribute?
    if (OldReachableSum.isZero()) {
      // If all oldBP[i] are zeroes then the proportional distribution results
      // in all zero probabilities and the error stays big. In this case we
      // evenly spread NewReachableSum over the reachable edges.
      BranchProbability PerEdge = NewReachableSum / ReachableIdxs.size();
      for (auto I : ReachableIdxs)
        BP[I] = PerEdge;
    } else {
      for (auto I : ReachableIdxs) {
        // We use uint64_t to avoid double rounding error of the following
        // calculation: BP[i] = BP[i] * NewReachableSum / OldReachableSum
        // The formula is taken from the private constructor
        // BranchProbability(uint32_t Numerator, uint32_t Denominator)
        uint64_t Mul = static_cast<uint64_t>(NewReachableSum.getNumerator()) *
                       BP[I].getNumerator();
        uint32_t Div = static_cast<uint32_t>(
            divideNearest(Mul, OldReachableSum.getNumerator()));
        BP[I] = BranchProbability::getRaw(Div);
      }
    }
  }

  setEdgeProbability(BB, BP);

  return true;
}

// Calculate Edge Weights using "Pointer Heuristics". Predict a comparison
// between two pointer or pointer and NULL will fail.
bool BranchProbabilityInfo::calcPointerHeuristics(const BasicBlock *BB) {
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  ICmpInst *CI = dyn_cast<ICmpInst>(Cond);
  if (!CI || !CI->isEquality())
    return false;

  Value *LHS = CI->getOperand(0);

  if (!LHS->getType()->isPointerTy())
    return false;

  assert(CI->getOperand(1)->getType()->isPointerTy());

  auto Search = PointerTable.find(CI->getPredicate());
  if (Search == PointerTable.end())
    return false;
  setEdgeProbability(BB, Search->second);
  return true;
}

// Compute the unlikely successors to the block BB in the loop L, specifically
// those that are unlikely because this is a loop, and add them to the
// UnlikelyBlocks set.
static void
computeUnlikelySuccessors(const BasicBlock *BB, Loop *L,
                          SmallPtrSetImpl<const BasicBlock*> &UnlikelyBlocks) {
  // Sometimes in a loop we have a branch whose condition is made false by
  // taking it. This is typically something like
  //  int n = 0;
  //  while (...) {
  //    if (++n >= MAX) {
  //      n = 0;
  //    }
  //  }
  // In this sort of situation taking the branch means that at the very least it
  // won't be taken again in the next iteration of the loop, so we should
  // consider it less likely than a typical branch.
  //
  // We detect this by looking back through the graph of PHI nodes that sets the
  // value that the condition depends on, and seeing if we can reach a successor
  // block which can be determined to make the condition false.
  //
  // FIXME: We currently consider unlikely blocks to be half as likely as other
  // blocks, but if we consider the example above the likelyhood is actually
  // 1/MAX. We could therefore be more precise in how unlikely we consider
  // blocks to be, but it would require more careful examination of the form
  // of the comparison expression.
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return;

  // Check if the branch is based on an instruction compared with a constant
  CmpInst *CI = dyn_cast<CmpInst>(BI->getCondition());
  if (!CI || !isa<Instruction>(CI->getOperand(0)) ||
      !isa<Constant>(CI->getOperand(1)))
    return;

  // Either the instruction must be a PHI, or a chain of operations involving
  // constants that ends in a PHI which we can then collapse into a single value
  // if the PHI value is known.
  Instruction *CmpLHS = dyn_cast<Instruction>(CI->getOperand(0));
  PHINode *CmpPHI = dyn_cast<PHINode>(CmpLHS);
  Constant *CmpConst = dyn_cast<Constant>(CI->getOperand(1));
  // Collect the instructions until we hit a PHI
  SmallVector<BinaryOperator *, 1> InstChain;
  while (!CmpPHI && CmpLHS && isa<BinaryOperator>(CmpLHS) &&
         isa<Constant>(CmpLHS->getOperand(1))) {
    // Stop if the chain extends outside of the loop
    if (!L->contains(CmpLHS))
      return;
    InstChain.push_back(cast<BinaryOperator>(CmpLHS));
    CmpLHS = dyn_cast<Instruction>(CmpLHS->getOperand(0));
    if (CmpLHS)
      CmpPHI = dyn_cast<PHINode>(CmpLHS);
  }
  if (!CmpPHI || !L->contains(CmpPHI))
    return;

  // Trace the phi node to find all values that come from successors of BB
  SmallPtrSet<PHINode*, 8> VisitedInsts;
  SmallVector<PHINode*, 8> WorkList;
  WorkList.push_back(CmpPHI);
  VisitedInsts.insert(CmpPHI);
  while (!WorkList.empty()) {
    PHINode *P = WorkList.pop_back_val();
    for (BasicBlock *B : P->blocks()) {
      // Skip blocks that aren't part of the loop
      if (!L->contains(B))
        continue;
      Value *V = P->getIncomingValueForBlock(B);
      // If the source is a PHI add it to the work list if we haven't
      // already visited it.
      if (PHINode *PN = dyn_cast<PHINode>(V)) {
        if (VisitedInsts.insert(PN).second)
          WorkList.push_back(PN);
        continue;
      }
      // If this incoming value is a constant and B is a successor of BB, then
      // we can constant-evaluate the compare to see if it makes the branch be
      // taken or not.
      Constant *CmpLHSConst = dyn_cast<Constant>(V);
      if (!CmpLHSConst || !llvm::is_contained(successors(BB), B))
        continue;
      // First collapse InstChain
      const DataLayout &DL = BB->getDataLayout();
      for (Instruction *I : llvm::reverse(InstChain)) {
        CmpLHSConst = ConstantFoldBinaryOpOperands(
            I->getOpcode(), CmpLHSConst, cast<Constant>(I->getOperand(1)), DL);
        if (!CmpLHSConst)
          break;
      }
      if (!CmpLHSConst)
        continue;
      // Now constant-evaluate the compare
      Constant *Result = ConstantFoldCompareInstOperands(
          CI->getPredicate(), CmpLHSConst, CmpConst, DL);
      // If the result means we don't branch to the block then that block is
      // unlikely.
      if (Result &&
          ((Result->isZeroValue() && B == BI->getSuccessor(0)) ||
           (Result->isOneValue() && B == BI->getSuccessor(1))))
        UnlikelyBlocks.insert(B);
    }
  }
}

std::optional<uint32_t>
BranchProbabilityInfo::getEstimatedBlockWeight(const BasicBlock *BB) const {
  auto WeightIt = EstimatedBlockWeight.find(BB);
  if (WeightIt == EstimatedBlockWeight.end())
    return std::nullopt;
  return WeightIt->second;
}

std::optional<uint32_t>
BranchProbabilityInfo::getEstimatedLoopWeight(const LoopData &L) const {
  auto WeightIt = EstimatedLoopWeight.find(L);
  if (WeightIt == EstimatedLoopWeight.end())
    return std::nullopt;
  return WeightIt->second;
}

std::optional<uint32_t>
BranchProbabilityInfo::getEstimatedEdgeWeight(const LoopEdge &Edge) const {
  // For edges entering a loop take weight of a loop rather than an individual
  // block in the loop.
  return isLoopEnteringEdge(Edge)
             ? getEstimatedLoopWeight(Edge.second.getLoopData())
             : getEstimatedBlockWeight(Edge.second.getBlock());
}

template <class IterT>
std::optional<uint32_t> BranchProbabilityInfo::getMaxEstimatedEdgeWeight(
    const LoopBlock &SrcLoopBB, iterator_range<IterT> Successors) const {
  SmallVector<uint32_t, 4> Weights;
  std::optional<uint32_t> MaxWeight;
  for (const BasicBlock *DstBB : Successors) {
    const LoopBlock DstLoopBB = getLoopBlock(DstBB);
    auto Weight = getEstimatedEdgeWeight({SrcLoopBB, DstLoopBB});

    if (!Weight)
      return std::nullopt;

    if (!MaxWeight || *MaxWeight < *Weight)
      MaxWeight = Weight;
  }

  return MaxWeight;
}

// Updates \p LoopBB's weight and returns true. If \p LoopBB has already
// an associated weight it is unchanged and false is returned.
//
// Please note by the algorithm the weight is not expected to change once set
// thus 'false' status is used to track visited blocks.
bool BranchProbabilityInfo::updateEstimatedBlockWeight(
    LoopBlock &LoopBB, uint32_t BBWeight,
    SmallVectorImpl<BasicBlock *> &BlockWorkList,
    SmallVectorImpl<LoopBlock> &LoopWorkList) {
  BasicBlock *BB = LoopBB.getBlock();

  // In general, weight is assigned to a block when it has final value and
  // can't/shouldn't be changed.  However, there are cases when a block
  // inherently has several (possibly "contradicting") weights. For example,
  // "unwind" block may also contain "cold" call. In that case the first
  // set weight is favored and all consequent weights are ignored.
  if (!EstimatedBlockWeight.insert({BB, BBWeight}).second)
    return false;

  for (BasicBlock *PredBlock : predecessors(BB)) {
    LoopBlock PredLoop = getLoopBlock(PredBlock);
    // Add affected block/loop to a working list.
    if (isLoopExitingEdge({PredLoop, LoopBB})) {
      if (!EstimatedLoopWeight.count(PredLoop.getLoopData()))
        LoopWorkList.push_back(PredLoop);
    } else if (!EstimatedBlockWeight.count(PredBlock))
      BlockWorkList.push_back(PredBlock);
  }
  return true;
}

// Starting from \p BB traverse through dominator blocks and assign \p BBWeight
// to all such blocks that are post dominated by \BB. In other words to all
// blocks that the one is executed if and only if another one is executed.
// Importantly, we skip loops here for two reasons. First weights of blocks in
// a loop should be scaled by trip count (yet possibly unknown). Second there is
// no any value in doing that because that doesn't give any additional
// information regarding distribution of probabilities inside the loop.
// Exception is loop 'enter' and 'exit' edges that are handled in a special way
// at calcEstimatedHeuristics.
//
// In addition, \p WorkList is populated with basic blocks if at leas one
// successor has updated estimated weight.
void BranchProbabilityInfo::propagateEstimatedBlockWeight(
    const LoopBlock &LoopBB, DominatorTree *DT, PostDominatorTree *PDT,
    uint32_t BBWeight, SmallVectorImpl<BasicBlock *> &BlockWorkList,
    SmallVectorImpl<LoopBlock> &LoopWorkList) {
  const BasicBlock *BB = LoopBB.getBlock();
  const auto *DTStartNode = DT->getNode(BB);
  const auto *PDTStartNode = PDT->getNode(BB);

  // TODO: Consider propagating weight down the domination line as well.
  for (const auto *DTNode = DTStartNode; DTNode != nullptr;
       DTNode = DTNode->getIDom()) {
    auto *DomBB = DTNode->getBlock();
    // Consider blocks which lie on one 'line'.
    if (!PDT->dominates(PDTStartNode, PDT->getNode(DomBB)))
      // If BB doesn't post dominate DomBB it will not post dominate dominators
      // of DomBB as well.
      break;

    LoopBlock DomLoopBB = getLoopBlock(DomBB);
    const LoopEdge Edge{DomLoopBB, LoopBB};
    // Don't propagate weight to blocks belonging to different loops.
    if (!isLoopEnteringExitingEdge(Edge)) {
      if (!updateEstimatedBlockWeight(DomLoopBB, BBWeight, BlockWorkList,
                                      LoopWorkList))
        // If DomBB has weight set then all it's predecessors are already
        // processed (since we propagate weight up to the top of IR each time).
        break;
    } else if (isLoopExitingEdge(Edge)) {
      LoopWorkList.push_back(DomLoopBB);
    }
  }
}

std::optional<uint32_t>
BranchProbabilityInfo::getInitialEstimatedBlockWeight(const BasicBlock *BB) {
  // Returns true if \p BB has call marked with "NoReturn" attribute.
  auto hasNoReturn = [&](const BasicBlock *BB) {
    for (const auto &I : reverse(*BB))
      if (const CallInst *CI = dyn_cast<CallInst>(&I))
        if (CI->hasFnAttr(Attribute::NoReturn))
          return true;

    return false;
  };

  // Important note regarding the order of checks. They are ordered by weight
  // from lowest to highest. Doing that allows to avoid "unstable" results
  // when several conditions heuristics can be applied simultaneously.
  if (isa<UnreachableInst>(BB->getTerminator()) ||
      // If this block is terminated by a call to
      // @llvm.experimental.deoptimize then treat it like an unreachable
      // since it is expected to practically never execute.
      // TODO: Should we actually treat as never returning call?
      BB->getTerminatingDeoptimizeCall())
    return hasNoReturn(BB)
               ? static_cast<uint32_t>(BlockExecWeight::NORETURN)
               : static_cast<uint32_t>(BlockExecWeight::UNREACHABLE);

  // Check if the block is an exception handling block.
  if (BB->isEHPad())
    return static_cast<uint32_t>(BlockExecWeight::UNWIND);

  // Check if the block contains 'cold' call.
  for (const auto &I : *BB)
    if (const CallInst *CI = dyn_cast<CallInst>(&I))
      if (CI->hasFnAttr(Attribute::Cold))
        return static_cast<uint32_t>(BlockExecWeight::COLD);

  return std::nullopt;
}

// Does RPO traversal over all blocks in \p F and assigns weights to
// 'unreachable', 'noreturn', 'cold', 'unwind' blocks. In addition it does its
// best to propagate the weight to up/down the IR.
void BranchProbabilityInfo::computeEestimateBlockWeight(
    const Function &F, DominatorTree *DT, PostDominatorTree *PDT) {
  SmallVector<BasicBlock *, 8> BlockWorkList;
  SmallVector<LoopBlock, 8> LoopWorkList;
  SmallDenseMap<LoopData, SmallVector<BasicBlock *, 4>> LoopExitBlocks;

  // By doing RPO we make sure that all predecessors already have weights
  // calculated before visiting theirs successors.
  ReversePostOrderTraversal<const Function *> RPOT(&F);
  for (const auto *BB : RPOT)
    if (auto BBWeight = getInitialEstimatedBlockWeight(BB))
      // If we were able to find estimated weight for the block set it to this
      // block and propagate up the IR.
      propagateEstimatedBlockWeight(getLoopBlock(BB), DT, PDT, *BBWeight,
                                    BlockWorkList, LoopWorkList);

  // BlockWorklist/LoopWorkList contains blocks/loops with at least one
  // successor/exit having estimated weight. Try to propagate weight to such
  // blocks/loops from successors/exits.
  // Process loops and blocks. Order is not important.
  do {
    while (!LoopWorkList.empty()) {
      const LoopBlock LoopBB = LoopWorkList.pop_back_val();
      const LoopData LD = LoopBB.getLoopData();
      if (EstimatedLoopWeight.count(LD))
        continue;

      auto Res = LoopExitBlocks.try_emplace(LD);
      SmallVectorImpl<BasicBlock *> &Exits = Res.first->second;
      if (Res.second)
        getLoopExitBlocks(LoopBB, Exits);
      auto LoopWeight = getMaxEstimatedEdgeWeight(
          LoopBB, make_range(Exits.begin(), Exits.end()));

      if (LoopWeight) {
        // If we never exit the loop then we can enter it once at maximum.
        if (LoopWeight <= static_cast<uint32_t>(BlockExecWeight::UNREACHABLE))
          LoopWeight = static_cast<uint32_t>(BlockExecWeight::LOWEST_NON_ZERO);

        EstimatedLoopWeight.insert({LD, *LoopWeight});
        // Add all blocks entering the loop into working list.
        getLoopEnterBlocks(LoopBB, BlockWorkList);
      }
    }

    while (!BlockWorkList.empty()) {
      // We can reach here only if BlockWorkList is not empty.
      const BasicBlock *BB = BlockWorkList.pop_back_val();
      if (EstimatedBlockWeight.count(BB))
        continue;

      // We take maximum over all weights of successors. In other words we take
      // weight of "hot" path. In theory we can probably find a better function
      // which gives higher accuracy results (comparing to "maximum") but I
      // can't
      // think of any right now. And I doubt it will make any difference in
      // practice.
      const LoopBlock LoopBB = getLoopBlock(BB);
      auto MaxWeight = getMaxEstimatedEdgeWeight(LoopBB, successors(BB));

      if (MaxWeight)
        propagateEstimatedBlockWeight(LoopBB, DT, PDT, *MaxWeight,
                                      BlockWorkList, LoopWorkList);
    }
  } while (!BlockWorkList.empty() || !LoopWorkList.empty());
}

// Calculate edge probabilities based on block's estimated weight.
// Note that gathered weights were not scaled for loops. Thus edges entering
// and exiting loops requires special processing.
bool BranchProbabilityInfo::calcEstimatedHeuristics(const BasicBlock *BB) {
  assert(BB->getTerminator()->getNumSuccessors() > 1 &&
         "expected more than one successor!");

  const LoopBlock LoopBB = getLoopBlock(BB);

  SmallPtrSet<const BasicBlock *, 8> UnlikelyBlocks;
  uint32_t TC = LBH_TAKEN_WEIGHT / LBH_NONTAKEN_WEIGHT;
  if (LoopBB.getLoop())
    computeUnlikelySuccessors(BB, LoopBB.getLoop(), UnlikelyBlocks);

  // Changed to 'true' if at least one successor has estimated weight.
  bool FoundEstimatedWeight = false;
  SmallVector<uint32_t, 4> SuccWeights;
  uint64_t TotalWeight = 0;
  // Go over all successors of BB and put their weights into SuccWeights.
  for (const BasicBlock *SuccBB : successors(BB)) {
    std::optional<uint32_t> Weight;
    const LoopBlock SuccLoopBB = getLoopBlock(SuccBB);
    const LoopEdge Edge{LoopBB, SuccLoopBB};

    Weight = getEstimatedEdgeWeight(Edge);

    if (isLoopExitingEdge(Edge) &&
        // Avoid adjustment of ZERO weight since it should remain unchanged.
        Weight != static_cast<uint32_t>(BlockExecWeight::ZERO)) {
      // Scale down loop exiting weight by trip count.
      Weight = std::max(
          static_cast<uint32_t>(BlockExecWeight::LOWEST_NON_ZERO),
          Weight.value_or(static_cast<uint32_t>(BlockExecWeight::DEFAULT)) /
              TC);
    }
    bool IsUnlikelyEdge = LoopBB.getLoop() && UnlikelyBlocks.contains(SuccBB);
    if (IsUnlikelyEdge &&
        // Avoid adjustment of ZERO weight since it should remain unchanged.
        Weight != static_cast<uint32_t>(BlockExecWeight::ZERO)) {
      // 'Unlikely' blocks have twice lower weight.
      Weight = std::max(
          static_cast<uint32_t>(BlockExecWeight::LOWEST_NON_ZERO),
          Weight.value_or(static_cast<uint32_t>(BlockExecWeight::DEFAULT)) / 2);
    }

    if (Weight)
      FoundEstimatedWeight = true;

    auto WeightVal =
        Weight.value_or(static_cast<uint32_t>(BlockExecWeight::DEFAULT));
    TotalWeight += WeightVal;
    SuccWeights.push_back(WeightVal);
  }

  // If non of blocks have estimated weight bail out.
  // If TotalWeight is 0 that means weight of each successor is 0 as well and
  // equally likely. Bail out early to not deal with devision by zero.
  if (!FoundEstimatedWeight || TotalWeight == 0)
    return false;

  assert(SuccWeights.size() == succ_size(BB) && "Missed successor?");
  const unsigned SuccCount = SuccWeights.size();

  // If the sum of weights does not fit in 32 bits, scale every weight down
  // accordingly.
  if (TotalWeight > UINT32_MAX) {
    uint64_t ScalingFactor = TotalWeight / UINT32_MAX + 1;
    TotalWeight = 0;
    for (unsigned Idx = 0; Idx < SuccCount; ++Idx) {
      SuccWeights[Idx] /= ScalingFactor;
      if (SuccWeights[Idx] == static_cast<uint32_t>(BlockExecWeight::ZERO))
        SuccWeights[Idx] =
            static_cast<uint32_t>(BlockExecWeight::LOWEST_NON_ZERO);
      TotalWeight += SuccWeights[Idx];
    }
    assert(TotalWeight <= UINT32_MAX && "Total weight overflows");
  }

  // Finally set probabilities to edges according to estimated block weights.
  SmallVector<BranchProbability, 4> EdgeProbabilities(
      SuccCount, BranchProbability::getUnknown());

  for (unsigned Idx = 0; Idx < SuccCount; ++Idx) {
    EdgeProbabilities[Idx] =
        BranchProbability(SuccWeights[Idx], (uint32_t)TotalWeight);
  }
  setEdgeProbability(BB, EdgeProbabilities);
  return true;
}

bool BranchProbabilityInfo::calcZeroHeuristics(const BasicBlock *BB,
                                               const TargetLibraryInfo *TLI) {
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  ICmpInst *CI = dyn_cast<ICmpInst>(Cond);
  if (!CI)
    return false;

  auto GetConstantInt = [](Value *V) {
    if (auto *I = dyn_cast<BitCastInst>(V))
      return dyn_cast<ConstantInt>(I->getOperand(0));
    return dyn_cast<ConstantInt>(V);
  };

  Value *RHS = CI->getOperand(1);
  ConstantInt *CV = GetConstantInt(RHS);
  if (!CV)
    return false;

  // If the LHS is the result of AND'ing a value with a single bit bitmask,
  // we don't have information about probabilities.
  if (Instruction *LHS = dyn_cast<Instruction>(CI->getOperand(0)))
    if (LHS->getOpcode() == Instruction::And)
      if (ConstantInt *AndRHS = GetConstantInt(LHS->getOperand(1)))
        if (AndRHS->getValue().isPowerOf2())
          return false;

  // Check if the LHS is the return value of a library function
  LibFunc Func = NumLibFuncs;
  if (TLI)
    if (CallInst *Call = dyn_cast<CallInst>(CI->getOperand(0)))
      if (Function *CalledFn = Call->getCalledFunction())
        TLI->getLibFunc(*CalledFn, Func);

  ProbabilityTable::const_iterator Search;
  if (Func == LibFunc_strcasecmp ||
      Func == LibFunc_strcmp ||
      Func == LibFunc_strncasecmp ||
      Func == LibFunc_strncmp ||
      Func == LibFunc_memcmp ||
      Func == LibFunc_bcmp) {
    Search = ICmpWithLibCallTable.find(CI->getPredicate());
    if (Search == ICmpWithLibCallTable.end())
      return false;
  } else if (CV->isZero()) {
    Search = ICmpWithZeroTable.find(CI->getPredicate());
    if (Search == ICmpWithZeroTable.end())
      return false;
  } else if (CV->isOne()) {
    Search = ICmpWithOneTable.find(CI->getPredicate());
    if (Search == ICmpWithOneTable.end())
      return false;
  } else if (CV->isMinusOne()) {
    Search = ICmpWithMinusOneTable.find(CI->getPredicate());
    if (Search == ICmpWithMinusOneTable.end())
      return false;
  } else {
    return false;
  }

  setEdgeProbability(BB, Search->second);
  return true;
}

bool BranchProbabilityInfo::calcFloatingPointHeuristics(const BasicBlock *BB) {
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  FCmpInst *FCmp = dyn_cast<FCmpInst>(Cond);
  if (!FCmp)
    return false;

  ProbabilityList ProbList;
  if (FCmp->isEquality()) {
    ProbList = !FCmp->isTrueWhenEqual() ?
      // f1 == f2 -> Unlikely
      ProbabilityList({FPTakenProb, FPUntakenProb}) :
      // f1 != f2 -> Likely
      ProbabilityList({FPUntakenProb, FPTakenProb});
  } else {
    auto Search = FCmpTable.find(FCmp->getPredicate());
    if (Search == FCmpTable.end())
      return false;
    ProbList = Search->second;
  }

  setEdgeProbability(BB, ProbList);
  return true;
}

void BranchProbabilityInfo::releaseMemory() {
  Probs.clear();
  Handles.clear();
}

bool BranchProbabilityInfo::invalidate(Function &, const PreservedAnalyses &PA,
                                       FunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on functions, or the function's
  // CFG have been preserved.
  auto PAC = PA.getChecker<BranchProbabilityAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>() ||
           PAC.preservedSet<CFGAnalyses>());
}

void BranchProbabilityInfo::print(raw_ostream &OS) const {
  OS << "---- Branch Probabilities ----\n";
  // We print the probabilities from the last function the analysis ran over,
  // or the function it is currently running over.
  assert(LastF && "Cannot print prior to running over a function");
  for (const auto &BI : *LastF) {
    for (const BasicBlock *Succ : successors(&BI))
      printEdgeProbability(OS << "  ", &BI, Succ);
  }
}

bool BranchProbabilityInfo::
isEdgeHot(const BasicBlock *Src, const BasicBlock *Dst) const {
  // Hot probability is at least 4/5 = 80%
  // FIXME: Compare against a static "hot" BranchProbability.
  return getEdgeProbability(Src, Dst) > BranchProbability(4, 5);
}

/// Get the raw edge probability for the edge. If can't find it, return a
/// default probability 1/N where N is the number of successors. Here an edge is
/// specified using PredBlock and an
/// index to the successors.
BranchProbability
BranchProbabilityInfo::getEdgeProbability(const BasicBlock *Src,
                                          unsigned IndexInSuccessors) const {
  auto I = Probs.find(std::make_pair(Src, IndexInSuccessors));
  assert((Probs.end() == Probs.find(std::make_pair(Src, 0))) ==
             (Probs.end() == I) &&
         "Probability for I-th successor must always be defined along with the "
         "probability for the first successor");

  if (I != Probs.end())
    return I->second;

  return {1, static_cast<uint32_t>(succ_size(Src))};
}

BranchProbability
BranchProbabilityInfo::getEdgeProbability(const BasicBlock *Src,
                                          const_succ_iterator Dst) const {
  return getEdgeProbability(Src, Dst.getSuccessorIndex());
}

/// Get the raw edge probability calculated for the block pair. This returns the
/// sum of all raw edge probabilities from Src to Dst.
BranchProbability
BranchProbabilityInfo::getEdgeProbability(const BasicBlock *Src,
                                          const BasicBlock *Dst) const {
  if (!Probs.count(std::make_pair(Src, 0)))
    return BranchProbability(llvm::count(successors(Src), Dst), succ_size(Src));

  auto Prob = BranchProbability::getZero();
  for (const_succ_iterator I = succ_begin(Src), E = succ_end(Src); I != E; ++I)
    if (*I == Dst)
      Prob += Probs.find(std::make_pair(Src, I.getSuccessorIndex()))->second;

  return Prob;
}

/// Set the edge probability for all edges at once.
void BranchProbabilityInfo::setEdgeProbability(
    const BasicBlock *Src, const SmallVectorImpl<BranchProbability> &Probs) {
  assert(Src->getTerminator()->getNumSuccessors() == Probs.size());
  eraseBlock(Src); // Erase stale data if any.
  if (Probs.size() == 0)
    return; // Nothing to set.

  Handles.insert(BasicBlockCallbackVH(Src, this));
  uint64_t TotalNumerator = 0;
  for (unsigned SuccIdx = 0; SuccIdx < Probs.size(); ++SuccIdx) {
    this->Probs[std::make_pair(Src, SuccIdx)] = Probs[SuccIdx];
    LLVM_DEBUG(dbgs() << "set edge " << Src->getName() << " -> " << SuccIdx
                      << " successor probability to " << Probs[SuccIdx]
                      << "\n");
    TotalNumerator += Probs[SuccIdx].getNumerator();
  }

  // Because of rounding errors the total probability cannot be checked to be
  // 1.0 exactly. That is TotalNumerator == BranchProbability::getDenominator.
  // Instead, every single probability in Probs must be as accurate as possible.
  // This results in error 1/denominator at most, thus the total absolute error
  // should be within Probs.size / BranchProbability::getDenominator.
  assert(TotalNumerator <= BranchProbability::getDenominator() + Probs.size());
  assert(TotalNumerator >= BranchProbability::getDenominator() - Probs.size());
  (void)TotalNumerator;
}

void BranchProbabilityInfo::copyEdgeProbabilities(BasicBlock *Src,
                                                  BasicBlock *Dst) {
  eraseBlock(Dst); // Erase stale data if any.
  unsigned NumSuccessors = Src->getTerminator()->getNumSuccessors();
  assert(NumSuccessors == Dst->getTerminator()->getNumSuccessors());
  if (NumSuccessors == 0)
    return; // Nothing to set.
  if (!this->Probs.contains(std::make_pair(Src, 0)))
    return; // No probability is set for edges from Src. Keep the same for Dst.

  Handles.insert(BasicBlockCallbackVH(Dst, this));
  for (unsigned SuccIdx = 0; SuccIdx < NumSuccessors; ++SuccIdx) {
    auto Prob = this->Probs[std::make_pair(Src, SuccIdx)];
    this->Probs[std::make_pair(Dst, SuccIdx)] = Prob;
    LLVM_DEBUG(dbgs() << "set edge " << Dst->getName() << " -> " << SuccIdx
                      << " successor probability to " << Prob << "\n");
  }
}

void BranchProbabilityInfo::swapSuccEdgesProbabilities(const BasicBlock *Src) {
  assert(Src->getTerminator()->getNumSuccessors() == 2);
  if (!Probs.contains(std::make_pair(Src, 0)))
    return; // No probability is set for edges from Src
  assert(Probs.contains(std::make_pair(Src, 1)));
  std::swap(Probs[std::make_pair(Src, 0)], Probs[std::make_pair(Src, 1)]);
}

raw_ostream &
BranchProbabilityInfo::printEdgeProbability(raw_ostream &OS,
                                            const BasicBlock *Src,
                                            const BasicBlock *Dst) const {
  const BranchProbability Prob = getEdgeProbability(Src, Dst);
  OS << "edge ";
  Src->printAsOperand(OS, false, Src->getModule());
  OS << " -> ";
  Dst->printAsOperand(OS, false, Dst->getModule());
  OS << " probability is " << Prob
     << (isEdgeHot(Src, Dst) ? " [HOT edge]\n" : "\n");

  return OS;
}

void BranchProbabilityInfo::eraseBlock(const BasicBlock *BB) {
  LLVM_DEBUG(dbgs() << "eraseBlock " << BB->getName() << "\n");

  // Note that we cannot use successors of BB because the terminator of BB may
  // have changed when eraseBlock is called as a BasicBlockCallbackVH callback.
  // Instead we remove prob data for the block by iterating successors by their
  // indices from 0 till the last which exists. There could not be prob data for
  // a pair (BB, N) if there is no data for (BB, N-1) because the data is always
  // set for all successors from 0 to M at once by the method
  // setEdgeProbability().
  Handles.erase(BasicBlockCallbackVH(BB, this));
  for (unsigned I = 0;; ++I) {
    auto MapI = Probs.find(std::make_pair(BB, I));
    if (MapI == Probs.end()) {
      assert(Probs.count(std::make_pair(BB, I + 1)) == 0 &&
             "Must be no more successors");
      return;
    }
    Probs.erase(MapI);
  }
}

void BranchProbabilityInfo::calculate(const Function &F, const LoopInfo &LoopI,
                                      const TargetLibraryInfo *TLI,
                                      DominatorTree *DT,
                                      PostDominatorTree *PDT) {
  LLVM_DEBUG(dbgs() << "---- Branch Probability Info : " << F.getName()
                    << " ----\n\n");
  LastF = &F; // Store the last function we ran on for printing.
  LI = &LoopI;

  SccI = std::make_unique<SccInfo>(F);

  assert(EstimatedBlockWeight.empty());
  assert(EstimatedLoopWeight.empty());

  std::unique_ptr<DominatorTree> DTPtr;
  std::unique_ptr<PostDominatorTree> PDTPtr;

  if (!DT) {
    DTPtr = std::make_unique<DominatorTree>(const_cast<Function &>(F));
    DT = DTPtr.get();
  }

  if (!PDT) {
    PDTPtr = std::make_unique<PostDominatorTree>(const_cast<Function &>(F));
    PDT = PDTPtr.get();
  }

  computeEestimateBlockWeight(F, DT, PDT);

  // Walk the basic blocks in post-order so that we can build up state about
  // the successors of a block iteratively.
  for (const auto *BB : post_order(&F.getEntryBlock())) {
    LLVM_DEBUG(dbgs() << "Computing probabilities for " << BB->getName()
                      << "\n");
    // If there is no at least two successors, no sense to set probability.
    if (BB->getTerminator()->getNumSuccessors() < 2)
      continue;
    if (calcMetadataWeights(BB))
      continue;
    if (calcEstimatedHeuristics(BB))
      continue;
    if (calcPointerHeuristics(BB))
      continue;
    if (calcZeroHeuristics(BB, TLI))
      continue;
    if (calcFloatingPointHeuristics(BB))
      continue;
  }

  EstimatedLoopWeight.clear();
  EstimatedBlockWeight.clear();
  SccI.reset();

  if (PrintBranchProb && (PrintBranchProbFuncName.empty() ||
                          F.getName() == PrintBranchProbFuncName)) {
    print(dbgs());
  }
}

void BranchProbabilityInfoWrapperPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  // We require DT so it's available when LI is available. The LI updating code
  // asserts that DT is also present so if we don't make sure that we have DT
  // here, that assert will trigger.
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<PostDominatorTreeWrapperPass>();
  AU.setPreservesAll();
}

bool BranchProbabilityInfoWrapperPass::runOnFunction(Function &F) {
  const LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  const TargetLibraryInfo &TLI =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  PostDominatorTree &PDT =
      getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
  BPI.calculate(F, LI, &TLI, &DT, &PDT);
  return false;
}

void BranchProbabilityInfoWrapperPass::releaseMemory() { BPI.releaseMemory(); }

void BranchProbabilityInfoWrapperPass::print(raw_ostream &OS,
                                             const Module *) const {
  BPI.print(OS);
}

AnalysisKey BranchProbabilityAnalysis::Key;
BranchProbabilityInfo
BranchProbabilityAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  BranchProbabilityInfo BPI;
  BPI.calculate(F, LI, &TLI, &DT, &PDT);
  return BPI;
}

PreservedAnalyses
BranchProbabilityPrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  OS << "Printing analysis 'Branch Probability Analysis' for function '"
     << F.getName() << "':\n";
  AM.getResult<BranchProbabilityAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}
