//===- LoopVersioning.cpp - Utility to version a loop ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a utility class to perform loop versioning.  The versioned
// loop speculates that otherwise may-aliasing memory accesses don't overlap and
// emits checks to prove this.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

#define DEBUG_TYPE "loop-versioning"

static cl::opt<bool>
    AnnotateNoAlias("loop-version-annotate-no-alias", cl::init(true),
                    cl::Hidden,
                    cl::desc("Add no-alias annotation for instructions that "
                             "are disambiguated by memchecks"));

LoopVersioning::LoopVersioning(const LoopAccessInfo &LAI,
                               ArrayRef<RuntimePointerCheck> Checks, Loop *L,
                               LoopInfo *LI, DominatorTree *DT,
                               ScalarEvolution *SE)
    : VersionedLoop(L), AliasChecks(Checks.begin(), Checks.end()),
      Preds(LAI.getPSE().getPredicate()), LAI(LAI), LI(LI), DT(DT),
      SE(SE) {
}

void LoopVersioning::versionLoop(
    const SmallVectorImpl<Instruction *> &DefsUsedOutside) {
  assert(VersionedLoop->getUniqueExitBlock() && "No single exit block");
  assert(VersionedLoop->isLoopSimplifyForm() &&
         "Loop is not in loop-simplify form");

  Value *MemRuntimeCheck;
  Value *SCEVRuntimeCheck;
  Value *RuntimeCheck = nullptr;

  // Add the memcheck in the original preheader (this is empty initially).
  BasicBlock *RuntimeCheckBB = VersionedLoop->getLoopPreheader();
  const auto &RtPtrChecking = *LAI.getRuntimePointerChecking();

  SCEVExpander Exp2(*RtPtrChecking.getSE(),
                    VersionedLoop->getHeader()->getDataLayout(),
                    "induction");
  MemRuntimeCheck = addRuntimeChecks(RuntimeCheckBB->getTerminator(),
                                     VersionedLoop, AliasChecks, Exp2);

  SCEVExpander Exp(*SE, RuntimeCheckBB->getDataLayout(),
                   "scev.check");
  SCEVRuntimeCheck =
      Exp.expandCodeForPredicate(&Preds, RuntimeCheckBB->getTerminator());

  IRBuilder<InstSimplifyFolder> Builder(
      RuntimeCheckBB->getContext(),
      InstSimplifyFolder(RuntimeCheckBB->getDataLayout()));
  if (MemRuntimeCheck && SCEVRuntimeCheck) {
    Builder.SetInsertPoint(RuntimeCheckBB->getTerminator());
    RuntimeCheck =
        Builder.CreateOr(MemRuntimeCheck, SCEVRuntimeCheck, "lver.safe");
  } else
    RuntimeCheck = MemRuntimeCheck ? MemRuntimeCheck : SCEVRuntimeCheck;

  assert(RuntimeCheck && "called even though we don't need "
                         "any runtime checks");

  // Rename the block to make the IR more readable.
  RuntimeCheckBB->setName(VersionedLoop->getHeader()->getName() +
                          ".lver.check");

  // Create empty preheader for the loop (and after cloning for the
  // non-versioned loop).
  BasicBlock *PH =
      SplitBlock(RuntimeCheckBB, RuntimeCheckBB->getTerminator(), DT, LI,
                 nullptr, VersionedLoop->getHeader()->getName() + ".ph");

  // Clone the loop including the preheader.
  //
  // FIXME: This does not currently preserve SimplifyLoop because the exit
  // block is a join between the two loops.
  SmallVector<BasicBlock *, 8> NonVersionedLoopBlocks;
  NonVersionedLoop =
      cloneLoopWithPreheader(PH, RuntimeCheckBB, VersionedLoop, VMap,
                             ".lver.orig", LI, DT, NonVersionedLoopBlocks);
  remapInstructionsInBlocks(NonVersionedLoopBlocks, VMap);

  // Insert the conditional branch based on the result of the memchecks.
  Instruction *OrigTerm = RuntimeCheckBB->getTerminator();
  Builder.SetInsertPoint(OrigTerm);
  Builder.CreateCondBr(RuntimeCheck, NonVersionedLoop->getLoopPreheader(),
                       VersionedLoop->getLoopPreheader());
  OrigTerm->eraseFromParent();

  // The loops merge in the original exit block.  This is now dominated by the
  // memchecking block.
  DT->changeImmediateDominator(VersionedLoop->getExitBlock(), RuntimeCheckBB);

  // Adds the necessary PHI nodes for the versioned loops based on the
  // loop-defined values used outside of the loop.
  addPHINodes(DefsUsedOutside);
  formDedicatedExitBlocks(NonVersionedLoop, DT, LI, nullptr, true);
  formDedicatedExitBlocks(VersionedLoop, DT, LI, nullptr, true);
  assert(NonVersionedLoop->isLoopSimplifyForm() &&
         VersionedLoop->isLoopSimplifyForm() &&
         "The versioned loops should be in simplify form.");
}

