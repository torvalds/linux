//===- PromoteMemoryToRegister.cpp - Convert allocas to registers ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file promotes memory references to be register references.  It promotes
// alloca instructions which only have loads and stores as uses.  An alloca is
// transformed by using iterated dominator frontiers to place PHI nodes, then
// traversing the function in depth-first order to rewrite loads and stores as
// appropriate.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mem2reg"

STATISTIC(NumLocalPromoted, "Number of alloca's promoted within one block");
STATISTIC(NumSingleStore,   "Number of alloca's promoted with a single store");
STATISTIC(NumDeadAlloca,    "Number of dead alloca's removed");
STATISTIC(NumPHIInsert,     "Number of PHI nodes inserted");

bool llvm::isAllocaPromotable(const AllocaInst *AI) {
  // Only allow direct and non-volatile loads and stores...
  for (const User *U : AI->users()) {
    if (const LoadInst *LI = dyn_cast<LoadInst>(U)) {
      // Note that atomic loads can be transformed; atomic semantics do
      // not have any meaning for a local alloca.
      if (LI->isVolatile() || LI->getType() != AI->getAllocatedType())
        return false;
    } else if (const StoreInst *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getValueOperand() == AI ||
          SI->getValueOperand()->getType() != AI->getAllocatedType())
        return false; // Don't allow a store OF the AI, only INTO the AI.
      // Note that atomic stores can be transformed; atomic semantics do
      // not have any meaning for a local alloca.
      if (SI->isVolatile())
        return false;
    } else if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(U)) {
      if (!II->isLifetimeStartOrEnd() && !II->isDroppable())
        return false;
    } else if (const BitCastInst *BCI = dyn_cast<BitCastInst>(U)) {
      if (!onlyUsedByLifetimeMarkersOrDroppableInsts(BCI))
        return false;
    } else if (const GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U)) {
      if (!GEPI->hasAllZeroIndices())
        return false;
      if (!onlyUsedByLifetimeMarkersOrDroppableInsts(GEPI))
        return false;
    } else if (const AddrSpaceCastInst *ASCI = dyn_cast<AddrSpaceCastInst>(U)) {
      if (!onlyUsedByLifetimeMarkers(ASCI))
        return false;
    } else {
      return false;
    }
  }

  return true;
}

namespace {

static void createDebugValue(DIBuilder &DIB, Value *NewValue,
                             DILocalVariable *Variable,
                             DIExpression *Expression, const DILocation *DI,
                             DbgVariableRecord *InsertBefore) {
  // FIXME: Merge these two functions now that DIBuilder supports
  // DbgVariableRecords. We neeed the API to accept DbgVariableRecords as an
  // insert point for that to work.
  (void)DIB;
  DbgVariableRecord::createDbgVariableRecord(NewValue, Variable, Expression, DI,
                                             *InsertBefore);
}
static void createDebugValue(DIBuilder &DIB, Value *NewValue,
                             DILocalVariable *Variable,
                             DIExpression *Expression, const DILocation *DI,
                             Instruction *InsertBefore) {
  DIB.insertDbgValueIntrinsic(NewValue, Variable, Expression, DI, InsertBefore);
}

/// Helper for updating assignment tracking debug info when promoting allocas.
class AssignmentTrackingInfo {
  /// DbgAssignIntrinsics linked to the alloca with at most one per variable
  /// fragment. (i.e. not be a comprehensive set if there are multiple
  /// dbg.assigns for one variable fragment).
  SmallVector<DbgVariableIntrinsic *> DbgAssigns;
  SmallVector<DbgVariableRecord *> DVRAssigns;

public:
  void init(AllocaInst *AI) {
    SmallSet<DebugVariable, 2> Vars;
    for (DbgAssignIntrinsic *DAI : at::getAssignmentMarkers(AI)) {
      if (Vars.insert(DebugVariable(DAI)).second)
        DbgAssigns.push_back(DAI);
    }
    for (DbgVariableRecord *DVR : at::getDVRAssignmentMarkers(AI)) {
      if (Vars.insert(DebugVariable(DVR)).second)
        DVRAssigns.push_back(DVR);
    }
  }

  /// Update assignment tracking debug info given for the to-be-deleted store
  /// \p ToDelete that stores to this alloca.
  void updateForDeletedStore(
      StoreInst *ToDelete, DIBuilder &DIB,
      SmallSet<DbgAssignIntrinsic *, 8> *DbgAssignsToDelete,
      SmallSet<DbgVariableRecord *, 8> *DVRAssignsToDelete) const {
    // There's nothing to do if the alloca doesn't have any variables using
    // assignment tracking.
    if (DbgAssigns.empty() && DVRAssigns.empty())
      return;

    // Insert a dbg.value where the linked dbg.assign is and remember to delete
    // the dbg.assign later. Demoting to dbg.value isn't necessary for
    // correctness but does reduce compile time and memory usage by reducing
    // unnecessary function-local metadata. Remember that we've seen a
    // dbg.assign for each variable fragment for the untracked store handling
    // (after this loop).
    SmallSet<DebugVariableAggregate, 2> VarHasDbgAssignForStore;
    auto InsertValueForAssign = [&](auto *DbgAssign, auto *&AssignList) {
      VarHasDbgAssignForStore.insert(DebugVariableAggregate(DbgAssign));
      AssignList->insert(DbgAssign);
      createDebugValue(DIB, DbgAssign->getValue(), DbgAssign->getVariable(),
                       DbgAssign->getExpression(), DbgAssign->getDebugLoc(),
                       DbgAssign);
    };
    for (auto *Assign : at::getAssignmentMarkers(ToDelete))
      InsertValueForAssign(Assign, DbgAssignsToDelete);
    for (auto *Assign : at::getDVRAssignmentMarkers(ToDelete))
      InsertValueForAssign(Assign, DVRAssignsToDelete);

    // It's possible for variables using assignment tracking to have no
    // dbg.assign linked to this store. These are variables in DbgAssigns that
    // are missing from VarHasDbgAssignForStore. Since there isn't a dbg.assign
    // to mark the assignment - and the store is going to be deleted - insert a
    // dbg.value to do that now. An untracked store may be either one that
    // cannot be represented using assignment tracking (non-const offset or
    // size) or one that is trackable but has had its DIAssignID attachment
    // dropped accidentally.
    auto ConvertUnlinkedAssignToValue = [&](auto *Assign) {
      if (VarHasDbgAssignForStore.contains(DebugVariableAggregate(Assign)))
        return;
      ConvertDebugDeclareToDebugValue(Assign, ToDelete, DIB);
    };
    for_each(DbgAssigns, ConvertUnlinkedAssignToValue);
    for_each(DVRAssigns, ConvertUnlinkedAssignToValue);
  }

