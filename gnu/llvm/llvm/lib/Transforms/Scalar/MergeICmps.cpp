//===- MergeICmps.cpp - Optimize chains of integer comparisons ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass turns chains of integer comparisons into memcmp (the memcmp is
// later typically inlined as a chain of efficient hardware comparisons). This
// typically benefits c++ member or nonmember operator==().
//
// The basic idea is to replace a longer chain of integer comparisons loaded
// from contiguous memory locations into a shorter chain of larger integer
// comparisons. Benefits are double:
//  - There are less jumps, and therefore less opportunities for mispredictions
//    and I-cache misses.
//  - Code size is smaller, both because jumps are removed and because the
//    encoding of a 2*n byte compare is smaller than that of two n-byte
//    compares.
//
// Example:
//
//  struct S {
//    int a;
//    char b;
//    char c;
//    uint16_t d;
//    bool operator==(const S& o) const {
//      return a == o.a && b == o.b && c == o.c && d == o.d;
//    }
//  };
//
//  Is optimized as :
//
//    bool S::operator==(const S& o) const {
//      return memcmp(this, &o, 8) == 0;
//    }
//
//  Which will later be expanded (ExpandMemCmp) as a single 8-bytes icmp.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/MergeICmps.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include <algorithm>
#include <numeric>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

#define DEBUG_TYPE "mergeicmps"

// A BCE atom "Binary Compare Expression Atom" represents an integer load
// that is a constant offset from a base value, e.g. `a` or `o.c` in the example
// at the top.
struct BCEAtom {
  BCEAtom() = default;
  BCEAtom(GetElementPtrInst *GEP, LoadInst *LoadI, int BaseId, APInt Offset)
      : GEP(GEP), LoadI(LoadI), BaseId(BaseId), Offset(std::move(Offset)) {}

  BCEAtom(const BCEAtom &) = delete;
  BCEAtom &operator=(const BCEAtom &) = delete;

  BCEAtom(BCEAtom &&that) = default;
  BCEAtom &operator=(BCEAtom &&that) {
    if (this == &that)
      return *this;
    GEP = that.GEP;
    LoadI = that.LoadI;
    BaseId = that.BaseId;
    Offset = std::move(that.Offset);
    return *this;
  }

  // We want to order BCEAtoms by (Base, Offset). However we cannot use
  // the pointer values for Base because these are non-deterministic.
  // To make sure that the sort order is stable, we first assign to each atom
  // base value an index based on its order of appearance in the chain of
  // comparisons. We call this index `BaseOrdering`. For example, for:
  //    b[3] == c[2] && a[1] == d[1] && b[4] == c[3]
  //    |  block 1 |    |  block 2 |    |  block 3 |
  // b gets assigned index 0 and a index 1, because b appears as LHS in block 1,
  // which is before block 2.
  // We then sort by (BaseOrdering[LHS.Base()], LHS.Offset), which is stable.
  bool operator<(const BCEAtom &O) const {
    return BaseId != O.BaseId ? BaseId < O.BaseId : Offset.slt(O.Offset);
  }

  GetElementPtrInst *GEP = nullptr;
  LoadInst *LoadI = nullptr;
  unsigned BaseId = 0;
  APInt Offset;
};

// A class that assigns increasing ids to values in the order in which they are
// seen. See comment in `BCEAtom::operator<()``.
class BaseIdentifier {
public:
  // Returns the id for value `Base`, after assigning one if `Base` has not been
  // seen before.
  int getBaseId(const Value *Base) {
    assert(Base && "invalid base");
    const auto Insertion = BaseToIndex.try_emplace(Base, Order);
    if (Insertion.second)
      ++Order;
    return Insertion.first->second;
  }

private:
  unsigned Order = 1;
  DenseMap<const Value*, int> BaseToIndex;
};

