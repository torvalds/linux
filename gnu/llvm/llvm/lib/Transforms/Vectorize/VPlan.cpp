//===- VPlan.cpp - Vectorizer Plan ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This is the LLVM vectorization plan. It represents a candidate for
/// vectorization, allowing to plan and optimize how to vectorize a given loop
/// before generating LLVM-IR.
/// The vectorizer uses vectorization plans to estimate the costs of potential
/// candidates and if profitable to execute the desired plan, generating vector
/// LLVM-IR code.
///
//===----------------------------------------------------------------------===//

#include "VPlan.h"
#include "LoopVectorizationPlanner.h"
#include "VPlanCFG.h"
#include "VPlanDominatorTree.h"
#include "VPlanPatternMatch.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GenericDomTreeConstruction.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <cassert>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::VPlanPatternMatch;

namespace llvm {
extern cl::opt<bool> EnableVPlanNativePath;
}

#define DEBUG_TYPE "vplan"

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
raw_ostream &llvm::operator<<(raw_ostream &OS, const VPValue &V) {
  const VPInstruction *Instr = dyn_cast<VPInstruction>(&V);
  VPSlotTracker SlotTracker(
      (Instr && Instr->getParent()) ? Instr->getParent()->getPlan() : nullptr);
  V.print(OS, SlotTracker);
  return OS;
}
#endif

Value *VPLane::getAsRuntimeExpr(IRBuilderBase &Builder,
                                const ElementCount &VF) const {
  switch (LaneKind) {
  case VPLane::Kind::ScalableLast:
    // Lane = RuntimeVF - VF.getKnownMinValue() + Lane
    return Builder.CreateSub(getRuntimeVF(Builder, Builder.getInt32Ty(), VF),
                             Builder.getInt32(VF.getKnownMinValue() - Lane));
  case VPLane::Kind::First:
    return Builder.getInt32(Lane);
  }
  llvm_unreachable("Unknown lane kind");
}

VPValue::VPValue(const unsigned char SC, Value *UV, VPDef *Def)
    : SubclassID(SC), UnderlyingVal(UV), Def(Def) {
  if (Def)
    Def->addDefinedValue(this);
}