  /// Update assignment tracking debug info given for the newly inserted PHI \p
  /// NewPhi.
  void updateForNewPhi(PHINode *NewPhi, DIBuilder &DIB) const {
    // Regardless of the position of dbg.assigns relative to stores, the
    // incoming values into a new PHI should be the same for the (imaginary)
    // debug-phi.
    for (auto *DAI : DbgAssigns)
      ConvertDebugDeclareToDebugValue(DAI, NewPhi, DIB);
    for (auto *DVR : DVRAssigns)
      ConvertDebugDeclareToDebugValue(DVR, NewPhi, DIB);
  }

  void clear() {
    DbgAssigns.clear();
    DVRAssigns.clear();
  }
  bool empty() { return DbgAssigns.empty() && DVRAssigns.empty(); }
};

struct AllocaInfo {
  using DbgUserVec = SmallVector<DbgVariableIntrinsic *, 1>;
  using DPUserVec = SmallVector<DbgVariableRecord *, 1>;

  SmallVector<BasicBlock *, 32> DefiningBlocks;
  SmallVector<BasicBlock *, 32> UsingBlocks;

  StoreInst *OnlyStore;
  BasicBlock *OnlyBlock;
  bool OnlyUsedInOneBlock;

  /// Debug users of the alloca - does not include dbg.assign intrinsics.
  DbgUserVec DbgUsers;
  DPUserVec DPUsers;
  /// Helper to update assignment tracking debug info.
  AssignmentTrackingInfo AssignmentTracking;

  void clear() {
    DefiningBlocks.clear();
    UsingBlocks.clear();
    OnlyStore = nullptr;
    OnlyBlock = nullptr;
    OnlyUsedInOneBlock = true;
    DbgUsers.clear();
    DPUsers.clear();
    AssignmentTracking.clear();
  }

  /// Scan the uses of the specified alloca, filling in the AllocaInfo used
  /// by the rest of the pass to reason about the uses of this alloca.
  void AnalyzeAlloca(AllocaInst *AI) {
    clear();

    // As we scan the uses of the alloca instruction, keep track of stores,
    // and decide whether all of the loads and stores to the alloca are within
    // the same basic block.
    for (User *U : AI->users()) {
      Instruction *User = cast<Instruction>(U);

      if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
        // Remember the basic blocks which define new values for the alloca
        DefiningBlocks.push_back(SI->getParent());
        OnlyStore = SI;
      } else {
        LoadInst *LI = cast<LoadInst>(User);
        // Otherwise it must be a load instruction, keep track of variable
        // reads.
        UsingBlocks.push_back(LI->getParent());
      }

      if (OnlyUsedInOneBlock) {
        if (!OnlyBlock)
          OnlyBlock = User->getParent();
        else if (OnlyBlock != User->getParent())
          OnlyUsedInOneBlock = false;
      }
    }
    DbgUserVec AllDbgUsers;
    SmallVector<DbgVariableRecord *> AllDPUsers;
    findDbgUsers(AllDbgUsers, AI, &AllDPUsers);
    std::copy_if(AllDbgUsers.begin(), AllDbgUsers.end(),
                 std::back_inserter(DbgUsers), [](DbgVariableIntrinsic *DII) {
                   return !isa<DbgAssignIntrinsic>(DII);
                 });
    std::copy_if(AllDPUsers.begin(), AllDPUsers.end(),
                 std::back_inserter(DPUsers),
                 [](DbgVariableRecord *DVR) { return !DVR->isDbgAssign(); });
    AssignmentTracking.init(AI);
  }
};

/// Data package used by RenamePass().
struct RenamePassData {
  using ValVector = std::vector<Value *>;
  using LocationVector = std::vector<DebugLoc>;

  RenamePassData(BasicBlock *B, BasicBlock *P, ValVector V, LocationVector L)
      : BB(B), Pred(P), Values(std::move(V)), Locations(std::move(L)) {}

  BasicBlock *BB;
  BasicBlock *Pred;
  ValVector Values;
  LocationVector Locations;
};

/// This assigns and keeps a per-bb relative ordering of load/store
/// instructions in the block that directly load or store an alloca.
///
/// This functionality is important because it avoids scanning large basic
/// blocks multiple times when promoting many allocas in the same block.
class LargeBlockInfo {
  /// For each instruction that we track, keep the index of the
  /// instruction.
  ///
  /// The index starts out as the number of the instruction from the start of
  /// the block.
  DenseMap<const Instruction *, unsigned> InstNumbers;

public:

  /// This code only looks at accesses to allocas.
  static bool isInterestingInstruction(const Instruction *I) {
    return (isa<LoadInst>(I) && isa<AllocaInst>(I->getOperand(0))) ||
           (isa<StoreInst>(I) && isa<AllocaInst>(I->getOperand(1)));
  }

  /// Get or calculate the index of the specified instruction.
  unsigned getInstructionIndex(const Instruction *I) {
    assert(isInterestingInstruction(I) &&
           "Not a load/store to/from an alloca?");

    // If we already have this instruction number, return it.
    DenseMap<const Instruction *, unsigned>::iterator It = InstNumbers.find(I);
    if (It != InstNumbers.end())
      return It->second;

    // Scan the whole block to get the instruction.  This accumulates
    // information for every interesting instruction in the block, in order to
    // avoid gratuitus rescans.
    const BasicBlock *BB = I->getParent();
    unsigned InstNo = 0;
    for (const Instruction &BBI : *BB)
      if (isInterestingInstruction(&BBI))
        InstNumbers[&BBI] = InstNo++;
    It = InstNumbers.find(I);

    assert(It != InstNumbers.end() && "Didn't insert instruction?");
    return It->second;
  }

  void deleteValue(const Instruction *I) { InstNumbers.erase(I); }

  void clear() { InstNumbers.clear(); }
};

struct PromoteMem2Reg {
  /// The alloca instructions being promoted.
  std::vector<AllocaInst *> Allocas;