void LoopVersioning::addPHINodes(
    const SmallVectorImpl<Instruction *> &DefsUsedOutside) {
  BasicBlock *PHIBlock = VersionedLoop->getExitBlock();
  assert(PHIBlock && "No single successor to loop exit block");
  PHINode *PN;

  // First add a single-operand PHI for each DefsUsedOutside if one does not
  // exists yet.
  for (auto *Inst : DefsUsedOutside) {
    // See if we have a single-operand PHI with the value defined by the
    // original loop.
    for (auto I = PHIBlock->begin(); (PN = dyn_cast<PHINode>(I)); ++I) {
      if (PN->getIncomingValue(0) == Inst) {
        SE->forgetValue(PN);
        break;
      }
    }
    // If not create it.
    if (!PN) {
      PN = PHINode::Create(Inst->getType(), 2, Inst->getName() + ".lver");
      PN->insertBefore(PHIBlock->begin());
      SmallVector<User*, 8> UsersToUpdate;
      for (User *U : Inst->users())
        if (!VersionedLoop->contains(cast<Instruction>(U)->getParent()))
          UsersToUpdate.push_back(U);
      for (User *U : UsersToUpdate)
        U->replaceUsesOfWith(Inst, PN);
      PN->addIncoming(Inst, VersionedLoop->getExitingBlock());
    }
  }

  // Then for each PHI add the operand for the edge from the cloned loop.
  for (auto I = PHIBlock->begin(); (PN = dyn_cast<PHINode>(I)); ++I) {
    assert(PN->getNumOperands() == 1 &&
           "Exit block should only have on predecessor");

    // If the definition was cloned used that otherwise use the same value.
    Value *ClonedValue = PN->getIncomingValue(0);
    auto Mapped = VMap.find(ClonedValue);
    if (Mapped != VMap.end())
      ClonedValue = Mapped->second;

    PN->addIncoming(ClonedValue, NonVersionedLoop->getExitingBlock());
  }
}

void LoopVersioning::prepareNoAliasMetadata() {
  // We need to turn the no-alias relation between pointer checking groups into
  // no-aliasing annotations between instructions.
  //
  // We accomplish this by mapping each pointer checking group (a set of
  // pointers memchecked together) to an alias scope and then also mapping each
  // group to the list of scopes it can't alias.

  const RuntimePointerChecking *RtPtrChecking = LAI.getRuntimePointerChecking();
  LLVMContext &Context = VersionedLoop->getHeader()->getContext();

  // First allocate an aliasing scope for each pointer checking group.
  //
  // While traversing through the checking groups in the loop, also create a
  // reverse map from pointers to the pointer checking group they were assigned
  // to.
  MDBuilder MDB(Context);
  MDNode *Domain = MDB.createAnonymousAliasScopeDomain("LVerDomain");

  for (const auto &Group : RtPtrChecking->CheckingGroups) {
    GroupToScope[&Group] = MDB.createAnonymousAliasScope(Domain);

    for (unsigned PtrIdx : Group.Members)
      PtrToGroup[RtPtrChecking->getPointerInfo(PtrIdx).PointerValue] = &Group;
  }

  // Go through the checks and for each pointer group, collect the scopes for
  // each non-aliasing pointer group.
  DenseMap<const RuntimeCheckingPtrGroup *, SmallVector<Metadata *, 4>>
      GroupToNonAliasingScopes;

  for (const auto &Check : AliasChecks)
    GroupToNonAliasingScopes[Check.first].push_back(GroupToScope[Check.second]);

  // Finally, transform the above to actually map to scope list which is what
  // the metadata uses.

  for (const auto &Pair : GroupToNonAliasingScopes)
    GroupToNonAliasingScopeList[Pair.first] = MDNode::get(Context, Pair.second);
}