VPValue::~VPValue() {
  assert(Users.empty() && "trying to delete a VPValue with remaining users");
  if (Def)
    Def->removeDefinedValue(this);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPValue::print(raw_ostream &OS, VPSlotTracker &SlotTracker) const {
  if (const VPRecipeBase *R = dyn_cast_or_null<VPRecipeBase>(Def))
    R->print(OS, "", SlotTracker);
  else
    printAsOperand(OS, SlotTracker);
}

void VPValue::dump() const {
  const VPRecipeBase *Instr = dyn_cast_or_null<VPRecipeBase>(this->Def);
  VPSlotTracker SlotTracker(
      (Instr && Instr->getParent()) ? Instr->getParent()->getPlan() : nullptr);
  print(dbgs(), SlotTracker);
  dbgs() << "\n";
}

void VPDef::dump() const {
  const VPRecipeBase *Instr = dyn_cast_or_null<VPRecipeBase>(this);
  VPSlotTracker SlotTracker(
      (Instr && Instr->getParent()) ? Instr->getParent()->getPlan() : nullptr);
  print(dbgs(), "", SlotTracker);
  dbgs() << "\n";
}
#endif

VPRecipeBase *VPValue::getDefiningRecipe() {
  return cast_or_null<VPRecipeBase>(Def);
}

const VPRecipeBase *VPValue::getDefiningRecipe() const {
  return cast_or_null<VPRecipeBase>(Def);
}

// Get the top-most entry block of \p Start. This is the entry block of the
// containing VPlan. This function is templated to support both const and non-const blocks
template <typename T> static T *getPlanEntry(T *Start) {
  T *Next = Start;
  T *Current = Start;
  while ((Next = Next->getParent()))
    Current = Next;

  SmallSetVector<T *, 8> WorkList;
  WorkList.insert(Current);

  for (unsigned i = 0; i < WorkList.size(); i++) {
    T *Current = WorkList[i];
    if (Current->getNumPredecessors() == 0)
      return Current;
    auto &Predecessors = Current->getPredecessors();
    WorkList.insert(Predecessors.begin(), Predecessors.end());
  }

  llvm_unreachable("VPlan without any entry node without predecessors");
}

VPlan *VPBlockBase::getPlan() { return getPlanEntry(this)->Plan; }

const VPlan *VPBlockBase::getPlan() const { return getPlanEntry(this)->Plan; }

/// \return the VPBasicBlock that is the entry of Block, possibly indirectly.
const VPBasicBlock *VPBlockBase::getEntryBasicBlock() const {
  const VPBlockBase *Block = this;
  while (const VPRegionBlock *Region = dyn_cast<VPRegionBlock>(Block))
    Block = Region->getEntry();
  return cast<VPBasicBlock>(Block);
}

VPBasicBlock *VPBlockBase::getEntryBasicBlock() {
  VPBlockBase *Block = this;
  while (VPRegionBlock *Region = dyn_cast<VPRegionBlock>(Block))
    Block = Region->getEntry();
  return cast<VPBasicBlock>(Block);
}

void VPBlockBase::setPlan(VPlan *ParentPlan) {
  assert(
      (ParentPlan->getEntry() == this || ParentPlan->getPreheader() == this) &&
      "Can only set plan on its entry or preheader block.");
  Plan = ParentPlan;
}

/// \return the VPBasicBlock that is the exit of Block, possibly indirectly.
const VPBasicBlock *VPBlockBase::getExitingBasicBlock() const {
  const VPBlockBase *Block = this;
  while (const VPRegionBlock *Region = dyn_cast<VPRegionBlock>(Block))
    Block = Region->getExiting();
  return cast<VPBasicBlock>(Block);
}

VPBasicBlock *VPBlockBase::getExitingBasicBlock() {
  VPBlockBase *Block = this;
  while (VPRegionBlock *Region = dyn_cast<VPRegionBlock>(Block))
    Block = Region->getExiting();
  return cast<VPBasicBlock>(Block);
}

VPBlockBase *VPBlockBase::getEnclosingBlockWithSuccessors() {
  if (!Successors.empty() || !Parent)
    return this;
  assert(Parent->getExiting() == this &&
         "Block w/o successors not the exiting block of its parent.");
  return Parent->getEnclosingBlockWithSuccessors();
}

VPBlockBase *VPBlockBase::getEnclosingBlockWithPredecessors() {
  if (!Predecessors.empty() || !Parent)
    return this;
  assert(Parent->getEntry() == this &&
         "Block w/o predecessors not the entry of its parent.");
  return Parent->getEnclosingBlockWithPredecessors();
}

void VPBlockBase::deleteCFG(VPBlockBase *Entry) {
  for (VPBlockBase *Block : to_vector(vp_depth_first_shallow(Entry)))
    delete Block;
}

VPBasicBlock::iterator VPBasicBlock::getFirstNonPhi() {
  iterator It = begin();
  while (It != end() && It->isPhi())
    It++;
  return It;
}

VPTransformState::VPTransformState(ElementCount VF, unsigned UF, LoopInfo *LI,
                                   DominatorTree *DT, IRBuilderBase &Builder,
                                   InnerLoopVectorizer *ILV, VPlan *Plan,
                                   LLVMContext &Ctx)
    : VF(VF), UF(UF), CFG(DT), LI(LI), Builder(Builder), ILV(ILV), Plan(Plan),
      LVer(nullptr),
      TypeAnalysis(Plan->getCanonicalIV()->getScalarType(), Ctx) {}

Value *VPTransformState::get(VPValue *Def, const VPIteration &Instance) {
  if (Def->isLiveIn())
    return Def->getLiveInIRValue();

  if (hasScalarValue(Def, Instance)) {
    return Data
        .PerPartScalars[Def][Instance.Part][Instance.Lane.mapToCacheIndex(VF)];
  }
  if (!Instance.Lane.isFirstLane() &&
      vputils::isUniformAfterVectorization(Def) &&
      hasScalarValue(Def, {Instance.Part, VPLane::getFirstLane()})) {
    return Data.PerPartScalars[Def][Instance.Part][0];
  }

  assert(hasVectorValue(Def, Instance.Part));
  auto *VecPart = Data.PerPartOutput[Def][Instance.Part];
  if (!VecPart->getType()->isVectorTy()) {
    assert(Instance.Lane.isFirstLane() && "cannot get lane > 0 for scalar");
    return VecPart;
  }
  // TODO: Cache created scalar values.
  Value *Lane = Instance.Lane.getAsRuntimeExpr(Builder, VF);
  auto *Extract = Builder.CreateExtractElement(VecPart, Lane);
  // set(Def, Extract, Instance);
  return Extract;
}

Value *VPTransformState::get(VPValue *Def, unsigned Part, bool NeedsScalar) {
  if (NeedsScalar) {
    assert((VF.isScalar() || Def->isLiveIn() || hasVectorValue(Def, Part) ||
            !vputils::onlyFirstLaneUsed(Def) ||
            (hasScalarValue(Def, VPIteration(Part, 0)) &&
             Data.PerPartScalars[Def][Part].size() == 1)) &&
           "Trying to access a single scalar per part but has multiple scalars "
           "per part.");
    return get(Def, VPIteration(Part, 0));
  }

  // If Values have been set for this Def return the one relevant for \p Part.
  if (hasVectorValue(Def, Part))
    return Data.PerPartOutput[Def][Part];

  auto GetBroadcastInstrs = [this, Def](Value *V) {
    bool SafeToHoist = Def->isDefinedOutsideVectorRegions();
    if (VF.isScalar())
      return V;
    // Place the code for broadcasting invariant variables in the new preheader.
    IRBuilder<>::InsertPointGuard Guard(Builder);
    if (SafeToHoist) {
      BasicBlock *LoopVectorPreHeader = CFG.VPBB2IRBB[cast<VPBasicBlock>(
          Plan->getVectorLoopRegion()->getSinglePredecessor())];
      if (LoopVectorPreHeader)
        Builder.SetInsertPoint(LoopVectorPreHeader->getTerminator());
    }

    // Place the code for broadcasting invariant variables in the new preheader.
    // Broadcast the scalar into all locations in the vector.
    Value *Shuf = Builder.CreateVectorSplat(VF, V, "broadcast");

    return Shuf;
  };

  if (!hasScalarValue(Def, {Part, 0})) {
    assert(Def->isLiveIn() && "expected a live-in");
    if (Part != 0)
      return get(Def, 0);
    Value *IRV = Def->getLiveInIRValue();
    Value *B = GetBroadcastInstrs(IRV);
    set(Def, B, Part);
    return B;
  }

  Value *ScalarValue = get(Def, {Part, 0});
  // If we aren't vectorizing, we can just copy the scalar map values over
  // to the vector map.
  if (VF.isScalar()) {
    set(Def, ScalarValue, Part);
    return ScalarValue;
  }

  bool IsUniform = vputils::isUniformAfterVectorization(Def);

  unsigned LastLane = IsUniform ? 0 : VF.getKnownMinValue() - 1;
  // Check if there is a scalar value for the selected lane.
  if (!hasScalarValue(Def, {Part, LastLane})) {
    // At the moment, VPWidenIntOrFpInductionRecipes, VPScalarIVStepsRecipes and
    // VPExpandSCEVRecipes can also be uniform.
    assert((isa<VPWidenIntOrFpInductionRecipe>(Def->getDefiningRecipe()) ||
            isa<VPScalarIVStepsRecipe>(Def->getDefiningRecipe()) ||
            isa<VPExpandSCEVRecipe>(Def->getDefiningRecipe())) &&
           "unexpected recipe found to be invariant");
    IsUniform = true;
    LastLane = 0;
  }

  auto *LastInst = cast<Instruction>(get(Def, {Part, LastLane}));
  // Set the insert point after the last scalarized instruction or after the
  // last PHI, if LastInst is a PHI. This ensures the insertelement sequence
  // will directly follow the scalar definitions.
  auto OldIP = Builder.saveIP();
  auto NewIP =
      isa<PHINode>(LastInst)
          ? BasicBlock::iterator(LastInst->getParent()->getFirstNonPHI())
          : std::next(BasicBlock::iterator(LastInst));
  Builder.SetInsertPoint(&*NewIP);

  // However, if we are vectorizing, we need to construct the vector values.
  // If the value is known to be uniform after vectorization, we can just
  // broadcast the scalar value corresponding to lane zero for each unroll
  // iteration. Otherwise, we construct the vector values using
  // insertelement instructions. Since the resulting vectors are stored in
  // State, we will only generate the insertelements once.
  Value *VectorValue = nullptr;
  if (IsUniform) {
    VectorValue = GetBroadcastInstrs(ScalarValue);
    set(Def, VectorValue, Part);
  } else {
    // Initialize packing with insertelements to start from undef.
    assert(!VF.isScalable() && "VF is assumed to be non scalable.");
    Value *Undef = PoisonValue::get(VectorType::get(LastInst->getType(), VF));
    set(Def, Undef, Part);
    for (unsigned Lane = 0; Lane < VF.getKnownMinValue(); ++Lane)
      packScalarIntoVectorValue(Def, {Part, Lane});
    VectorValue = get(Def, Part);
  }
  Builder.restoreIP(OldIP);
  return VectorValue;
}

BasicBlock *VPTransformState::CFGState::getPreheaderBBFor(VPRecipeBase *R) {
  VPRegionBlock *LoopRegion = R->getParent()->getEnclosingLoopRegion();
  return VPBB2IRBB[LoopRegion->getPreheaderVPBB()];
}

void VPTransformState::addNewMetadata(Instruction *To,
                                      const Instruction *Orig) {
  // If the loop was versioned with memchecks, add the corresponding no-alias
  // metadata.
  if (LVer && (isa<LoadInst>(Orig) || isa<StoreInst>(Orig)))
    LVer->annotateInstWithNoAlias(To, Orig);
}

void VPTransformState::addMetadata(Value *To, Instruction *From) {
  // No source instruction to transfer metadata from?
  if (!From)
    return;

  if (Instruction *ToI = dyn_cast<Instruction>(To)) {
    propagateMetadata(ToI, From);
    addNewMetadata(ToI, From);
  }
}

void VPTransformState::setDebugLocFrom(DebugLoc DL) {
  const DILocation *DIL = DL;
  // When a FSDiscriminator is enabled, we don't need to add the multiply
  // factors to the discriminators.
  if (DIL &&
      Builder.GetInsertBlock()
          ->getParent()
          ->shouldEmitDebugInfoForProfiling() &&
      !EnableFSDiscriminator) {
    // FIXME: For scalable vectors, assume vscale=1.
    auto NewDIL =
        DIL->cloneByMultiplyingDuplicationFactor(UF * VF.getKnownMinValue());
    if (NewDIL)
      Builder.SetCurrentDebugLocation(*NewDIL);
    else
      LLVM_DEBUG(dbgs() << "Failed to create new discriminator: "
                        << DIL->getFilename() << " Line: " << DIL->getLine());
  } else
    Builder.SetCurrentDebugLocation(DIL);
}

void VPTransformState::packScalarIntoVectorValue(VPValue *Def,
                                                 const VPIteration &Instance) {
  Value *ScalarInst = get(Def, Instance);
  Value *VectorValue = get(Def, Instance.Part);
  VectorValue = Builder.CreateInsertElement(
      VectorValue, ScalarInst, Instance.Lane.getAsRuntimeExpr(Builder, VF));
  set(Def, VectorValue, Instance.Part);
}

BasicBlock *
VPBasicBlock::createEmptyBasicBlock(VPTransformState::CFGState &CFG) {
  // BB stands for IR BasicBlocks. VPBB stands for VPlan VPBasicBlocks.
  // Pred stands for Predessor. Prev stands for Previous - last visited/created.
  BasicBlock *PrevBB = CFG.PrevBB;
  BasicBlock *NewBB = BasicBlock::Create(PrevBB->getContext(), getName(),
                                         PrevBB->getParent(), CFG.ExitBB);
  LLVM_DEBUG(dbgs() << "LV: created " << NewBB->getName() << '\n');

  // Hook up the new basic block to its predecessors.
  for (VPBlockBase *PredVPBlock : getHierarchicalPredecessors()) {
    VPBasicBlock *PredVPBB = PredVPBlock->getExitingBasicBlock();
    auto &PredVPSuccessors = PredVPBB->getHierarchicalSuccessors();
    BasicBlock *PredBB = CFG.VPBB2IRBB[PredVPBB];

    assert(PredBB && "Predecessor basic-block not found building successor.");
    auto *PredBBTerminator = PredBB->getTerminator();
    LLVM_DEBUG(dbgs() << "LV: draw edge from" << PredBB->getName() << '\n');

    auto *TermBr = dyn_cast<BranchInst>(PredBBTerminator);
    if (isa<UnreachableInst>(PredBBTerminator)) {
      assert(PredVPSuccessors.size() == 1 &&
             "Predecessor ending w/o branch must have single successor.");
      DebugLoc DL = PredBBTerminator->getDebugLoc();
      PredBBTerminator->eraseFromParent();
      auto *Br = BranchInst::Create(NewBB, PredBB);
      Br->setDebugLoc(DL);
    } else if (TermBr && !TermBr->isConditional()) {
      TermBr->setSuccessor(0, NewBB);
    } else {
      // Set each forward successor here when it is created, excluding
      // backedges. A backward successor is set when the branch is created.
      unsigned idx = PredVPSuccessors.front() == this ? 0 : 1;
      assert(!TermBr->getSuccessor(idx) &&
             "Trying to reset an existing successor block.");
      TermBr->setSuccessor(idx, NewBB);
    }
    CFG.DTU.applyUpdates({{DominatorTree::Insert, PredBB, NewBB}});
  }
  return NewBB;
}

void VPIRBasicBlock::execute(VPTransformState *State) {
  assert(getHierarchicalSuccessors().size() <= 2 &&
         "VPIRBasicBlock can have at most two successors at the moment!");
  State->Builder.SetInsertPoint(getIRBasicBlock()->getTerminator());
  executeRecipes(State, getIRBasicBlock());
  if (getSingleSuccessor()) {
    assert(isa<UnreachableInst>(getIRBasicBlock()->getTerminator()));
    auto *Br = State->Builder.CreateBr(getIRBasicBlock());
    Br->setOperand(0, nullptr);
    getIRBasicBlock()->getTerminator()->eraseFromParent();
  }

  for (VPBlockBase *PredVPBlock : getHierarchicalPredecessors()) {
    VPBasicBlock *PredVPBB = PredVPBlock->getExitingBasicBlock();
    BasicBlock *PredBB = State->CFG.VPBB2IRBB[PredVPBB];
    assert(PredBB && "Predecessor basic-block not found building successor.");
    LLVM_DEBUG(dbgs() << "LV: draw edge from" << PredBB->getName() << '\n');

    auto *PredBBTerminator = PredBB->getTerminator();
    auto *TermBr = cast<BranchInst>(PredBBTerminator);
    // Set each forward successor here when it is created, excluding
    // backedges. A backward successor is set when the branch is created.
    const auto &PredVPSuccessors = PredVPBB->getHierarchicalSuccessors();
    unsigned idx = PredVPSuccessors.front() == this ? 0 : 1;
    assert(!TermBr->getSuccessor(idx) &&
           "Trying to reset an existing successor block.");
    TermBr->setSuccessor(idx, IRBB);
    State->CFG.DTU.applyUpdates({{DominatorTree::Insert, PredBB, IRBB}});
  }
}

void VPBasicBlock::execute(VPTransformState *State) {
  bool Replica = State->Instance && !State->Instance->isFirstIteration();
  VPBasicBlock *PrevVPBB = State->CFG.PrevVPBB;
  VPBlockBase *SingleHPred = nullptr;
  BasicBlock *NewBB = State->CFG.PrevBB; // Reuse it if possible.

  auto IsLoopRegion = [](VPBlockBase *BB) {
    auto *R = dyn_cast<VPRegionBlock>(BB);
    return R && !R->isReplicator();
  };

  // 1. Create an IR basic block.
  if (PrevVPBB && /* A */
      !((SingleHPred = getSingleHierarchicalPredecessor()) &&
        SingleHPred->getExitingBasicBlock() == PrevVPBB &&
        PrevVPBB->getSingleHierarchicalSuccessor() &&
        (SingleHPred->getParent() == getEnclosingLoopRegion() &&
         !IsLoopRegion(SingleHPred))) &&         /* B */
      !(Replica && getPredecessors().empty())) { /* C */
    // The last IR basic block is reused, as an optimization, in three cases:
    // A. the first VPBB reuses the loop pre-header BB - when PrevVPBB is null;
    // B. when the current VPBB has a single (hierarchical) predecessor which
    //    is PrevVPBB and the latter has a single (hierarchical) successor which
    //    both are in the same non-replicator region; and
    // C. when the current VPBB is an entry of a region replica - where PrevVPBB
    //    is the exiting VPBB of this region from a previous instance, or the
    //    predecessor of this region.

    NewBB = createEmptyBasicBlock(State->CFG);
    State->Builder.SetInsertPoint(NewBB);
    // Temporarily terminate with unreachable until CFG is rewired.
    UnreachableInst *Terminator = State->Builder.CreateUnreachable();
    // Register NewBB in its loop. In innermost loops its the same for all
    // BB's.
    if (State->CurrentVectorLoop)
      State->CurrentVectorLoop->addBasicBlockToLoop(NewBB, *State->LI);
    State->Builder.SetInsertPoint(Terminator);
    State->CFG.PrevBB = NewBB;
  }

  // 2. Fill the IR basic block with IR instructions.
  executeRecipes(State, NewBB);
}

void VPBasicBlock::dropAllReferences(VPValue *NewValue) {
  for (VPRecipeBase &R : Recipes) {
    for (auto *Def : R.definedValues())
      Def->replaceAllUsesWith(NewValue);

    for (unsigned I = 0, E = R.getNumOperands(); I != E; I++)
      R.setOperand(I, NewValue);
  }
}

void VPBasicBlock::executeRecipes(VPTransformState *State, BasicBlock *BB) {
  LLVM_DEBUG(dbgs() << "LV: vectorizing VPBB:" << getName()
                    << " in BB:" << BB->getName() << '\n');

  State->CFG.VPBB2IRBB[this] = BB;
  State->CFG.PrevVPBB = this;

  for (VPRecipeBase &Recipe : Recipes)
    Recipe.execute(*State);

  LLVM_DEBUG(dbgs() << "LV: filled BB:" << *BB);
}

VPBasicBlock *VPBasicBlock::splitAt(iterator SplitAt) {
  assert((SplitAt == end() || SplitAt->getParent() == this) &&
         "can only split at a position in the same block");

  SmallVector<VPBlockBase *, 2> Succs(successors());
  // First, disconnect the current block from its successors.
  for (VPBlockBase *Succ : Succs)
    VPBlockUtils::disconnectBlocks(this, Succ);

  // Create new empty block after the block to split.
  auto *SplitBlock = new VPBasicBlock(getName() + ".split");
  VPBlockUtils::insertBlockAfter(SplitBlock, this);

  // Add successors for block to split to new block.
  for (VPBlockBase *Succ : Succs)
    VPBlockUtils::connectBlocks(SplitBlock, Succ);

  // Finally, move the recipes starting at SplitAt to new block.
  for (VPRecipeBase &ToMove :
       make_early_inc_range(make_range(SplitAt, this->end())))
    ToMove.moveBefore(*SplitBlock, SplitBlock->end());

  return SplitBlock;
}

VPRegionBlock *VPBasicBlock::getEnclosingLoopRegion() {
  VPRegionBlock *P = getParent();
  if (P && P->isReplicator()) {
    P = P->getParent();
    assert(!cast<VPRegionBlock>(P)->isReplicator() &&
           "unexpected nested replicate regions");
  }
  return P;
}

static bool hasConditionalTerminator(const VPBasicBlock *VPBB) {
  if (VPBB->empty()) {
    assert(
        VPBB->getNumSuccessors() < 2 &&
        "block with multiple successors doesn't have a recipe as terminator");
    return false;
  }

  const VPRecipeBase *R = &VPBB->back();
  bool IsCondBranch = isa<VPBranchOnMaskRecipe>(R) ||
                      match(R, m_BranchOnCond(m_VPValue())) ||
                      match(R, m_BranchOnCount(m_VPValue(), m_VPValue()));
  (void)IsCondBranch;

  if (VPBB->getNumSuccessors() >= 2 ||
      (VPBB->isExiting() && !VPBB->getParent()->isReplicator())) {
    assert(IsCondBranch && "block with multiple successors not terminated by "
                           "conditional branch recipe");

    return true;
  }

  assert(
      !IsCondBranch &&
      "block with 0 or 1 successors terminated by conditional branch recipe");
  return false;
}

VPRecipeBase *VPBasicBlock::getTerminator() {
  if (hasConditionalTerminator(this))
    return &back();
  return nullptr;
}

const VPRecipeBase *VPBasicBlock::getTerminator() const {
  if (hasConditionalTerminator(this))
    return &back();
  return nullptr;
}

bool VPBasicBlock::isExiting() const {
  return getParent() && getParent()->getExitingBasicBlock() == this;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPBlockBase::printSuccessors(raw_ostream &O, const Twine &Indent) const {
  if (getSuccessors().empty()) {
    O << Indent << "No successors\n";
  } else {
    O << Indent << "Successor(s): ";
    ListSeparator LS;
    for (auto *Succ : getSuccessors())
      O << LS << Succ->getName();
    O << '\n';
  }
}

void VPBasicBlock::print(raw_ostream &O, const Twine &Indent,
                         VPSlotTracker &SlotTracker) const {
  O << Indent << getName() << ":\n";

  auto RecipeIndent = Indent + "  ";
  for (const VPRecipeBase &Recipe : *this) {
    Recipe.print(O, RecipeIndent, SlotTracker);
    O << '\n';
  }

  printSuccessors(O, Indent);
}
#endif

static std::pair<VPBlockBase *, VPBlockBase *> cloneFrom(VPBlockBase *Entry);

// Clone the CFG for all nodes reachable from \p Entry, this includes cloning
// the blocks and their recipes. Operands of cloned recipes will NOT be updated.
// Remapping of operands must be done separately. Returns a pair with the new
// entry and exiting blocks of the cloned region. If \p Entry isn't part of a
// region, return nullptr for the exiting block.
static std::pair<VPBlockBase *, VPBlockBase *> cloneFrom(VPBlockBase *Entry) {
  DenseMap<VPBlockBase *, VPBlockBase *> Old2NewVPBlocks;
  VPBlockBase *Exiting = nullptr;
  bool InRegion = Entry->getParent();
  // First, clone blocks reachable from Entry.
  for (VPBlockBase *BB : vp_depth_first_shallow(Entry)) {
    VPBlockBase *NewBB = BB->clone();
    Old2NewVPBlocks[BB] = NewBB;
    if (InRegion && BB->getNumSuccessors() == 0) {
      assert(!Exiting && "Multiple exiting blocks?");
      Exiting = BB;
    }
  }
  assert((!InRegion || Exiting) && "regions must have a single exiting block");

  // Second, update the predecessors & successors of the cloned blocks.
  for (VPBlockBase *BB : vp_depth_first_shallow(Entry)) {
    VPBlockBase *NewBB = Old2NewVPBlocks[BB];
    SmallVector<VPBlockBase *> NewPreds;
    for (VPBlockBase *Pred : BB->getPredecessors()) {
      NewPreds.push_back(Old2NewVPBlocks[Pred]);
    }
    NewBB->setPredecessors(NewPreds);
    SmallVector<VPBlockBase *> NewSuccs;
    for (VPBlockBase *Succ : BB->successors()) {
      NewSuccs.push_back(Old2NewVPBlocks[Succ]);
    }
    NewBB->setSuccessors(NewSuccs);
  }

#if !defined(NDEBUG)
  // Verify that the order of predecessors and successors matches in the cloned
  // version.
  for (const auto &[OldBB, NewBB] :
       zip(vp_depth_first_shallow(Entry),
           vp_depth_first_shallow(Old2NewVPBlocks[Entry]))) {
    for (const auto &[OldPred, NewPred] :
         zip(OldBB->getPredecessors(), NewBB->getPredecessors()))
      assert(NewPred == Old2NewVPBlocks[OldPred] && "Different predecessors");

    for (const auto &[OldSucc, NewSucc] :
         zip(OldBB->successors(), NewBB->successors()))
      assert(NewSucc == Old2NewVPBlocks[OldSucc] && "Different successors");
  }
#endif

  return std::make_pair(Old2NewVPBlocks[Entry],
                        Exiting ? Old2NewVPBlocks[Exiting] : nullptr);
}

VPRegionBlock *VPRegionBlock::clone() {
  const auto &[NewEntry, NewExiting] = cloneFrom(getEntry());
  auto *NewRegion =
      new VPRegionBlock(NewEntry, NewExiting, getName(), isReplicator());
  for (VPBlockBase *Block : vp_depth_first_shallow(NewEntry))
    Block->setParent(NewRegion);
  return NewRegion;
}

void VPRegionBlock::dropAllReferences(VPValue *NewValue) {
  for (VPBlockBase *Block : vp_depth_first_shallow(Entry))
    // Drop all references in VPBasicBlocks and replace all uses with
    // DummyValue.
    Block->dropAllReferences(NewValue);
}

void VPRegionBlock::execute(VPTransformState *State) {
  ReversePostOrderTraversal<VPBlockShallowTraversalWrapper<VPBlockBase *>>
      RPOT(Entry);

  if (!isReplicator()) {
    // Create and register the new vector loop.
    Loop *PrevLoop = State->CurrentVectorLoop;
    State->CurrentVectorLoop = State->LI->AllocateLoop();
    BasicBlock *VectorPH = State->CFG.VPBB2IRBB[getPreheaderVPBB()];
    Loop *ParentLoop = State->LI->getLoopFor(VectorPH);

    // Insert the new loop into the loop nest and register the new basic blocks
    // before calling any utilities such as SCEV that require valid LoopInfo.
    if (ParentLoop)
      ParentLoop->addChildLoop(State->CurrentVectorLoop);
    else
      State->LI->addTopLevelLoop(State->CurrentVectorLoop);

    // Visit the VPBlocks connected to "this", starting from it.
    for (VPBlockBase *Block : RPOT) {
      LLVM_DEBUG(dbgs() << "LV: VPBlock in RPO " << Block->getName() << '\n');
      Block->execute(State);
    }

    State->CurrentVectorLoop = PrevLoop;
    return;
  }

  assert(!State->Instance && "Replicating a Region with non-null instance.");

  // Enter replicating mode.
  State->Instance = VPIteration(0, 0);

  for (unsigned Part = 0, UF = State->UF; Part < UF; ++Part) {
    State->Instance->Part = Part;
    assert(!State->VF.isScalable() && "VF is assumed to be non scalable.");
    for (unsigned Lane = 0, VF = State->VF.getKnownMinValue(); Lane < VF;
         ++Lane) {
      State->Instance->Lane = VPLane(Lane, VPLane::Kind::First);
      // Visit the VPBlocks connected to \p this, starting from it.
      for (VPBlockBase *Block : RPOT) {
        LLVM_DEBUG(dbgs() << "LV: VPBlock in RPO " << Block->getName() << '\n');
        Block->execute(State);
      }
    }
  }

  // Exit replicating mode.
  State->Instance.reset();
}

InstructionCost VPBasicBlock::cost(ElementCount VF, VPCostContext &Ctx) {
  InstructionCost Cost = 0;
  for (VPRecipeBase &R : Recipes)
    Cost += R.cost(VF, Ctx);
  return Cost;
}

InstructionCost VPRegionBlock::cost(ElementCount VF, VPCostContext &Ctx) {
  if (!isReplicator()) {
    InstructionCost Cost = 0;
    for (VPBlockBase *Block : vp_depth_first_shallow(getEntry()))
      Cost += Block->cost(VF, Ctx);
    InstructionCost BackedgeCost =
        Ctx.TTI.getCFInstrCost(Instruction::Br, TTI::TCK_RecipThroughput);
    LLVM_DEBUG(dbgs() << "Cost of " << BackedgeCost << " for VF " << VF
                      << ": vector loop backedge\n");
    Cost += BackedgeCost;
    return Cost;
  }

  // Compute the cost of a replicate region. Replicating isn't supported for
  // scalable vectors, return an invalid cost for them.
  // TODO: Discard scalable VPlans with replicate recipes earlier after
  // construction.
  if (VF.isScalable())
    return InstructionCost::getInvalid();

  // First compute the cost of the conditionally executed recipes, followed by
  // account for the branching cost, except if the mask is a header mask or
  // uniform condition.
  using namespace llvm::VPlanPatternMatch;
  VPBasicBlock *Then = cast<VPBasicBlock>(getEntry()->getSuccessors()[0]);
  InstructionCost ThenCost = Then->cost(VF, Ctx);

  // For the scalar case, we may not always execute the original predicated
  // block, Thus, scale the block's cost by the probability of executing it.
  if (VF.isScalar())
    return ThenCost / getReciprocalPredBlockProb();

  return ThenCost;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPRegionBlock::print(raw_ostream &O, const Twine &Indent,
                          VPSlotTracker &SlotTracker) const {
  O << Indent << (isReplicator() ? "<xVFxUF> " : "<x1> ") << getName() << ": {";
  auto NewIndent = Indent + "  ";
  for (auto *BlockBase : vp_depth_first_shallow(Entry)) {
    O << '\n';
    BlockBase->print(O, NewIndent, SlotTracker);
  }
  O << Indent << "}\n";

  printSuccessors(O, Indent);
}
#endif

VPlan::~VPlan() {
  for (auto &KV : LiveOuts)
    delete KV.second;
  LiveOuts.clear();

  if (Entry) {
    VPValue DummyValue;
    for (VPBlockBase *Block : vp_depth_first_shallow(Entry))
      Block->dropAllReferences(&DummyValue);

    VPBlockBase::deleteCFG(Entry);

    Preheader->dropAllReferences(&DummyValue);
    delete Preheader;
  }
  for (VPValue *VPV : VPLiveInsToFree)
    delete VPV;
  if (BackedgeTakenCount)
    delete BackedgeTakenCount;
}

VPlanPtr VPlan::createInitialVPlan(const SCEV *TripCount, ScalarEvolution &SE,
                                   bool RequiresScalarEpilogueCheck,
                                   bool TailFolded, Loop *TheLoop) {
  VPIRBasicBlock *Entry = new VPIRBasicBlock(TheLoop->getLoopPreheader());
  VPBasicBlock *VecPreheader = new VPBasicBlock("vector.ph");
  auto Plan = std::make_unique<VPlan>(Entry, VecPreheader);
  Plan->TripCount =
      vputils::getOrCreateVPValueForSCEVExpr(*Plan, TripCount, SE);
  // Create VPRegionBlock, with empty header and latch blocks, to be filled
  // during processing later.
  VPBasicBlock *HeaderVPBB = new VPBasicBlock("vector.body");
  VPBasicBlock *LatchVPBB = new VPBasicBlock("vector.latch");
  VPBlockUtils::insertBlockAfter(LatchVPBB, HeaderVPBB);
  auto *TopRegion = new VPRegionBlock(HeaderVPBB, LatchVPBB, "vector loop",
                                      false /*isReplicator*/);

  VPBlockUtils::insertBlockAfter(TopRegion, VecPreheader);
  VPBasicBlock *MiddleVPBB = new VPBasicBlock("middle.block");
  VPBlockUtils::insertBlockAfter(MiddleVPBB, TopRegion);

  VPBasicBlock *ScalarPH = new VPBasicBlock("scalar.ph");
  if (!RequiresScalarEpilogueCheck) {
    VPBlockUtils::connectBlocks(MiddleVPBB, ScalarPH);
    return Plan;
  }

  // If needed, add a check in the middle block to see if we have completed
  // all of the iterations in the first vector loop.  Three cases:
  // 1) If (N - N%VF) == N, then we *don't* need to run the remainder.
  //    Thus if tail is to be folded, we know we don't need to run the
  //    remainder and we can set the condition to true.
  // 2) If we require a scalar epilogue, there is no conditional branch as
  //    we unconditionally branch to the scalar preheader.  Do nothing.
  // 3) Otherwise, construct a runtime check.
  BasicBlock *IRExitBlock = TheLoop->getUniqueExitBlock();
  auto *VPExitBlock = new VPIRBasicBlock(IRExitBlock);
  // The connection order corresponds to the operands of the conditional branch.
  VPBlockUtils::insertBlockAfter(VPExitBlock, MiddleVPBB);
  VPBlockUtils::connectBlocks(MiddleVPBB, ScalarPH);

  auto *ScalarLatchTerm = TheLoop->getLoopLatch()->getTerminator();
  // Here we use the same DebugLoc as the scalar loop latch terminator instead
  // of the corresponding compare because they may have ended up with
  // different line numbers and we want to avoid awkward line stepping while
  // debugging. Eg. if the compare has got a line number inside the loop.
  VPBuilder Builder(MiddleVPBB);
  VPValue *Cmp =
      TailFolded
          ? Plan->getOrAddLiveIn(ConstantInt::getTrue(
                IntegerType::getInt1Ty(TripCount->getType()->getContext())))
          : Builder.createICmp(CmpInst::ICMP_EQ, Plan->getTripCount(),
                               &Plan->getVectorTripCount(),
                               ScalarLatchTerm->getDebugLoc(), "cmp.n");
  Builder.createNaryOp(VPInstruction::BranchOnCond, {Cmp},
                       ScalarLatchTerm->getDebugLoc());
  return Plan;
}

void VPlan::prepareToExecute(Value *TripCountV, Value *VectorTripCountV,
                             Value *CanonicalIVStartValue,
                             VPTransformState &State) {
  // Check if the backedge taken count is needed, and if so build it.
  if (BackedgeTakenCount && BackedgeTakenCount->getNumUsers()) {
    IRBuilder<> Builder(State.CFG.PrevBB->getTerminator());
    auto *TCMO = Builder.CreateSub(TripCountV,
                                   ConstantInt::get(TripCountV->getType(), 1),
                                   "trip.count.minus.1");
    BackedgeTakenCount->setUnderlyingValue(TCMO);
  }

  VectorTripCount.setUnderlyingValue(VectorTripCountV);

  IRBuilder<> Builder(State.CFG.PrevBB->getTerminator());
  // FIXME: Model VF * UF computation completely in VPlan.
  VFxUF.setUnderlyingValue(
      createStepForVF(Builder, TripCountV->getType(), State.VF, State.UF));

  // When vectorizing the epilogue loop, the canonical induction start value
  // needs to be changed from zero to the value after the main vector loop.
  // FIXME: Improve modeling for canonical IV start values in the epilogue loop.
  if (CanonicalIVStartValue) {
    VPValue *VPV = getOrAddLiveIn(CanonicalIVStartValue);
    auto *IV = getCanonicalIV();
    assert(all_of(IV->users(),
                  [](const VPUser *U) {
                    return isa<VPScalarIVStepsRecipe>(U) ||
                           isa<VPScalarCastRecipe>(U) ||
                           isa<VPDerivedIVRecipe>(U) ||
                           cast<VPInstruction>(U)->getOpcode() ==
                               Instruction::Add;
                  }) &&
           "the canonical IV should only be used by its increment or "
           "ScalarIVSteps when resetting the start value");
    IV->setOperand(0, VPV);
  }
}

/// Replace \p VPBB with a VPIRBasicBlock wrapping \p IRBB. All recipes from \p
/// VPBB are moved to the newly created VPIRBasicBlock.  VPBB must have a single
/// predecessor, which is rewired to the new VPIRBasicBlock. All successors of
/// VPBB, if any, are rewired to the new VPIRBasicBlock.
static void replaceVPBBWithIRVPBB(VPBasicBlock *VPBB, BasicBlock *IRBB) {
  VPIRBasicBlock *IRMiddleVPBB = new VPIRBasicBlock(IRBB);
  for (auto &R : make_early_inc_range(*VPBB))
    R.moveBefore(*IRMiddleVPBB, IRMiddleVPBB->end());
  VPBlockBase *PredVPBB = VPBB->getSinglePredecessor();
  VPBlockUtils::disconnectBlocks(PredVPBB, VPBB);
  VPBlockUtils::connectBlocks(PredVPBB, IRMiddleVPBB);
  for (auto *Succ : to_vector(VPBB->getSuccessors())) {
    VPBlockUtils::connectBlocks(IRMiddleVPBB, Succ);
    VPBlockUtils::disconnectBlocks(VPBB, Succ);
  }
  delete VPBB;
}

/// Generate the code inside the preheader and body of the vectorized loop.
/// Assumes a single pre-header basic-block was created for this. Introduce
/// additional basic-blocks as needed, and fill them all.
void VPlan::execute(VPTransformState *State) {
  // Initialize CFG state.
  State->CFG.PrevVPBB = nullptr;
  State->CFG.ExitBB = State->CFG.PrevBB->getSingleSuccessor();
  BasicBlock *VectorPreHeader = State->CFG.PrevBB;
  State->Builder.SetInsertPoint(VectorPreHeader->getTerminator());

  // Disconnect VectorPreHeader from ExitBB in both the CFG and DT.
  cast<BranchInst>(VectorPreHeader->getTerminator())->setSuccessor(0, nullptr);
  State->CFG.DTU.applyUpdates(
      {{DominatorTree::Delete, VectorPreHeader, State->CFG.ExitBB}});

  // Replace regular VPBB's for the middle and scalar preheader blocks with
  // VPIRBasicBlocks wrapping their IR blocks. The IR blocks are created during
  // skeleton creation, so we can only create the VPIRBasicBlocks now during
  // VPlan execution rather than earlier during VPlan construction.
  BasicBlock *MiddleBB = State->CFG.ExitBB;
  VPBasicBlock *MiddleVPBB =
      cast<VPBasicBlock>(getVectorLoopRegion()->getSingleSuccessor());
  // Find the VPBB for the scalar preheader, relying on the current structure
  // when creating the middle block and its successrs: if there's a single
  // predecessor, it must be the scalar preheader. Otherwise, the second
  // successor is the scalar preheader.
  BasicBlock *ScalarPh = MiddleBB->getSingleSuccessor();
  auto &MiddleSuccs = MiddleVPBB->getSuccessors();
  assert((MiddleSuccs.size() == 1 || MiddleSuccs.size() == 2) &&
         "middle block has unexpected successors");
  VPBasicBlock *ScalarPhVPBB = cast<VPBasicBlock>(
      MiddleSuccs.size() == 1 ? MiddleSuccs[0] : MiddleSuccs[1]);
  assert(!isa<VPIRBasicBlock>(ScalarPhVPBB) &&
         "scalar preheader cannot be wrapped already");
  replaceVPBBWithIRVPBB(ScalarPhVPBB, ScalarPh);
  replaceVPBBWithIRVPBB(MiddleVPBB, MiddleBB);

  // Disconnect the middle block from its single successor (the scalar loop
  // header) in both the CFG and DT. The branch will be recreated during VPlan
  // execution.
  auto *BrInst = new UnreachableInst(MiddleBB->getContext());
  BrInst->insertBefore(MiddleBB->getTerminator());
  MiddleBB->getTerminator()->eraseFromParent();
  State->CFG.DTU.applyUpdates({{DominatorTree::Delete, MiddleBB, ScalarPh}});

  // Generate code in the loop pre-header and body.
  for (VPBlockBase *Block : vp_depth_first_shallow(Entry))
    Block->execute(State);

  VPBasicBlock *LatchVPBB = getVectorLoopRegion()->getExitingBasicBlock();
  BasicBlock *VectorLatchBB = State->CFG.VPBB2IRBB[LatchVPBB];

  // Fix the latch value of canonical, reduction and first-order recurrences
  // phis in the vector loop.
  VPBasicBlock *Header = getVectorLoopRegion()->getEntryBasicBlock();
  for (VPRecipeBase &R : Header->phis()) {
    // Skip phi-like recipes that generate their backedege values themselves.
    if (isa<VPWidenPHIRecipe>(&R))
      continue;

    if (isa<VPWidenPointerInductionRecipe>(&R) ||
        isa<VPWidenIntOrFpInductionRecipe>(&R)) {
      PHINode *Phi = nullptr;
      if (isa<VPWidenIntOrFpInductionRecipe>(&R)) {
        Phi = cast<PHINode>(State->get(R.getVPSingleValue(), 0));
      } else {
        auto *WidenPhi = cast<VPWidenPointerInductionRecipe>(&R);
        assert(!WidenPhi->onlyScalarsGenerated(State->VF.isScalable()) &&
               "recipe generating only scalars should have been replaced");
        auto *GEP = cast<GetElementPtrInst>(State->get(WidenPhi, 0));
        Phi = cast<PHINode>(GEP->getPointerOperand());
      }

      Phi->setIncomingBlock(1, VectorLatchBB);

      // Move the last step to the end of the latch block. This ensures
      // consistent placement of all induction updates.
      Instruction *Inc = cast<Instruction>(Phi->getIncomingValue(1));
      Inc->moveBefore(VectorLatchBB->getTerminator()->getPrevNode());
      continue;
    }

    auto *PhiR = cast<VPHeaderPHIRecipe>(&R);
    // For  canonical IV, first-order recurrences and in-order reduction phis,
    // only a single part is generated, which provides the last part from the
    // previous iteration. For non-ordered reductions all UF parts are
    // generated.
    bool SinglePartNeeded =
        isa<VPCanonicalIVPHIRecipe>(PhiR) ||
        isa<VPFirstOrderRecurrencePHIRecipe, VPEVLBasedIVPHIRecipe>(PhiR) ||
        (isa<VPReductionPHIRecipe>(PhiR) &&
         cast<VPReductionPHIRecipe>(PhiR)->isOrdered());
    bool NeedsScalar =
        isa<VPCanonicalIVPHIRecipe, VPEVLBasedIVPHIRecipe>(PhiR) ||
        (isa<VPReductionPHIRecipe>(PhiR) &&
         cast<VPReductionPHIRecipe>(PhiR)->isInLoop());
    unsigned LastPartForNewPhi = SinglePartNeeded ? 1 : State->UF;

    for (unsigned Part = 0; Part < LastPartForNewPhi; ++Part) {
      Value *Phi = State->get(PhiR, Part, NeedsScalar);
      Value *Val =
          State->get(PhiR->getBackedgeValue(),
                     SinglePartNeeded ? State->UF - 1 : Part, NeedsScalar);
      cast<PHINode>(Phi)->addIncoming(Val, VectorLatchBB);
    }
  }

  State->CFG.DTU.flush();
  assert(State->CFG.DTU.getDomTree().verify(
             DominatorTree::VerificationLevel::Fast) &&
         "DT not preserved correctly");
}

InstructionCost VPlan::cost(ElementCount VF, VPCostContext &Ctx) {
  // For now only return the cost of the vector loop region, ignoring any other
  // blocks, like the preheader or middle blocks.
  return getVectorLoopRegion()->cost(VF, Ctx);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPlan::printLiveIns(raw_ostream &O) const {
  VPSlotTracker SlotTracker(this);

  if (VFxUF.getNumUsers() > 0) {
    O << "\nLive-in ";
    VFxUF.printAsOperand(O, SlotTracker);
    O << " = VF * UF";
  }

  if (VectorTripCount.getNumUsers() > 0) {
    O << "\nLive-in ";
    VectorTripCount.printAsOperand(O, SlotTracker);
    O << " = vector-trip-count";
  }

  if (BackedgeTakenCount && BackedgeTakenCount->getNumUsers()) {
    O << "\nLive-in ";
    BackedgeTakenCount->printAsOperand(O, SlotTracker);
    O << " = backedge-taken count";
  }

  O << "\n";
  if (TripCount->isLiveIn())
    O << "Live-in ";
  TripCount->printAsOperand(O, SlotTracker);
  O << " = original trip-count";
  O << "\n";
}

LLVM_DUMP_METHOD
void VPlan::print(raw_ostream &O) const {
  VPSlotTracker SlotTracker(this);

  O << "VPlan '" << getName() << "' {";

  printLiveIns(O);

  if (!getPreheader()->empty()) {
    O << "\n";
    getPreheader()->print(O, "", SlotTracker);
  }

  for (const VPBlockBase *Block : vp_depth_first_shallow(getEntry())) {
    O << '\n';
    Block->print(O, "", SlotTracker);
  }

  if (!LiveOuts.empty())
    O << "\n";
  for (const auto &KV : LiveOuts) {
    KV.second->print(O, SlotTracker);
  }

  O << "}\n";
}

std::string VPlan::getName() const {
  std::string Out;
  raw_string_ostream RSO(Out);
  RSO << Name << " for ";
  if (!VFs.empty()) {
    RSO << "VF={" << VFs[0];
    for (ElementCount VF : drop_begin(VFs))
      RSO << "," << VF;
    RSO << "},";
  }

  if (UFs.empty()) {
    RSO << "UF>=1";
  } else {
    RSO << "UF={" << UFs[0];
    for (unsigned UF : drop_begin(UFs))
      RSO << "," << UF;
    RSO << "}";
  }

  return Out;
}

LLVM_DUMP_METHOD
void VPlan::printDOT(raw_ostream &O) const {
  VPlanPrinter Printer(O, *this);
  Printer.dump();
}

LLVM_DUMP_METHOD
void VPlan::dump() const { print(dbgs()); }
#endif

void VPlan::addLiveOut(PHINode *PN, VPValue *V) {
  assert(LiveOuts.count(PN) == 0 && "an exit value for PN already exists");
  LiveOuts.insert({PN, new VPLiveOut(PN, V)});
}

static void remapOperands(VPBlockBase *Entry, VPBlockBase *NewEntry,
                          DenseMap<VPValue *, VPValue *> &Old2NewVPValues) {
  // Update the operands of all cloned recipes starting at NewEntry. This
  // traverses all reachable blocks. This is done in two steps, to handle cycles
  // in PHI recipes.
  ReversePostOrderTraversal<VPBlockDeepTraversalWrapper<VPBlockBase *>>
      OldDeepRPOT(Entry);
  ReversePostOrderTraversal<VPBlockDeepTraversalWrapper<VPBlockBase *>>
      NewDeepRPOT(NewEntry);
  // First, collect all mappings from old to new VPValues defined by cloned
  // recipes.
  for (const auto &[OldBB, NewBB] :
       zip(VPBlockUtils::blocksOnly<VPBasicBlock>(OldDeepRPOT),
           VPBlockUtils::blocksOnly<VPBasicBlock>(NewDeepRPOT))) {
    assert(OldBB->getRecipeList().size() == NewBB->getRecipeList().size() &&
           "blocks must have the same number of recipes");
    for (const auto &[OldR, NewR] : zip(*OldBB, *NewBB)) {
      assert(OldR.getNumOperands() == NewR.getNumOperands() &&
             "recipes must have the same number of operands");
      assert(OldR.getNumDefinedValues() == NewR.getNumDefinedValues() &&
             "recipes must define the same number of operands");
      for (const auto &[OldV, NewV] :
           zip(OldR.definedValues(), NewR.definedValues()))
        Old2NewVPValues[OldV] = NewV;
    }
  }

  // Update all operands to use cloned VPValues.
  for (VPBasicBlock *NewBB :
       VPBlockUtils::blocksOnly<VPBasicBlock>(NewDeepRPOT)) {
    for (VPRecipeBase &NewR : *NewBB)
      for (unsigned I = 0, E = NewR.getNumOperands(); I != E; ++I) {
        VPValue *NewOp = Old2NewVPValues.lookup(NewR.getOperand(I));
        NewR.setOperand(I, NewOp);
      }
  }
}

VPlan *VPlan::duplicate() {
  // Clone blocks.
  VPBasicBlock *NewPreheader = Preheader->clone();
  const auto &[NewEntry, __] = cloneFrom(Entry);

  // Create VPlan, clone live-ins and remap operands in the cloned blocks.
  auto *NewPlan = new VPlan(NewPreheader, cast<VPBasicBlock>(NewEntry));
  DenseMap<VPValue *, VPValue *> Old2NewVPValues;
  for (VPValue *OldLiveIn : VPLiveInsToFree) {
    Old2NewVPValues[OldLiveIn] =
        NewPlan->getOrAddLiveIn(OldLiveIn->getLiveInIRValue());
  }
  Old2NewVPValues[&VectorTripCount] = &NewPlan->VectorTripCount;
  Old2NewVPValues[&VFxUF] = &NewPlan->VFxUF;
  if (BackedgeTakenCount) {
    NewPlan->BackedgeTakenCount = new VPValue();
    Old2NewVPValues[BackedgeTakenCount] = NewPlan->BackedgeTakenCount;
  }
  assert(TripCount && "trip count must be set");
  if (TripCount->isLiveIn())
    Old2NewVPValues[TripCount] =
        NewPlan->getOrAddLiveIn(TripCount->getLiveInIRValue());
  // else NewTripCount will be created and inserted into Old2NewVPValues when
  // TripCount is cloned. In any case NewPlan->TripCount is updated below.

  remapOperands(Preheader, NewPreheader, Old2NewVPValues);
  remapOperands(Entry, NewEntry, Old2NewVPValues);

  // Clone live-outs.
  for (const auto &[_, LO] : LiveOuts)
    NewPlan->addLiveOut(LO->getPhi(), Old2NewVPValues[LO->getOperand(0)]);

  // Initialize remaining fields of cloned VPlan.
  NewPlan->VFs = VFs;
  NewPlan->UFs = UFs;
  // TODO: Adjust names.
  NewPlan->Name = Name;
  assert(Old2NewVPValues.contains(TripCount) &&
         "TripCount must have been added to Old2NewVPValues");
  NewPlan->TripCount = Old2NewVPValues[TripCount];
  return NewPlan;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)

Twine VPlanPrinter::getUID(const VPBlockBase *Block) {
  return (isa<VPRegionBlock>(Block) ? "cluster_N" : "N") +
         Twine(getOrCreateBID(Block));
}

Twine VPlanPrinter::getOrCreateName(const VPBlockBase *Block) {
  const std::string &Name = Block->getName();
  if (!Name.empty())
    return Name;
  return "VPB" + Twine(getOrCreateBID(Block));
}

void VPlanPrinter::dump() {
  Depth = 1;
  bumpIndent(0);
  OS << "digraph VPlan {\n";
  OS << "graph [labelloc=t, fontsize=30; label=\"Vectorization Plan";
  if (!Plan.getName().empty())
    OS << "\\n" << DOT::EscapeString(Plan.getName());

  {
    // Print live-ins.
  std::string Str;
  raw_string_ostream SS(Str);
  Plan.printLiveIns(SS);
  SmallVector<StringRef, 0> Lines;
  StringRef(Str).rtrim('\n').split(Lines, "\n");
  for (auto Line : Lines)
    OS << DOT::EscapeString(Line.str()) << "\\n";
  }

  OS << "\"]\n";
  OS << "node [shape=rect, fontname=Courier, fontsize=30]\n";
  OS << "edge [fontname=Courier, fontsize=30]\n";
  OS << "compound=true\n";

  dumpBlock(Plan.getPreheader());

  for (const VPBlockBase *Block : vp_depth_first_shallow(Plan.getEntry()))
    dumpBlock(Block);

  OS << "}\n";
}

void VPlanPrinter::dumpBlock(const VPBlockBase *Block) {
  if (const VPBasicBlock *BasicBlock = dyn_cast<VPBasicBlock>(Block))
    dumpBasicBlock(BasicBlock);
  else if (const VPRegionBlock *Region = dyn_cast<VPRegionBlock>(Block))
    dumpRegion(Region);
  else
    llvm_unreachable("Unsupported kind of VPBlock.");
}

void VPlanPrinter::drawEdge(const VPBlockBase *From, const VPBlockBase *To,
                            bool Hidden, const Twine &Label) {
  // Due to "dot" we print an edge between two regions as an edge between the
  // exiting basic block and the entry basic of the respective regions.
  const VPBlockBase *Tail = From->getExitingBasicBlock();
  const VPBlockBase *Head = To->getEntryBasicBlock();
  OS << Indent << getUID(Tail) << " -> " << getUID(Head);
  OS << " [ label=\"" << Label << '\"';
  if (Tail != From)
    OS << " ltail=" << getUID(From);
  if (Head != To)
    OS << " lhead=" << getUID(To);
  if (Hidden)
    OS << "; splines=none";
  OS << "]\n";
}

void VPlanPrinter::dumpEdges(const VPBlockBase *Block) {
  auto &Successors = Block->getSuccessors();
  if (Successors.size() == 1)
    drawEdge(Block, Successors.front(), false, "");
  else if (Successors.size() == 2) {
    drawEdge(Block, Successors.front(), false, "T");
    drawEdge(Block, Successors.back(), false, "F");
  } else {
    unsigned SuccessorNumber = 0;
    for (auto *Successor : Successors)
      drawEdge(Block, Successor, false, Twine(SuccessorNumber++));
  }
}

void VPlanPrinter::dumpBasicBlock(const VPBasicBlock *BasicBlock) {
  // Implement dot-formatted dump by performing plain-text dump into the
  // temporary storage followed by some post-processing.
  OS << Indent << getUID(BasicBlock) << " [label =\n";
  bumpIndent(1);
  std::string Str;
  raw_string_ostream SS(Str);
  // Use no indentation as we need to wrap the lines into quotes ourselves.
  BasicBlock->print(SS, "", SlotTracker);

  // We need to process each line of the output separately, so split
  // single-string plain-text dump.
  SmallVector<StringRef, 0> Lines;
  StringRef(Str).rtrim('\n').split(Lines, "\n");

  auto EmitLine = [&](StringRef Line, StringRef Suffix) {
    OS << Indent << '"' << DOT::EscapeString(Line.str()) << "\\l\"" << Suffix;
  };

  // Don't need the "+" after the last line.
  for (auto Line : make_range(Lines.begin(), Lines.end() - 1))
    EmitLine(Line, " +\n");
  EmitLine(Lines.back(), "\n");

  bumpIndent(-1);
  OS << Indent << "]\n";

  dumpEdges(BasicBlock);
}

void VPlanPrinter::dumpRegion(const VPRegionBlock *Region) {
  OS << Indent << "subgraph " << getUID(Region) << " {\n";
  bumpIndent(1);
  OS << Indent << "fontname=Courier\n"
     << Indent << "label=\""
     << DOT::EscapeString(Region->isReplicator() ? "<xVFxUF> " : "<x1> ")
     << DOT::EscapeString(Region->getName()) << "\"\n";
  // Dump the blocks of the region.
  assert(Region->getEntry() && "Region contains no inner blocks.");
  for (const VPBlockBase *Block : vp_depth_first_shallow(Region->getEntry()))
    dumpBlock(Block);
  bumpIndent(-1);
  OS << Indent << "}\n";
  dumpEdges(Region);
}

void VPlanIngredient::print(raw_ostream &O) const {
  if (auto *Inst = dyn_cast<Instruction>(V)) {
    if (!Inst->getType()->isVoidTy()) {
      Inst->printAsOperand(O, false);
      O << " = ";
    }
    O << Inst->getOpcodeName() << " ";
    unsigned E = Inst->getNumOperands();
    if (E > 0) {
      Inst->getOperand(0)->printAsOperand(O, false);
      for (unsigned I = 1; I < E; ++I)
        Inst->getOperand(I)->printAsOperand(O << ", ", false);
    }
  } else // !Inst
    V->printAsOperand(O, false);
}

#endif

template void DomTreeBuilder::Calculate<VPDominatorTree>(VPDominatorTree &DT);

void VPValue::replaceAllUsesWith(VPValue *New) {
  replaceUsesWithIf(New, [](VPUser &, unsigned) { return true; });
}

void VPValue::replaceUsesWithIf(
    VPValue *New,
    llvm::function_ref<bool(VPUser &U, unsigned Idx)> ShouldReplace) {
  // Note that this early exit is required for correctness; the implementation
  // below relies on the number of users for this VPValue to decrease, which
  // isn't the case if this == New.
  if (this == New)
    return;

  for (unsigned J = 0; J < getNumUsers();) {
    VPUser *User = Users[J];
    bool RemovedUser = false;
    for (unsigned I = 0, E = User->getNumOperands(); I < E; ++I) {
      if (User->getOperand(I) != this || !ShouldReplace(*User, I))
        continue;

      RemovedUser = true;
      User->setOperand(I, New);
    }
    // If a user got removed after updating the current user, the next user to
    // update will be moved to the current position, so we only need to
    // increment the index if the number of users did not change.
    if (!RemovedUser)
      J++;
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPValue::printAsOperand(raw_ostream &OS, VPSlotTracker &Tracker) const {
  OS << Tracker.getOrCreateName(this);
}

void VPUser::printOperands(raw_ostream &O, VPSlotTracker &SlotTracker) const {
  interleaveComma(operands(), O, [&O, &SlotTracker](VPValue *Op) {
    Op->printAsOperand(O, SlotTracker);
  });
}
#endif

void VPInterleavedAccessInfo::visitRegion(VPRegionBlock *Region,
                                          Old2NewTy &Old2New,
                                          InterleavedAccessInfo &IAI) {
  ReversePostOrderTraversal<VPBlockShallowTraversalWrapper<VPBlockBase *>>
      RPOT(Region->getEntry());
  for (VPBlockBase *Base : RPOT) {
    visitBlock(Base, Old2New, IAI);
  }
}

void VPInterleavedAccessInfo::visitBlock(VPBlockBase *Block, Old2NewTy &Old2New,
                                         InterleavedAccessInfo &IAI) {
  if (VPBasicBlock *VPBB = dyn_cast<VPBasicBlock>(Block)) {
    for (VPRecipeBase &VPI : *VPBB) {
      if (isa<VPWidenPHIRecipe>(&VPI))
        continue;
      assert(isa<VPInstruction>(&VPI) && "Can only handle VPInstructions");
      auto *VPInst = cast<VPInstruction>(&VPI);

      auto *Inst = dyn_cast_or_null<Instruction>(VPInst->getUnderlyingValue());
      if (!Inst)
        continue;
      auto *IG = IAI.getInterleaveGroup(Inst);
      if (!IG)
        continue;

      auto NewIGIter = Old2New.find(IG);
      if (NewIGIter == Old2New.end())
        Old2New[IG] = new InterleaveGroup<VPInstruction>(
            IG->getFactor(), IG->isReverse(), IG->getAlign());

      if (Inst == IG->getInsertPos())
        Old2New[IG]->setInsertPos(VPInst);

      InterleaveGroupMap[VPInst] = Old2New[IG];
      InterleaveGroupMap[VPInst]->insertMember(
          VPInst, IG->getIndex(Inst),
          Align(IG->isReverse() ? (-1) * int(IG->getFactor())
                                : IG->getFactor()));
    }
  } else if (VPRegionBlock *Region = dyn_cast<VPRegionBlock>(Block))
    visitRegion(Region, Old2New, IAI);
  else
    llvm_unreachable("Unsupported kind of VPBlock.");
}

VPInterleavedAccessInfo::VPInterleavedAccessInfo(VPlan &Plan,
                                                 InterleavedAccessInfo &IAI) {
  Old2NewTy Old2New;
  visitRegion(Plan.getVectorLoopRegion(), Old2New, IAI);
}

void VPSlotTracker::assignName(const VPValue *V) {
  assert(!VPValue2Name.contains(V) && "VPValue already has a name!");
  auto *UV = V->getUnderlyingValue();
  if (!UV) {
    VPValue2Name[V] = (Twine("vp<%") + Twine(NextSlot) + ">").str();
    NextSlot++;
    return;
  }

  // Use the name of the underlying Value, wrapped in "ir<>", and versioned by
  // appending ".Number" to the name if there are multiple uses.
  std::string Name;
  raw_string_ostream S(Name);
  UV->printAsOperand(S, false);
  assert(!Name.empty() && "Name cannot be empty.");
  std::string BaseName = (Twine("ir<") + Name + Twine(">")).str();

  // First assign the base name for V.
  const auto &[A, _] = VPValue2Name.insert({V, BaseName});
  // Integer or FP constants with different types will result in he same string
  // due to stripping types.
  if (V->isLiveIn() && isa<ConstantInt, ConstantFP>(UV))
    return;

  // If it is already used by C > 0 other VPValues, increase the version counter
  // C and use it for V.
  const auto &[C, UseInserted] = BaseName2Version.insert({BaseName, 0});
  if (!UseInserted) {
    C->second++;
    A->second = (BaseName + Twine(".") + Twine(C->second)).str();
  }
}

void VPSlotTracker::assignNames(const VPlan &Plan) {
  if (Plan.VFxUF.getNumUsers() > 0)
    assignName(&Plan.VFxUF);
  assignName(&Plan.VectorTripCount);
  if (Plan.BackedgeTakenCount)
    assignName(Plan.BackedgeTakenCount);
  for (VPValue *LI : Plan.VPLiveInsToFree)
    assignName(LI);
  assignNames(Plan.getPreheader());

  ReversePostOrderTraversal<VPBlockDeepTraversalWrapper<const VPBlockBase *>>
      RPOT(VPBlockDeepTraversalWrapper<const VPBlockBase *>(Plan.getEntry()));
  for (const VPBasicBlock *VPBB :
       VPBlockUtils::blocksOnly<const VPBasicBlock>(RPOT))
    assignNames(VPBB);
}

void VPSlotTracker::assignNames(const VPBasicBlock *VPBB) {
  for (const VPRecipeBase &Recipe : *VPBB)
    for (VPValue *Def : Recipe.definedValues())
      assignName(Def);
}

std::string VPSlotTracker::getOrCreateName(const VPValue *V) const {
  std::string Name = VPValue2Name.lookup(V);
  if (!Name.empty())
    return Name;

  // If no name was assigned, no VPlan was provided when creating the slot
  // tracker or it is not reachable from the provided VPlan. This can happen,
  // e.g. when trying to print a recipe that has not been inserted into a VPlan
  // in a debugger.
  // TODO: Update VPSlotTracker constructor to assign names to recipes &
  // VPValues not associated with a VPlan, instead of constructing names ad-hoc
  // here.
  const VPRecipeBase *DefR = V->getDefiningRecipe();
  (void)DefR;
  assert((!DefR || !DefR->getParent() || !DefR->getParent()->getPlan()) &&
         "VPValue defined by a recipe in a VPlan?");

  // Use the underlying value's name, if there is one.
  if (auto *UV = V->getUnderlyingValue()) {
    std::string Name;
    raw_string_ostream S(Name);
    UV->printAsOperand(S, false);
    return (Twine("ir<") + Name + ">").str();
  }

  return "<badref>";
}

bool vputils::onlyFirstLaneUsed(const VPValue *Def) {
  return all_of(Def->users(),
                [Def](const VPUser *U) { return U->onlyFirstLaneUsed(Def); });
}

bool vputils::onlyFirstPartUsed(const VPValue *Def) {
  return all_of(Def->users(),
                [Def](const VPUser *U) { return U->onlyFirstPartUsed(Def); });
}

VPValue *vputils::getOrCreateVPValueForSCEVExpr(VPlan &Plan, const SCEV *Expr,
                                                ScalarEvolution &SE) {
  if (auto *Expanded = Plan.getSCEVExpansion(Expr))
    return Expanded;
  VPValue *Expanded = nullptr;
  if (auto *E = dyn_cast<SCEVConstant>(Expr))
    Expanded = Plan.getOrAddLiveIn(E->getValue());
  else if (auto *E = dyn_cast<SCEVUnknown>(Expr))
    Expanded = Plan.getOrAddLiveIn(E->getValue());
  else {
    Expanded = new VPExpandSCEVRecipe(Expr, SE);
    Plan.getPreheader()->appendRecipe(Expanded->getDefiningRecipe());
  }
  Plan.addSCEVExpansion(Expr, Expanded);
  return Expanded;
}

bool vputils::isHeaderMask(VPValue *V, VPlan &Plan) {
  if (isa<VPActiveLaneMaskPHIRecipe>(V))
    return true;

  auto IsWideCanonicalIV = [](VPValue *A) {
    return isa<VPWidenCanonicalIVRecipe>(A) ||
           (isa<VPWidenIntOrFpInductionRecipe>(A) &&
            cast<VPWidenIntOrFpInductionRecipe>(A)->isCanonical());
  };

  VPValue *A, *B;
  if (match(V, m_ActiveLaneMask(m_VPValue(A), m_VPValue(B))))
    return B == Plan.getTripCount() &&
           (match(A, m_ScalarIVSteps(m_CanonicalIV(), m_SpecificInt(1))) ||
            IsWideCanonicalIV(A));

  return match(V, m_Binary<Instruction::ICmp>(m_VPValue(A), m_VPValue(B))) &&
         IsWideCanonicalIV(A) && B == Plan.getOrCreateBackedgeTakenCount();
}