  DominatorTree &DT;
  DIBuilder DIB;

  /// A cache of @llvm.assume intrinsics used by SimplifyInstruction.
  AssumptionCache *AC;

  const SimplifyQuery SQ;

  /// Reverse mapping of Allocas.
  DenseMap<AllocaInst *, unsigned> AllocaLookup;

  /// The PhiNodes we're adding.
  ///
  /// That map is used to simplify some Phi nodes as we iterate over it, so
  /// it should have deterministic iterators.  We could use a MapVector, but
  /// since we already maintain a map from BasicBlock* to a stable numbering
  /// (BBNumbers), the DenseMap is more efficient (also supports removal).
  DenseMap<std::pair<unsigned, unsigned>, PHINode *> NewPhiNodes;

  /// For each PHI node, keep track of which entry in Allocas it corresponds
  /// to.
  DenseMap<PHINode *, unsigned> PhiToAllocaMap;

  /// For each alloca, we keep track of the dbg.declare intrinsic that
  /// describes it, if any, so that we can convert it to a dbg.value
  /// intrinsic if the alloca gets promoted.
  SmallVector<AllocaInfo::DbgUserVec, 8> AllocaDbgUsers;
  SmallVector<AllocaInfo::DPUserVec, 8> AllocaDPUsers;

  /// For each alloca, keep an instance of a helper class that gives us an easy
  /// way to update assignment tracking debug info if the alloca is promoted.
  SmallVector<AssignmentTrackingInfo, 8> AllocaATInfo;
  /// A set of dbg.assigns to delete because they've been demoted to
  /// dbg.values. Call cleanUpDbgAssigns to delete them.
  SmallSet<DbgAssignIntrinsic *, 8> DbgAssignsToDelete;
  SmallSet<DbgVariableRecord *, 8> DVRAssignsToDelete;

  /// The set of basic blocks the renamer has already visited.
  SmallPtrSet<BasicBlock *, 16> Visited;

  /// Contains a stable numbering of basic blocks to avoid non-determinstic
  /// behavior.
  DenseMap<BasicBlock *, unsigned> BBNumbers;

  /// Lazily compute the number of predecessors a block has.
  DenseMap<const BasicBlock *, unsigned> BBNumPreds;

  /// Whether the function has the no-signed-zeros-fp-math attribute set.
  bool NoSignedZeros = false;

public:
  PromoteMem2Reg(ArrayRef<AllocaInst *> Allocas, DominatorTree &DT,
                 AssumptionCache *AC)
      : Allocas(Allocas.begin(), Allocas.end()), DT(DT),
        DIB(*DT.getRoot()->getParent()->getParent(), /*AllowUnresolved*/ false),
        AC(AC), SQ(DT.getRoot()->getDataLayout(),
                   nullptr, &DT, AC) {}

  void run();

private:
  void RemoveFromAllocasList(unsigned &AllocaIdx) {
    Allocas[AllocaIdx] = Allocas.back();
    Allocas.pop_back();
    --AllocaIdx;
  }

  unsigned getNumPreds(const BasicBlock *BB) {
    unsigned &NP = BBNumPreds[BB];
    if (NP == 0)
      NP = pred_size(BB) + 1;
    return NP - 1;
  }

  void ComputeLiveInBlocks(AllocaInst *AI, AllocaInfo &Info,
                           const SmallPtrSetImpl<BasicBlock *> &DefBlocks,
                           SmallPtrSetImpl<BasicBlock *> &LiveInBlocks);
  void RenamePass(BasicBlock *BB, BasicBlock *Pred,
                  RenamePassData::ValVector &IncVals,
                  RenamePassData::LocationVector &IncLocs,
                  std::vector<RenamePassData> &Worklist);
  bool QueuePhiNode(BasicBlock *BB, unsigned AllocaIdx, unsigned &Version);

  /// Delete dbg.assigns that have been demoted to dbg.values.
  void cleanUpDbgAssigns() {
    for (auto *DAI : DbgAssignsToDelete)
      DAI->eraseFromParent();
    DbgAssignsToDelete.clear();
    for (auto *DVR : DVRAssignsToDelete)
      DVR->eraseFromParent();
    DVRAssignsToDelete.clear();
  }
};

} // end anonymous namespace

/// Given a LoadInst LI this adds assume(LI != null) after it.
static void addAssumeNonNull(AssumptionCache *AC, LoadInst *LI) {
  Function *AssumeIntrinsic =
      Intrinsic::getDeclaration(LI->getModule(), Intrinsic::assume);
  ICmpInst *LoadNotNull = new ICmpInst(ICmpInst::ICMP_NE, LI,
                                       Constant::getNullValue(LI->getType()));
  LoadNotNull->insertAfter(LI);
  CallInst *CI = CallInst::Create(AssumeIntrinsic, {LoadNotNull});
  CI->insertAfter(LoadNotNull);
  AC->registerAssumption(cast<AssumeInst>(CI));
}

static void convertMetadataToAssumes(LoadInst *LI, Value *Val,
                                     const DataLayout &DL, AssumptionCache *AC,
                                     const DominatorTree *DT) {
  if (isa<UndefValue>(Val) && LI->hasMetadata(LLVMContext::MD_noundef)) {
    // Insert non-terminator unreachable.
    LLVMContext &Ctx = LI->getContext();
    new StoreInst(ConstantInt::getTrue(Ctx),
                  PoisonValue::get(PointerType::getUnqual(Ctx)),
                  /*isVolatile=*/false, Align(1), LI);
    return;
  }

  // If the load was marked as nonnull we don't want to lose that information
  // when we erase this Load. So we preserve it with an assume. As !nonnull
  // returns poison while assume violations are immediate undefined behavior,
  // we can only do this if the value is known non-poison.
  if (AC && LI->getMetadata(LLVMContext::MD_nonnull) &&
      LI->getMetadata(LLVMContext::MD_noundef) &&
      !isKnownNonZero(Val, SimplifyQuery(DL, DT, AC, LI)))
    addAssumeNonNull(AC, LI);
}