// If this value is a load from a constant offset w.r.t. a base address, and
// there are no other users of the load or address, returns the base address and
// the offset.
BCEAtom visitICmpLoadOperand(Value *const Val, BaseIdentifier &BaseId) {
  auto *const LoadI = dyn_cast<LoadInst>(Val);
  if (!LoadI)
    return {};
  LLVM_DEBUG(dbgs() << "load\n");
  if (LoadI->isUsedOutsideOfBlock(LoadI->getParent())) {
    LLVM_DEBUG(dbgs() << "used outside of block\n");
    return {};
  }
  // Do not optimize atomic loads to non-atomic memcmp
  if (!LoadI->isSimple()) {
    LLVM_DEBUG(dbgs() << "volatile or atomic\n");
    return {};
  }
  Value *Addr = LoadI->getOperand(0);
  if (Addr->getType()->getPointerAddressSpace() != 0) {
    LLVM_DEBUG(dbgs() << "from non-zero AddressSpace\n");
    return {};
  }
  const auto &DL = LoadI->getDataLayout();
  if (!isDereferenceablePointer(Addr, LoadI->getType(), DL)) {
    LLVM_DEBUG(dbgs() << "not dereferenceable\n");
    // We need to make sure that we can do comparison in any order, so we
    // require memory to be unconditionally dereferenceable.
    return {};
  }

  APInt Offset = APInt(DL.getIndexTypeSizeInBits(Addr->getType()), 0);
  Value *Base = Addr;
  auto *GEP = dyn_cast<GetElementPtrInst>(Addr);
  if (GEP) {
    LLVM_DEBUG(dbgs() << "GEP\n");
    if (GEP->isUsedOutsideOfBlock(LoadI->getParent())) {
      LLVM_DEBUG(dbgs() << "used outside of block\n");
      return {};
    }
    if (!GEP->accumulateConstantOffset(DL, Offset))
      return {};
    Base = GEP->getPointerOperand();
  }
  return BCEAtom(GEP, LoadI, BaseId.getBaseId(Base), Offset);
}

// A comparison between two BCE atoms, e.g. `a == o.a` in the example at the
// top.
// Note: the terminology is misleading: the comparison is symmetric, so there
// is no real {l/r}hs. What we want though is to have the same base on the
// left (resp. right), so that we can detect consecutive loads. To ensure this
// we put the smallest atom on the left.
struct BCECmp {
  BCEAtom Lhs;
  BCEAtom Rhs;
  int SizeBits;
  const ICmpInst *CmpI;

  BCECmp(BCEAtom L, BCEAtom R, int SizeBits, const ICmpInst *CmpI)
      : Lhs(std::move(L)), Rhs(std::move(R)), SizeBits(SizeBits), CmpI(CmpI) {
    if (Rhs < Lhs) std::swap(Rhs, Lhs);
  }
};

// A basic block with a comparison between two BCE atoms.
// The block might do extra work besides the atom comparison, in which case
// doesOtherWork() returns true. Under some conditions, the block can be
// split into the atom comparison part and the "other work" part
// (see canSplit()).
class BCECmpBlock {
 public:
  typedef SmallDenseSet<const Instruction *, 8> InstructionSet;

  BCECmpBlock(BCECmp Cmp, BasicBlock *BB, InstructionSet BlockInsts)
      : BB(BB), BlockInsts(std::move(BlockInsts)), Cmp(std::move(Cmp)) {}

  const BCEAtom &Lhs() const { return Cmp.Lhs; }
  const BCEAtom &Rhs() const { return Cmp.Rhs; }
  int SizeBits() const { return Cmp.SizeBits; }

  // Returns true if the block does other works besides comparison.
  bool doesOtherWork() const;

  // Returns true if the non-BCE-cmp instructions can be separated from BCE-cmp
  // instructions in the block.
  bool canSplit(AliasAnalysis &AA) const;

  // Return true if this all the relevant instructions in the BCE-cmp-block can
  // be sunk below this instruction. By doing this, we know we can separate the
  // BCE-cmp-block instructions from the non-BCE-cmp-block instructions in the
  // block.
  bool canSinkBCECmpInst(const Instruction *, AliasAnalysis &AA) const;

  // We can separate the BCE-cmp-block instructions and the non-BCE-cmp-block
  // instructions. Split the old block and move all non-BCE-cmp-insts into the
  // new parent block.
  void split(BasicBlock *NewParent, AliasAnalysis &AA) const;

  // The basic block where this comparison happens.
  BasicBlock *BB;
  // Instructions relating to the BCECmp and branch.
  InstructionSet BlockInsts;
  // The block requires splitting.
  bool RequireSplit = false;
  // Original order of this block in the chain.
  unsigned OrigOrder = 0;

private:
  BCECmp Cmp;
};

bool BCECmpBlock::canSinkBCECmpInst(const Instruction *Inst,
                                    AliasAnalysis &AA) const {
  // If this instruction may clobber the loads and is in middle of the BCE cmp
  // block instructions, then bail for now.
  if (Inst->mayWriteToMemory()) {
    auto MayClobber = [&](LoadInst *LI) {
      // If a potentially clobbering instruction comes before the load,
      // we can still safely sink the load.
      return (Inst->getParent() != LI->getParent() || !Inst->comesBefore(LI)) &&
             isModSet(AA.getModRefInfo(Inst, MemoryLocation::get(LI)));
    };
    if (MayClobber(Cmp.Lhs.LoadI) || MayClobber(Cmp.Rhs.LoadI))
      return false;
  }
  // Make sure this instruction does not use any of the BCE cmp block
  // instructions as operand.
  return llvm::none_of(Inst->operands(), [&](const Value *Op) {
    const Instruction *OpI = dyn_cast<Instruction>(Op);
    return OpI && BlockInsts.contains(OpI);
  });
}

