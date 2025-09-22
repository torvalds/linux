//===- AssumeBundleBuilder.cpp - tools to preserve informations -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

namespace llvm {
cl::opt<bool> ShouldPreserveAllAttributes(
    "assume-preserve-all", cl::init(false), cl::Hidden,
    cl::desc("enable preservation of all attrbitues. even those that are "
             "unlikely to be usefull"));

cl::opt<bool> EnableKnowledgeRetention(
    "enable-knowledge-retention", cl::init(false), cl::Hidden,
    cl::desc(
        "enable preservation of attributes throughout code transformation"));
} // namespace llvm

#define DEBUG_TYPE "assume-builder"

STATISTIC(NumAssumeBuilt, "Number of assume built by the assume builder");
STATISTIC(NumBundlesInAssumes, "Total number of Bundles in the assume built");
STATISTIC(NumAssumesMerged,
          "Number of assume merged by the assume simplify pass");
STATISTIC(NumAssumesRemoved,
          "Number of assume removed by the assume simplify pass");

DEBUG_COUNTER(BuildAssumeCounter, "assume-builder-counter",
              "Controls which assumes gets created");

namespace {

bool isUsefullToPreserve(Attribute::AttrKind Kind) {
  switch (Kind) {
    case Attribute::NonNull:
    case Attribute::NoUndef:
    case Attribute::Alignment:
    case Attribute::Dereferenceable:
    case Attribute::DereferenceableOrNull:
    case Attribute::Cold:
      return true;
    default:
      return false;
  }
}

/// This function will try to transform the given knowledge into a more
/// canonical one. the canonical knowledge maybe the given one.
RetainedKnowledge canonicalizedKnowledge(RetainedKnowledge RK,
                                         const DataLayout &DL) {
  switch (RK.AttrKind) {
  default:
    return RK;
  case Attribute::NonNull:
    RK.WasOn = getUnderlyingObject(RK.WasOn);
    return RK;
  case Attribute::Alignment: {
    Value *V = RK.WasOn->stripInBoundsOffsets([&](const Value *Strip) {
      if (auto *GEP = dyn_cast<GEPOperator>(Strip))
        RK.ArgValue =
            MinAlign(RK.ArgValue, GEP->getMaxPreservedAlignment(DL).value());
    });
    RK.WasOn = V;
    return RK;
  }
  case Attribute::Dereferenceable:
  case Attribute::DereferenceableOrNull: {
    int64_t Offset = 0;
    Value *V = GetPointerBaseWithConstantOffset(RK.WasOn, Offset, DL,
                                                /*AllowNonInBounds*/ false);
    if (Offset < 0)
      return RK;
    RK.ArgValue = RK.ArgValue + Offset;
    RK.WasOn = V;
  }
  }
  return RK;
}

/// This class contain all knowledge that have been gather while building an
/// llvm.assume and the function to manipulate it.
struct AssumeBuilderState {
  Module *M;

  using MapKey = std::pair<Value *, Attribute::AttrKind>;
  SmallMapVector<MapKey, uint64_t, 8> AssumedKnowledgeMap;
  Instruction *InstBeingModified = nullptr;
  AssumptionCache* AC = nullptr;
  DominatorTree* DT = nullptr;

  AssumeBuilderState(Module *M, Instruction *I = nullptr,
                     AssumptionCache *AC = nullptr, DominatorTree *DT = nullptr)
      : M(M), InstBeingModified(I), AC(AC), DT(DT) {}

  bool tryToPreserveWithoutAddingAssume(RetainedKnowledge RK) {
    if (!InstBeingModified || !RK.WasOn)
      return false;
    bool HasBeenPreserved = false;
    Use* ToUpdate = nullptr;
    getKnowledgeForValue(
        RK.WasOn, {RK.AttrKind}, AC,
        [&](RetainedKnowledge RKOther, Instruction *Assume,
            const CallInst::BundleOpInfo *Bundle) {
          if (!isValidAssumeForContext(Assume, InstBeingModified, DT))
            return false;
          if (RKOther.ArgValue >= RK.ArgValue) {
            HasBeenPreserved = true;
            return true;
          } else if (isValidAssumeForContext(InstBeingModified, Assume, DT)) {
            HasBeenPreserved = true;
            IntrinsicInst *Intr = cast<IntrinsicInst>(Assume);
            ToUpdate = &Intr->op_begin()[Bundle->Begin + ABA_Argument];
            return true;
          }
          return false;
        });
    if (ToUpdate)
      ToUpdate->set(
          ConstantInt::get(Type::getInt64Ty(M->getContext()), RK.ArgValue));
    return HasBeenPreserved;
  }