static void removeIntrinsicUsers(AllocaInst *AI) {
  // Knowing that this alloca is promotable, we know that it's safe to kill all
  // instructions except for load and store.

  for (Use &U : llvm::make_early_inc_range(AI->uses())) {
    Instruction *I = cast<Instruction>(U.getUser());
    if (isa<LoadInst>(I) || isa<StoreInst>(I))
      continue;

    // Drop the use of AI in droppable instructions.
    if (I->isDroppable()) {
      I->dropDroppableUse(U);
      continue;
    }

    if (!I->getType()->isVoidTy()) {
      // The only users of this bitcast/GEP instruction are lifetime intrinsics.
      // Follow the use/def chain to erase them now instead of leaving it for
      // dead code elimination later.
      for (Use &UU : llvm::make_early_inc_range(I->uses())) {
        Instruction *Inst = cast<Instruction>(UU.getUser());

        // Drop the use of I in droppable instructions.
        if (Inst->isDroppable()) {
          Inst->dropDroppableUse(UU);
          continue;
        }
        Inst->eraseFromParent();
      }
    }
    I->eraseFromParent();
  }
}

/// Rewrite as many loads as possible given a single store.
///
/// When there is only a single store, we can use the domtree to trivially
/// replace all of the dominated loads with the stored value. Do so, and return
/// true if this has successfully promoted the alloca entirely. If this returns
/// false there were some loads which were not dominated by the single store
/// and thus must be phi-ed with undef. We fall back to the standard alloca
/// promotion algorithm in that case.
static bool
rewriteSingleStoreAlloca(AllocaInst *AI, AllocaInfo &Info, LargeBlockInfo &LBI,
                         const DataLayout &DL, DominatorTree &DT,
                         AssumptionCache *AC,
                         SmallSet<DbgAssignIntrinsic *, 8> *DbgAssignsToDelete,
                         SmallSet<DbgVariableRecord *, 8> *DVRAssignsToDelete) {
  StoreInst *OnlyStore = Info.OnlyStore;
  Value *ReplVal = OnlyStore->getOperand(0);
  // Loads may either load the stored value or uninitialized memory (undef).
  // If the stored value may be poison, then replacing an uninitialized memory
  // load with it would be incorrect. If the store dominates the load, we know
  // it is always initialized.
  bool RequireDominatingStore =
      isa<Instruction>(ReplVal) || !isGuaranteedNotToBePoison(ReplVal);
  BasicBlock *StoreBB = OnlyStore->getParent();
  int StoreIndex = -1;

  // Clear out UsingBlocks.  We will reconstruct it here if needed.
  Info.UsingBlocks.clear();

  for (User *U : make_early_inc_range(AI->users())) {
    Instruction *UserInst = cast<Instruction>(U);
    if (UserInst == OnlyStore)
      continue;
    LoadInst *LI = cast<LoadInst>(UserInst);

    // Okay, if we have a load from the alloca, we want to replace it with the
    // only value stored to the alloca.  We can do this if the value is
    // dominated by the store.  If not, we use the rest of the mem2reg machinery
    // to insert the phi nodes as needed.
    if (RequireDominatingStore) {
      if (LI->getParent() == StoreBB) {
        // If we have a use that is in the same block as the store, compare the
        // indices of the two instructions to see which one came first.  If the
        // load came before the store, we can't handle it.
        if (StoreIndex == -1)
          StoreIndex = LBI.getInstructionIndex(OnlyStore);

        if (unsigned(StoreIndex) > LBI.getInstructionIndex(LI)) {
          // Can't handle this load, bail out.
          Info.UsingBlocks.push_back(StoreBB);
          continue;
        }
      } else if (!DT.dominates(StoreBB, LI->getParent())) {
        // If the load and store are in different blocks, use BB dominance to
        // check their relationships.  If the store doesn't dom the use, bail
        // out.
        Info.UsingBlocks.push_back(LI->getParent());
        continue;
      }
    }

    // Otherwise, we *can* safely rewrite this load.
    // If the replacement value is the load, this must occur in unreachable
    // code.
    if (ReplVal == LI)
      ReplVal = PoisonValue::get(LI->getType());

    convertMetadataToAssumes(LI, ReplVal, DL, AC, &DT);
    LI->replaceAllUsesWith(ReplVal);
    LI->eraseFromParent();
    LBI.deleteValue(LI);
  }

  // Finally, after the scan, check to see if the store is all that is left.
  if (!Info.UsingBlocks.empty())
    return false; // If not, we'll have to fall back for the remainder.

  DIBuilder DIB(*AI->getModule(), /*AllowUnresolved*/ false);
  // Update assignment tracking info for the store we're going to delete.
  Info.AssignmentTracking.updateForDeletedStore(
      Info.OnlyStore, DIB, DbgAssignsToDelete, DVRAssignsToDelete);

  // Record debuginfo for the store and remove the declaration's
  // debuginfo.
  auto ConvertDebugInfoForStore = [&](auto &Container) {
    for (auto *DbgItem : Container) {
      if (DbgItem->isAddressOfVariable()) {
        ConvertDebugDeclareToDebugValue(DbgItem, Info.OnlyStore, DIB);
        DbgItem->eraseFromParent();
      } else if (DbgItem->getExpression()->startsWithDeref()) {
        DbgItem->eraseFromParent();
      }
    }
  };
  ConvertDebugInfoForStore(Info.DbgUsers);
  ConvertDebugInfoForStore(Info.DPUsers);

  // Remove dbg.assigns linked to the alloca as these are now redundant.
  at::deleteAssignmentMarkers(AI);

  // Remove the (now dead) store and alloca.
  Info.OnlyStore->eraseFromParent();
  LBI.deleteValue(Info.OnlyStore);

  AI->eraseFromParent();
  return true;
}