void BCECmpBlock::split(BasicBlock *NewParent, AliasAnalysis &AA) const {
  llvm::SmallVector<Instruction *, 4> OtherInsts;
  for (Instruction &Inst : *BB) {
    if (BlockInsts.count(&Inst))
      continue;
    assert(canSinkBCECmpInst(&Inst, AA) && "Split unsplittable block");
    // This is a non-BCE-cmp-block instruction. And it can be separated
    // from the BCE-cmp-block instruction.
    OtherInsts.push_back(&Inst);
  }

  // Do the actual spliting.
  for (Instruction *Inst : reverse(OtherInsts))
    Inst->moveBeforePreserving(*NewParent, NewParent->begin());
}

bool BCECmpBlock::canSplit(AliasAnalysis &AA) const {
  for (Instruction &Inst : *BB) {
    if (!BlockInsts.count(&Inst)) {
      if (!canSinkBCECmpInst(&Inst, AA))
        return false;
    }
  }
  return true;
}

bool BCECmpBlock::doesOtherWork() const {
  // TODO(courbet): Can we allow some other things ? This is very conservative.
  // We might be able to get away with anything does not have any side
  // effects outside of the basic block.
  // Note: The GEPs and/or loads are not necessarily in the same block.
  for (const Instruction &Inst : *BB) {
    if (!BlockInsts.count(&Inst))
      return true;
  }
  return false;
}

// Visit the given comparison. If this is a comparison between two valid
// BCE atoms, returns the comparison.
std::optional<BCECmp> visitICmp(const ICmpInst *const CmpI,
                                const ICmpInst::Predicate ExpectedPredicate,
                                BaseIdentifier &BaseId) {
  // The comparison can only be used once:
  //  - For intermediate blocks, as a branch condition.
  //  - For the final block, as an incoming value for the Phi.
  // If there are any other uses of the comparison, we cannot merge it with
  // other comparisons as we would create an orphan use of the value.
  if (!CmpI->hasOneUse()) {
    LLVM_DEBUG(dbgs() << "cmp has several uses\n");
    return std::nullopt;
  }
  if (CmpI->getPredicate() != ExpectedPredicate)
    return std::nullopt;
  LLVM_DEBUG(dbgs() << "cmp "
                    << (ExpectedPredicate == ICmpInst::ICMP_EQ ? "eq" : "ne")
                    << "\n");
  auto Lhs = visitICmpLoadOperand(CmpI->getOperand(0), BaseId);
  if (!Lhs.BaseId)
    return std::nullopt;
  auto Rhs = visitICmpLoadOperand(CmpI->getOperand(1), BaseId);
  if (!Rhs.BaseId)
    return std::nullopt;
  const auto &DL = CmpI->getDataLayout();
  return BCECmp(std::move(Lhs), std::move(Rhs),
                DL.getTypeSizeInBits(CmpI->getOperand(0)->getType()), CmpI);
}

// Visit the given comparison block. If this is a comparison between two valid
// BCE atoms, returns the comparison.
std::optional<BCECmpBlock> visitCmpBlock(Value *const Val,
                                         BasicBlock *const Block,
                                         const BasicBlock *const PhiBlock,
                                         BaseIdentifier &BaseId) {
  if (Block->empty())
    return std::nullopt;
  auto *const BranchI = dyn_cast<BranchInst>(Block->getTerminator());
  if (!BranchI)
    return std::nullopt;
  LLVM_DEBUG(dbgs() << "branch\n");
  Value *Cond;
  ICmpInst::Predicate ExpectedPredicate;
  if (BranchI->isUnconditional()) {
    // In this case, we expect an incoming value which is the result of the
    // comparison. This is the last link in the chain of comparisons (note
    // that this does not mean that this is the last incoming value, blocks
    // can be reordered).
    Cond = Val;
    ExpectedPredicate = ICmpInst::ICMP_EQ;
  } else {
    // In this case, we expect a constant incoming value (the comparison is
    // chained).
    const auto *const Const = cast<ConstantInt>(Val);
    LLVM_DEBUG(dbgs() << "const\n");
    if (!Const->isZero())
      return std::nullopt;
    LLVM_DEBUG(dbgs() << "false\n");
    assert(BranchI->getNumSuccessors() == 2 && "expecting a cond branch");
    BasicBlock *const FalseBlock = BranchI->getSuccessor(1);
    Cond = BranchI->getCondition();
    ExpectedPredicate =
        FalseBlock == PhiBlock ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;
  }

  auto *CmpI = dyn_cast<ICmpInst>(Cond);
  if (!CmpI)
    return std::nullopt;
  LLVM_DEBUG(dbgs() << "icmp\n");

  std::optional<BCECmp> Result = visitICmp(CmpI, ExpectedPredicate, BaseId);
  if (!Result)
    return std::nullopt;

  BCECmpBlock::InstructionSet BlockInsts(
      {Result->Lhs.LoadI, Result->Rhs.LoadI, Result->CmpI, BranchI});
  if (Result->Lhs.GEP)
    BlockInsts.insert(Result->Lhs.GEP);
  if (Result->Rhs.GEP)
    BlockInsts.insert(Result->Rhs.GEP);
  return BCECmpBlock(std::move(*Result), Block, BlockInsts);
}