  bool isKnowledgeWorthPreserving(RetainedKnowledge RK) {
    if (!RK)
      return false;
    if (!RK.WasOn)
      return true;
    if (RK.WasOn->getType()->isPointerTy()) {
      Value *UnderlyingPtr = getUnderlyingObject(RK.WasOn);
      if (isa<AllocaInst>(UnderlyingPtr) || isa<GlobalValue>(UnderlyingPtr))
        return false;
    }
    if (auto *Arg = dyn_cast<Argument>(RK.WasOn)) {
      if (Arg->hasAttribute(RK.AttrKind) &&
          (!Attribute::isIntAttrKind(RK.AttrKind) ||
           Arg->getAttribute(RK.AttrKind).getValueAsInt() >= RK.ArgValue))
        return false;
      return true;
    }
    if (auto *Inst = dyn_cast<Instruction>(RK.WasOn))
      if (wouldInstructionBeTriviallyDead(Inst)) {
        if (RK.WasOn->use_empty())
          return false;
        Use *SingleUse = RK.WasOn->getSingleUndroppableUse();
        if (SingleUse && SingleUse->getUser() == InstBeingModified)
          return false;
      }
    return true;
  }

  void addKnowledge(RetainedKnowledge RK) {
    RK = canonicalizedKnowledge(RK, M->getDataLayout());

    if (!isKnowledgeWorthPreserving(RK))
      return;

    if (tryToPreserveWithoutAddingAssume(RK))
      return;
    MapKey Key{RK.WasOn, RK.AttrKind};
    auto Lookup = AssumedKnowledgeMap.find(Key);
    if (Lookup == AssumedKnowledgeMap.end()) {
      AssumedKnowledgeMap[Key] = RK.ArgValue;
      return;
    }
    assert(((Lookup->second == 0 && RK.ArgValue == 0) ||
            (Lookup->second != 0 && RK.ArgValue != 0)) &&
           "inconsistent argument value");

    /// This is only desirable because for all attributes taking an argument
    /// higher is better.
    Lookup->second = std::max(Lookup->second, RK.ArgValue);
  }

  void addAttribute(Attribute Attr, Value *WasOn) {
    if (Attr.isTypeAttribute() || Attr.isStringAttribute() ||
        (!ShouldPreserveAllAttributes &&
         !isUsefullToPreserve(Attr.getKindAsEnum())))
      return;
    uint64_t AttrArg = 0;
    if (Attr.isIntAttribute())
      AttrArg = Attr.getValueAsInt();
    addKnowledge({Attr.getKindAsEnum(), AttrArg, WasOn});
  }

  void addCall(const CallBase *Call) {
    auto addAttrList = [&](AttributeList AttrList, unsigned NumArgs) {
      for (unsigned Idx = 0; Idx < NumArgs; Idx++)
        for (Attribute Attr : AttrList.getParamAttrs(Idx)) {
          bool IsPoisonAttr = Attr.hasAttribute(Attribute::NonNull) ||
                              Attr.hasAttribute(Attribute::Alignment);
          if (!IsPoisonAttr || Call->isPassingUndefUB(Idx))
            addAttribute(Attr, Call->getArgOperand(Idx));
        }
      for (Attribute Attr : AttrList.getFnAttrs())
        addAttribute(Attr, nullptr);
    };
    addAttrList(Call->getAttributes(), Call->arg_size());
    if (Function *Fn = Call->getCalledFunction())
      addAttrList(Fn->getAttributes(), Fn->arg_size());
  }

  AssumeInst *build() {
    if (AssumedKnowledgeMap.empty())
      return nullptr;
    if (!DebugCounter::shouldExecute(BuildAssumeCounter))
      return nullptr;
    Function *FnAssume = Intrinsic::getDeclaration(M, Intrinsic::assume);
    LLVMContext &C = M->getContext();
    SmallVector<OperandBundleDef, 8> OpBundle;
    for (auto &MapElem : AssumedKnowledgeMap) {
      SmallVector<Value *, 2> Args;
      if (MapElem.first.first)
        Args.push_back(MapElem.first.first);

      /// This is only valid because for all attribute that currently exist a
      /// value of 0 is useless. and should not be preserved.
      if (MapElem.second)
        Args.push_back(ConstantInt::get(Type::getInt64Ty(M->getContext()),
                                        MapElem.second));
      OpBundle.push_back(OperandBundleDefT<Value *>(
          std::string(Attribute::getNameFromAttrKind(MapElem.first.second)),
          Args));
      NumBundlesInAssumes++;
    }
    NumAssumeBuilt++;
    return cast<AssumeInst>(CallInst::Create(
        FnAssume, ArrayRef<Value *>({ConstantInt::getTrue(C)}), OpBundle));
  }