/// Many allocas are only used within a single basic block.  If this is the
/// case, avoid traversing the CFG and inserting a lot of potentially useless
/// PHI nodes by just performing a single linear pass over the basic block
/// using the Alloca.
///
/// If we cannot promote this alloca (because it is read before it is written),
/// return false.  This is necessary in cases where, due to control flow, the
/// alloca is undefined only on some control flow paths.  e.g. code like
/// this is correct in LLVM IR:
///  // A is an alloca with no stores so far
///  for (...) {
///    int t = *A;
///    if (!first_iteration)
///      use(t);
///    *A = 42;
///  }
static bool
promoteSingleBlockAlloca(AllocaInst *AI, const AllocaInfo &Info,
                         LargeBlockInfo &LBI, const DataLayout &DL,
                         DominatorTree &DT, AssumptionCache *AC,
                         SmallSet<DbgAssignIntrinsic *, 8> *DbgAssignsToDelete,
                         SmallSet<DbgVariableRecord *, 8> *DVRAssignsToDelete) {
  // The trickiest case to handle is when we have large blocks. Because of this,
  // this code is optimized assuming that large blocks happen.  This does not
  // significantly pessimize the small block case.  This uses LargeBlockInfo to
  // make it efficient to get the index of various operations in the block.

  // Walk the use-def list of the alloca, getting the locations of all stores.
  using StoresByIndexTy = SmallVector<std::pair<unsigned, StoreInst *>, 64>;
  StoresByIndexTy StoresByIndex;

  for (User *U : AI->users())
    if (StoreInst *SI = dyn_cast<StoreInst>(U))
      StoresByIndex.push_back(std::make_pair(LBI.getInstructionIndex(SI), SI));

  // Sort the stores by their index, making it efficient to do a lookup with a
  // binary search.
  llvm::sort(StoresByIndex, less_first());

  // Walk all of the loads from this alloca, replacing them with the nearest
  // store above them, if any.
  for (User *U : make_early_inc_range(AI->users())) {
    LoadInst *LI = dyn_cast<LoadInst>(U);
    if (!LI)
      continue;

    unsigned LoadIdx = LBI.getInstructionIndex(LI);

    // Find the nearest store that has a lower index than this load.
    StoresByIndexTy::iterator I = llvm::lower_bound(
        StoresByIndex,
        std::make_pair(LoadIdx, static_cast<StoreInst *>(nullptr)),
        less_first());
    Value *ReplVal;
    if (I == StoresByIndex.begin()) {
      if (StoresByIndex.empty())
        // If there are no stores, the load takes the undef value.
        ReplVal = UndefValue::get(LI->getType());
      else
        // There is no store before this load, bail out (load may be affected
        // by the following stores - see main comment).
        return false;
    } else {
      // Otherwise, there was a store before this load, the load takes its
      // value.
      ReplVal = std::prev(I)->second->getOperand(0);
    }

    convertMetadataToAssumes(LI, ReplVal, DL, AC, &DT);

    // If the replacement value is the load, this must occur in unreachable
    // code.
    if (ReplVal == LI)
      ReplVal = PoisonValue::get(LI->getType());

    LI->replaceAllUsesWith(ReplVal);
    LI->eraseFromParent();
    LBI.deleteValue(LI);
  }

  // Remove the (now dead) stores and alloca.
  DIBuilder DIB(*AI->getModule(), /*AllowUnresolved*/ false);
  while (!AI->use_empty()) {
    StoreInst *SI = cast<StoreInst>(AI->user_back());
    // Update assignment tracking info for the store we're going to delete.
    Info.AssignmentTracking.updateForDeletedStore(SI, DIB, DbgAssignsToDelete,
                                                  DVRAssignsToDelete);
    // Record debuginfo for the store before removing it.
    auto DbgUpdateForStore = [&](auto &Container) {
      for (auto *DbgItem : Container) {
        if (DbgItem->isAddressOfVariable()) {
          ConvertDebugDeclareToDebugValue(DbgItem, SI, DIB);
        }
      }
    };
    DbgUpdateForStore(Info.DbgUsers);
    DbgUpdateForStore(Info.DPUsers);

    SI->eraseFromParent();
    LBI.deleteValue(SI);
  }

  // Remove dbg.assigns linked to the alloca as these are now redundant.
  at::deleteAssignmentMarkers(AI);
  AI->eraseFromParent();

  // The alloca's debuginfo can be removed as well.
  auto DbgUpdateForAlloca = [&](auto &Container) {
    for (auto *DbgItem : Container)
      if (DbgItem->isAddressOfVariable() ||
          DbgItem->getExpression()->startsWithDeref())
        DbgItem->eraseFromParent();
  };
  DbgUpdateForAlloca(Info.DbgUsers);
  DbgUpdateForAlloca(Info.DPUsers);

  ++NumLocalPromoted;
  return true;
}