static inline void enqueueBlock(std::vector<BCECmpBlock> &Comparisons,
                                BCECmpBlock &&Comparison) {
  LLVM_DEBUG(dbgs() << "Block '" << Comparison.BB->getName()
                    << "': Found cmp of " << Comparison.SizeBits()
                    << " bits between " << Comparison.Lhs().BaseId << " + "
                    << Comparison.Lhs().Offset << " and "
                    << Comparison.Rhs().BaseId << " + "
                    << Comparison.Rhs().Offset << "\n");
  LLVM_DEBUG(dbgs() << "\n");
  Comparison.OrigOrder = Comparisons.size();
  Comparisons.push_back(std::move(Comparison));
}

// A chain of comparisons.
class BCECmpChain {
public:
  using ContiguousBlocks = std::vector<BCECmpBlock>;

  BCECmpChain(const std::vector<BasicBlock *> &Blocks, PHINode &Phi,
              AliasAnalysis &AA);

  bool simplify(const TargetLibraryInfo &TLI, AliasAnalysis &AA,
                DomTreeUpdater &DTU);

  bool atLeastOneMerged() const {
    return any_of(MergedBlocks_,
                  [](const auto &Blocks) { return Blocks.size() > 1; });
  }

private:
  PHINode &Phi_;
  // The list of all blocks in the chain, grouped by contiguity.
  std::vector<ContiguousBlocks> MergedBlocks_;
  // The original entry block (before sorting);
  BasicBlock *EntryBlock_;
};

static bool areContiguous(const BCECmpBlock &First, const BCECmpBlock &Second) {
  return First.Lhs().BaseId == Second.Lhs().BaseId &&
         First.Rhs().BaseId == Second.Rhs().BaseId &&
         First.Lhs().Offset + First.SizeBits() / 8 == Second.Lhs().Offset &&
         First.Rhs().Offset + First.SizeBits() / 8 == Second.Rhs().Offset;
}

static unsigned getMinOrigOrder(const BCECmpChain::ContiguousBlocks &Blocks) {
  unsigned MinOrigOrder = std::numeric_limits<unsigned>::max();
  for (const BCECmpBlock &Block : Blocks)
    MinOrigOrder = std::min(MinOrigOrder, Block.OrigOrder);
  return MinOrigOrder;
}

/// Given a chain of comparison blocks, groups the blocks into contiguous
/// ranges that can be merged together into a single comparison.
static std::vector<BCECmpChain::ContiguousBlocks>
mergeBlocks(std::vector<BCECmpBlock> &&Blocks) {
  std::vector<BCECmpChain::ContiguousBlocks> MergedBlocks;

  // Sort to detect continuous offsets.
  llvm::sort(Blocks,
             [](const BCECmpBlock &LhsBlock, const BCECmpBlock &RhsBlock) {
               return std::tie(LhsBlock.Lhs(), LhsBlock.Rhs()) <
                      std::tie(RhsBlock.Lhs(), RhsBlock.Rhs());
             });

  BCECmpChain::ContiguousBlocks *LastMergedBlock = nullptr;
  for (BCECmpBlock &Block : Blocks) {
    if (!LastMergedBlock || !areContiguous(LastMergedBlock->back(), Block)) {
      MergedBlocks.emplace_back();
      LastMergedBlock = &MergedBlocks.back();
    } else {
      LLVM_DEBUG(dbgs() << "Merging block " << Block.BB->getName() << " into "
                        << LastMergedBlock->back().BB->getName() << "\n");
    }
    LastMergedBlock->push_back(std::move(Block));
  }

  // While we allow reordering for merging, do not reorder unmerged comparisons.
  // Doing so may introduce branch on poison.
  llvm::sort(MergedBlocks, [](const BCECmpChain::ContiguousBlocks &LhsBlocks,
                              const BCECmpChain::ContiguousBlocks &RhsBlocks) {
    return getMinOrigOrder(LhsBlocks) < getMinOrigOrder(RhsBlocks);
  });

  return MergedBlocks;
}