  void addAccessedPtr(Instruction *MemInst, Value *Pointer, Type *AccType,
                      MaybeAlign MA) {
    unsigned DerefSize = MemInst->getModule()
                             ->getDataLayout()
                             .getTypeStoreSize(AccType)
                             .getKnownMinValue();
    if (DerefSize != 0) {
      addKnowledge({Attribute::Dereferenceable, DerefSize, Pointer});
      if (!NullPointerIsDefined(MemInst->getFunction(),
                                Pointer->getType()->getPointerAddressSpace()))
        addKnowledge({Attribute::NonNull, 0u, Pointer});
    }
    if (MA.valueOrOne() > 1)
      addKnowledge({Attribute::Alignment, MA.valueOrOne().value(), Pointer});
  }

  void addInstruction(Instruction *I) {
    if (auto *Call = dyn_cast<CallBase>(I))
      return addCall(Call);
    if (auto *Load = dyn_cast<LoadInst>(I))
      return addAccessedPtr(I, Load->getPointerOperand(), Load->getType(),
                            Load->getAlign());
    if (auto *Store = dyn_cast<StoreInst>(I))
      return addAccessedPtr(I, Store->getPointerOperand(),
                            Store->getValueOperand()->getType(),
                            Store->getAlign());
    // TODO: Add support for the other Instructions.
    // TODO: Maybe we should look around and merge with other llvm.assume.
  }
};

} // namespace

AssumeInst *llvm::buildAssumeFromInst(Instruction *I) {
  if (!EnableKnowledgeRetention)
    return nullptr;
  AssumeBuilderState Builder(I->getModule());
  Builder.addInstruction(I);
  return Builder.build();
}

bool llvm::salvageKnowledge(Instruction *I, AssumptionCache *AC,
                            DominatorTree *DT) {
  if (!EnableKnowledgeRetention || I->isTerminator())
    return false;
  bool Changed = false;
  AssumeBuilderState Builder(I->getModule(), I, AC, DT);
  Builder.addInstruction(I);
  if (auto *Intr = Builder.build()) {
    Intr->insertBefore(I);
    Changed = true;
    if (AC)
      AC->registerAssumption(Intr);
  }
  return Changed;
}

AssumeInst *
llvm::buildAssumeFromKnowledge(ArrayRef<RetainedKnowledge> Knowledge,
                               Instruction *CtxI, AssumptionCache *AC,
                               DominatorTree *DT) {
  AssumeBuilderState Builder(CtxI->getModule(), CtxI, AC, DT);
  for (const RetainedKnowledge &RK : Knowledge)
    Builder.addKnowledge(RK);
  return Builder.build();
}

RetainedKnowledge llvm::simplifyRetainedKnowledge(AssumeInst *Assume,
                                                  RetainedKnowledge RK,
                                                  AssumptionCache *AC,
                                                  DominatorTree *DT) {
  AssumeBuilderState Builder(Assume->getModule(), Assume, AC, DT);
  RK = canonicalizedKnowledge(RK, Assume->getDataLayout());

  if (!Builder.isKnowledgeWorthPreserving(RK))
    return RetainedKnowledge::none();

  if (Builder.tryToPreserveWithoutAddingAssume(RK))
    return RetainedKnowledge::none();
  return RK;
}

namespace {

struct AssumeSimplify {
  Function &F;
  AssumptionCache &AC;
  DominatorTree *DT;
  LLVMContext &C;
  SmallDenseSet<IntrinsicInst *> CleanupToDo;
  StringMapEntry<uint32_t> *IgnoreTag;
  SmallDenseMap<BasicBlock *, SmallVector<IntrinsicInst *, 4>, 8> BBToAssume;
  bool MadeChange = false;

  AssumeSimplify(Function &F, AssumptionCache &AC, DominatorTree *DT,
                 LLVMContext &C)
      : F(F), AC(AC), DT(DT), C(C),
        IgnoreTag(C.getOrInsertBundleTag(IgnoreBundleTag)) {}