void PromoteMem2Reg::run() {
  Function &F = *DT.getRoot()->getParent();

  AllocaDbgUsers.resize(Allocas.size());
  AllocaATInfo.resize(Allocas.size());
  AllocaDPUsers.resize(Allocas.size());

  AllocaInfo Info;
  LargeBlockInfo LBI;
  ForwardIDFCalculator IDF(DT);

  NoSignedZeros = F.getFnAttribute("no-signed-zeros-fp-math").getValueAsBool();

  for (unsigned AllocaNum = 0; AllocaNum != Allocas.size(); ++AllocaNum) {
    AllocaInst *AI = Allocas[AllocaNum];

    assert(isAllocaPromotable(AI) && "Cannot promote non-promotable alloca!");
    assert(AI->getParent()->getParent() == &F &&
           "All allocas should be in the same function, which is same as DF!");

    removeIntrinsicUsers(AI);

    if (AI->use_empty()) {
      // If there are no uses of the alloca, just delete it now.
      AI->eraseFromParent();

      // Remove the alloca from the Allocas list, since it has been processed
      RemoveFromAllocasList(AllocaNum);
      ++NumDeadAlloca;
      continue;
    }

    // Calculate the set of read and write-locations for each alloca.  This is
    // analogous to finding the 'uses' and 'definitions' of each variable.
    Info.AnalyzeAlloca(AI);

    // If there is only a single store to this value, replace any loads of
    // it that are directly dominated by the definition with the value stored.
    if (Info.DefiningBlocks.size() == 1) {
      if (rewriteSingleStoreAlloca(AI, Info, LBI, SQ.DL, DT, AC,
                                   &DbgAssignsToDelete, &DVRAssignsToDelete)) {
        // The alloca has been processed, move on.
        RemoveFromAllocasList(AllocaNum);
        ++NumSingleStore;
        continue;
      }
    }

    // If the alloca is only read and written in one basic block, just perform a
    // linear sweep over the block to eliminate it.
    if (Info.OnlyUsedInOneBlock &&
        promoteSingleBlockAlloca(AI, Info, LBI, SQ.DL, DT, AC,
                                 &DbgAssignsToDelete, &DVRAssignsToDelete)) {
      // The alloca has been processed, move on.
      RemoveFromAllocasList(AllocaNum);
      continue;
    }

    // If we haven't computed a numbering for the BB's in the function, do so
    // now.
    if (BBNumbers.empty()) {
      unsigned ID = 0;
      for (auto &BB : F)
        BBNumbers[&BB] = ID++;
    }

    // Remember the dbg.declare intrinsic describing this alloca, if any.
    if (!Info.DbgUsers.empty())
      AllocaDbgUsers[AllocaNum] = Info.DbgUsers;
    if (!Info.AssignmentTracking.empty())
      AllocaATInfo[AllocaNum] = Info.AssignmentTracking;
    if (!Info.DPUsers.empty())
      AllocaDPUsers[AllocaNum] = Info.DPUsers;

    // Keep the reverse mapping of the 'Allocas' array for the rename pass.
    AllocaLookup[Allocas[AllocaNum]] = AllocaNum;

    // Unique the set of defining blocks for efficient lookup.
    SmallPtrSet<BasicBlock *, 32> DefBlocks(Info.DefiningBlocks.begin(),
                                            Info.DefiningBlocks.end());

    // Determine which blocks the value is live in.  These are blocks which lead
    // to uses.
    SmallPtrSet<BasicBlock *, 32> LiveInBlocks;
    ComputeLiveInBlocks(AI, Info, DefBlocks, LiveInBlocks);

    // At this point, we're committed to promoting the alloca using IDF's, and
    // the standard SSA construction algorithm.  Determine which blocks need phi
    // nodes and see if we can optimize out some work by avoiding insertion of
    // dead phi nodes.
    IDF.setLiveInBlocks(LiveInBlocks);
    IDF.setDefiningBlocks(DefBlocks);
    SmallVector<BasicBlock *, 32> PHIBlocks;
    IDF.calculate(PHIBlocks);
    llvm::sort(PHIBlocks, [this](BasicBlock *A, BasicBlock *B) {
      return BBNumbers.find(A)->second < BBNumbers.find(B)->second;
    });

    unsigned CurrentVersion = 0;
    for (BasicBlock *BB : PHIBlocks)
      QueuePhiNode(BB, AllocaNum, CurrentVersion);
  }

  if (Allocas.empty()) {
    cleanUpDbgAssigns();
    return; // All of the allocas must have been trivial!
  }
  LBI.clear();

  // Set the incoming values for the basic block to be null values for all of
  // the alloca's.  We do this in case there is a load of a value that has not
  // been stored yet.  In this case, it will get this null value.
  RenamePassData::ValVector Values(Allocas.size());
  for (unsigned i = 0, e = Allocas.size(); i != e; ++i)
    Values[i] = UndefValue::get(Allocas[i]->getAllocatedType());

  // When handling debug info, treat all incoming values as if they have unknown
  // locations until proven otherwise.
  RenamePassData::LocationVector Locations(Allocas.size());

  // Walks all basic blocks in the function performing the SSA rename algorithm
  // and inserting the phi nodes we marked as necessary
  std::vector<RenamePassData> RenamePassWorkList;
  RenamePassWorkList.emplace_back(&F.front(), nullptr, std::move(Values),
                                  std::move(Locations));
  do {
    RenamePassData RPD = std::move(RenamePassWorkList.back());
    RenamePassWorkList.pop_back();
    // RenamePass may add new worklist entries.
    RenamePass(RPD.BB, RPD.Pred, RPD.Values, RPD.Locations, RenamePassWorkList);
  } while (!RenamePassWorkList.empty());

  // The renamer uses the Visited set to avoid infinite loops.  Clear it now.
  Visited.clear();

  // Remove the allocas themselves from the function.
  for (Instruction *A : Allocas) {
    // Remove dbg.assigns linked to the alloca as these are now redundant.
    at::deleteAssignmentMarkers(A);
    // If there are any uses of the alloca instructions left, they must be in
    // unreachable basic blocks that were not processed by walking the dominator
    // tree. Just delete the users now.
    if (!A->use_empty())
      A->replaceAllUsesWith(PoisonValue::get(A->getType()));
    A->eraseFromParent();
  }

  // Remove alloca's dbg.declare intrinsics from the function.
  auto RemoveDbgDeclares = [&](auto &Container) {
    for (auto &DbgUsers : Container) {
      for (auto *DbgItem : DbgUsers)
        if (DbgItem->isAddressOfVariable() ||
            DbgItem->getExpression()->startsWithDeref())
          DbgItem->eraseFromParent();
    }
  };
  RemoveDbgDeclares(AllocaDbgUsers);
  RemoveDbgDeclares(AllocaDPUsers);

  // Loop over all of the PHI nodes and see if there are any that we can get
  // rid of because they merge all of the same incoming values.  This can
  // happen due to undef values coming into the PHI nodes.  This process is
  // iterative, because eliminating one PHI node can cause others to be removed.
  bool EliminatedAPHI = true;
  while (EliminatedAPHI) {
    EliminatedAPHI = false;

    // Iterating over NewPhiNodes is deterministic, so it is safe to try to
    // simplify and RAUW them as we go.  If it was not, we could add uses to
    // the values we replace with in a non-deterministic order, thus creating
    // non-deterministic def->use chains.
    for (DenseMap<std::pair<unsigned, unsigned>, PHINode *>::iterator
             I = NewPhiNodes.begin(),
             E = NewPhiNodes.end();
         I != E;) {
      PHINode *PN = I->second;

      // If this PHI node merges one value and/or undefs, get the value.
      if (Value *V = simplifyInstruction(PN, SQ)) {
        PN->replaceAllUsesWith(V);
        PN->eraseFromParent();
        NewPhiNodes.erase(I++);
        EliminatedAPHI = true;
        continue;
      }
      ++I;
    }
  }

  // At this point, the renamer has added entries to PHI nodes for all reachable
  // code.  Unfortunately, there may be unreachable blocks which the renamer
  // hasn't traversed.  If this is the case, the PHI nodes may not
  // have incoming values for all predecessors.  Loop over all PHI nodes we have
  // created, inserting poison values if they are missing any incoming values.
  for (DenseMap<std::pair<unsigned, unsigned>, PHINode *>::iterator
           I = NewPhiNodes.begin(),
           E = NewPhiNodes.end();
       I != E; ++I) {
    // We want to do this once per basic block.  As such, only process a block
    // when we find the PHI that is the first entry in the block.
    PHINode *SomePHI = I->second;
    BasicBlock *BB = SomePHI->getParent();
    if (&BB->front() != SomePHI)
      continue;

    // Only do work here if there the PHI nodes are missing incoming values.  We
    // know that all PHI nodes that were inserted in a block will have the same
    // number of incoming values, so we can just check any of them.
    if (SomePHI->getNumIncomingValues() == getNumPreds(BB))
      continue;

    // Get the preds for BB.
    SmallVector<BasicBlock *, 16> Preds(predecessors(BB));

    // Ok, now we know that all of the PHI nodes are missing entries for some
    // basic blocks.  Start by sorting the incoming predecessors for efficient
    // access.
    auto CompareBBNumbers = [this](BasicBlock *A, BasicBlock *B) {
      return BBNumbers.find(A)->second < BBNumbers.find(B)->second;
    };
    llvm::sort(Preds, CompareBBNumbers);

    // Now we loop through all BB's which have entries in SomePHI and remove
    // them from the Preds list.
    for (unsigned i = 0, e = SomePHI->getNumIncomingValues(); i != e; ++i) {
      // Do a log(n) search of the Preds list for the entry we want.
      SmallVectorImpl<BasicBlock *>::iterator EntIt = llvm::lower_bound(
          Preds, SomePHI->getIncomingBlock(i), CompareBBNumbers);
      assert(EntIt != Preds.end() && *EntIt == SomePHI->getIncomingBlock(i) &&
             "PHI node has entry for a block which is not a predecessor!");

      // Remove the entry
      Preds.erase(EntIt);
    }

    // At this point, the blocks left in the preds list must have dummy
    // entries inserted into every PHI nodes for the block.  Update all the phi
    // nodes in this block that we are inserting (there could be phis before
    // mem2reg runs).
    unsigned NumBadPreds = SomePHI->getNumIncomingValues();
    BasicBlock::iterator BBI = BB->begin();
    while ((SomePHI = dyn_cast<PHINode>(BBI++)) &&
           SomePHI->getNumIncomingValues() == NumBadPreds) {
      Value *PoisonVal = PoisonValue::get(SomePHI->getType());
      for (BasicBlock *Pred : Preds)
        SomePHI->addIncoming(PoisonVal, Pred);
    }
  }

  NewPhiNodes.clear();
  cleanUpDbgAssigns();
}