BCECmpChain::BCECmpChain(const std::vector<BasicBlock *> &Blocks, PHINode &Phi,
                         AliasAnalysis &AA)
    : Phi_(Phi) {
  assert(!Blocks.empty() && "a chain should have at least one block");
  // Now look inside blocks to check for BCE comparisons.
  std::vector<BCECmpBlock> Comparisons;
  BaseIdentifier BaseId;
  for (BasicBlock *const Block : Blocks) {
    assert(Block && "invalid block");
    std::optional<BCECmpBlock> Comparison = visitCmpBlock(
        Phi.getIncomingValueForBlock(Block), Block, Phi.getParent(), BaseId);
    if (!Comparison) {
      LLVM_DEBUG(dbgs() << "chain with invalid BCECmpBlock, no merge.\n");
      return;
    }
    if (Comparison->doesOtherWork()) {
      LLVM_DEBUG(dbgs() << "block '" << Comparison->BB->getName()
                        << "' does extra work besides compare\n");
      if (Comparisons.empty()) {
        // This is the initial block in the chain, in case this block does other
        // work, we can try to split the block and move the irrelevant
        // instructions to the predecessor.
        //
        // If this is not the initial block in the chain, splitting it wont
        // work.
        //
        // As once split, there will still be instructions before the BCE cmp
        // instructions that do other work in program order, i.e. within the
        // chain before sorting. Unless we can abort the chain at this point
        // and start anew.
        //
        // NOTE: we only handle blocks a with single predecessor for now.
        if (Comparison->canSplit(AA)) {
          LLVM_DEBUG(dbgs()
                     << "Split initial block '" << Comparison->BB->getName()
                     << "' that does extra work besides compare\n");
          Comparison->RequireSplit = true;
          enqueueBlock(Comparisons, std::move(*Comparison));
        } else {
          LLVM_DEBUG(dbgs()
                     << "ignoring initial block '" << Comparison->BB->getName()
                     << "' that does extra work besides compare\n");
        }
        continue;
      }
      // TODO(courbet): Right now we abort the whole chain. We could be
      // merging only the blocks that don't do other work and resume the
      // chain from there. For example:
      //  if (a[0] == b[0]) {  // bb1
      //    if (a[1] == b[1]) {  // bb2
      //      some_value = 3; //bb3
      //      if (a[2] == b[2]) { //bb3
      //        do a ton of stuff  //bb4
      //      }
      //    }
      //  }
      //
      // This is:
      //
      // bb1 --eq--> bb2 --eq--> bb3* -eq--> bb4 --+
      //  \            \           \               \
      //   ne           ne          ne              \
      //    \            \           \               v
      //     +------------+-----------+----------> bb_phi
      //
      // We can only merge the first two comparisons, because bb3* does
      // "other work" (setting some_value to 3).
      // We could still merge bb1 and bb2 though.
      return;
    }
    enqueueBlock(Comparisons, std::move(*Comparison));
  }

  // It is possible we have no suitable comparison to merge.
  if (Comparisons.empty()) {
    LLVM_DEBUG(dbgs() << "chain with no BCE basic blocks, no merge\n");
    return;
  }
  EntryBlock_ = Comparisons[0].BB;
  MergedBlocks_ = mergeBlocks(std::move(Comparisons));
}

namespace {

// A class to compute the name of a set of merged basic blocks.
// This is optimized for the common case of no block names.
class MergedBlockName {
  // Storage for the uncommon case of several named blocks.
  SmallString<16> Scratch;

public:
  explicit MergedBlockName(ArrayRef<BCECmpBlock> Comparisons)
      : Name(makeName(Comparisons)) {}
  const StringRef Name;

private:
  StringRef makeName(ArrayRef<BCECmpBlock> Comparisons) {
    assert(!Comparisons.empty() && "no basic block");
    // Fast path: only one block, or no names at all.
    if (Comparisons.size() == 1)
      return Comparisons[0].BB->getName();
    const int size = std::accumulate(Comparisons.begin(), Comparisons.end(), 0,
                                     [](int i, const BCECmpBlock &Cmp) {
                                       return i + Cmp.BB->getName().size();
                                     });
    if (size == 0)
      return StringRef("", 0);

    // Slow path: at least two blocks, at least one block with a name.
    Scratch.clear();
    // We'll have `size` bytes for name and `Comparisons.size() - 1` bytes for
    // separators.
    Scratch.reserve(size + Comparisons.size() - 1);
    const auto append = [this](StringRef str) {
      Scratch.append(str.begin(), str.end());
    };
    append(Comparisons[0].BB->getName());
    for (int I = 1, E = Comparisons.size(); I < E; ++I) {
      const BasicBlock *const BB = Comparisons[I].BB;
      if (!BB->getName().empty()) {
        append("+");
        append(BB->getName());
      }
    }
    return Scratch.str();
  }
};
} // namespace