void LoopVersioning::annotateLoopWithNoAlias() {
  if (!AnnotateNoAlias)
    return;

  // First prepare the maps.
  prepareNoAliasMetadata();

  // Add the scope and no-alias metadata to the instructions.
  for (Instruction *I : LAI.getDepChecker().getMemoryInstructions()) {
    annotateInstWithNoAlias(I);
  }
}

void LoopVersioning::annotateInstWithNoAlias(Instruction *VersionedInst,
                                             const Instruction *OrigInst) {
  if (!AnnotateNoAlias)
    return;

  LLVMContext &Context = VersionedLoop->getHeader()->getContext();
  const Value *Ptr = isa<LoadInst>(OrigInst)
                         ? cast<LoadInst>(OrigInst)->getPointerOperand()
                         : cast<StoreInst>(OrigInst)->getPointerOperand();

  // Find the group for the pointer and then add the scope metadata.
  auto Group = PtrToGroup.find(Ptr);
  if (Group != PtrToGroup.end()) {
    VersionedInst->setMetadata(
        LLVMContext::MD_alias_scope,
        MDNode::concatenate(
            VersionedInst->getMetadata(LLVMContext::MD_alias_scope),
            MDNode::get(Context, GroupToScope[Group->second])));

    // Add the no-alias metadata.
    auto NonAliasingScopeList = GroupToNonAliasingScopeList.find(Group->second);
    if (NonAliasingScopeList != GroupToNonAliasingScopeList.end())
      VersionedInst->setMetadata(
          LLVMContext::MD_noalias,
          MDNode::concatenate(
              VersionedInst->getMetadata(LLVMContext::MD_noalias),
              NonAliasingScopeList->second));
  }
}

namespace {
bool runImpl(LoopInfo *LI, LoopAccessInfoManager &LAIs, DominatorTree *DT,
             ScalarEvolution *SE) {
  // Build up a worklist of inner-loops to version. This is necessary as the
  // act of versioning a loop creates new loops and can invalidate iterators
  // across the loops.
  SmallVector<Loop *, 8> Worklist;

  for (Loop *TopLevelLoop : *LI)
    for (Loop *L : depth_first(TopLevelLoop))
      // We only handle inner-most loops.
      if (L->isInnermost())
        Worklist.push_back(L);

  // Now walk the identified inner loops.
  bool Changed = false;
  for (Loop *L : Worklist) {
    if (!L->isLoopSimplifyForm() || !L->isRotatedForm() ||
        !L->getExitingBlock())
      continue;
    const LoopAccessInfo &LAI = LAIs.getInfo(*L);
    if (!LAI.hasConvergentOp() &&
        (LAI.getNumRuntimePointerChecks() ||
         !LAI.getPSE().getPredicate().isAlwaysTrue())) {
      LoopVersioning LVer(LAI, LAI.getRuntimePointerChecking()->getChecks(), L,
                          LI, DT, SE);
      LVer.versionLoop();
      LVer.annotateLoopWithNoAlias();
      Changed = true;
      LAIs.clear();
    }
  }

  return Changed;
}
}

PreservedAnalyses LoopVersioningPass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  LoopAccessInfoManager &LAIs = AM.getResult<LoopAccessAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  if (runImpl(&LI, LAIs, &DT, &SE))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