/// Determine which blocks the value is live in.
///
/// These are blocks which lead to uses.  Knowing this allows us to avoid
/// inserting PHI nodes into blocks which don't lead to uses (thus, the
/// inserted phi nodes would be dead).
void PromoteMem2Reg::ComputeLiveInBlocks(
    AllocaInst *AI, AllocaInfo &Info,
    const SmallPtrSetImpl<BasicBlock *> &DefBlocks,
    SmallPtrSetImpl<BasicBlock *> &LiveInBlocks) {
  // To determine liveness, we must iterate through the predecessors of blocks
  // where the def is live.  Blocks are added to the worklist if we need to
  // check their predecessors.  Start with all the using blocks.
  SmallVector<BasicBlock *, 64> LiveInBlockWorklist(Info.UsingBlocks.begin(),
                                                    Info.UsingBlocks.end());

  // If any of the using blocks is also a definition block, check to see if the
  // definition occurs before or after the use.  If it happens before the use,
  // the value isn't really live-in.
  for (unsigned i = 0, e = LiveInBlockWorklist.size(); i != e; ++i) {
    BasicBlock *BB = LiveInBlockWorklist[i];
    if (!DefBlocks.count(BB))
      continue;

    // Okay, this is a block that both uses and defines the value.  If the first
    // reference to the alloca is a def (store), then we know it isn't live-in.
    for (BasicBlock::iterator I = BB->begin();; ++I) {
      if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        if (SI->getOperand(1) != AI)
          continue;

        // We found a store to the alloca before a load.  The alloca is not
        // actually live-in here.
        LiveInBlockWorklist[i] = LiveInBlockWorklist.back();
        LiveInBlockWorklist.pop_back();
        --i;
        --e;
        break;
      }

      if (LoadInst *LI = dyn_cast<LoadInst>(I))
        // Okay, we found a load before a store to the alloca.  It is actually
        // live into this block.
        if (LI->getOperand(0) == AI)
          break;
    }
  }

  // Now that we have a set of blocks where the phi is live-in, recursively add
  // their predecessors until we find the full region the value is live.
  while (!LiveInBlockWorklist.empty()) {
    BasicBlock *BB = LiveInBlockWorklist.pop_back_val();

    // The block really is live in here, insert it into the set.  If already in
    // the set, then it has already been processed.
    if (!LiveInBlocks.insert(BB).second)
      continue;

    // Since the value is live into BB, it is either defined in a predecessor or
    // live into it to.  Add the preds to the worklist unless they are a
    // defining block.
    for (BasicBlock *P : predecessors(BB)) {
      // The value is not live into a predecessor if it defines the value.
      if (DefBlocks.count(P))
        continue;

      // Otherwise it is, add to the worklist.
      LiveInBlockWorklist.push_back(P);
    }
  }
}

/// Queue a phi-node to be added to a basic-block for a specific Alloca.
///
/// Returns true if there wasn't already a phi-node for that variable
bool PromoteMem2Reg::QueuePhiNode(BasicBlock *BB, unsigned AllocaNo,
                                  unsigned &Version) {
  // Look up the basic-block in question.
  PHINode *&PN = NewPhiNodes[std::make_pair(BBNumbers[BB], AllocaNo)];

  // If the BB already has a phi node added for the i'th alloca then we're done!
  if (PN)
    return false;

  // Create a PhiNode using the dereferenced type... and add the phi-node to the
  // BasicBlock.
  PN = PHINode::Create(Allocas[AllocaNo]->getAllocatedType(), getNumPreds(BB),
                       Allocas[AllocaNo]->getName() + "." + Twine(Version++));
  PN->insertBefore(BB->begin());
  ++NumPHIInsert;
  PhiToAllocaMap[PN] = AllocaNo;
  return true;
}

/// Update the debug location of a phi. \p ApplyMergedLoc indicates whether to
/// create a merged location incorporating \p DL, or to set \p DL directly.
static void updateForIncomingValueLocation(PHINode *PN, DebugLoc DL,
                                           bool ApplyMergedLoc) {
  if (ApplyMergedLoc)
    PN->applyMergedLocation(PN->getDebugLoc(), DL);
  else
    PN->setDebugLoc(DL);
}