// Merges the given contiguous comparison blocks into one memcmp block.
static BasicBlock *mergeComparisons(ArrayRef<BCECmpBlock> Comparisons,
                                    BasicBlock *const InsertBefore,
                                    BasicBlock *const NextCmpBlock,
                                    PHINode &Phi, const TargetLibraryInfo &TLI,
                                    AliasAnalysis &AA, DomTreeUpdater &DTU) {
  assert(!Comparisons.empty() && "merging zero comparisons");
  LLVMContext &Context = NextCmpBlock->getContext();
  const BCECmpBlock &FirstCmp = Comparisons[0];

  // Create a new cmp block before next cmp block.
  BasicBlock *const BB =
      BasicBlock::Create(Context, MergedBlockName(Comparisons).Name,
                         NextCmpBlock->getParent(), InsertBefore);
  IRBuilder<> Builder(BB);
  // Add the GEPs from the first BCECmpBlock.
  Value *Lhs, *Rhs;
  if (FirstCmp.Lhs().GEP)
    Lhs = Builder.Insert(FirstCmp.Lhs().GEP->clone());
  else
    Lhs = FirstCmp.Lhs().LoadI->getPointerOperand();
  if (FirstCmp.Rhs().GEP)
    Rhs = Builder.Insert(FirstCmp.Rhs().GEP->clone());
  else
    Rhs = FirstCmp.Rhs().LoadI->getPointerOperand();

  Value *IsEqual = nullptr;
  LLVM_DEBUG(dbgs() << "Merging " << Comparisons.size() << " comparisons -> "
                    << BB->getName() << "\n");

  // If there is one block that requires splitting, we do it now, i.e.
  // just before we know we will collapse the chain. The instructions
  // can be executed before any of the instructions in the chain.
  const auto ToSplit = llvm::find_if(
      Comparisons, [](const BCECmpBlock &B) { return B.RequireSplit; });
  if (ToSplit != Comparisons.end()) {
    LLVM_DEBUG(dbgs() << "Splitting non_BCE work to header\n");
    ToSplit->split(BB, AA);
  }

  if (Comparisons.size() == 1) {
    LLVM_DEBUG(dbgs() << "Only one comparison, updating branches\n");
    // Use clone to keep the metadata
    Instruction *const LhsLoad = Builder.Insert(FirstCmp.Lhs().LoadI->clone());
    Instruction *const RhsLoad = Builder.Insert(FirstCmp.Rhs().LoadI->clone());
    LhsLoad->replaceUsesOfWith(LhsLoad->getOperand(0), Lhs);
    RhsLoad->replaceUsesOfWith(RhsLoad->getOperand(0), Rhs);
    // There are no blocks to merge, just do the comparison.
    IsEqual = Builder.CreateICmpEQ(LhsLoad, RhsLoad);
  } else {
    const unsigned TotalSizeBits = std::accumulate(
        Comparisons.begin(), Comparisons.end(), 0u,
        [](int Size, const BCECmpBlock &C) { return Size + C.SizeBits(); });

    // memcmp expects a 'size_t' argument and returns 'int'.
    unsigned SizeTBits = TLI.getSizeTSize(*Phi.getModule());
    unsigned IntBits = TLI.getIntSize();

    // Create memcmp() == 0.
    const auto &DL = Phi.getDataLayout();
    Value *const MemCmpCall = emitMemCmp(
        Lhs, Rhs,
        ConstantInt::get(Builder.getIntNTy(SizeTBits), TotalSizeBits / 8),
        Builder, DL, &TLI);
    IsEqual = Builder.CreateICmpEQ(
        MemCmpCall, ConstantInt::get(Builder.getIntNTy(IntBits), 0));
  }

  BasicBlock *const PhiBB = Phi.getParent();
  // Add a branch to the next basic block in the chain.
  if (NextCmpBlock == PhiBB) {
    // Continue to phi, passing it the comparison result.
    Builder.CreateBr(PhiBB);
    Phi.addIncoming(IsEqual, BB);
    DTU.applyUpdates({{DominatorTree::Insert, BB, PhiBB}});
  } else {
    // Continue to next block if equal, exit to phi else.
    Builder.CreateCondBr(IsEqual, NextCmpBlock, PhiBB);
    Phi.addIncoming(ConstantInt::getFalse(Context), BB);
    DTU.applyUpdates({{DominatorTree::Insert, BB, NextCmpBlock},
                      {DominatorTree::Insert, BB, PhiBB}});
  }
  return BB;
}