  void buildMapping(bool FilterBooleanArgument) {
    BBToAssume.clear();
    for (Value *V : AC.assumptions()) {
      if (!V)
        continue;
      IntrinsicInst *Assume = cast<IntrinsicInst>(V);
      if (FilterBooleanArgument) {
        auto *Arg = dyn_cast<ConstantInt>(Assume->getOperand(0));
        if (!Arg || Arg->isZero())
          continue;
      }
      BBToAssume[Assume->getParent()].push_back(Assume);
    }

    for (auto &Elem : BBToAssume) {
      llvm::sort(Elem.second,
                 [](const IntrinsicInst *LHS, const IntrinsicInst *RHS) {
                   return LHS->comesBefore(RHS);
                 });
    }
  }

  /// Remove all asumes in CleanupToDo if there boolean argument is true and
  /// ForceCleanup is set or the assume doesn't hold valuable knowledge.
  void RunCleanup(bool ForceCleanup) {
    for (IntrinsicInst *Assume : CleanupToDo) {
      auto *Arg = dyn_cast<ConstantInt>(Assume->getOperand(0));
      if (!Arg || Arg->isZero() ||
          (!ForceCleanup &&
           !isAssumeWithEmptyBundle(cast<AssumeInst>(*Assume))))
        continue;
      MadeChange = true;
      if (ForceCleanup)
        NumAssumesMerged++;
      else
        NumAssumesRemoved++;
      Assume->eraseFromParent();
    }
    CleanupToDo.clear();
  }

  /// Remove knowledge stored in assume when it is already know by an attribute
  /// or an other assume. This can when valid update an existing knowledge in an
  /// attribute or an other assume.
  void dropRedundantKnowledge() {
    struct MapValue {
      IntrinsicInst *Assume;
      uint64_t ArgValue;
      CallInst::BundleOpInfo *BOI;
    };
    buildMapping(false);
    SmallDenseMap<std::pair<Value *, Attribute::AttrKind>,
                  SmallVector<MapValue, 2>, 16>
        Knowledge;
    for (BasicBlock *BB : depth_first(&F))
      for (Value *V : BBToAssume[BB]) {
        if (!V)
          continue;
        IntrinsicInst *Assume = cast<IntrinsicInst>(V);
        for (CallInst::BundleOpInfo &BOI : Assume->bundle_op_infos()) {
          auto RemoveFromAssume = [&]() {
            CleanupToDo.insert(Assume);
            if (BOI.Begin != BOI.End) {
              Use *U = &Assume->op_begin()[BOI.Begin + ABA_WasOn];
              U->set(UndefValue::get(U->get()->getType()));
            }
            BOI.Tag = IgnoreTag;
          };
          if (BOI.Tag == IgnoreTag) {
            CleanupToDo.insert(Assume);
            continue;
          }
          RetainedKnowledge RK =
            getKnowledgeFromBundle(cast<AssumeInst>(*Assume), BOI);
          if (auto *Arg = dyn_cast_or_null<Argument>(RK.WasOn)) {
            bool HasSameKindAttr = Arg->hasAttribute(RK.AttrKind);
            if (HasSameKindAttr)
              if (!Attribute::isIntAttrKind(RK.AttrKind) ||
                  Arg->getAttribute(RK.AttrKind).getValueAsInt() >=
                      RK.ArgValue) {
                RemoveFromAssume();
                continue;
              }
            if (isValidAssumeForContext(
                    Assume, &*F.getEntryBlock().getFirstInsertionPt()) ||
                Assume == &*F.getEntryBlock().getFirstInsertionPt()) {
              if (HasSameKindAttr)
                Arg->removeAttr(RK.AttrKind);
              Arg->addAttr(Attribute::get(C, RK.AttrKind, RK.ArgValue));
              MadeChange = true;
              RemoveFromAssume();
              continue;
            }
          }
          auto &Lookup = Knowledge[{RK.WasOn, RK.AttrKind}];
          for (MapValue &Elem : Lookup) {
            if (!isValidAssumeForContext(Elem.Assume, Assume, DT))
              continue;
            if (Elem.ArgValue >= RK.ArgValue) {
              RemoveFromAssume();
              continue;
            } else if (isValidAssumeForContext(Assume, Elem.Assume, DT)) {
              Elem.Assume->op_begin()[Elem.BOI->Begin + ABA_Argument].set(
                  ConstantInt::get(Type::getInt64Ty(C), RK.ArgValue));
              MadeChange = true;
              RemoveFromAssume();
              continue;
            }
          }
          Lookup.push_back({Assume, RK.ArgValue, &BOI});
        }
      }
  }

  using MergeIterator = SmallVectorImpl<IntrinsicInst *>::iterator;