/// Recursively traverse the CFG of the function, renaming loads and
/// stores to the allocas which we are promoting.
///
/// IncomingVals indicates what value each Alloca contains on exit from the
/// predecessor block Pred.
void PromoteMem2Reg::RenamePass(BasicBlock *BB, BasicBlock *Pred,
                                RenamePassData::ValVector &IncomingVals,
                                RenamePassData::LocationVector &IncomingLocs,
                                std::vector<RenamePassData> &Worklist) {
NextIteration:
  // If we are inserting any phi nodes into this BB, they will already be in the
  // block.
  if (PHINode *APN = dyn_cast<PHINode>(BB->begin())) {
    // If we have PHI nodes to update, compute the number of edges from Pred to
    // BB.
    if (PhiToAllocaMap.count(APN)) {
      // We want to be able to distinguish between PHI nodes being inserted by
      // this invocation of mem2reg from those phi nodes that already existed in
      // the IR before mem2reg was run.  We determine that APN is being inserted
      // because it is missing incoming edges.  All other PHI nodes being
      // inserted by this pass of mem2reg will have the same number of incoming
      // operands so far.  Remember this count.
      unsigned NewPHINumOperands = APN->getNumOperands();

      unsigned NumEdges = llvm::count(successors(Pred), BB);
      assert(NumEdges && "Must be at least one edge from Pred to BB!");

      // Add entries for all the phis.
      BasicBlock::iterator PNI = BB->begin();
      do {
        unsigned AllocaNo = PhiToAllocaMap[APN];

        // Update the location of the phi node.
        updateForIncomingValueLocation(APN, IncomingLocs[AllocaNo],
                                       APN->getNumIncomingValues() > 0);

        // Add N incoming values to the PHI node.
        for (unsigned i = 0; i != NumEdges; ++i)
          APN->addIncoming(IncomingVals[AllocaNo], Pred);

        // For the  sequence `return X > 0.0 ? X : -X`, it is expected that this
        // results in fabs intrinsic. However, without no-signed-zeros(nsz) flag
        // on the phi node generated at this stage, fabs folding does not
        // happen. So, we try to infer nsz flag from the function attributes to
        // enable this fabs folding.
        if (isa<FPMathOperator>(APN) && NoSignedZeros)
          APN->setHasNoSignedZeros(true);

        // The currently active variable for this block is now the PHI.
        IncomingVals[AllocaNo] = APN;
        AllocaATInfo[AllocaNo].updateForNewPhi(APN, DIB);
        auto ConvertDbgDeclares = [&](auto &Container) {
          for (auto *DbgItem : Container)
            if (DbgItem->isAddressOfVariable())
              ConvertDebugDeclareToDebugValue(DbgItem, APN, DIB);
        };
        ConvertDbgDeclares(AllocaDbgUsers[AllocaNo]);
        ConvertDbgDeclares(AllocaDPUsers[AllocaNo]);

        // Get the next phi node.
        ++PNI;
        APN = dyn_cast<PHINode>(PNI);
        if (!APN)
          break;

        // Verify that it is missing entries.  If not, it is not being inserted
        // by this mem2reg invocation so we want to ignore it.
      } while (APN->getNumOperands() == NewPHINumOperands);
    }
  }

  // Don't revisit blocks.
  if (!Visited.insert(BB).second)
    return;

  for (BasicBlock::iterator II = BB->begin(); !II->isTerminator();) {
    Instruction *I = &*II++; // get the instruction, increment iterator

    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
      AllocaInst *Src = dyn_cast<AllocaInst>(LI->getPointerOperand());
      if (!Src)
        continue;

      DenseMap<AllocaInst *, unsigned>::iterator AI = AllocaLookup.find(Src);
      if (AI == AllocaLookup.end())
        continue;

      Value *V = IncomingVals[AI->second];
      convertMetadataToAssumes(LI, V, SQ.DL, AC, &DT);

      // Anything using the load now uses the current value.
      LI->replaceAllUsesWith(V);
      LI->eraseFromParent();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
      // Delete this instruction and mark the name as the current holder of the
      // value
      AllocaInst *Dest = dyn_cast<AllocaInst>(SI->getPointerOperand());
      if (!Dest)
        continue;

      DenseMap<AllocaInst *, unsigned>::iterator ai = AllocaLookup.find(Dest);
      if (ai == AllocaLookup.end())
        continue;

      // what value were we writing?
      unsigned AllocaNo = ai->second;
      IncomingVals[AllocaNo] = SI->getOperand(0);

      // Record debuginfo for the store before removing it.
      IncomingLocs[AllocaNo] = SI->getDebugLoc();
      AllocaATInfo[AllocaNo].updateForDeletedStore(SI, DIB, &DbgAssignsToDelete,
                                                   &DVRAssignsToDelete);
      auto ConvertDbgDeclares = [&](auto &Container) {
        for (auto *DbgItem : Container)
          if (DbgItem->isAddressOfVariable())
            ConvertDebugDeclareToDebugValue(DbgItem, SI, DIB);
      };
      ConvertDbgDeclares(AllocaDbgUsers[ai->second]);
      ConvertDbgDeclares(AllocaDPUsers[ai->second]);
      SI->eraseFromParent();
    }
  }

  // 'Recurse' to our successors.
  succ_iterator I = succ_begin(BB), E = succ_end(BB);
  if (I == E)
    return;

  // Keep track of the successors so we don't visit the same successor twice
  SmallPtrSet<BasicBlock *, 8> VisitedSuccs;

  // Handle the first successor without using the worklist.
  VisitedSuccs.insert(*I);
  Pred = BB;
  BB = *I;
  ++I;

  for (; I != E; ++I)
    if (VisitedSuccs.insert(*I).second)
      Worklist.emplace_back(*I, Pred, IncomingVals, IncomingLocs);

  goto NextIteration;
}

void llvm::PromoteMemToReg(ArrayRef<AllocaInst *> Allocas, DominatorTree &DT,
                           AssumptionCache *AC) {
  // If there is nothing to do, bail out...
  if (Allocas.empty())
    return;

  PromoteMem2Reg(Allocas, DT, AC).run();
}