bool BCECmpChain::simplify(const TargetLibraryInfo &TLI, AliasAnalysis &AA,
                           DomTreeUpdater &DTU) {
  assert(atLeastOneMerged() && "simplifying trivial BCECmpChain");
  LLVM_DEBUG(dbgs() << "Simplifying comparison chain starting at block "
                    << EntryBlock_->getName() << "\n");

  // Effectively merge blocks. We go in the reverse direction from the phi block
  // so that the next block is always available to branch to.
  BasicBlock *InsertBefore = EntryBlock_;
  BasicBlock *NextCmpBlock = Phi_.getParent();
  for (const auto &Blocks : reverse(MergedBlocks_)) {
    InsertBefore = NextCmpBlock = mergeComparisons(
        Blocks, InsertBefore, NextCmpBlock, Phi_, TLI, AA, DTU);
  }

  // Replace the original cmp chain with the new cmp chain by pointing all
  // predecessors of EntryBlock_ to NextCmpBlock instead. This makes all cmp
  // blocks in the old chain unreachable.
  while (!pred_empty(EntryBlock_)) {
    BasicBlock* const Pred = *pred_begin(EntryBlock_);
    LLVM_DEBUG(dbgs() << "Updating jump into old chain from " << Pred->getName()
                      << "\n");
    Pred->getTerminator()->replaceUsesOfWith(EntryBlock_, NextCmpBlock);
    DTU.applyUpdates({{DominatorTree::Delete, Pred, EntryBlock_},
                      {DominatorTree::Insert, Pred, NextCmpBlock}});
  }

  // If the old cmp chain was the function entry, we need to update the function
  // entry.
  const bool ChainEntryIsFnEntry = EntryBlock_->isEntryBlock();
  if (ChainEntryIsFnEntry && DTU.hasDomTree()) {
    LLVM_DEBUG(dbgs() << "Changing function entry from "
                      << EntryBlock_->getName() << " to "
                      << NextCmpBlock->getName() << "\n");
    DTU.getDomTree().setNewRoot(NextCmpBlock);
    DTU.applyUpdates({{DominatorTree::Delete, NextCmpBlock, EntryBlock_}});
  }
  EntryBlock_ = nullptr;

  // Delete merged blocks. This also removes incoming values in phi.
  SmallVector<BasicBlock *, 16> DeadBlocks;
  for (const auto &Blocks : MergedBlocks_) {
    for (const BCECmpBlock &Block : Blocks) {
      LLVM_DEBUG(dbgs() << "Deleting merged block " << Block.BB->getName()
                        << "\n");
      DeadBlocks.push_back(Block.BB);
    }
  }
  DeleteDeadBlocks(DeadBlocks, &DTU);

  MergedBlocks_.clear();
  return true;
}

std::vector<BasicBlock *> getOrderedBlocks(PHINode &Phi,
                                           BasicBlock *const LastBlock,
                                           int NumBlocks) {
  // Walk up from the last block to find other blocks.
  std::vector<BasicBlock *> Blocks(NumBlocks);
  assert(LastBlock && "invalid last block");
  BasicBlock *CurBlock = LastBlock;
  for (int BlockIndex = NumBlocks - 1; BlockIndex > 0; --BlockIndex) {
    if (CurBlock->hasAddressTaken()) {
      // Somebody is jumping to the block through an address, all bets are
      // off.
      LLVM_DEBUG(dbgs() << "skip: block " << BlockIndex
                        << " has its address taken\n");
      return {};
    }
    Blocks[BlockIndex] = CurBlock;
    auto *SinglePredecessor = CurBlock->getSinglePredecessor();
    if (!SinglePredecessor) {
      // The block has two or more predecessors.
      LLVM_DEBUG(dbgs() << "skip: block " << BlockIndex
                        << " has two or more predecessors\n");
      return {};
    }
    if (Phi.getBasicBlockIndex(SinglePredecessor) < 0) {
      // The block does not link back to the phi.
      LLVM_DEBUG(dbgs() << "skip: block " << BlockIndex
                        << " does not link back to the phi\n");
      return {};
    }
    CurBlock = SinglePredecessor;
  }
  Blocks[0] = CurBlock;
  return Blocks;
}