  /// Merge all Assumes from Begin to End in and insert the resulting assume as
  /// high as possible in the basicblock.
  void mergeRange(BasicBlock *BB, MergeIterator Begin, MergeIterator End) {
    if (Begin == End || std::next(Begin) == End)
      return;
    /// Provide no additional information so that AssumeBuilderState doesn't
    /// try to do any punning since it already has been done better.
    AssumeBuilderState Builder(F.getParent());

    /// For now it is initialized to the best value it could have
    Instruction *InsertPt = BB->getFirstNonPHI();
    if (isa<LandingPadInst>(InsertPt))
      InsertPt = InsertPt->getNextNode();
    for (IntrinsicInst *I : make_range(Begin, End)) {
      CleanupToDo.insert(I);
      for (CallInst::BundleOpInfo &BOI : I->bundle_op_infos()) {
        RetainedKnowledge RK =
          getKnowledgeFromBundle(cast<AssumeInst>(*I), BOI);
        if (!RK)
          continue;
        Builder.addKnowledge(RK);
        if (auto *I = dyn_cast_or_null<Instruction>(RK.WasOn))
          if (I->getParent() == InsertPt->getParent() &&
              (InsertPt->comesBefore(I) || InsertPt == I))
            InsertPt = I->getNextNode();
      }
    }

    /// Adjust InsertPt if it is before Begin, since mergeAssumes only
    /// guarantees we can place the resulting assume between Begin and End.
    if (InsertPt->comesBefore(*Begin))
      for (auto It = (*Begin)->getIterator(), E = InsertPt->getIterator();
           It != E; --It)
        if (!isGuaranteedToTransferExecutionToSuccessor(&*It)) {
          InsertPt = It->getNextNode();
          break;
        }
    auto *MergedAssume = Builder.build();
    if (!MergedAssume)
      return;
    MadeChange = true;
    MergedAssume->insertBefore(InsertPt);
    AC.registerAssumption(MergedAssume);
  }

  /// Merge assume when they are in the same BasicBlock and for all instruction
  /// between them isGuaranteedToTransferExecutionToSuccessor returns true.
  void mergeAssumes() {
    buildMapping(true);

    SmallVector<MergeIterator, 4> SplitPoints;
    for (auto &Elem : BBToAssume) {
      SmallVectorImpl<IntrinsicInst *> &AssumesInBB = Elem.second;
      if (AssumesInBB.size() < 2)
        continue;
      /// AssumesInBB is already sorted by order in the block.

      BasicBlock::iterator It = AssumesInBB.front()->getIterator();
      BasicBlock::iterator E = AssumesInBB.back()->getIterator();
      SplitPoints.push_back(AssumesInBB.begin());
      MergeIterator LastSplit = AssumesInBB.begin();
      for (; It != E; ++It)
        if (!isGuaranteedToTransferExecutionToSuccessor(&*It)) {
          for (; (*LastSplit)->comesBefore(&*It); ++LastSplit)
            ;
          if (SplitPoints.back() != LastSplit)
            SplitPoints.push_back(LastSplit);
        }
      SplitPoints.push_back(AssumesInBB.end());
      for (auto SplitIt = SplitPoints.begin();
           SplitIt != std::prev(SplitPoints.end()); SplitIt++) {
        mergeRange(Elem.first, *SplitIt, *(SplitIt + 1));
      }
      SplitPoints.clear();
    }
  }
};

bool simplifyAssumes(Function &F, AssumptionCache *AC, DominatorTree *DT) {
  AssumeSimplify AS(F, *AC, DT, F.getContext());

  /// Remove knowledge that is already known by a dominating other assume or an
  /// attribute.
  AS.dropRedundantKnowledge();

  /// Remove assume that are empty.
  AS.RunCleanup(false);

  /// Merge assume in the same basicblock when possible.
  AS.mergeAssumes();

  /// Remove assume that were merged.
  AS.RunCleanup(true);
  return AS.MadeChange;
}

} // namespace

PreservedAnalyses AssumeSimplifyPass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  if (!EnableKnowledgeRetention)
    return PreservedAnalyses::all();
  if (!simplifyAssumes(F, &AM.getResult<AssumptionAnalysis>(F),
                       AM.getCachedResult<DominatorTreeAnalysis>(F)))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

PreservedAnalyses AssumeBuilderPass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  AssumptionCache *AC = &AM.getResult<AssumptionAnalysis>(F);
  DominatorTree* DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  bool Changed = false;
  for (Instruction &I : instructions(F))
    Changed |= salvageKnowledge(&I, AC, DT);
  if (!Changed)
    PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