bool processPhi(PHINode &Phi, const TargetLibraryInfo &TLI, AliasAnalysis &AA,
                DomTreeUpdater &DTU) {
  LLVM_DEBUG(dbgs() << "processPhi()\n");
  if (Phi.getNumIncomingValues() <= 1) {
    LLVM_DEBUG(dbgs() << "skip: only one incoming value in phi\n");
    return false;
  }
  // We are looking for something that has the following structure:
  //   bb1 --eq--> bb2 --eq--> bb3 --eq--> bb4 --+
  //     \            \           \               \
  //      ne           ne          ne              \
  //       \            \           \               v
  //        +------------+-----------+----------> bb_phi
  //
  //  - The last basic block (bb4 here) must branch unconditionally to bb_phi.
  //    It's the only block that contributes a non-constant value to the Phi.
  //  - All other blocks (b1, b2, b3) must have exactly two successors, one of
  //    them being the phi block.
  //  - All intermediate blocks (bb2, bb3) must have only one predecessor.
  //  - Blocks cannot do other work besides the comparison, see doesOtherWork()

  // The blocks are not necessarily ordered in the phi, so we start from the
  // last block and reconstruct the order.
  BasicBlock *LastBlock = nullptr;
  for (unsigned I = 0; I < Phi.getNumIncomingValues(); ++I) {
    if (isa<ConstantInt>(Phi.getIncomingValue(I))) continue;
    if (LastBlock) {
      // There are several non-constant values.
      LLVM_DEBUG(dbgs() << "skip: several non-constant values\n");
      return false;
    }
    if (!isa<ICmpInst>(Phi.getIncomingValue(I)) ||
        cast<ICmpInst>(Phi.getIncomingValue(I))->getParent() !=
            Phi.getIncomingBlock(I)) {
      // Non-constant incoming value is not from a cmp instruction or not
      // produced by the last block. We could end up processing the value
      // producing block more than once.
      //
      // This is an uncommon case, so we bail.
      LLVM_DEBUG(
          dbgs()
          << "skip: non-constant value not from cmp or not from last block.\n");
      return false;
    }
    LastBlock = Phi.getIncomingBlock(I);
  }
  if (!LastBlock) {
    // There is no non-constant block.
    LLVM_DEBUG(dbgs() << "skip: no non-constant block\n");
    return false;
  }
  if (LastBlock->getSingleSuccessor() != Phi.getParent()) {
    LLVM_DEBUG(dbgs() << "skip: last block non-phi successor\n");
    return false;
  }

  const auto Blocks =
      getOrderedBlocks(Phi, LastBlock, Phi.getNumIncomingValues());
  if (Blocks.empty()) return false;
  BCECmpChain CmpChain(Blocks, Phi, AA);

  if (!CmpChain.atLeastOneMerged()) {
    LLVM_DEBUG(dbgs() << "skip: nothing merged\n");
    return false;
  }

  return CmpChain.simplify(TLI, AA, DTU);
}

static bool runImpl(Function &F, const TargetLibraryInfo &TLI,
                    const TargetTransformInfo &TTI, AliasAnalysis &AA,
                    DominatorTree *DT) {
  LLVM_DEBUG(dbgs() << "MergeICmpsLegacyPass: " << F.getName() << "\n");

  // We only try merging comparisons if the target wants to expand memcmp later.
  // The rationale is to avoid turning small chains into memcmp calls.
  if (!TTI.enableMemCmpExpansion(F.hasOptSize(), true))
    return false;

  // If we don't have memcmp avaiable we can't emit calls to it.
  if (!TLI.has(LibFunc_memcmp))
    return false;

  DomTreeUpdater DTU(DT, /*PostDominatorTree*/ nullptr,
                     DomTreeUpdater::UpdateStrategy::Eager);

  bool MadeChange = false;

  for (BasicBlock &BB : llvm::drop_begin(F)) {
    // A Phi operation is always first in a basic block.
    if (auto *const Phi = dyn_cast<PHINode>(&*BB.begin()))
      MadeChange |= processPhi(*Phi, TLI, AA, DTU);
  }

  return MadeChange;
}

class MergeICmpsLegacyPass : public FunctionPass {
public:
  static char ID;

  MergeICmpsLegacyPass() : FunctionPass(ID) {
    initializeMergeICmpsLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F)) return false;
    const auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    const auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    // MergeICmps does not need the DominatorTree, but we update it if it's
    // already available.
    auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
    auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
    return runImpl(F, TLI, TTI, AA, DTWP ? &DTWP->getDomTree() : nullptr);
  }

 private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
  }
};

} // namespace

char MergeICmpsLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(MergeICmpsLegacyPass, "mergeicmps",
                      "Merge contiguous icmps into a memcmp", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MergeICmpsLegacyPass, "mergeicmps",
                    "Merge contiguous icmps into a memcmp", false, false)

Pass *llvm::createMergeICmpsLegacyPass() { return new MergeICmpsLegacyPass(); }

PreservedAnalyses MergeICmpsPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  auto *DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  const bool MadeChanges = runImpl(F, TLI, TTI, AA, DT);
  if (!MadeChanges)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
