//===- ExprEngine.cpp - Path-Sensitive Expression-Level Dataflow ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a meta-engine for path-sensitive dataflow analysis that
//  is built on CoreEngine, but provides the boilerplate to execute transfer
//  functions and build the ExplodedGraph at the expression level.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "PrettyStackTraceLocationContext.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/ConstructionContext.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/JsonSupport.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CoreEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopUnrolling.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopWidening.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;

#define DEBUG_TYPE "ExprEngine"

STATISTIC(NumRemoveDeadBindings,
            "The # of times RemoveDeadBindings is called");
STATISTIC(NumMaxBlockCountReached,
            "The # of aborted paths due to reaching the maximum block count in "
            "a top level function");
STATISTIC(NumMaxBlockCountReachedInInlined,
            "The # of aborted paths due to reaching the maximum block count in "
            "an inlined function");
STATISTIC(NumTimesRetriedWithoutInlining,
            "The # of times we re-evaluated a call without inlining");

//===----------------------------------------------------------------------===//
// Internal program state traits.
//===----------------------------------------------------------------------===//

namespace {

// When modeling a C++ constructor, for a variety of reasons we need to track
// the location of the object for the duration of its ConstructionContext.
// ObjectsUnderConstruction maps statements within the construction context
// to the object's location, so that on every such statement the location
// could have been retrieved.

/// ConstructedObjectKey is used for being able to find the path-sensitive
/// memory region of a freshly constructed object while modeling the AST node
/// that syntactically represents the object that is being constructed.
/// Semantics of such nodes may sometimes require access to the region that's
/// not otherwise present in the program state, or to the very fact that
/// the construction context was present and contained references to these
/// AST nodes.
class ConstructedObjectKey {
  using ConstructedObjectKeyImpl =
      std::pair<ConstructionContextItem, const LocationContext *>;
  const ConstructedObjectKeyImpl Impl;

public:
  explicit ConstructedObjectKey(const ConstructionContextItem &Item,
                       const LocationContext *LC)
      : Impl(Item, LC) {}

  const ConstructionContextItem &getItem() const { return Impl.first; }
  const LocationContext *getLocationContext() const { return Impl.second; }

  ASTContext &getASTContext() const {
    return getLocationContext()->getDecl()->getASTContext();
  }

  void printJson(llvm::raw_ostream &Out, PrinterHelper *Helper,
                 PrintingPolicy &PP) const {
    const Stmt *S = getItem().getStmtOrNull();
    const CXXCtorInitializer *I = nullptr;
    if (!S)
      I = getItem().getCXXCtorInitializer();

    if (S)
      Out << "\"stmt_id\": " << S->getID(getASTContext());
    else
      Out << "\"init_id\": " << I->getID(getASTContext());

    // Kind
    Out << ", \"kind\": \"" << getItem().getKindAsString()
        << "\", \"argument_index\": ";

    if (getItem().getKind() == ConstructionContextItem::ArgumentKind)
      Out << getItem().getIndex();
    else
      Out << "null";

    // Pretty-print
    Out << ", \"pretty\": ";

    if (S) {
      S->printJson(Out, Helper, PP, /*AddQuotes=*/true);
    } else {
      Out << '\"' << I->getAnyMember()->getDeclName() << '\"';
    }
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.Add(Impl.first);
    ID.AddPointer(Impl.second);
  }

  bool operator==(const ConstructedObjectKey &RHS) const {
    return Impl == RHS.Impl;
  }

  bool operator<(const ConstructedObjectKey &RHS) const {
    return Impl < RHS.Impl;
  }
};
} // namespace

typedef llvm::ImmutableMap<ConstructedObjectKey, SVal>
    ObjectsUnderConstructionMap;
REGISTER_TRAIT_WITH_PROGRAMSTATE(ObjectsUnderConstruction,
                                 ObjectsUnderConstructionMap)

// This trait is responsible for storing the index of the element that is to be
// constructed in the next iteration. As a result a CXXConstructExpr is only
// stored if it is array type. Also the index is the index of the continuous
// memory region, which is important for multi-dimensional arrays. E.g:: int
// arr[2][2]; assume arr[1][1] will be the next element under construction, so
// the index is 3.
typedef llvm::ImmutableMap<
    std::pair<const CXXConstructExpr *, const LocationContext *>, unsigned>
    IndexOfElementToConstructMap;
REGISTER_TRAIT_WITH_PROGRAMSTATE(IndexOfElementToConstruct,
                                 IndexOfElementToConstructMap)

// This trait is responsible for holding our pending ArrayInitLoopExprs.
// It pairs the LocationContext and the initializer CXXConstructExpr with
// the size of the array that's being copy initialized.
typedef llvm::ImmutableMap<
    std::pair<const CXXConstructExpr *, const LocationContext *>, unsigned>
    PendingInitLoopMap;
REGISTER_TRAIT_WITH_PROGRAMSTATE(PendingInitLoop, PendingInitLoopMap)

typedef llvm::ImmutableMap<const LocationContext *, unsigned>
    PendingArrayDestructionMap;
REGISTER_TRAIT_WITH_PROGRAMSTATE(PendingArrayDestruction,
                                 PendingArrayDestructionMap)

//===----------------------------------------------------------------------===//
// Engine construction and deletion.
//===----------------------------------------------------------------------===//

static const char* TagProviderName = "ExprEngine";

ExprEngine::ExprEngine(cross_tu::CrossTranslationUnitContext &CTU,
                       AnalysisManager &mgr, SetOfConstDecls *VisitedCalleesIn,
                       FunctionSummariesTy *FS, InliningModes HowToInlineIn)
    : CTU(CTU), IsCTUEnabled(mgr.getAnalyzerOptions().IsNaiveCTUEnabled),
      AMgr(mgr), AnalysisDeclContexts(mgr.getAnalysisDeclContextManager()),
      Engine(*this, FS, mgr.getAnalyzerOptions()), G(Engine.getGraph()),
      StateMgr(getContext(), mgr.getStoreManagerCreator(),
               mgr.getConstraintManagerCreator(), G.getAllocator(), this),
      SymMgr(StateMgr.getSymbolManager()), MRMgr(StateMgr.getRegionManager()),
      svalBuilder(StateMgr.getSValBuilder()), ObjCNoRet(mgr.getASTContext()),
      BR(mgr, *this), VisitedCallees(VisitedCalleesIn),
      HowToInline(HowToInlineIn) {
  unsigned TrimInterval = mgr.options.GraphTrimInterval;
  if (TrimInterval != 0) {
    // Enable eager node reclamation when constructing the ExplodedGraph.
    G.enableNodeReclamation(TrimInterval);
  }
}

//===----------------------------------------------------------------------===//
// Utility methods.
//===----------------------------------------------------------------------===//

ProgramStateRef ExprEngine::getInitialState(const LocationContext *InitLoc) {
  ProgramStateRef state = StateMgr.getInitialState(InitLoc);
  const Decl *D = InitLoc->getDecl();

  // Preconditions.
  // FIXME: It would be nice if we had a more general mechanism to add
  // such preconditions.  Some day.
  do {
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Precondition: the first argument of 'main' is an integer guaranteed
      //  to be > 0.
      const IdentifierInfo *II = FD->getIdentifier();
      if (!II || !(II->getName() == "main" && FD->getNumParams() > 0))
        break;

      const ParmVarDecl *PD = FD->getParamDecl(0);
      QualType T = PD->getType();
      const auto *BT = dyn_cast<BuiltinType>(T);
      if (!BT || !BT->isInteger())
        break;

      const MemRegion *R = state->getRegion(PD, InitLoc);
      if (!R)
        break;

      SVal V = state->getSVal(loc::MemRegionVal(R));
      SVal Constraint_untested = evalBinOp(state, BO_GT, V,
                                           svalBuilder.makeZeroVal(T),
                                           svalBuilder.getConditionType());

      std::optional<DefinedOrUnknownSVal> Constraint =
          Constraint_untested.getAs<DefinedOrUnknownSVal>();

      if (!Constraint)
        break;

      if (ProgramStateRef newState = state->assume(*Constraint, true))
        state = newState;
    }
    break;
  }
  while (false);

  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    // Precondition: 'self' is always non-null upon entry to an Objective-C
    // method.
    const ImplicitParamDecl *SelfD = MD->getSelfDecl();
    const MemRegion *R = state->getRegion(SelfD, InitLoc);
    SVal V = state->getSVal(loc::MemRegionVal(R));

    if (std::optional<Loc> LV = V.getAs<Loc>()) {
      // Assume that the pointer value in 'self' is non-null.
      state = state->assume(*LV, true);
      assert(state && "'self' cannot be null");
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->isImplicitObjectMemberFunction()) {
      // Precondition: 'this' is always non-null upon entry to the
      // top-level function.  This is our starting assumption for
      // analyzing an "open" program.
      const StackFrameContext *SFC = InitLoc->getStackFrame();
      if (SFC->getParent() == nullptr) {
        loc::MemRegionVal L = svalBuilder.getCXXThis(MD, SFC);
        SVal V = state->getSVal(L);
        if (std::optional<Loc> LV = V.getAs<Loc>()) {
          state = state->assume(*LV, true);
          assert(state && "'this' cannot be null");
        }
      }
    }
  }

  return state;
}

ProgramStateRef ExprEngine::createTemporaryRegionIfNeeded(
    ProgramStateRef State, const LocationContext *LC,
    const Expr *InitWithAdjustments, const Expr *Result,
    const SubRegion **OutRegionWithAdjustments) {
  // FIXME: This function is a hack that works around the quirky AST
  // we're often having with respect to C++ temporaries. If only we modelled
  // the actual execution order of statements properly in the CFG,
  // all the hassle with adjustments would not be necessary,
  // and perhaps the whole function would be removed.
  SVal InitValWithAdjustments = State->getSVal(InitWithAdjustments, LC);
  if (!Result) {
    // If we don't have an explicit result expression, we're in "if needed"
    // mode. Only create a region if the current value is a NonLoc.
    if (!isa<NonLoc>(InitValWithAdjustments)) {
      if (OutRegionWithAdjustments)
        *OutRegionWithAdjustments = nullptr;
      return State;
    }
    Result = InitWithAdjustments;
  } else {
    // We need to create a region no matter what. Make sure we don't try to
    // stuff a Loc into a non-pointer temporary region.
    assert(!isa<Loc>(InitValWithAdjustments) ||
           Loc::isLocType(Result->getType()) ||
           Result->getType()->isMemberPointerType());
  }

  ProgramStateManager &StateMgr = State->getStateManager();
  MemRegionManager &MRMgr = StateMgr.getRegionManager();
  StoreManager &StoreMgr = StateMgr.getStoreManager();

  // MaterializeTemporaryExpr may appear out of place, after a few field and
  // base-class accesses have been made to the object, even though semantically
  // it is the whole object that gets materialized and lifetime-extended.
  //
  // For example:
  //
  //   `-MaterializeTemporaryExpr
  //     `-MemberExpr
  //       `-CXXTemporaryObjectExpr
  //
  // instead of the more natural
  //
  //   `-MemberExpr
  //     `-MaterializeTemporaryExpr
  //       `-CXXTemporaryObjectExpr
  //
  // Use the usual methods for obtaining the expression of the base object,
  // and record the adjustments that we need to make to obtain the sub-object
  // that the whole expression 'Ex' refers to. This trick is usual,
  // in the sense that CodeGen takes a similar route.

  SmallVector<const Expr *, 2> CommaLHSs;
  SmallVector<SubobjectAdjustment, 2> Adjustments;

  const Expr *Init = InitWithAdjustments->skipRValueSubobjectAdjustments(
      CommaLHSs, Adjustments);

  // Take the region for Init, i.e. for the whole object. If we do not remember
  // the region in which the object originally was constructed, come up with
  // a new temporary region out of thin air and copy the contents of the object
  // (which are currently present in the Environment, because Init is an rvalue)
  // into that region. This is not correct, but it is better than nothing.
  const TypedValueRegion *TR = nullptr;
  if (const auto *MT = dyn_cast<MaterializeTemporaryExpr>(Result)) {
    if (std::optional<SVal> V = getObjectUnderConstruction(State, MT, LC)) {
      State = finishObjectConstruction(State, MT, LC);
      State = State->BindExpr(Result, LC, *V);
      return State;
    } else if (const ValueDecl *VD = MT->getExtendingDecl()) {
      StorageDuration SD = MT->getStorageDuration();
      assert(SD != SD_FullExpression);
      // If this object is bound to a reference with static storage duration, we
      // put it in a different region to prevent "address leakage" warnings.
      if (SD == SD_Static || SD == SD_Thread) {
        TR = MRMgr.getCXXStaticLifetimeExtendedObjectRegion(Init, VD);
      } else {
        TR = MRMgr.getCXXLifetimeExtendedObjectRegion(Init, VD, LC);
      }
    } else {
      assert(MT->getStorageDuration() == SD_FullExpression);
      TR = MRMgr.getCXXTempObjectRegion(Init, LC);
    }
  } else {
    TR = MRMgr.getCXXTempObjectRegion(Init, LC);
  }

  SVal Reg = loc::MemRegionVal(TR);
  SVal BaseReg = Reg;

  // Make the necessary adjustments to obtain the sub-object.
  for (const SubobjectAdjustment &Adj : llvm::reverse(Adjustments)) {
    switch (Adj.Kind) {
    case SubobjectAdjustment::DerivedToBaseAdjustment:
      Reg = StoreMgr.evalDerivedToBase(Reg, Adj.DerivedToBase.BasePath);
      break;
    case SubobjectAdjustment::FieldAdjustment:
      Reg = StoreMgr.getLValueField(Adj.Field, Reg);
      break;
    case SubobjectAdjustment::MemberPointerAdjustment:
      // FIXME: Unimplemented.
      State = State->invalidateRegions(Reg, InitWithAdjustments,
                                       currBldrCtx->blockCount(), LC, true,
                                       nullptr, nullptr, nullptr);
      return State;
    }
  }

  // What remains is to copy the value of the object to the new region.
  // FIXME: In other words, what we should always do is copy value of the
  // Init expression (which corresponds to the bigger object) to the whole
  // temporary region TR. However, this value is often no longer present
  // in the Environment. If it has disappeared, we instead invalidate TR.
  // Still, what we can do is assign the value of expression Ex (which
  // corresponds to the sub-object) to the TR's sub-region Reg. At least,
  // values inside Reg would be correct.
  SVal InitVal = State->getSVal(Init, LC);
  if (InitVal.isUnknown()) {
    InitVal = getSValBuilder().conjureSymbolVal(Result, LC, Init->getType(),
                                                currBldrCtx->blockCount());
    State = State->bindLoc(BaseReg.castAs<Loc>(), InitVal, LC, false);

    // Then we'd need to take the value that certainly exists and bind it
    // over.
    if (InitValWithAdjustments.isUnknown()) {
      // Try to recover some path sensitivity in case we couldn't
      // compute the value.
      InitValWithAdjustments = getSValBuilder().conjureSymbolVal(
          Result, LC, InitWithAdjustments->getType(),
          currBldrCtx->blockCount());
    }
    State =
        State->bindLoc(Reg.castAs<Loc>(), InitValWithAdjustments, LC, false);
  } else {
    State = State->bindLoc(BaseReg.castAs<Loc>(), InitVal, LC, false);
  }

  // The result expression would now point to the correct sub-region of the
  // newly created temporary region. Do this last in order to getSVal of Init
  // correctly in case (Result == Init).
  if (Result->isGLValue()) {
    State = State->BindExpr(Result, LC, Reg);
  } else {
    State = State->BindExpr(Result, LC, InitValWithAdjustments);
  }

  // Notify checkers once for two bindLoc()s.
  State = processRegionChange(State, TR, LC);

  if (OutRegionWithAdjustments)
    *OutRegionWithAdjustments = cast<SubRegion>(Reg.getAsRegion());
  return State;
}

ProgramStateRef ExprEngine::setIndexOfElementToConstruct(
    ProgramStateRef State, const CXXConstructExpr *E,
    const LocationContext *LCtx, unsigned Idx) {
  auto Key = std::make_pair(E, LCtx->getStackFrame());

  assert(!State->contains<IndexOfElementToConstruct>(Key) || Idx > 0);

  return State->set<IndexOfElementToConstruct>(Key, Idx);
}

std::optional<unsigned>
ExprEngine::getPendingInitLoop(ProgramStateRef State, const CXXConstructExpr *E,
                               const LocationContext *LCtx) {
  const unsigned *V = State->get<PendingInitLoop>({E, LCtx->getStackFrame()});
  return V ? std::make_optional(*V) : std::nullopt;
}

ProgramStateRef ExprEngine::removePendingInitLoop(ProgramStateRef State,
                                                  const CXXConstructExpr *E,
                                                  const LocationContext *LCtx) {
  auto Key = std::make_pair(E, LCtx->getStackFrame());

  assert(E && State->contains<PendingInitLoop>(Key));
  return State->remove<PendingInitLoop>(Key);
}

ProgramStateRef ExprEngine::setPendingInitLoop(ProgramStateRef State,
                                               const CXXConstructExpr *E,
                                               const LocationContext *LCtx,
                                               unsigned Size) {
  auto Key = std::make_pair(E, LCtx->getStackFrame());

  assert(!State->contains<PendingInitLoop>(Key) && Size > 0);

  return State->set<PendingInitLoop>(Key, Size);
}

std::optional<unsigned>
ExprEngine::getIndexOfElementToConstruct(ProgramStateRef State,
                                         const CXXConstructExpr *E,
                                         const LocationContext *LCtx) {
  const unsigned *V =
      State->get<IndexOfElementToConstruct>({E, LCtx->getStackFrame()});
  return V ? std::make_optional(*V) : std::nullopt;
}

ProgramStateRef
ExprEngine::removeIndexOfElementToConstruct(ProgramStateRef State,
                                            const CXXConstructExpr *E,
                                            const LocationContext *LCtx) {
  auto Key = std::make_pair(E, LCtx->getStackFrame());

  assert(E && State->contains<IndexOfElementToConstruct>(Key));
  return State->remove<IndexOfElementToConstruct>(Key);
}

std::optional<unsigned>
ExprEngine::getPendingArrayDestruction(ProgramStateRef State,
                                       const LocationContext *LCtx) {
  assert(LCtx && "LocationContext shouldn't be null!");

  const unsigned *V =
      State->get<PendingArrayDestruction>(LCtx->getStackFrame());
  return V ? std::make_optional(*V) : std::nullopt;
}

ProgramStateRef ExprEngine::setPendingArrayDestruction(
    ProgramStateRef State, const LocationContext *LCtx, unsigned Idx) {
  assert(LCtx && "LocationContext shouldn't be null!");

  auto Key = LCtx->getStackFrame();

  return State->set<PendingArrayDestruction>(Key, Idx);
}

ProgramStateRef
ExprEngine::removePendingArrayDestruction(ProgramStateRef State,
                                          const LocationContext *LCtx) {
  assert(LCtx && "LocationContext shouldn't be null!");

  auto Key = LCtx->getStackFrame();

  assert(LCtx && State->contains<PendingArrayDestruction>(Key));
  return State->remove<PendingArrayDestruction>(Key);
}

ProgramStateRef
ExprEngine::addObjectUnderConstruction(ProgramStateRef State,
                                       const ConstructionContextItem &Item,
                                       const LocationContext *LC, SVal V) {
  ConstructedObjectKey Key(Item, LC->getStackFrame());

  const Expr *Init = nullptr;

  if (auto DS = dyn_cast_or_null<DeclStmt>(Item.getStmtOrNull())) {
    if (auto VD = dyn_cast_or_null<VarDecl>(DS->getSingleDecl()))
      Init = VD->getInit();
  }

  if (auto LE = dyn_cast_or_null<LambdaExpr>(Item.getStmtOrNull()))
    Init = *(LE->capture_init_begin() + Item.getIndex());

  if (!Init && !Item.getStmtOrNull())
    Init = Item.getCXXCtorInitializer()->getInit();

  // In an ArrayInitLoopExpr the real initializer is returned by
  // getSubExpr(). Note that AILEs can be nested in case of
  // multidimesnional arrays.
  if (const auto *AILE = dyn_cast_or_null<ArrayInitLoopExpr>(Init))
    Init = extractElementInitializerFromNestedAILE(AILE);

  // FIXME: Currently the state might already contain the marker due to
  // incorrect handling of temporaries bound to default parameters.
  // The state will already contain the marker if we construct elements
  // in an array, as we visit the same statement multiple times before
  // the array declaration. The marker is removed when we exit the
  // constructor call.
  assert((!State->get<ObjectsUnderConstruction>(Key) ||
          Key.getItem().getKind() ==
              ConstructionContextItem::TemporaryDestructorKind ||
          State->contains<IndexOfElementToConstruct>(
              {dyn_cast_or_null<CXXConstructExpr>(Init), LC})) &&
         "The object is already marked as `UnderConstruction`, when it's not "
         "supposed to!");
  return State->set<ObjectsUnderConstruction>(Key, V);
}

std::optional<SVal>
ExprEngine::getObjectUnderConstruction(ProgramStateRef State,
                                       const ConstructionContextItem &Item,
                                       const LocationContext *LC) {
  ConstructedObjectKey Key(Item, LC->getStackFrame());
  const SVal *V = State->get<ObjectsUnderConstruction>(Key);
  return V ? std::make_optional(*V) : std::nullopt;
}

ProgramStateRef
ExprEngine::finishObjectConstruction(ProgramStateRef State,
                                     const ConstructionContextItem &Item,
                                     const LocationContext *LC) {
  ConstructedObjectKey Key(Item, LC->getStackFrame());
  assert(State->contains<ObjectsUnderConstruction>(Key));
  return State->remove<ObjectsUnderConstruction>(Key);
}

ProgramStateRef ExprEngine::elideDestructor(ProgramStateRef State,
                                            const CXXBindTemporaryExpr *BTE,
                                            const LocationContext *LC) {
  ConstructedObjectKey Key({BTE, /*IsElided=*/true}, LC);
  // FIXME: Currently the state might already contain the marker due to
  // incorrect handling of temporaries bound to default parameters.
  return State->set<ObjectsUnderConstruction>(Key, UnknownVal());
}

ProgramStateRef
ExprEngine::cleanupElidedDestructor(ProgramStateRef State,
                                    const CXXBindTemporaryExpr *BTE,
                                    const LocationContext *LC) {
  ConstructedObjectKey Key({BTE, /*IsElided=*/true}, LC);
  assert(State->contains<ObjectsUnderConstruction>(Key));
  return State->remove<ObjectsUnderConstruction>(Key);
}

bool ExprEngine::isDestructorElided(ProgramStateRef State,
                                    const CXXBindTemporaryExpr *BTE,
                                    const LocationContext *LC) {
  ConstructedObjectKey Key({BTE, /*IsElided=*/true}, LC);
  return State->contains<ObjectsUnderConstruction>(Key);
}

bool ExprEngine::areAllObjectsFullyConstructed(ProgramStateRef State,
                                               const LocationContext *FromLC,
                                               const LocationContext *ToLC) {
  const LocationContext *LC = FromLC;
  while (LC != ToLC) {
    assert(LC && "ToLC must be a parent of FromLC!");
    for (auto I : State->get<ObjectsUnderConstruction>())
      if (I.first.getLocationContext() == LC)
        return false;

    LC = LC->getParent();
  }
  return true;
}


//===----------------------------------------------------------------------===//
// Top-level transfer function logic (Dispatcher).
//===----------------------------------------------------------------------===//

/// evalAssume - Called by ConstraintManager. Used to call checker-specific
///  logic for handling assumptions on symbolic values.
ProgramStateRef ExprEngine::processAssume(ProgramStateRef state,
                                              SVal cond, bool assumption) {
  return getCheckerManager().runCheckersForEvalAssume(state, cond, assumption);
}

ProgramStateRef
ExprEngine::processRegionChanges(ProgramStateRef state,
                                 const InvalidatedSymbols *invalidated,
                                 ArrayRef<const MemRegion *> Explicits,
                                 ArrayRef<const MemRegion *> Regions,
                                 const LocationContext *LCtx,
                                 const CallEvent *Call) {
  return getCheckerManager().runCheckersForRegionChanges(state, invalidated,
                                                         Explicits, Regions,
                                                         LCtx, Call);
}

static void
printObjectsUnderConstructionJson(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, const LocationContext *LCtx,
                                  unsigned int Space = 0, bool IsDot = false) {
  PrintingPolicy PP =
      LCtx->getAnalysisDeclContext()->getASTContext().getPrintingPolicy();

  ++Space;
  bool HasItem = false;

  // Store the last key.
  const ConstructedObjectKey *LastKey = nullptr;
  for (const auto &I : State->get<ObjectsUnderConstruction>()) {
    const ConstructedObjectKey &Key = I.first;
    if (Key.getLocationContext() != LCtx)
      continue;

    if (!HasItem) {
      Out << '[' << NL;
      HasItem = true;
    }

    LastKey = &Key;
  }

  for (const auto &I : State->get<ObjectsUnderConstruction>()) {
    const ConstructedObjectKey &Key = I.first;
    SVal Value = I.second;
    if (Key.getLocationContext() != LCtx)
      continue;

    Indent(Out, Space, IsDot) << "{ ";
    Key.printJson(Out, nullptr, PP);
    Out << ", \"value\": \"" << Value << "\" }";

    if (&Key != LastKey)
      Out << ',';
    Out << NL;
  }

  if (HasItem)
    Indent(Out, --Space, IsDot) << ']'; // End of "location_context".
  else {
    Out << "null ";
  }
}

static void printIndicesOfElementsToConstructJson(
    raw_ostream &Out, ProgramStateRef State, const char *NL,
    const LocationContext *LCtx, unsigned int Space = 0, bool IsDot = false) {
  using KeyT = std::pair<const Expr *, const LocationContext *>;

  const auto &Context = LCtx->getAnalysisDeclContext()->getASTContext();
  PrintingPolicy PP = Context.getPrintingPolicy();

  ++Space;
  bool HasItem = false;

  // Store the last key.
  KeyT LastKey;
  for (const auto &I : State->get<IndexOfElementToConstruct>()) {
    const KeyT &Key = I.first;
    if (Key.second != LCtx)
      continue;

    if (!HasItem) {
      Out << '[' << NL;
      HasItem = true;
    }

    LastKey = Key;
  }

  for (const auto &I : State->get<IndexOfElementToConstruct>()) {
    const KeyT &Key = I.first;
    unsigned Value = I.second;
    if (Key.second != LCtx)
      continue;

    Indent(Out, Space, IsDot) << "{ ";

    // Expr
    const Expr *E = Key.first;
    Out << "\"stmt_id\": " << E->getID(Context);

    // Kind
    Out << ", \"kind\": null";

    // Pretty-print
    Out << ", \"pretty\": ";
    Out << "\"" << E->getStmtClassName() << ' '
        << E->getSourceRange().printToString(Context.getSourceManager()) << " '"
        << QualType::getAsString(E->getType().split(), PP);
    Out << "'\"";

    Out << ", \"value\": \"Current index: " << Value - 1 << "\" }";

    if (Key != LastKey)
      Out << ',';
    Out << NL;
  }

  if (HasItem)
    Indent(Out, --Space, IsDot) << ']'; // End of "location_context".
  else {
    Out << "null ";
  }
}

static void printPendingInitLoopJson(raw_ostream &Out, ProgramStateRef State,
                                     const char *NL,
                                     const LocationContext *LCtx,
                                     unsigned int Space = 0,
                                     bool IsDot = false) {
  using KeyT = std::pair<const CXXConstructExpr *, const LocationContext *>;

  const auto &Context = LCtx->getAnalysisDeclContext()->getASTContext();
  PrintingPolicy PP = Context.getPrintingPolicy();

  ++Space;
  bool HasItem = false;

  // Store the last key.
  KeyT LastKey;
  for (const auto &I : State->get<PendingInitLoop>()) {
    const KeyT &Key = I.first;
    if (Key.second != LCtx)
      continue;

    if (!HasItem) {
      Out << '[' << NL;
      HasItem = true;
    }

    LastKey = Key;
  }

  for (const auto &I : State->get<PendingInitLoop>()) {
    const KeyT &Key = I.first;
    unsigned Value = I.second;
    if (Key.second != LCtx)
      continue;

    Indent(Out, Space, IsDot) << "{ ";

    const CXXConstructExpr *E = Key.first;
    Out << "\"stmt_id\": " << E->getID(Context);

    Out << ", \"kind\": null";
    Out << ", \"pretty\": ";
    Out << '\"' << E->getStmtClassName() << ' '
        << E->getSourceRange().printToString(Context.getSourceManager()) << " '"
        << QualType::getAsString(E->getType().split(), PP);
    Out << "'\"";

    Out << ", \"value\": \"Flattened size: " << Value << "\"}";

    if (Key != LastKey)
      Out << ',';
    Out << NL;
  }

  if (HasItem)
    Indent(Out, --Space, IsDot) << ']'; // End of "location_context".
  else {
    Out << "null ";
  }
}

static void
printPendingArrayDestructionsJson(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, const LocationContext *LCtx,
                                  unsigned int Space = 0, bool IsDot = false) {
  using KeyT = const LocationContext *;

  ++Space;
  bool HasItem = false;

  // Store the last key.
  KeyT LastKey = nullptr;
  for (const auto &I : State->get<PendingArrayDestruction>()) {
    const KeyT &Key = I.first;
    if (Key != LCtx)
      continue;

    if (!HasItem) {
      Out << '[' << NL;
      HasItem = true;
    }

    LastKey = Key;
  }

  for (const auto &I : State->get<PendingArrayDestruction>()) {
    const KeyT &Key = I.first;
    if (Key != LCtx)
      continue;

    Indent(Out, Space, IsDot) << "{ ";

    Out << "\"stmt_id\": null";
    Out << ", \"kind\": null";
    Out << ", \"pretty\": \"Current index: \"";
    Out << ", \"value\": \"" << I.second << "\" }";

    if (Key != LastKey)
      Out << ',';
    Out << NL;
  }

  if (HasItem)
    Indent(Out, --Space, IsDot) << ']'; // End of "location_context".
  else {
    Out << "null ";
  }
}

/// A helper function to generalize program state trait printing.
/// The function invokes Printer as 'Printer(Out, State, NL, LC, Space, IsDot,
/// std::forward<Args>(args)...)'. \n One possible type for Printer is
/// 'void()(raw_ostream &, ProgramStateRef, const char *, const LocationContext
/// *, unsigned int, bool, ...)' \n \param Trait The state trait to be printed.
/// \param Printer A void function that prints Trait.
/// \param Args An additional parameter pack that is passed to Print upon
/// invocation.
template <typename Trait, typename Printer, typename... Args>
static void printStateTraitWithLocationContextJson(
    raw_ostream &Out, ProgramStateRef State, const LocationContext *LCtx,
    const char *NL, unsigned int Space, bool IsDot,
    const char *jsonPropertyName, Printer printer, Args &&...args) {

  using RequiredType =
      void (*)(raw_ostream &, ProgramStateRef, const char *,
               const LocationContext *, unsigned int, bool, Args &&...);

  // Try to do as much compile time checking as possible.
  // FIXME: check for invocable instead of function?
  static_assert(std::is_function_v<std::remove_pointer_t<Printer>>,
                "Printer is not a function!");
  static_assert(std::is_convertible_v<Printer, RequiredType>,
                "Printer doesn't have the required type!");

  if (LCtx && !State->get<Trait>().isEmpty()) {
    Indent(Out, Space, IsDot) << '\"' << jsonPropertyName << "\": ";
    ++Space;
    Out << '[' << NL;
    LCtx->printJson(Out, NL, Space, IsDot, [&](const LocationContext *LC) {
      printer(Out, State, NL, LC, Space, IsDot, std::forward<Args>(args)...);
    });

    --Space;
    Indent(Out, Space, IsDot) << "]," << NL; // End of "jsonPropertyName".
  }
}

void ExprEngine::printJson(raw_ostream &Out, ProgramStateRef State,
                           const LocationContext *LCtx, const char *NL,
                           unsigned int Space, bool IsDot) const {

  printStateTraitWithLocationContextJson<ObjectsUnderConstruction>(
      Out, State, LCtx, NL, Space, IsDot, "constructing_objects",
      printObjectsUnderConstructionJson);
  printStateTraitWithLocationContextJson<IndexOfElementToConstruct>(
      Out, State, LCtx, NL, Space, IsDot, "index_of_element",
      printIndicesOfElementsToConstructJson);
  printStateTraitWithLocationContextJson<PendingInitLoop>(
      Out, State, LCtx, NL, Space, IsDot, "pending_init_loops",
      printPendingInitLoopJson);
  printStateTraitWithLocationContextJson<PendingArrayDestruction>(
      Out, State, LCtx, NL, Space, IsDot, "pending_destructors",
      printPendingArrayDestructionsJson);

  getCheckerManager().runCheckersForPrintStateJson(Out, State, NL, Space,
                                                   IsDot);
}

void ExprEngine::processEndWorklist() {
  // This prints the name of the top-level function if we crash.
  PrettyStackTraceLocationContext CrashInfo(getRootLocationContext());
  getCheckerManager().runCheckersForEndAnalysis(G, BR, *this);
}

void ExprEngine::processCFGElement(const CFGElement E, ExplodedNode *Pred,
                                   unsigned StmtIdx, NodeBuilderContext *Ctx) {
  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  currStmtIdx = StmtIdx;
  currBldrCtx = Ctx;

  switch (E.getKind()) {
    case CFGElement::Statement:
    case CFGElement::Constructor:
    case CFGElement::CXXRecordTypedCall:
      ProcessStmt(E.castAs<CFGStmt>().getStmt(), Pred);
      return;
    case CFGElement::Initializer:
      ProcessInitializer(E.castAs<CFGInitializer>(), Pred);
      return;
    case CFGElement::NewAllocator:
      ProcessNewAllocator(E.castAs<CFGNewAllocator>().getAllocatorExpr(),
                          Pred);
      return;
    case CFGElement::AutomaticObjectDtor:
    case CFGElement::DeleteDtor:
    case CFGElement::BaseDtor:
    case CFGElement::MemberDtor:
    case CFGElement::TemporaryDtor:
      ProcessImplicitDtor(E.castAs<CFGImplicitDtor>(), Pred);
      return;
    case CFGElement::LoopExit:
      ProcessLoopExit(E.castAs<CFGLoopExit>().getLoopStmt(), Pred);
      return;
    case CFGElement::LifetimeEnds:
    case CFGElement::CleanupFunction:
    case CFGElement::ScopeBegin:
    case CFGElement::ScopeEnd:
      return;
  }
}

static bool shouldRemoveDeadBindings(AnalysisManager &AMgr,
                                     const Stmt *S,
                                     const ExplodedNode *Pred,
                                     const LocationContext *LC) {
  // Are we never purging state values?
  if (AMgr.options.AnalysisPurgeOpt == PurgeNone)
    return false;

  // Is this the beginning of a basic block?
  if (Pred->getLocation().getAs<BlockEntrance>())
    return true;

  // Is this on a non-expression?
  if (!isa<Expr>(S))
    return true;

  // Run before processing a call.
  if (CallEvent::isCallStmt(S))
    return true;

  // Is this an expression that is consumed by another expression?  If so,
  // postpone cleaning out the state.
  ParentMap &PM = LC->getAnalysisDeclContext()->getParentMap();
  return !PM.isConsumedExpr(cast<Expr>(S));
}

void ExprEngine::removeDead(ExplodedNode *Pred, ExplodedNodeSet &Out,
                            const Stmt *ReferenceStmt,
                            const LocationContext *LC,
                            const Stmt *DiagnosticStmt,
                            ProgramPoint::Kind K) {
  assert((K == ProgramPoint::PreStmtPurgeDeadSymbolsKind ||
          ReferenceStmt == nullptr || isa<ReturnStmt>(ReferenceStmt))
          && "PostStmt is not generally supported by the SymbolReaper yet");
  assert(LC && "Must pass the current (or expiring) LocationContext");

  if (!DiagnosticStmt) {
    DiagnosticStmt = ReferenceStmt;
    assert(DiagnosticStmt && "Required for clearing a LocationContext");
  }

  NumRemoveDeadBindings++;
  ProgramStateRef CleanedState = Pred->getState();

  // LC is the location context being destroyed, but SymbolReaper wants a
  // location context that is still live. (If this is the top-level stack
  // frame, this will be null.)
  if (!ReferenceStmt) {
    assert(K == ProgramPoint::PostStmtPurgeDeadSymbolsKind &&
           "Use PostStmtPurgeDeadSymbolsKind for clearing a LocationContext");
    LC = LC->getParent();
  }

  const StackFrameContext *SFC = LC ? LC->getStackFrame() : nullptr;
  SymbolReaper SymReaper(SFC, ReferenceStmt, SymMgr, getStoreManager());

  for (auto I : CleanedState->get<ObjectsUnderConstruction>()) {
    if (SymbolRef Sym = I.second.getAsSymbol())
      SymReaper.markLive(Sym);
    if (const MemRegion *MR = I.second.getAsRegion())
      SymReaper.markLive(MR);
  }

  getCheckerManager().runCheckersForLiveSymbols(CleanedState, SymReaper);

  // Create a state in which dead bindings are removed from the environment
  // and the store. TODO: The function should just return new env and store,
  // not a new state.
  CleanedState = StateMgr.removeDeadBindingsFromEnvironmentAndStore(
      CleanedState, SFC, SymReaper);

  // Process any special transfer function for dead symbols.
  // A tag to track convenience transitions, which can be removed at cleanup.
  static SimpleProgramPointTag cleanupTag(TagProviderName, "Clean Node");
  // Call checkers with the non-cleaned state so that they could query the
  // values of the soon to be dead symbols.
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForDeadSymbols(CheckedSet, Pred, SymReaper,
                                                DiagnosticStmt, *this, K);

  // For each node in CheckedSet, generate CleanedNodes that have the
  // environment, the store, and the constraints cleaned up but have the
  // user-supplied states as the predecessors.
  StmtNodeBuilder Bldr(CheckedSet, Out, *currBldrCtx);
  for (const auto I : CheckedSet) {
    ProgramStateRef CheckerState = I->getState();

    // The constraint manager has not been cleaned up yet, so clean up now.
    CheckerState =
        getConstraintManager().removeDeadBindings(CheckerState, SymReaper);

    assert(StateMgr.haveEqualEnvironments(CheckerState, Pred->getState()) &&
           "Checkers are not allowed to modify the Environment as a part of "
           "checkDeadSymbols processing.");
    assert(StateMgr.haveEqualStores(CheckerState, Pred->getState()) &&
           "Checkers are not allowed to modify the Store as a part of "
           "checkDeadSymbols processing.");

    // Create a state based on CleanedState with CheckerState GDM and
    // generate a transition to that state.
    ProgramStateRef CleanedCheckerSt =
        StateMgr.getPersistentStateWithGDM(CleanedState, CheckerState);
    Bldr.generateNode(DiagnosticStmt, I, CleanedCheckerSt, &cleanupTag, K);
  }
}

void ExprEngine::ProcessStmt(const Stmt *currStmt, ExplodedNode *Pred) {
  // Reclaim any unnecessary nodes in the ExplodedGraph.
  G.reclaimRecentlyAllocatedNodes();

  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                currStmt->getBeginLoc(),
                                "Error evaluating statement");

  // Remove dead bindings and symbols.
  ExplodedNodeSet CleanedStates;
  if (shouldRemoveDeadBindings(AMgr, currStmt, Pred,
                               Pred->getLocationContext())) {
    removeDead(Pred, CleanedStates, currStmt,
                                    Pred->getLocationContext());
  } else
    CleanedStates.Add(Pred);

  // Visit the statement.
  ExplodedNodeSet Dst;
  for (const auto I : CleanedStates) {
    ExplodedNodeSet DstI;
    // Visit the statement.
    Visit(currStmt, I, DstI);
    Dst.insert(DstI);
  }

  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessLoopExit(const Stmt* S, ExplodedNode *Pred) {
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                S->getBeginLoc(),
                                "Error evaluating end of the loop");
  ExplodedNodeSet Dst;
  Dst.Add(Pred);
  NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  ProgramStateRef NewState = Pred->getState();

  if(AMgr.options.ShouldUnrollLoops)
    NewState = processLoopEnd(S, NewState);

  LoopExit PP(S, Pred->getLocationContext());
  Bldr.generateNode(PP, NewState, Pred);
  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessInitializer(const CFGInitializer CFGInit,
                                    ExplodedNode *Pred) {
  const CXXCtorInitializer *BMI = CFGInit.getInitializer();
  const Expr *Init = BMI->getInit()->IgnoreImplicit();
  const LocationContext *LC = Pred->getLocationContext();

  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                BMI->getSourceLocation(),
                                "Error evaluating initializer");

  // We don't clean up dead bindings here.
  const auto *stackFrame = cast<StackFrameContext>(Pred->getLocationContext());
  const auto *decl = cast<CXXConstructorDecl>(stackFrame->getDecl());

  ProgramStateRef State = Pred->getState();
  SVal thisVal = State->getSVal(svalBuilder.getCXXThis(decl, stackFrame));

  ExplodedNodeSet Tmp;
  SVal FieldLoc;

  // Evaluate the initializer, if necessary
  if (BMI->isAnyMemberInitializer()) {
    // Constructors build the object directly in the field,
    // but non-objects must be copied in from the initializer.
    if (getObjectUnderConstruction(State, BMI, LC)) {
      // The field was directly constructed, so there is no need to bind.
      // But we still need to stop tracking the object under construction.
      State = finishObjectConstruction(State, BMI, LC);
      NodeBuilder Bldr(Pred, Tmp, *currBldrCtx);
      PostStore PS(Init, LC, /*Loc*/ nullptr, /*tag*/ nullptr);
      Bldr.generateNode(PS, State, Pred);
    } else {
      const ValueDecl *Field;
      if (BMI->isIndirectMemberInitializer()) {
        Field = BMI->getIndirectMember();
        FieldLoc = State->getLValue(BMI->getIndirectMember(), thisVal);
      } else {
        Field = BMI->getMember();
        FieldLoc = State->getLValue(BMI->getMember(), thisVal);
      }

      SVal InitVal;
      if (Init->getType()->isArrayType()) {
        // Handle arrays of trivial type. We can represent this with a
        // primitive load/copy from the base array region.
        const ArraySubscriptExpr *ASE;
        while ((ASE = dyn_cast<ArraySubscriptExpr>(Init)))
          Init = ASE->getBase()->IgnoreImplicit();

        SVal LValue = State->getSVal(Init, stackFrame);
        if (!Field->getType()->isReferenceType())
          if (std::optional<Loc> LValueLoc = LValue.getAs<Loc>())
            InitVal = State->getSVal(*LValueLoc);

        // If we fail to get the value for some reason, use a symbolic value.
        if (InitVal.isUnknownOrUndef()) {
          SValBuilder &SVB = getSValBuilder();
          InitVal = SVB.conjureSymbolVal(BMI->getInit(), stackFrame,
                                         Field->getType(),
                                         currBldrCtx->blockCount());
        }
      } else {
        InitVal = State->getSVal(BMI->getInit(), stackFrame);
      }

      PostInitializer PP(BMI, FieldLoc.getAsRegion(), stackFrame);
      evalBind(Tmp, Init, Pred, FieldLoc, InitVal, /*isInit=*/true, &PP);
    }
  } else if (BMI->isBaseInitializer() && isa<InitListExpr>(Init)) {
    // When the base class is initialized with an initialization list and the
    // base class does not have a ctor, there will not be a CXXConstructExpr to
    // initialize the base region. Hence, we need to make the bind for it.
    SVal BaseLoc = getStoreManager().evalDerivedToBase(
        thisVal, QualType(BMI->getBaseClass(), 0), BMI->isBaseVirtual());
    SVal InitVal = State->getSVal(Init, stackFrame);
    evalBind(Tmp, Init, Pred, BaseLoc, InitVal, /*isInit=*/true);
  } else {
    assert(BMI->isBaseInitializer() || BMI->isDelegatingInitializer());
    Tmp.insert(Pred);
    // We already did all the work when visiting the CXXConstructExpr.
  }

  // Construct PostInitializer nodes whether the state changed or not,
  // so that the diagnostics don't get confused.
  PostInitializer PP(BMI, FieldLoc.getAsRegion(), stackFrame);
  ExplodedNodeSet Dst;
  NodeBuilder Bldr(Tmp, Dst, *currBldrCtx);
  for (const auto I : Tmp) {
    ProgramStateRef State = I->getState();
    Bldr.generateNode(PP, State, I);
  }

  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

std::pair<ProgramStateRef, uint64_t>
ExprEngine::prepareStateForArrayDestruction(const ProgramStateRef State,
                                            const MemRegion *Region,
                                            const QualType &ElementTy,
                                            const LocationContext *LCtx,
                                            SVal *ElementCountVal) {
  assert(Region != nullptr && "Not-null region expected");

  QualType Ty = ElementTy.getDesugaredType(getContext());
  while (const auto *NTy = dyn_cast<ArrayType>(Ty))
    Ty = NTy->getElementType().getDesugaredType(getContext());

  auto ElementCount = getDynamicElementCount(State, Region, svalBuilder, Ty);

  if (ElementCountVal)
    *ElementCountVal = ElementCount;

  // Note: the destructors are called in reverse order.
  unsigned Idx = 0;
  if (auto OptionalIdx = getPendingArrayDestruction(State, LCtx)) {
    Idx = *OptionalIdx;
  } else {
    // The element count is either unknown, or an SVal that's not an integer.
    if (!ElementCount.isConstant())
      return {State, 0};

    Idx = ElementCount.getAsInteger()->getLimitedValue();
  }

  if (Idx == 0)
    return {State, 0};

  --Idx;

  return {setPendingArrayDestruction(State, LCtx, Idx), Idx};
}

void ExprEngine::ProcessImplicitDtor(const CFGImplicitDtor D,
                                     ExplodedNode *Pred) {
  ExplodedNodeSet Dst;
  switch (D.getKind()) {
  case CFGElement::AutomaticObjectDtor:
    ProcessAutomaticObjDtor(D.castAs<CFGAutomaticObjDtor>(), Pred, Dst);
    break;
  case CFGElement::BaseDtor:
    ProcessBaseDtor(D.castAs<CFGBaseDtor>(), Pred, Dst);
    break;
  case CFGElement::MemberDtor:
    ProcessMemberDtor(D.castAs<CFGMemberDtor>(), Pred, Dst);
    break;
  case CFGElement::TemporaryDtor:
    ProcessTemporaryDtor(D.castAs<CFGTemporaryDtor>(), Pred, Dst);
    break;
  case CFGElement::DeleteDtor:
    ProcessDeleteDtor(D.castAs<CFGDeleteDtor>(), Pred, Dst);
    break;
  default:
    llvm_unreachable("Unexpected dtor kind.");
  }

  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessNewAllocator(const CXXNewExpr *NE,
                                     ExplodedNode *Pred) {
  ExplodedNodeSet Dst;
  AnalysisManager &AMgr = getAnalysisManager();
  AnalyzerOptions &Opts = AMgr.options;
  // TODO: We're not evaluating allocators for all cases just yet as
  // we're not handling the return value correctly, which causes false
  // positives when the alpha.cplusplus.NewDeleteLeaks check is on.
  if (Opts.MayInlineCXXAllocator)
    VisitCXXNewAllocatorCall(NE, Pred, Dst);
  else {
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    const LocationContext *LCtx = Pred->getLocationContext();
    PostImplicitCall PP(NE->getOperatorNew(), NE->getBeginLoc(), LCtx,
                        getCFGElementRef());
    Bldr.generateNode(PP, Pred->getState(), Pred);
  }
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessAutomaticObjDtor(const CFGAutomaticObjDtor Dtor,
                                         ExplodedNode *Pred,
                                         ExplodedNodeSet &Dst) {
  const auto *DtorDecl = Dtor.getDestructorDecl(getContext());
  const VarDecl *varDecl = Dtor.getVarDecl();
  QualType varType = varDecl->getType();

  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  SVal dest = state->getLValue(varDecl, LCtx);
  const MemRegion *Region = dest.castAs<loc::MemRegionVal>().getRegion();

  if (varType->isReferenceType()) {
    const MemRegion *ValueRegion = state->getSVal(Region).getAsRegion();
    if (!ValueRegion) {
      // FIXME: This should not happen. The language guarantees a presence
      // of a valid initializer here, so the reference shall not be undefined.
      // It seems that we're calling destructors over variables that
      // were not initialized yet.
      return;
    }
    Region = ValueRegion->getBaseRegion();
    varType = cast<TypedValueRegion>(Region)->getValueType();
  }

  unsigned Idx = 0;
  if (isa<ArrayType>(varType)) {
    SVal ElementCount;
    std::tie(state, Idx) = prepareStateForArrayDestruction(
        state, Region, varType, LCtx, &ElementCount);

    if (ElementCount.isConstant()) {
      uint64_t ArrayLength = ElementCount.getAsInteger()->getLimitedValue();
      assert(ArrayLength &&
             "An automatic dtor for a 0 length array shouldn't be triggered!");

      // Still handle this case if we don't have assertions enabled.
      if (!ArrayLength) {
        static SimpleProgramPointTag PT(
            "ExprEngine", "Skipping automatic 0 length array destruction, "
                          "which shouldn't be in the CFG.");
        PostImplicitCall PP(DtorDecl, varDecl->getLocation(), LCtx,
                            getCFGElementRef(), &PT);
        NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
        Bldr.generateSink(PP, Pred->getState(), Pred);
        return;
      }
    }
  }

  EvalCallOptions CallOpts;
  Region = makeElementRegion(state, loc::MemRegionVal(Region), varType,
                             CallOpts.IsArrayCtorOrDtor, Idx)
               .getAsRegion();

  NodeBuilder Bldr(Pred, Dst, getBuilderContext());

  static SimpleProgramPointTag PT("ExprEngine",
                                  "Prepare for object destruction");
  PreImplicitCall PP(DtorDecl, varDecl->getLocation(), LCtx, getCFGElementRef(),
                     &PT);
  Pred = Bldr.generateNode(PP, state, Pred);

  if (!Pred)
    return;
  Bldr.takeNodes(Pred);

  VisitCXXDestructor(varType, Region, Dtor.getTriggerStmt(),
                     /*IsBase=*/false, Pred, Dst, CallOpts);
}

void ExprEngine::ProcessDeleteDtor(const CFGDeleteDtor Dtor,
                                   ExplodedNode *Pred,
                                   ExplodedNodeSet &Dst) {
  ProgramStateRef State = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();
  const CXXDeleteExpr *DE = Dtor.getDeleteExpr();
  const Stmt *Arg = DE->getArgument();
  QualType DTy = DE->getDestroyedType();
  SVal ArgVal = State->getSVal(Arg, LCtx);

  // If the argument to delete is known to be a null value,
  // don't run destructor.
  if (State->isNull(ArgVal).isConstrainedTrue()) {
    QualType BTy = getContext().getBaseElementType(DTy);
    const CXXRecordDecl *RD = BTy->getAsCXXRecordDecl();
    const CXXDestructorDecl *Dtor = RD->getDestructor();

    PostImplicitCall PP(Dtor, DE->getBeginLoc(), LCtx, getCFGElementRef());
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    Bldr.generateNode(PP, Pred->getState(), Pred);
    return;
  }

  auto getDtorDecl = [](const QualType &DTy) {
    const CXXRecordDecl *RD = DTy->getAsCXXRecordDecl();
    return RD->getDestructor();
  };

  unsigned Idx = 0;
  EvalCallOptions CallOpts;
  const MemRegion *ArgR = ArgVal.getAsRegion();

  if (DE->isArrayForm()) {
    CallOpts.IsArrayCtorOrDtor = true;
    // Yes, it may even be a multi-dimensional array.
    while (const auto *AT = getContext().getAsArrayType(DTy))
      DTy = AT->getElementType();

    if (ArgR) {
      SVal ElementCount;
      std::tie(State, Idx) = prepareStateForArrayDestruction(
          State, ArgR, DTy, LCtx, &ElementCount);

      // If we're about to destruct a 0 length array, don't run any of the
      // destructors.
      if (ElementCount.isConstant() &&
          ElementCount.getAsInteger()->getLimitedValue() == 0) {

        static SimpleProgramPointTag PT(
            "ExprEngine", "Skipping 0 length array delete destruction");
        PostImplicitCall PP(getDtorDecl(DTy), DE->getBeginLoc(), LCtx,
                            getCFGElementRef(), &PT);
        NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
        Bldr.generateNode(PP, Pred->getState(), Pred);
        return;
      }

      ArgR = State->getLValue(DTy, svalBuilder.makeArrayIndex(Idx), ArgVal)
                 .getAsRegion();
    }
  }

  NodeBuilder Bldr(Pred, Dst, getBuilderContext());
  static SimpleProgramPointTag PT("ExprEngine",
                                  "Prepare for object destruction");
  PreImplicitCall PP(getDtorDecl(DTy), DE->getBeginLoc(), LCtx,
                     getCFGElementRef(), &PT);
  Pred = Bldr.generateNode(PP, State, Pred);

  if (!Pred)
    return;
  Bldr.takeNodes(Pred);

  VisitCXXDestructor(DTy, ArgR, DE, /*IsBase=*/false, Pred, Dst, CallOpts);
}

void ExprEngine::ProcessBaseDtor(const CFGBaseDtor D,
                                 ExplodedNode *Pred, ExplodedNodeSet &Dst) {
  const LocationContext *LCtx = Pred->getLocationContext();

  const auto *CurDtor = cast<CXXDestructorDecl>(LCtx->getDecl());
  Loc ThisPtr = getSValBuilder().getCXXThis(CurDtor,
                                            LCtx->getStackFrame());
  SVal ThisVal = Pred->getState()->getSVal(ThisPtr);

  // Create the base object region.
  const CXXBaseSpecifier *Base = D.getBaseSpecifier();
  QualType BaseTy = Base->getType();
  SVal BaseVal = getStoreManager().evalDerivedToBase(ThisVal, BaseTy,
                                                     Base->isVirtual());

  EvalCallOptions CallOpts;
  VisitCXXDestructor(BaseTy, BaseVal.getAsRegion(), CurDtor->getBody(),
                     /*IsBase=*/true, Pred, Dst, CallOpts);
}

void ExprEngine::ProcessMemberDtor(const CFGMemberDtor D,
                                   ExplodedNode *Pred, ExplodedNodeSet &Dst) {
  const auto *DtorDecl = D.getDestructorDecl(getContext());
  const FieldDecl *Member = D.getFieldDecl();
  QualType T = Member->getType();
  ProgramStateRef State = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  const auto *CurDtor = cast<CXXDestructorDecl>(LCtx->getDecl());
  Loc ThisStorageLoc =
      getSValBuilder().getCXXThis(CurDtor, LCtx->getStackFrame());
  Loc ThisLoc = State->getSVal(ThisStorageLoc).castAs<Loc>();
  SVal FieldVal = State->getLValue(Member, ThisLoc);

  unsigned Idx = 0;
  if (isa<ArrayType>(T)) {
    SVal ElementCount;
    std::tie(State, Idx) = prepareStateForArrayDestruction(
        State, FieldVal.getAsRegion(), T, LCtx, &ElementCount);

    if (ElementCount.isConstant()) {
      uint64_t ArrayLength = ElementCount.getAsInteger()->getLimitedValue();
      assert(ArrayLength &&
             "A member dtor for a 0 length array shouldn't be triggered!");

      // Still handle this case if we don't have assertions enabled.
      if (!ArrayLength) {
        static SimpleProgramPointTag PT(
            "ExprEngine", "Skipping member 0 length array destruction, which "
                          "shouldn't be in the CFG.");
        PostImplicitCall PP(DtorDecl, Member->getLocation(), LCtx,
                            getCFGElementRef(), &PT);
        NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
        Bldr.generateSink(PP, Pred->getState(), Pred);
        return;
      }
    }
  }

  EvalCallOptions CallOpts;
  FieldVal =
      makeElementRegion(State, FieldVal, T, CallOpts.IsArrayCtorOrDtor, Idx);

  NodeBuilder Bldr(Pred, Dst, getBuilderContext());

  static SimpleProgramPointTag PT("ExprEngine",
                                  "Prepare for object destruction");
  PreImplicitCall PP(DtorDecl, Member->getLocation(), LCtx, getCFGElementRef(),
                     &PT);
  Pred = Bldr.generateNode(PP, State, Pred);

  if (!Pred)
    return;
  Bldr.takeNodes(Pred);

  VisitCXXDestructor(T, FieldVal.getAsRegion(), CurDtor->getBody(),
                     /*IsBase=*/false, Pred, Dst, CallOpts);
}

void ExprEngine::ProcessTemporaryDtor(const CFGTemporaryDtor D,
                                      ExplodedNode *Pred,
                                      ExplodedNodeSet &Dst) {
  const CXXBindTemporaryExpr *BTE = D.getBindTemporaryExpr();
  ProgramStateRef State = Pred->getState();
  const LocationContext *LC = Pred->getLocationContext();
  const MemRegion *MR = nullptr;

  if (std::optional<SVal> V = getObjectUnderConstruction(
          State, D.getBindTemporaryExpr(), Pred->getLocationContext())) {
    // FIXME: Currently we insert temporary destructors for default parameters,
    // but we don't insert the constructors, so the entry in
    // ObjectsUnderConstruction may be missing.
    State = finishObjectConstruction(State, D.getBindTemporaryExpr(),
                                     Pred->getLocationContext());
    MR = V->getAsRegion();
  }

  // If copy elision has occurred, and the constructor corresponding to the
  // destructor was elided, we need to skip the destructor as well.
  if (isDestructorElided(State, BTE, LC)) {
    State = cleanupElidedDestructor(State, BTE, LC);
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    PostImplicitCall PP(D.getDestructorDecl(getContext()),
                        D.getBindTemporaryExpr()->getBeginLoc(),
                        Pred->getLocationContext(), getCFGElementRef());
    Bldr.generateNode(PP, State, Pred);
    return;
  }

  ExplodedNodeSet CleanDtorState;
  StmtNodeBuilder StmtBldr(Pred, CleanDtorState, *currBldrCtx);
  StmtBldr.generateNode(D.getBindTemporaryExpr(), Pred, State);

  QualType T = D.getBindTemporaryExpr()->getSubExpr()->getType();
  // FIXME: Currently CleanDtorState can be empty here due to temporaries being
  // bound to default parameters.
  assert(CleanDtorState.size() <= 1);
  ExplodedNode *CleanPred =
      CleanDtorState.empty() ? Pred : *CleanDtorState.begin();

  EvalCallOptions CallOpts;
  CallOpts.IsTemporaryCtorOrDtor = true;
  if (!MR) {
    // FIXME: If we have no MR, we still need to unwrap the array to avoid
    // destroying the whole array at once.
    //
    // For this case there is no universal solution as there is no way to
    // directly create an array of temporary objects. There are some expressions
    // however which can create temporary objects and have an array type.
    //
    // E.g.: std::initializer_list<S>{S(), S()};
    //
    // The expression above has a type of 'const struct S[2]' but it's a single
    // 'std::initializer_list<>'. The destructors of the 2 temporary 'S()'
    // objects will be called anyway, because they are 2 separate objects in 2
    // separate clusters, i.e.: not an array.
    //
    // Now the 'std::initializer_list<>' is not an array either even though it
    // has the type of an array. The point is, we only want to invoke the
    // destructor for the initializer list once not twice or so.
    while (const ArrayType *AT = getContext().getAsArrayType(T)) {
      T = AT->getElementType();

      // FIXME: Enable this flag once we handle this case properly.
      // CallOpts.IsArrayCtorOrDtor = true;
    }
  } else {
    // FIXME: We'd eventually need to makeElementRegion() trick here,
    // but for now we don't have the respective construction contexts,
    // so MR would always be null in this case. Do nothing for now.
  }
  VisitCXXDestructor(T, MR, D.getBindTemporaryExpr(),
                     /*IsBase=*/false, CleanPred, Dst, CallOpts);
}

void ExprEngine::processCleanupTemporaryBranch(const CXXBindTemporaryExpr *BTE,
                                               NodeBuilderContext &BldCtx,
                                               ExplodedNode *Pred,
                                               ExplodedNodeSet &Dst,
                                               const CFGBlock *DstT,
                                               const CFGBlock *DstF) {
  BranchNodeBuilder TempDtorBuilder(Pred, Dst, BldCtx, DstT, DstF);
  ProgramStateRef State = Pred->getState();
  const LocationContext *LC = Pred->getLocationContext();
  if (getObjectUnderConstruction(State, BTE, LC)) {
    TempDtorBuilder.markInfeasible(false);
    TempDtorBuilder.generateNode(State, true, Pred);
  } else {
    TempDtorBuilder.markInfeasible(true);
    TempDtorBuilder.generateNode(State, false, Pred);
  }
}

void ExprEngine::VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *BTE,
                                           ExplodedNodeSet &PreVisit,
                                           ExplodedNodeSet &Dst) {
  // This is a fallback solution in case we didn't have a construction
  // context when we were constructing the temporary. Otherwise the map should
  // have been populated there.
  if (!getAnalysisManager().options.ShouldIncludeTemporaryDtorsInCFG) {
    // In case we don't have temporary destructors in the CFG, do not mark
    // the initialization - we would otherwise never clean it up.
    Dst = PreVisit;
    return;
  }
  StmtNodeBuilder StmtBldr(PreVisit, Dst, *currBldrCtx);
  for (ExplodedNode *Node : PreVisit) {
    ProgramStateRef State = Node->getState();
    const LocationContext *LC = Node->getLocationContext();
    if (!getObjectUnderConstruction(State, BTE, LC)) {
      // FIXME: Currently the state might also already contain the marker due to
      // incorrect handling of temporaries bound to default parameters; for
      // those, we currently skip the CXXBindTemporaryExpr but rely on adding
      // temporary destructor nodes.
      State = addObjectUnderConstruction(State, BTE, LC, UnknownVal());
    }
    StmtBldr.generateNode(BTE, Node, State);
  }
}

ProgramStateRef ExprEngine::escapeValues(ProgramStateRef State,
                                         ArrayRef<SVal> Vs,
                                         PointerEscapeKind K,
                                         const CallEvent *Call) const {
  class CollectReachableSymbolsCallback final : public SymbolVisitor {
    InvalidatedSymbols &Symbols;

  public:
    explicit CollectReachableSymbolsCallback(InvalidatedSymbols &Symbols)
        : Symbols(Symbols) {}

    const InvalidatedSymbols &getSymbols() const { return Symbols; }

    bool VisitSymbol(SymbolRef Sym) override {
      Symbols.insert(Sym);
      return true;
    }
  };
  InvalidatedSymbols Symbols;
  CollectReachableSymbolsCallback CallBack(Symbols);
  for (SVal V : Vs)
    State->scanReachableSymbols(V, CallBack);

  return getCheckerManager().runCheckersForPointerEscape(
      State, CallBack.getSymbols(), Call, K, nullptr);
}

void ExprEngine::Visit(const Stmt *S, ExplodedNode *Pred,
                       ExplodedNodeSet &DstTop) {
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                S->getBeginLoc(), "Error evaluating statement");
  ExplodedNodeSet Dst;
  StmtNodeBuilder Bldr(Pred, DstTop, *currBldrCtx);

  assert(!isa<Expr>(S) || S == cast<Expr>(S)->IgnoreParens());

  switch (S->getStmtClass()) {
    // C++, OpenMP and ARC stuff we don't support yet.
    case Stmt::CXXDependentScopeMemberExprClass:
    case Stmt::CXXTryStmtClass:
    case Stmt::CXXTypeidExprClass:
    case Stmt::CXXUuidofExprClass:
    case Stmt::CXXFoldExprClass:
    case Stmt::MSPropertyRefExprClass:
    case Stmt::MSPropertySubscriptExprClass:
    case Stmt::CXXUnresolvedConstructExprClass:
    case Stmt::DependentScopeDeclRefExprClass:
    case Stmt::ArrayTypeTraitExprClass:
    case Stmt::ExpressionTraitExprClass:
    case Stmt::UnresolvedLookupExprClass:
    case Stmt::UnresolvedMemberExprClass:
    case Stmt::TypoExprClass:
    case Stmt::RecoveryExprClass:
    case Stmt::CXXNoexceptExprClass:
    case Stmt::PackExpansionExprClass:
    case Stmt::PackIndexingExprClass:
    case Stmt::SubstNonTypeTemplateParmPackExprClass:
    case Stmt::FunctionParmPackExprClass:
    case Stmt::CoroutineBodyStmtClass:
    case Stmt::CoawaitExprClass:
    case Stmt::DependentCoawaitExprClass:
    case Stmt::CoreturnStmtClass:
    case Stmt::CoyieldExprClass:
    case Stmt::SEHTryStmtClass:
    case Stmt::SEHExceptStmtClass:
    case Stmt::SEHLeaveStmtClass:
    case Stmt::SEHFinallyStmtClass:
    case Stmt::OMPCanonicalLoopClass:
    case Stmt::OMPParallelDirectiveClass:
    case Stmt::OMPSimdDirectiveClass:
    case Stmt::OMPForDirectiveClass:
    case Stmt::OMPForSimdDirectiveClass:
    case Stmt::OMPSectionsDirectiveClass:
    case Stmt::OMPSectionDirectiveClass:
    case Stmt::OMPScopeDirectiveClass:
    case Stmt::OMPSingleDirectiveClass:
    case Stmt::OMPMasterDirectiveClass:
    case Stmt::OMPCriticalDirectiveClass:
    case Stmt::OMPParallelForDirectiveClass:
    case Stmt::OMPParallelForSimdDirectiveClass:
    case Stmt::OMPParallelSectionsDirectiveClass:
    case Stmt::OMPParallelMasterDirectiveClass:
    case Stmt::OMPParallelMaskedDirectiveClass:
    case Stmt::OMPTaskDirectiveClass:
    case Stmt::OMPTaskyieldDirectiveClass:
    case Stmt::OMPBarrierDirectiveClass:
    case Stmt::OMPTaskwaitDirectiveClass:
    case Stmt::OMPErrorDirectiveClass:
    case Stmt::OMPTaskgroupDirectiveClass:
    case Stmt::OMPFlushDirectiveClass:
    case Stmt::OMPDepobjDirectiveClass:
    case Stmt::OMPScanDirectiveClass:
    case Stmt::OMPOrderedDirectiveClass:
    case Stmt::OMPAtomicDirectiveClass:
    case Stmt::OMPTargetDirectiveClass:
    case Stmt::OMPTargetDataDirectiveClass:
    case Stmt::OMPTargetEnterDataDirectiveClass:
    case Stmt::OMPTargetExitDataDirectiveClass:
    case Stmt::OMPTargetParallelDirectiveClass:
    case Stmt::OMPTargetParallelForDirectiveClass:
    case Stmt::OMPTargetUpdateDirectiveClass:
    case Stmt::OMPTeamsDirectiveClass:
    case Stmt::OMPCancellationPointDirectiveClass:
    case Stmt::OMPCancelDirectiveClass:
    case Stmt::OMPTaskLoopDirectiveClass:
    case Stmt::OMPTaskLoopSimdDirectiveClass:
    case Stmt::OMPMasterTaskLoopDirectiveClass:
    case Stmt::OMPMaskedTaskLoopDirectiveClass:
    case Stmt::OMPMasterTaskLoopSimdDirectiveClass:
    case Stmt::OMPMaskedTaskLoopSimdDirectiveClass:
    case Stmt::OMPParallelMasterTaskLoopDirectiveClass:
    case Stmt::OMPParallelMaskedTaskLoopDirectiveClass:
    case Stmt::OMPParallelMasterTaskLoopSimdDirectiveClass:
    case Stmt::OMPParallelMaskedTaskLoopSimdDirectiveClass:
    case Stmt::OMPDistributeDirectiveClass:
    case Stmt::OMPDistributeParallelForDirectiveClass:
    case Stmt::OMPDistributeParallelForSimdDirectiveClass:
    case Stmt::OMPDistributeSimdDirectiveClass:
    case Stmt::OMPTargetParallelForSimdDirectiveClass:
    case Stmt::OMPTargetSimdDirectiveClass:
    case Stmt::OMPTeamsDistributeDirectiveClass:
    case Stmt::OMPTeamsDistributeSimdDirectiveClass:
    case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
    case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
    case Stmt::OMPTargetTeamsDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
    case Stmt::OMPReverseDirectiveClass:
    case Stmt::OMPTileDirectiveClass:
    case Stmt::OMPInterchangeDirectiveClass:
    case Stmt::OMPInteropDirectiveClass:
    case Stmt::OMPDispatchDirectiveClass:
    case Stmt::OMPMaskedDirectiveClass:
    case Stmt::OMPGenericLoopDirectiveClass:
    case Stmt::OMPTeamsGenericLoopDirectiveClass:
    case Stmt::OMPTargetTeamsGenericLoopDirectiveClass:
    case Stmt::OMPParallelGenericLoopDirectiveClass:
    case Stmt::OMPTargetParallelGenericLoopDirectiveClass:
    case Stmt::CapturedStmtClass:
    case Stmt::OpenACCComputeConstructClass:
    case Stmt::OpenACCLoopConstructClass:
    case Stmt::OMPUnrollDirectiveClass:
    case Stmt::OMPMetaDirectiveClass: {
      const ExplodedNode *node = Bldr.generateSink(S, Pred, Pred->getState());
      Engine.addAbortedBlock(node, currBldrCtx->getBlock());
      break;
    }

    case Stmt::ParenExprClass:
      llvm_unreachable("ParenExprs already handled.");
    case Stmt::GenericSelectionExprClass:
      llvm_unreachable("GenericSelectionExprs already handled.");
    // Cases that should never be evaluated simply because they shouldn't
    // appear in the CFG.
    case Stmt::BreakStmtClass:
    case Stmt::CaseStmtClass:
    case Stmt::CompoundStmtClass:
    case Stmt::ContinueStmtClass:
    case Stmt::CXXForRangeStmtClass:
    case Stmt::DefaultStmtClass:
    case Stmt::DoStmtClass:
    case Stmt::ForStmtClass:
    case Stmt::GotoStmtClass:
    case Stmt::IfStmtClass:
    case Stmt::IndirectGotoStmtClass:
    case Stmt::LabelStmtClass:
    case Stmt::NoStmtClass:
    case Stmt::NullStmtClass:
    case Stmt::SwitchStmtClass:
    case Stmt::WhileStmtClass:
    case Expr::MSDependentExistsStmtClass:
      llvm_unreachable("Stmt should not be in analyzer evaluation loop");
    case Stmt::ImplicitValueInitExprClass:
      // These nodes are shared in the CFG and would case caching out.
      // Moreover, no additional evaluation required for them, the
      // analyzer can reconstruct these values from the AST.
      llvm_unreachable("Should be pruned from CFG");

    case Stmt::ObjCSubscriptRefExprClass:
    case Stmt::ObjCPropertyRefExprClass:
      llvm_unreachable("These are handled by PseudoObjectExpr");

    case Stmt::GNUNullExprClass: {
      // GNU __null is a pointer-width integer, not an actual pointer.
      ProgramStateRef state = Pred->getState();
      state = state->BindExpr(
          S, Pred->getLocationContext(),
          svalBuilder.makeIntValWithWidth(getContext().VoidPtrTy, 0));
      Bldr.generateNode(S, Pred, state);
      break;
    }

    case Stmt::ObjCAtSynchronizedStmtClass:
      Bldr.takeNodes(Pred);
      VisitObjCAtSynchronizedStmt(cast<ObjCAtSynchronizedStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Expr::ConstantExprClass:
    case Stmt::ExprWithCleanupsClass:
      // Handled due to fully linearised CFG.
      break;

    case Stmt::CXXBindTemporaryExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);
      ExplodedNodeSet Next;
      VisitCXXBindTemporaryExpr(cast<CXXBindTemporaryExpr>(S), PreVisit, Next);
      getCheckerManager().runCheckersForPostStmt(Dst, Next, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::ArrayInitLoopExprClass:
      Bldr.takeNodes(Pred);
      VisitArrayInitLoopExpr(cast<ArrayInitLoopExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    // Cases not handled yet; but will handle some day.
    case Stmt::DesignatedInitExprClass:
    case Stmt::DesignatedInitUpdateExprClass:
    case Stmt::ArrayInitIndexExprClass:
    case Stmt::ExtVectorElementExprClass:
    case Stmt::ImaginaryLiteralClass:
    case Stmt::ObjCAtCatchStmtClass:
    case Stmt::ObjCAtFinallyStmtClass:
    case Stmt::ObjCAtTryStmtClass:
    case Stmt::ObjCAutoreleasePoolStmtClass:
    case Stmt::ObjCEncodeExprClass:
    case Stmt::ObjCIsaExprClass:
    case Stmt::ObjCProtocolExprClass:
    case Stmt::ObjCSelectorExprClass:
    case Stmt::ParenListExprClass:
    case Stmt::ShuffleVectorExprClass:
    case Stmt::ConvertVectorExprClass:
    case Stmt::VAArgExprClass:
    case Stmt::CUDAKernelCallExprClass:
    case Stmt::OpaqueValueExprClass:
    case Stmt::AsTypeExprClass:
    case Stmt::ConceptSpecializationExprClass:
    case Stmt::CXXRewrittenBinaryOperatorClass:
    case Stmt::RequiresExprClass:
    case Expr::CXXParenListInitExprClass:
    case Stmt::EmbedExprClass:
      // Fall through.

    // Cases we intentionally don't evaluate, since they don't need
    // to be explicitly evaluated.
    case Stmt::PredefinedExprClass:
    case Stmt::AddrLabelExprClass:
    case Stmt::AttributedStmtClass:
    case Stmt::IntegerLiteralClass:
    case Stmt::FixedPointLiteralClass:
    case Stmt::CharacterLiteralClass:
    case Stmt::CXXScalarValueInitExprClass:
    case Stmt::CXXBoolLiteralExprClass:
    case Stmt::ObjCBoolLiteralExprClass:
    case Stmt::ObjCAvailabilityCheckExprClass:
    case Stmt::FloatingLiteralClass:
    case Stmt::NoInitExprClass:
    case Stmt::SizeOfPackExprClass:
    case Stmt::StringLiteralClass:
    case Stmt::SourceLocExprClass:
    case Stmt::ObjCStringLiteralClass:
    case Stmt::CXXPseudoDestructorExprClass:
    case Stmt::SubstNonTypeTemplateParmExprClass:
    case Stmt::CXXNullPtrLiteralExprClass:
    case Stmt::ArraySectionExprClass:
    case Stmt::OMPArrayShapingExprClass:
    case Stmt::OMPIteratorExprClass:
    case Stmt::SYCLUniqueStableNameExprClass:
    case Stmt::TypeTraitExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet preVisit;
      getCheckerManager().runCheckersForPreStmt(preVisit, Pred, S, *this);
      getCheckerManager().runCheckersForPostStmt(Dst, preVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXDefaultArgExprClass:
    case Stmt::CXXDefaultInitExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      ExplodedNodeSet Tmp;
      StmtNodeBuilder Bldr2(PreVisit, Tmp, *currBldrCtx);

      const Expr *ArgE;
      if (const auto *DefE = dyn_cast<CXXDefaultArgExpr>(S))
        ArgE = DefE->getExpr();
      else if (const auto *DefE = dyn_cast<CXXDefaultInitExpr>(S))
        ArgE = DefE->getExpr();
      else
        llvm_unreachable("unknown constant wrapper kind");

      bool IsTemporary = false;
      if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(ArgE)) {
        ArgE = MTE->getSubExpr();
        IsTemporary = true;
      }

      std::optional<SVal> ConstantVal = svalBuilder.getConstantVal(ArgE);
      if (!ConstantVal)
        ConstantVal = UnknownVal();

      const LocationContext *LCtx = Pred->getLocationContext();
      for (const auto I : PreVisit) {
        ProgramStateRef State = I->getState();
        State = State->BindExpr(S, LCtx, *ConstantVal);
        if (IsTemporary)
          State = createTemporaryRegionIfNeeded(State, LCtx,
                                                cast<Expr>(S),
                                                cast<Expr>(S));
        Bldr2.generateNode(S, I, State);
      }

      getCheckerManager().runCheckersForPostStmt(Dst, Tmp, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    // Cases we evaluate as opaque expressions, conjuring a symbol.
    case Stmt::CXXStdInitializerListExprClass:
    case Expr::ObjCArrayLiteralClass:
    case Expr::ObjCDictionaryLiteralClass:
    case Expr::ObjCBoxedExprClass: {
      Bldr.takeNodes(Pred);

      ExplodedNodeSet preVisit;
      getCheckerManager().runCheckersForPreStmt(preVisit, Pred, S, *this);

      ExplodedNodeSet Tmp;
      StmtNodeBuilder Bldr2(preVisit, Tmp, *currBldrCtx);

      const auto *Ex = cast<Expr>(S);
      QualType resultType = Ex->getType();

      for (const auto N : preVisit) {
        const LocationContext *LCtx = N->getLocationContext();
        SVal result = svalBuilder.conjureSymbolVal(nullptr, Ex, LCtx,
                                                   resultType,
                                                   currBldrCtx->blockCount());
        ProgramStateRef State = N->getState()->BindExpr(Ex, LCtx, result);

        // Escape pointers passed into the list, unless it's an ObjC boxed
        // expression which is not a boxable C structure.
        if (!(isa<ObjCBoxedExpr>(Ex) &&
              !cast<ObjCBoxedExpr>(Ex)->getSubExpr()
                                      ->getType()->isRecordType()))
          for (auto Child : Ex->children()) {
            assert(Child);
            SVal Val = State->getSVal(Child, LCtx);
            State = escapeValues(State, Val, PSK_EscapeOther);
          }

        Bldr2.generateNode(S, N, State);
      }

      getCheckerManager().runCheckersForPostStmt(Dst, Tmp, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::ArraySubscriptExprClass:
      Bldr.takeNodes(Pred);
      VisitArraySubscriptExpr(cast<ArraySubscriptExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::MatrixSubscriptExprClass:
      llvm_unreachable("Support for MatrixSubscriptExpr is not implemented.");
      break;

    case Stmt::GCCAsmStmtClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);
      ExplodedNodeSet PostVisit;
      for (ExplodedNode *const N : PreVisit)
        VisitGCCAsmStmt(cast<GCCAsmStmt>(S), N, PostVisit);
      getCheckerManager().runCheckersForPostStmt(Dst, PostVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::MSAsmStmtClass:
      Bldr.takeNodes(Pred);
      VisitMSAsmStmt(cast<MSAsmStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::BlockExprClass:
      Bldr.takeNodes(Pred);
      VisitBlockExpr(cast<BlockExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::LambdaExprClass:
      if (AMgr.options.ShouldInlineLambdas) {
        Bldr.takeNodes(Pred);
        VisitLambdaExpr(cast<LambdaExpr>(S), Pred, Dst);
        Bldr.addNodes(Dst);
      } else {
        const ExplodedNode *node = Bldr.generateSink(S, Pred, Pred->getState());
        Engine.addAbortedBlock(node, currBldrCtx->getBlock());
      }
      break;

    case Stmt::BinaryOperatorClass: {
      const auto *B = cast<BinaryOperator>(S);
      if (B->isLogicalOp()) {
        Bldr.takeNodes(Pred);
        VisitLogicalExpr(B, Pred, Dst);
        Bldr.addNodes(Dst);
        break;
      }
      else if (B->getOpcode() == BO_Comma) {
        ProgramStateRef state = Pred->getState();
        Bldr.generateNode(B, Pred,
                          state->BindExpr(B, Pred->getLocationContext(),
                                          state->getSVal(B->getRHS(),
                                                  Pred->getLocationContext())));
        break;
      }

      Bldr.takeNodes(Pred);

      if (AMgr.options.ShouldEagerlyAssume &&
          (B->isRelationalOp() || B->isEqualityOp())) {
        ExplodedNodeSet Tmp;
        VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Tmp);
        evalEagerlyAssumeBinOpBifurcation(Dst, Tmp, cast<Expr>(S));
      }
      else
        VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);

      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXOperatorCallExprClass: {
      const auto *OCE = cast<CXXOperatorCallExpr>(S);

      // For instance method operators, make sure the 'this' argument has a
      // valid region.
      const Decl *Callee = OCE->getCalleeDecl();
      if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(Callee)) {
        if (MD->isImplicitObjectMemberFunction()) {
          ProgramStateRef State = Pred->getState();
          const LocationContext *LCtx = Pred->getLocationContext();
          ProgramStateRef NewState =
            createTemporaryRegionIfNeeded(State, LCtx, OCE->getArg(0));
          if (NewState != State) {
            Pred = Bldr.generateNode(OCE, Pred, NewState, /*tag=*/nullptr,
                                     ProgramPoint::PreStmtKind);
            // Did we cache out?
            if (!Pred)
              break;
          }
        }
      }
      [[fallthrough]];
    }

    case Stmt::CallExprClass:
    case Stmt::CXXMemberCallExprClass:
    case Stmt::UserDefinedLiteralClass:
      Bldr.takeNodes(Pred);
      VisitCallExpr(cast<CallExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXCatchStmtClass:
      Bldr.takeNodes(Pred);
      VisitCXXCatchStmt(cast<CXXCatchStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXTemporaryObjectExprClass:
    case Stmt::CXXConstructExprClass:
      Bldr.takeNodes(Pred);
      VisitCXXConstructExpr(cast<CXXConstructExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXInheritedCtorInitExprClass:
      Bldr.takeNodes(Pred);
      VisitCXXInheritedCtorInitExpr(cast<CXXInheritedCtorInitExpr>(S), Pred,
                                    Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXNewExprClass: {
      Bldr.takeNodes(Pred);

      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      ExplodedNodeSet PostVisit;
      for (const auto i : PreVisit)
        VisitCXXNewExpr(cast<CXXNewExpr>(S), i, PostVisit);

      getCheckerManager().runCheckersForPostStmt(Dst, PostVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXDeleteExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      const auto *CDE = cast<CXXDeleteExpr>(S);
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);
      ExplodedNodeSet PostVisit;
      getCheckerManager().runCheckersForPostStmt(PostVisit, PreVisit, S, *this);

      for (const auto i : PostVisit)
        VisitCXXDeleteExpr(CDE, i, Dst);

      Bldr.addNodes(Dst);
      break;
    }
      // FIXME: ChooseExpr is really a constant.  We need to fix
      //        the CFG do not model them as explicit control-flow.

    case Stmt::ChooseExprClass: { // __builtin_choose_expr
      Bldr.takeNodes(Pred);
      const auto *C = cast<ChooseExpr>(S);
      VisitGuardedExpr(C, C->getLHS(), C->getRHS(), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CompoundAssignOperatorClass:
      Bldr.takeNodes(Pred);
      VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CompoundLiteralExprClass:
      Bldr.takeNodes(Pred);
      VisitCompoundLiteralExpr(cast<CompoundLiteralExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass: { // '?' operator
      Bldr.takeNodes(Pred);
      const auto *C = cast<AbstractConditionalOperator>(S);
      VisitGuardedExpr(C, C->getTrueExpr(), C->getFalseExpr(), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXThisExprClass:
      Bldr.takeNodes(Pred);
      VisitCXXThisExpr(cast<CXXThisExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::DeclRefExprClass: {
      Bldr.takeNodes(Pred);
      const auto *DE = cast<DeclRefExpr>(S);
      VisitCommonDeclRefExpr(DE, DE->getDecl(), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::DeclStmtClass:
      Bldr.takeNodes(Pred);
      VisitDeclStmt(cast<DeclStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ImplicitCastExprClass:
    case Stmt::CStyleCastExprClass:
    case Stmt::CXXStaticCastExprClass:
    case Stmt::CXXDynamicCastExprClass:
    case Stmt::CXXReinterpretCastExprClass:
    case Stmt::CXXConstCastExprClass:
    case Stmt::CXXFunctionalCastExprClass:
    case Stmt::BuiltinBitCastExprClass:
    case Stmt::ObjCBridgedCastExprClass:
    case Stmt::CXXAddrspaceCastExprClass: {
      Bldr.takeNodes(Pred);
      const auto *C = cast<CastExpr>(S);
      ExplodedNodeSet dstExpr;
      VisitCast(C, C->getSubExpr(), Pred, dstExpr);

      // Handle the postvisit checks.
      getCheckerManager().runCheckersForPostStmt(Dst, dstExpr, C, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Expr::MaterializeTemporaryExprClass: {
      Bldr.takeNodes(Pred);
      const auto *MTE = cast<MaterializeTemporaryExpr>(S);
      ExplodedNodeSet dstPrevisit;
      getCheckerManager().runCheckersForPreStmt(dstPrevisit, Pred, MTE, *this);
      ExplodedNodeSet dstExpr;
      for (const auto i : dstPrevisit)
        CreateCXXTemporaryObject(MTE, i, dstExpr);
      getCheckerManager().runCheckersForPostStmt(Dst, dstExpr, MTE, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::InitListExprClass:
      Bldr.takeNodes(Pred);
      VisitInitListExpr(cast<InitListExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::MemberExprClass:
      Bldr.takeNodes(Pred);
      VisitMemberExpr(cast<MemberExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::AtomicExprClass:
      Bldr.takeNodes(Pred);
      VisitAtomicExpr(cast<AtomicExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCIvarRefExprClass:
      Bldr.takeNodes(Pred);
      VisitLvalObjCIvarRefExpr(cast<ObjCIvarRefExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCForCollectionStmtClass:
      Bldr.takeNodes(Pred);
      VisitObjCForCollectionStmt(cast<ObjCForCollectionStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCMessageExprClass:
      Bldr.takeNodes(Pred);
      VisitObjCMessage(cast<ObjCMessageExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCAtThrowStmtClass:
    case Stmt::CXXThrowExprClass:
      // FIXME: This is not complete.  We basically treat @throw as
      // an abort.
      Bldr.generateSink(S, Pred, Pred->getState());
      break;

    case Stmt::ReturnStmtClass:
      Bldr.takeNodes(Pred);
      VisitReturnStmt(cast<ReturnStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::OffsetOfExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      ExplodedNodeSet PostVisit;
      for (const auto Node : PreVisit)
        VisitOffsetOfExpr(cast<OffsetOfExpr>(S), Node, PostVisit);

      getCheckerManager().runCheckersForPostStmt(Dst, PostVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::UnaryExprOrTypeTraitExprClass:
      Bldr.takeNodes(Pred);
      VisitUnaryExprOrTypeTraitExpr(cast<UnaryExprOrTypeTraitExpr>(S),
                                    Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::StmtExprClass: {
      const auto *SE = cast<StmtExpr>(S);

      if (SE->getSubStmt()->body_empty()) {
        // Empty statement expression.
        assert(SE->getType() == getContext().VoidTy
               && "Empty statement expression must have void type.");
        break;
      }

      if (const auto *LastExpr =
              dyn_cast<Expr>(*SE->getSubStmt()->body_rbegin())) {
        ProgramStateRef state = Pred->getState();
        Bldr.generateNode(SE, Pred,
                          state->BindExpr(SE, Pred->getLocationContext(),
                                          state->getSVal(LastExpr,
                                                  Pred->getLocationContext())));
      }
      break;
    }

    case Stmt::UnaryOperatorClass: {
      Bldr.takeNodes(Pred);
      const auto *U = cast<UnaryOperator>(S);
      if (AMgr.options.ShouldEagerlyAssume && (U->getOpcode() == UO_LNot)) {
        ExplodedNodeSet Tmp;
        VisitUnaryOperator(U, Pred, Tmp);
        evalEagerlyAssumeBinOpBifurcation(Dst, Tmp, U);
      }
      else
        VisitUnaryOperator(U, Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::PseudoObjectExprClass: {
      Bldr.takeNodes(Pred);
      ProgramStateRef state = Pred->getState();
      const auto *PE = cast<PseudoObjectExpr>(S);
      if (const Expr *Result = PE->getResultExpr()) {
        SVal V = state->getSVal(Result, Pred->getLocationContext());
        Bldr.generateNode(S, Pred,
                          state->BindExpr(S, Pred->getLocationContext(), V));
      }
      else
        Bldr.generateNode(S, Pred,
                          state->BindExpr(S, Pred->getLocationContext(),
                                                   UnknownVal()));

      Bldr.addNodes(Dst);
      break;
    }

    case Expr::ObjCIndirectCopyRestoreExprClass: {
      // ObjCIndirectCopyRestoreExpr implies passing a temporary for
      // correctness of lifetime management.  Due to limited analysis
      // of ARC, this is implemented as direct arg passing.
      Bldr.takeNodes(Pred);
      ProgramStateRef state = Pred->getState();
      const auto *OIE = cast<ObjCIndirectCopyRestoreExpr>(S);
      const Expr *E = OIE->getSubExpr();
      SVal V = state->getSVal(E, Pred->getLocationContext());
      Bldr.generateNode(S, Pred,
              state->BindExpr(S, Pred->getLocationContext(), V));
      Bldr.addNodes(Dst);
      break;
    }
  }
}

bool ExprEngine::replayWithoutInlining(ExplodedNode *N,
                                       const LocationContext *CalleeLC) {
  const StackFrameContext *CalleeSF = CalleeLC->getStackFrame();
  const StackFrameContext *CallerSF = CalleeSF->getParent()->getStackFrame();
  assert(CalleeSF && CallerSF);
  ExplodedNode *BeforeProcessingCall = nullptr;
  const Stmt *CE = CalleeSF->getCallSite();

  // Find the first node before we started processing the call expression.
  while (N) {
    ProgramPoint L = N->getLocation();
    BeforeProcessingCall = N;
    N = N->pred_empty() ? nullptr : *(N->pred_begin());

    // Skip the nodes corresponding to the inlined code.
    if (L.getStackFrame() != CallerSF)
      continue;
    // We reached the caller. Find the node right before we started
    // processing the call.
    if (L.isPurgeKind())
      continue;
    if (L.getAs<PreImplicitCall>())
      continue;
    if (L.getAs<CallEnter>())
      continue;
    if (std::optional<StmtPoint> SP = L.getAs<StmtPoint>())
      if (SP->getStmt() == CE)
        continue;
    break;
  }

  if (!BeforeProcessingCall)
    return false;

  // TODO: Clean up the unneeded nodes.

  // Build an Epsilon node from which we will restart the analyzes.
  // Note that CE is permitted to be NULL!
  static SimpleProgramPointTag PT("ExprEngine", "Replay without inlining");
  ProgramPoint NewNodeLoc = EpsilonPoint(
      BeforeProcessingCall->getLocationContext(), CE, nullptr, &PT);
  // Add the special flag to GDM to signal retrying with no inlining.
  // Note, changing the state ensures that we are not going to cache out.
  ProgramStateRef NewNodeState = BeforeProcessingCall->getState();
  NewNodeState =
    NewNodeState->set<ReplayWithoutInlining>(const_cast<Stmt *>(CE));

  // Make the new node a successor of BeforeProcessingCall.
  bool IsNew = false;
  ExplodedNode *NewNode = G.getNode(NewNodeLoc, NewNodeState, false, &IsNew);
  // We cached out at this point. Caching out is common due to us backtracking
  // from the inlined function, which might spawn several paths.
  if (!IsNew)
    return true;

  NewNode->addPredecessor(BeforeProcessingCall, G);

  // Add the new node to the work list.
  Engine.enqueueStmtNode(NewNode, CalleeSF->getCallSiteBlock(),
                                  CalleeSF->getIndex());
  NumTimesRetriedWithoutInlining++;
  return true;
}

/// Block entrance.  (Update counters).
void ExprEngine::processCFGBlockEntrance(const BlockEdge &L,
                                         NodeBuilderWithSinks &nodeBuilder,
                                         ExplodedNode *Pred) {
  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  // If we reach a loop which has a known bound (and meets
  // other constraints) then consider completely unrolling it.
  if(AMgr.options.ShouldUnrollLoops) {
    unsigned maxBlockVisitOnPath = AMgr.options.maxBlockVisitOnPath;
    const Stmt *Term = nodeBuilder.getContext().getBlock()->getTerminatorStmt();
    if (Term) {
      ProgramStateRef NewState = updateLoopStack(Term, AMgr.getASTContext(),
                                                 Pred, maxBlockVisitOnPath);
      if (NewState != Pred->getState()) {
        ExplodedNode *UpdatedNode = nodeBuilder.generateNode(NewState, Pred);
        if (!UpdatedNode)
          return;
        Pred = UpdatedNode;
      }
    }
    // Is we are inside an unrolled loop then no need the check the counters.
    if(isUnrolledState(Pred->getState()))
      return;
  }

  // If this block is terminated by a loop and it has already been visited the
  // maximum number of times, widen the loop.
  unsigned int BlockCount = nodeBuilder.getContext().blockCount();
  if (BlockCount == AMgr.options.maxBlockVisitOnPath - 1 &&
      AMgr.options.ShouldWidenLoops) {
    const Stmt *Term = nodeBuilder.getContext().getBlock()->getTerminatorStmt();
    if (!isa_and_nonnull<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(Term))
      return;
    // Widen.
    const LocationContext *LCtx = Pred->getLocationContext();
    ProgramStateRef WidenedState =
        getWidenedLoopState(Pred->getState(), LCtx, BlockCount, Term);
    nodeBuilder.generateNode(WidenedState, Pred);
    return;
  }

  // FIXME: Refactor this into a checker.
  if (BlockCount >= AMgr.options.maxBlockVisitOnPath) {
    static SimpleProgramPointTag tag(TagProviderName, "Block count exceeded");
    const ExplodedNode *Sink =
                   nodeBuilder.generateSink(Pred->getState(), Pred, &tag);

    // Check if we stopped at the top level function or not.
    // Root node should have the location context of the top most function.
    const LocationContext *CalleeLC = Pred->getLocation().getLocationContext();
    const LocationContext *CalleeSF = CalleeLC->getStackFrame();
    const LocationContext *RootLC =
                        (*G.roots_begin())->getLocation().getLocationContext();
    if (RootLC->getStackFrame() != CalleeSF) {
      Engine.FunctionSummaries->markReachedMaxBlockCount(CalleeSF->getDecl());

      // Re-run the call evaluation without inlining it, by storing the
      // no-inlining policy in the state and enqueuing the new work item on
      // the list. Replay should almost never fail. Use the stats to catch it
      // if it does.
      if ((!AMgr.options.NoRetryExhausted &&
           replayWithoutInlining(Pred, CalleeLC)))
        return;
      NumMaxBlockCountReachedInInlined++;
    } else
      NumMaxBlockCountReached++;

    // Make sink nodes as exhausted(for stats) only if retry failed.
    Engine.blocksExhausted.push_back(std::make_pair(L, Sink));
  }
}

//===----------------------------------------------------------------------===//
// Branch processing.
//===----------------------------------------------------------------------===//

/// RecoverCastedSymbol - A helper function for ProcessBranch that is used
/// to try to recover some path-sensitivity for casts of symbolic
/// integers that promote their values (which are currently not tracked well).
/// This function returns the SVal bound to Condition->IgnoreCasts if all the
//  cast(s) did was sign-extend the original value.
static SVal RecoverCastedSymbol(ProgramStateRef state,
                                const Stmt *Condition,
                                const LocationContext *LCtx,
                                ASTContext &Ctx) {

  const auto *Ex = dyn_cast<Expr>(Condition);
  if (!Ex)
    return UnknownVal();

  uint64_t bits = 0;
  bool bitsInit = false;

  while (const auto *CE = dyn_cast<CastExpr>(Ex)) {
    QualType T = CE->getType();

    if (!T->isIntegralOrEnumerationType())
      return UnknownVal();

    uint64_t newBits = Ctx.getTypeSize(T);
    if (!bitsInit || newBits < bits) {
      bitsInit = true;
      bits = newBits;
    }

    Ex = CE->getSubExpr();
  }

  // We reached a non-cast.  Is it a symbolic value?
  QualType T = Ex->getType();

  if (!bitsInit || !T->isIntegralOrEnumerationType() ||
      Ctx.getTypeSize(T) > bits)
    return UnknownVal();

  return state->getSVal(Ex, LCtx);
}

#ifndef NDEBUG
static const Stmt *getRightmostLeaf(const Stmt *Condition) {
  while (Condition) {
    const auto *BO = dyn_cast<BinaryOperator>(Condition);
    if (!BO || !BO->isLogicalOp()) {
      return Condition;
    }
    Condition = BO->getRHS()->IgnoreParens();
  }
  return nullptr;
}
#endif

// Returns the condition the branch at the end of 'B' depends on and whose value
// has been evaluated within 'B'.
// In most cases, the terminator condition of 'B' will be evaluated fully in
// the last statement of 'B'; in those cases, the resolved condition is the
// given 'Condition'.
// If the condition of the branch is a logical binary operator tree, the CFG is
// optimized: in that case, we know that the expression formed by all but the
// rightmost leaf of the logical binary operator tree must be true, and thus
// the branch condition is at this point equivalent to the truth value of that
// rightmost leaf; the CFG block thus only evaluates this rightmost leaf
// expression in its final statement. As the full condition in that case was
// not evaluated, and is thus not in the SVal cache, we need to use that leaf
// expression to evaluate the truth value of the condition in the current state
// space.
static const Stmt *ResolveCondition(const Stmt *Condition,
                                    const CFGBlock *B) {
  if (const auto *Ex = dyn_cast<Expr>(Condition))
    Condition = Ex->IgnoreParens();

  const auto *BO = dyn_cast<BinaryOperator>(Condition);
  if (!BO || !BO->isLogicalOp())
    return Condition;

  assert(B->getTerminator().isStmtBranch() &&
         "Other kinds of branches are handled separately!");

  // For logical operations, we still have the case where some branches
  // use the traditional "merge" approach and others sink the branch
  // directly into the basic blocks representing the logical operation.
  // We need to distinguish between those two cases here.

  // The invariants are still shifting, but it is possible that the
  // last element in a CFGBlock is not a CFGStmt.  Look for the last
  // CFGStmt as the value of the condition.
  for (CFGElement Elem : llvm::reverse(*B)) {
    std::optional<CFGStmt> CS = Elem.getAs<CFGStmt>();
    if (!CS)
      continue;
    const Stmt *LastStmt = CS->getStmt();
    assert(LastStmt == Condition || LastStmt == getRightmostLeaf(Condition));
    return LastStmt;
  }
  llvm_unreachable("could not resolve condition");
}

using ObjCForLctxPair =
    std::pair<const ObjCForCollectionStmt *, const LocationContext *>;

REGISTER_MAP_WITH_PROGRAMSTATE(ObjCForHasMoreIterations, ObjCForLctxPair, bool)

ProgramStateRef ExprEngine::setWhetherHasMoreIteration(
    ProgramStateRef State, const ObjCForCollectionStmt *O,
    const LocationContext *LC, bool HasMoreIteraton) {
  assert(!State->contains<ObjCForHasMoreIterations>({O, LC}));
  return State->set<ObjCForHasMoreIterations>({O, LC}, HasMoreIteraton);
}

ProgramStateRef
ExprEngine::removeIterationState(ProgramStateRef State,
                                 const ObjCForCollectionStmt *O,
                                 const LocationContext *LC) {
  assert(State->contains<ObjCForHasMoreIterations>({O, LC}));
  return State->remove<ObjCForHasMoreIterations>({O, LC});
}

bool ExprEngine::hasMoreIteration(ProgramStateRef State,
                                  const ObjCForCollectionStmt *O,
                                  const LocationContext *LC) {
  assert(State->contains<ObjCForHasMoreIterations>({O, LC}));
  return *State->get<ObjCForHasMoreIterations>({O, LC});
}

/// Split the state on whether there are any more iterations left for this loop.
/// Returns a (HasMoreIteration, HasNoMoreIteration) pair, or std::nullopt when
/// the acquisition of the loop condition value failed.
static std::optional<std::pair<ProgramStateRef, ProgramStateRef>>
assumeCondition(const Stmt *Condition, ExplodedNode *N) {
  ProgramStateRef State = N->getState();
  if (const auto *ObjCFor = dyn_cast<ObjCForCollectionStmt>(Condition)) {
    bool HasMoreIteraton =
        ExprEngine::hasMoreIteration(State, ObjCFor, N->getLocationContext());
    // Checkers have already ran on branch conditions, so the current
    // information as to whether the loop has more iteration becomes outdated
    // after this point.
    State = ExprEngine::removeIterationState(State, ObjCFor,
                                             N->getLocationContext());
    if (HasMoreIteraton)
      return std::pair<ProgramStateRef, ProgramStateRef>{State, nullptr};
    else
      return std::pair<ProgramStateRef, ProgramStateRef>{nullptr, State};
  }
  SVal X = State->getSVal(Condition, N->getLocationContext());

  if (X.isUnknownOrUndef()) {
    // Give it a chance to recover from unknown.
    if (const auto *Ex = dyn_cast<Expr>(Condition)) {
      if (Ex->getType()->isIntegralOrEnumerationType()) {
        // Try to recover some path-sensitivity.  Right now casts of symbolic
        // integers that promote their values are currently not tracked well.
        // If 'Condition' is such an expression, try and recover the
        // underlying value and use that instead.
        SVal recovered =
            RecoverCastedSymbol(State, Condition, N->getLocationContext(),
                                N->getState()->getStateManager().getContext());

        if (!recovered.isUnknown()) {
          X = recovered;
        }
      }
    }
  }

  // If the condition is still unknown, give up.
  if (X.isUnknownOrUndef())
    return std::nullopt;

  DefinedSVal V = X.castAs<DefinedSVal>();

  ProgramStateRef StTrue, StFalse;
  return State->assume(V);
}

void ExprEngine::processBranch(const Stmt *Condition,
                               NodeBuilderContext& BldCtx,
                               ExplodedNode *Pred,
                               ExplodedNodeSet &Dst,
                               const CFGBlock *DstT,
                               const CFGBlock *DstF) {
  assert((!Condition || !isa<CXXBindTemporaryExpr>(Condition)) &&
         "CXXBindTemporaryExprs are handled by processBindTemporary.");
  const LocationContext *LCtx = Pred->getLocationContext();
  PrettyStackTraceLocationContext StackCrashInfo(LCtx);
  currBldrCtx = &BldCtx;

  // Check for NULL conditions; e.g. "for(;;)"
  if (!Condition) {
    BranchNodeBuilder NullCondBldr(Pred, Dst, BldCtx, DstT, DstF);
    NullCondBldr.markInfeasible(false);
    NullCondBldr.generateNode(Pred->getState(), true, Pred);
    return;
  }

  if (const auto *Ex = dyn_cast<Expr>(Condition))
    Condition = Ex->IgnoreParens();

  Condition = ResolveCondition(Condition, BldCtx.getBlock());
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                Condition->getBeginLoc(),
                                "Error evaluating branch");

  ExplodedNodeSet CheckersOutSet;
  getCheckerManager().runCheckersForBranchCondition(Condition, CheckersOutSet,
                                                    Pred, *this);
  // We generated only sinks.
  if (CheckersOutSet.empty())
    return;

  BranchNodeBuilder builder(CheckersOutSet, Dst, BldCtx, DstT, DstF);
  for (ExplodedNode *PredN : CheckersOutSet) {
    if (PredN->isSink())
      continue;

    ProgramStateRef PrevState = PredN->getState();

    ProgramStateRef StTrue, StFalse;
    if (const auto KnownCondValueAssumption = assumeCondition(Condition, PredN))
      std::tie(StTrue, StFalse) = *KnownCondValueAssumption;
    else {
      assert(!isa<ObjCForCollectionStmt>(Condition));
      builder.generateNode(PrevState, true, PredN);
      builder.generateNode(PrevState, false, PredN);
      continue;
    }
    if (StTrue && StFalse)
      assert(!isa<ObjCForCollectionStmt>(Condition));

    // Process the true branch.
    if (builder.isFeasible(true)) {
      if (StTrue)
        builder.generateNode(StTrue, true, PredN);
      else
        builder.markInfeasible(true);
    }

    // Process the false branch.
    if (builder.isFeasible(false)) {
      if (StFalse)
        builder.generateNode(StFalse, false, PredN);
      else
        builder.markInfeasible(false);
    }
  }
  currBldrCtx = nullptr;
}

/// The GDM component containing the set of global variables which have been
/// previously initialized with explicit initializers.
REGISTER_TRAIT_WITH_PROGRAMSTATE(InitializedGlobalsSet,
                                 llvm::ImmutableSet<const VarDecl *>)

void ExprEngine::processStaticInitializer(const DeclStmt *DS,
                                          NodeBuilderContext &BuilderCtx,
                                          ExplodedNode *Pred,
                                          ExplodedNodeSet &Dst,
                                          const CFGBlock *DstT,
                                          const CFGBlock *DstF) {
  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  currBldrCtx = &BuilderCtx;

  const auto *VD = cast<VarDecl>(DS->getSingleDecl());
  ProgramStateRef state = Pred->getState();
  bool initHasRun = state->contains<InitializedGlobalsSet>(VD);
  BranchNodeBuilder builder(Pred, Dst, BuilderCtx, DstT, DstF);

  if (!initHasRun) {
    state = state->add<InitializedGlobalsSet>(VD);
  }

  builder.generateNode(state, initHasRun, Pred);
  builder.markInfeasible(!initHasRun);

  currBldrCtx = nullptr;
}

/// processIndirectGoto - Called by CoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a computed goto jump.
void ExprEngine::processIndirectGoto(IndirectGotoNodeBuilder &builder) {
  ProgramStateRef state = builder.getState();
  SVal V = state->getSVal(builder.getTarget(), builder.getLocationContext());

  // Three possibilities:
  //
  //   (1) We know the computed label.
  //   (2) The label is NULL (or some other constant), or Undefined.
  //   (3) We have no clue about the label.  Dispatch to all targets.
  //

  using iterator = IndirectGotoNodeBuilder::iterator;

  if (std::optional<loc::GotoLabel> LV = V.getAs<loc::GotoLabel>()) {
    const LabelDecl *L = LV->getLabel();

    for (iterator Succ : builder) {
      if (Succ.getLabel() == L) {
        builder.generateNode(Succ, state);
        return;
      }
    }

    llvm_unreachable("No block with label.");
  }

  if (isa<UndefinedVal, loc::ConcreteInt>(V)) {
    // Dispatch to the first target and mark it as a sink.
    //ExplodedNode* N = builder.generateNode(builder.begin(), state, true);
    // FIXME: add checker visit.
    //    UndefBranches.insert(N);
    return;
  }

  // This is really a catch-all.  We don't support symbolics yet.
  // FIXME: Implement dispatch for symbolic pointers.

  for (iterator Succ : builder)
    builder.generateNode(Succ, state);
}

void ExprEngine::processBeginOfFunction(NodeBuilderContext &BC,
                                        ExplodedNode *Pred,
                                        ExplodedNodeSet &Dst,
                                        const BlockEdge &L) {
  SaveAndRestore<const NodeBuilderContext *> NodeContextRAII(currBldrCtx, &BC);
  getCheckerManager().runCheckersForBeginFunction(Dst, L, Pred, *this);
}

/// ProcessEndPath - Called by CoreEngine.  Used to generate end-of-path
///  nodes when the control reaches the end of a function.
void ExprEngine::processEndOfFunction(NodeBuilderContext& BC,
                                      ExplodedNode *Pred,
                                      const ReturnStmt *RS) {
  ProgramStateRef State = Pred->getState();

  if (!Pred->getStackFrame()->inTopFrame())
    State = finishArgumentConstruction(
        State, *getStateManager().getCallEventManager().getCaller(
                   Pred->getStackFrame(), Pred->getState()));

  // FIXME: We currently cannot assert that temporaries are clear, because
  // lifetime extended temporaries are not always modelled correctly. In some
  // cases when we materialize the temporary, we do
  // createTemporaryRegionIfNeeded(), and the region changes, and also the
  // respective destructor becomes automatic from temporary. So for now clean up
  // the state manually before asserting. Ideally, this braced block of code
  // should go away.
  {
    const LocationContext *FromLC = Pred->getLocationContext();
    const LocationContext *ToLC = FromLC->getStackFrame()->getParent();
    const LocationContext *LC = FromLC;
    while (LC != ToLC) {
      assert(LC && "ToLC must be a parent of FromLC!");
      for (auto I : State->get<ObjectsUnderConstruction>())
        if (I.first.getLocationContext() == LC) {
          // The comment above only pardons us for not cleaning up a
          // temporary destructor. If any other statements are found here,
          // it must be a separate problem.
          assert(I.first.getItem().getKind() ==
                     ConstructionContextItem::TemporaryDestructorKind ||
                 I.first.getItem().getKind() ==
                     ConstructionContextItem::ElidedDestructorKind);
          State = State->remove<ObjectsUnderConstruction>(I.first);
        }
      LC = LC->getParent();
    }
  }

  // Perform the transition with cleanups.
  if (State != Pred->getState()) {
    ExplodedNodeSet PostCleanup;
    NodeBuilder Bldr(Pred, PostCleanup, BC);
    Pred = Bldr.generateNode(Pred->getLocation(), State, Pred);
    if (!Pred) {
      // The node with clean temporaries already exists. We might have reached
      // it on a path on which we initialize different temporaries.
      return;
    }
  }

  assert(areAllObjectsFullyConstructed(Pred->getState(),
                                       Pred->getLocationContext(),
                                       Pred->getStackFrame()->getParent()));

  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());

  ExplodedNodeSet Dst;
  if (Pred->getLocationContext()->inTopFrame()) {
    // Remove dead symbols.
    ExplodedNodeSet AfterRemovedDead;
    removeDeadOnEndOfFunction(BC, Pred, AfterRemovedDead);

    // Notify checkers.
    for (const auto I : AfterRemovedDead)
      getCheckerManager().runCheckersForEndFunction(BC, Dst, I, *this, RS);
  } else {
    getCheckerManager().runCheckersForEndFunction(BC, Dst, Pred, *this, RS);
  }

  Engine.enqueueEndOfFunction(Dst, RS);
}

/// ProcessSwitch - Called by CoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a switch statement.
void ExprEngine::processSwitch(SwitchNodeBuilder& builder) {
  using iterator = SwitchNodeBuilder::iterator;

  ProgramStateRef state = builder.getState();
  const Expr *CondE = builder.getCondition();
  SVal  CondV_untested = state->getSVal(CondE, builder.getLocationContext());

  if (CondV_untested.isUndef()) {
    //ExplodedNode* N = builder.generateDefaultCaseNode(state, true);
    // FIXME: add checker
    //UndefBranches.insert(N);

    return;
  }
  DefinedOrUnknownSVal CondV = CondV_untested.castAs<DefinedOrUnknownSVal>();

  ProgramStateRef DefaultSt = state;

  iterator I = builder.begin(), EI = builder.end();
  bool defaultIsFeasible = I == EI;

  for ( ; I != EI; ++I) {
    // Successor may be pruned out during CFG construction.
    if (!I.getBlock())
      continue;

    const CaseStmt *Case = I.getCase();

    // Evaluate the LHS of the case value.
    llvm::APSInt V1 = Case->getLHS()->EvaluateKnownConstInt(getContext());
    assert(V1.getBitWidth() == getContext().getIntWidth(CondE->getType()));

    // Get the RHS of the case, if it exists.
    llvm::APSInt V2;
    if (const Expr *E = Case->getRHS())
      V2 = E->EvaluateKnownConstInt(getContext());
    else
      V2 = V1;

    ProgramStateRef StateCase;
    if (std::optional<NonLoc> NL = CondV.getAs<NonLoc>())
      std::tie(StateCase, DefaultSt) =
          DefaultSt->assumeInclusiveRange(*NL, V1, V2);
    else // UnknownVal
      StateCase = DefaultSt;

    if (StateCase)
      builder.generateCaseStmtNode(I, StateCase);

    // Now "assume" that the case doesn't match.  Add this state
    // to the default state (if it is feasible).
    if (DefaultSt)
      defaultIsFeasible = true;
    else {
      defaultIsFeasible = false;
      break;
    }
  }

  if (!defaultIsFeasible)
    return;

  // If we have switch(enum value), the default branch is not
  // feasible if all of the enum constants not covered by 'case:' statements
  // are not feasible values for the switch condition.
  //
  // Note that this isn't as accurate as it could be.  Even if there isn't
  // a case for a particular enum value as long as that enum value isn't
  // feasible then it shouldn't be considered for making 'default:' reachable.
  const SwitchStmt *SS = builder.getSwitch();
  const Expr *CondExpr = SS->getCond()->IgnoreParenImpCasts();
  if (CondExpr->getType()->getAs<EnumType>()) {
    if (SS->isAllEnumCasesCovered())
      return;
  }

  builder.generateDefaultCaseNode(DefaultSt);
}

//===----------------------------------------------------------------------===//
// Transfer functions: Loads and stores.
//===----------------------------------------------------------------------===//

void ExprEngine::VisitCommonDeclRefExpr(const Expr *Ex, const NamedDecl *D,
                                        ExplodedNode *Pred,
                                        ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);

  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // C permits "extern void v", and if you cast the address to a valid type,
    // you can even do things with it. We simply pretend
    assert(Ex->isGLValue() || VD->getType()->isVoidType());
    const LocationContext *LocCtxt = Pred->getLocationContext();
    const Decl *D = LocCtxt->getDecl();
    const auto *MD = dyn_cast_or_null<CXXMethodDecl>(D);
    const auto *DeclRefEx = dyn_cast<DeclRefExpr>(Ex);
    std::optional<std::pair<SVal, QualType>> VInfo;

    if (AMgr.options.ShouldInlineLambdas && DeclRefEx &&
        DeclRefEx->refersToEnclosingVariableOrCapture() && MD &&
        MD->getParent()->isLambda()) {
      // Lookup the field of the lambda.
      const CXXRecordDecl *CXXRec = MD->getParent();
      llvm::DenseMap<const ValueDecl *, FieldDecl *> LambdaCaptureFields;
      FieldDecl *LambdaThisCaptureField;
      CXXRec->getCaptureFields(LambdaCaptureFields, LambdaThisCaptureField);

      // Sema follows a sequence of complex rules to determine whether the
      // variable should be captured.
      if (const FieldDecl *FD = LambdaCaptureFields[VD]) {
        Loc CXXThis =
            svalBuilder.getCXXThis(MD, LocCtxt->getStackFrame());
        SVal CXXThisVal = state->getSVal(CXXThis);
        VInfo = std::make_pair(state->getLValue(FD, CXXThisVal), FD->getType());
      }
    }

    if (!VInfo)
      VInfo = std::make_pair(state->getLValue(VD, LocCtxt), VD->getType());

    SVal V = VInfo->first;
    bool IsReference = VInfo->second->isReferenceType();

    // For references, the 'lvalue' is the pointer address stored in the
    // reference region.
    if (IsReference) {
      if (const MemRegion *R = V.getAsRegion())
        V = state->getSVal(R);
      else
        V = UnknownVal();
    }

    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V), nullptr,
                      ProgramPoint::PostLValueKind);
    return;
  }
  if (const auto *ED = dyn_cast<EnumConstantDecl>(D)) {
    assert(!Ex->isGLValue());
    SVal V = svalBuilder.makeIntVal(ED->getInitVal());
    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V));
    return;
  }
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    SVal V = svalBuilder.getFunctionPointer(FD);
    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V), nullptr,
                      ProgramPoint::PostLValueKind);
    return;
  }
  if (isa<FieldDecl, IndirectFieldDecl>(D)) {
    // Delegate all work related to pointer to members to the surrounding
    // operator&.
    return;
  }
  if (const auto *BD = dyn_cast<BindingDecl>(D)) {
    const auto *DD = cast<DecompositionDecl>(BD->getDecomposedDecl());

    SVal Base = state->getLValue(DD, LCtx);
    if (DD->getType()->isReferenceType()) {
      if (const MemRegion *R = Base.getAsRegion())
        Base = state->getSVal(R);
      else
        Base = UnknownVal();
    }

    SVal V = UnknownVal();

    // Handle binding to data members
    if (const auto *ME = dyn_cast<MemberExpr>(BD->getBinding())) {
      const auto *Field = cast<FieldDecl>(ME->getMemberDecl());
      V = state->getLValue(Field, Base);
    }
    // Handle binding to arrays
    else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BD->getBinding())) {
      SVal Idx = state->getSVal(ASE->getIdx(), LCtx);

      // Note: the index of an element in a structured binding is automatically
      // created and it is a unique identifier of the specific element. Thus it
      // cannot be a value that varies at runtime.
      assert(Idx.isConstant() && "BindingDecl array index is not a constant!");

      V = state->getLValue(BD->getType(), Idx, Base);
    }
    // Handle binding to tuple-like structures
    else if (const auto *HV = BD->getHoldingVar()) {
      V = state->getLValue(HV, LCtx);

      if (HV->getType()->isReferenceType()) {
        if (const MemRegion *R = V.getAsRegion())
          V = state->getSVal(R);
        else
          V = UnknownVal();
      }
    } else
      llvm_unreachable("An unknown case of structured binding encountered!");

    // In case of tuple-like types the references are already handled, so we
    // don't want to handle them again.
    if (BD->getType()->isReferenceType() && !BD->getHoldingVar()) {
      if (const MemRegion *R = V.getAsRegion())
        V = state->getSVal(R);
      else
        V = UnknownVal();
    }

    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V), nullptr,
                      ProgramPoint::PostLValueKind);

    return;
  }

  if (const auto *TPO = dyn_cast<TemplateParamObjectDecl>(D)) {
    // FIXME: We should meaningfully implement this.
    (void)TPO;
    return;
  }

  llvm_unreachable("Support for this Decl not implemented.");
}

/// VisitArrayInitLoopExpr - Transfer function for array init loop.
void ExprEngine::VisitArrayInitLoopExpr(const ArrayInitLoopExpr *Ex,
                                        ExplodedNode *Pred,
                                        ExplodedNodeSet &Dst) {
  ExplodedNodeSet CheckerPreStmt;
  getCheckerManager().runCheckersForPreStmt(CheckerPreStmt, Pred, Ex, *this);

  ExplodedNodeSet EvalSet;
  StmtNodeBuilder Bldr(CheckerPreStmt, EvalSet, *currBldrCtx);

  const Expr *Arr = Ex->getCommonExpr()->getSourceExpr();

  for (auto *Node : CheckerPreStmt) {

    // The constructor visitior has already taken care of everything.
    if (isa<CXXConstructExpr>(Ex->getSubExpr()))
      break;

    const LocationContext *LCtx = Node->getLocationContext();
    ProgramStateRef state = Node->getState();

    SVal Base = UnknownVal();

    // As in case of this expression the sub-expressions are not visited by any
    // other transfer functions, they are handled by matching their AST.

    // Case of implicit copy or move ctor of object with array member
    //
    // Note: ExprEngine::VisitMemberExpr is not able to bind the array to the
    // environment.
    //
    //    struct S {
    //      int arr[2];
    //    };
    //
    //
    //    S a;
    //    S b = a;
    //
    // The AST in case of a *copy constructor* looks like this:
    //    ArrayInitLoopExpr
    //    |-OpaqueValueExpr
    //    | `-MemberExpr              <-- match this
    //    |   `-DeclRefExpr
    //    ` ...
    //
    //
    //    S c;
    //    S d = std::move(d);
    //
    // In case of a *move constructor* the resulting AST looks like:
    //    ArrayInitLoopExpr
    //    |-OpaqueValueExpr
    //    | `-MemberExpr              <-- match this first
    //    |   `-CXXStaticCastExpr     <-- match this after
    //    |     `-DeclRefExpr
    //    ` ...
    if (const auto *ME = dyn_cast<MemberExpr>(Arr)) {
      Expr *MEBase = ME->getBase();

      // Move ctor
      if (auto CXXSCE = dyn_cast<CXXStaticCastExpr>(MEBase)) {
        MEBase = CXXSCE->getSubExpr();
      }

      auto ObjDeclExpr = cast<DeclRefExpr>(MEBase);
      SVal Obj = state->getLValue(cast<VarDecl>(ObjDeclExpr->getDecl()), LCtx);

      Base = state->getLValue(cast<FieldDecl>(ME->getMemberDecl()), Obj);
    }

    // Case of lambda capture and decomposition declaration
    //
    //    int arr[2];
    //
    //    [arr]{ int a = arr[0]; }();
    //    auto[a, b] = arr;
    //
    // In both of these cases the AST looks like the following:
    //    ArrayInitLoopExpr
    //    |-OpaqueValueExpr
    //    | `-DeclRefExpr             <-- match this
    //    ` ...
    if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Arr))
      Base = state->getLValue(cast<VarDecl>(DRE->getDecl()), LCtx);

    // Create a lazy compound value to the original array
    if (const MemRegion *R = Base.getAsRegion())
      Base = state->getSVal(R);
    else
      Base = UnknownVal();

    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, Base));
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, Ex, *this);
}

/// VisitArraySubscriptExpr - Transfer function for array accesses
void ExprEngine::VisitArraySubscriptExpr(const ArraySubscriptExpr *A,
                                             ExplodedNode *Pred,
                                             ExplodedNodeSet &Dst){
  const Expr *Base = A->getBase()->IgnoreParens();
  const Expr *Idx  = A->getIdx()->IgnoreParens();

  ExplodedNodeSet CheckerPreStmt;
  getCheckerManager().runCheckersForPreStmt(CheckerPreStmt, Pred, A, *this);

  ExplodedNodeSet EvalSet;
  StmtNodeBuilder Bldr(CheckerPreStmt, EvalSet, *currBldrCtx);

  bool IsVectorType = A->getBase()->getType()->isVectorType();

  // The "like" case is for situations where C standard prohibits the type to
  // be an lvalue, e.g. taking the address of a subscript of an expression of
  // type "void *".
  bool IsGLValueLike = A->isGLValue() ||
    (A->getType().isCForbiddenLValueType() && !AMgr.getLangOpts().CPlusPlus);

  for (auto *Node : CheckerPreStmt) {
    const LocationContext *LCtx = Node->getLocationContext();
    ProgramStateRef state = Node->getState();

    if (IsGLValueLike) {
      QualType T = A->getType();

      // One of the forbidden LValue types! We still need to have sensible
      // symbolic locations to represent this stuff. Note that arithmetic on
      // void pointers is a GCC extension.
      if (T->isVoidType())
        T = getContext().CharTy;

      SVal V = state->getLValue(T,
                                state->getSVal(Idx, LCtx),
                                state->getSVal(Base, LCtx));
      Bldr.generateNode(A, Node, state->BindExpr(A, LCtx, V), nullptr,
          ProgramPoint::PostLValueKind);
    } else if (IsVectorType) {
      // FIXME: non-glvalue vector reads are not modelled.
      Bldr.generateNode(A, Node, state, nullptr);
    } else {
      llvm_unreachable("Array subscript should be an lValue when not \
a vector and not a forbidden lvalue type");
    }
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, A, *this);
}

/// VisitMemberExpr - Transfer function for member expressions.
void ExprEngine::VisitMemberExpr(const MemberExpr *M, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  // FIXME: Prechecks eventually go in ::Visit().
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForPreStmt(CheckedSet, Pred, M, *this);

  ExplodedNodeSet EvalSet;
  ValueDecl *Member = M->getMemberDecl();

  // Handle static member variables and enum constants accessed via
  // member syntax.
  if (isa<VarDecl, EnumConstantDecl>(Member)) {
    for (const auto I : CheckedSet)
      VisitCommonDeclRefExpr(M, Member, I, EvalSet);
  } else {
    StmtNodeBuilder Bldr(CheckedSet, EvalSet, *currBldrCtx);
    ExplodedNodeSet Tmp;

    for (const auto I : CheckedSet) {
      ProgramStateRef state = I->getState();
      const LocationContext *LCtx = I->getLocationContext();
      Expr *BaseExpr = M->getBase();

      // Handle C++ method calls.
      if (const auto *MD = dyn_cast<CXXMethodDecl>(Member)) {
        if (MD->isImplicitObjectMemberFunction())
          state = createTemporaryRegionIfNeeded(state, LCtx, BaseExpr);

        SVal MDVal = svalBuilder.getFunctionPointer(MD);
        state = state->BindExpr(M, LCtx, MDVal);

        Bldr.generateNode(M, I, state);
        continue;
      }

      // Handle regular struct fields / member variables.
      const SubRegion *MR = nullptr;
      state = createTemporaryRegionIfNeeded(state, LCtx, BaseExpr,
                                            /*Result=*/nullptr,
                                            /*OutRegionWithAdjustments=*/&MR);
      SVal baseExprVal =
          MR ? loc::MemRegionVal(MR) : state->getSVal(BaseExpr, LCtx);

      // FIXME: Copied from RegionStoreManager::bind()
      if (const auto *SR =
              dyn_cast_or_null<SymbolicRegion>(baseExprVal.getAsRegion())) {
        QualType T = SR->getPointeeStaticType();
        baseExprVal =
            loc::MemRegionVal(getStoreManager().GetElementZeroRegion(SR, T));
      }

      const auto *field = cast<FieldDecl>(Member);
      SVal L = state->getLValue(field, baseExprVal);

      if (M->isGLValue() || M->getType()->isArrayType()) {
        // We special-case rvalues of array type because the analyzer cannot
        // reason about them, since we expect all regions to be wrapped in Locs.
        // We instead treat these as lvalues and assume that they will decay to
        // pointers as soon as they are used.
        if (!M->isGLValue()) {
          assert(M->getType()->isArrayType());
          const auto *PE =
            dyn_cast<ImplicitCastExpr>(I->getParentMap().getParentIgnoreParens(M));
          if (!PE || PE->getCastKind() != CK_ArrayToPointerDecay) {
            llvm_unreachable("should always be wrapped in ArrayToPointerDecay");
          }
        }

        if (field->getType()->isReferenceType()) {
          if (const MemRegion *R = L.getAsRegion())
            L = state->getSVal(R);
          else
            L = UnknownVal();
        }

        Bldr.generateNode(M, I, state->BindExpr(M, LCtx, L), nullptr,
                          ProgramPoint::PostLValueKind);
      } else {
        Bldr.takeNodes(I);
        evalLoad(Tmp, M, M, I, state, L);
        Bldr.addNodes(Tmp);
      }
    }
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, M, *this);
}

void ExprEngine::VisitAtomicExpr(const AtomicExpr *AE, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  ExplodedNodeSet AfterPreSet;
  getCheckerManager().runCheckersForPreStmt(AfterPreSet, Pred, AE, *this);

  // For now, treat all the arguments to C11 atomics as escaping.
  // FIXME: Ideally we should model the behavior of the atomics precisely here.

  ExplodedNodeSet AfterInvalidateSet;
  StmtNodeBuilder Bldr(AfterPreSet, AfterInvalidateSet, *currBldrCtx);

  for (const auto I : AfterPreSet) {
    ProgramStateRef State = I->getState();
    const LocationContext *LCtx = I->getLocationContext();

    SmallVector<SVal, 8> ValuesToInvalidate;
    for (unsigned SI = 0, Count = AE->getNumSubExprs(); SI != Count; SI++) {
      const Expr *SubExpr = AE->getSubExprs()[SI];
      SVal SubExprVal = State->getSVal(SubExpr, LCtx);
      ValuesToInvalidate.push_back(SubExprVal);
    }

    State = State->invalidateRegions(ValuesToInvalidate, AE,
                                    currBldrCtx->blockCount(),
                                    LCtx,
                                    /*CausedByPointerEscape*/true,
                                    /*Symbols=*/nullptr);

    SVal ResultVal = UnknownVal();
    State = State->BindExpr(AE, LCtx, ResultVal);
    Bldr.generateNode(AE, I, State, nullptr,
                      ProgramPoint::PostStmtKind);
  }

  getCheckerManager().runCheckersForPostStmt(Dst, AfterInvalidateSet, AE, *this);
}

// A value escapes in four possible cases:
// (1) We are binding to something that is not a memory region.
// (2) We are binding to a MemRegion that does not have stack storage.
// (3) We are binding to a top-level parameter region with a non-trivial
//     destructor. We won't see the destructor during analysis, but it's there.
// (4) We are binding to a MemRegion with stack storage that the store
//     does not understand.
ProgramStateRef ExprEngine::processPointerEscapedOnBind(
    ProgramStateRef State, ArrayRef<std::pair<SVal, SVal>> LocAndVals,
    const LocationContext *LCtx, PointerEscapeKind Kind,
    const CallEvent *Call) {
  SmallVector<SVal, 8> Escaped;
  for (const std::pair<SVal, SVal> &LocAndVal : LocAndVals) {
    // Cases (1) and (2).
    const MemRegion *MR = LocAndVal.first.getAsRegion();
    if (!MR ||
        !isa<StackSpaceRegion, StaticGlobalSpaceRegion>(MR->getMemorySpace())) {
      Escaped.push_back(LocAndVal.second);
      continue;
    }

    // Case (3).
    if (const auto *VR = dyn_cast<VarRegion>(MR->getBaseRegion()))
      if (VR->hasStackParametersStorage() && VR->getStackFrame()->inTopFrame())
        if (const auto *RD = VR->getValueType()->getAsCXXRecordDecl())
          if (!RD->hasTrivialDestructor()) {
            Escaped.push_back(LocAndVal.second);
            continue;
          }

    // Case (4): in order to test that, generate a new state with the binding
    // added. If it is the same state, then it escapes (since the store cannot
    // represent the binding).
    // Do this only if we know that the store is not supposed to generate the
    // same state.
    SVal StoredVal = State->getSVal(MR);
    if (StoredVal != LocAndVal.second)
      if (State ==
          (State->bindLoc(loc::MemRegionVal(MR), LocAndVal.second, LCtx)))
        Escaped.push_back(LocAndVal.second);
  }

  if (Escaped.empty())
    return State;

  return escapeValues(State, Escaped, Kind, Call);
}

ProgramStateRef
ExprEngine::processPointerEscapedOnBind(ProgramStateRef State, SVal Loc,
                                        SVal Val, const LocationContext *LCtx) {
  std::pair<SVal, SVal> LocAndVal(Loc, Val);
  return processPointerEscapedOnBind(State, LocAndVal, LCtx, PSK_EscapeOnBind,
                                     nullptr);
}

ProgramStateRef
ExprEngine::notifyCheckersOfPointerEscape(ProgramStateRef State,
    const InvalidatedSymbols *Invalidated,
    ArrayRef<const MemRegion *> ExplicitRegions,
    const CallEvent *Call,
    RegionAndSymbolInvalidationTraits &ITraits) {
  if (!Invalidated || Invalidated->empty())
    return State;

  if (!Call)
    return getCheckerManager().runCheckersForPointerEscape(State,
                                                           *Invalidated,
                                                           nullptr,
                                                           PSK_EscapeOther,
                                                           &ITraits);

  // If the symbols were invalidated by a call, we want to find out which ones
  // were invalidated directly due to being arguments to the call.
  InvalidatedSymbols SymbolsDirectlyInvalidated;
  for (const auto I : ExplicitRegions) {
    if (const SymbolicRegion *R = I->StripCasts()->getAs<SymbolicRegion>())
      SymbolsDirectlyInvalidated.insert(R->getSymbol());
  }

  InvalidatedSymbols SymbolsIndirectlyInvalidated;
  for (const auto &sym : *Invalidated) {
    if (SymbolsDirectlyInvalidated.count(sym))
      continue;
    SymbolsIndirectlyInvalidated.insert(sym);
  }

  if (!SymbolsDirectlyInvalidated.empty())
    State = getCheckerManager().runCheckersForPointerEscape(State,
        SymbolsDirectlyInvalidated, Call, PSK_DirectEscapeOnCall, &ITraits);

  // Notify about the symbols that get indirectly invalidated by the call.
  if (!SymbolsIndirectlyInvalidated.empty())
    State = getCheckerManager().runCheckersForPointerEscape(State,
        SymbolsIndirectlyInvalidated, Call, PSK_IndirectEscapeOnCall, &ITraits);

  return State;
}

/// evalBind - Handle the semantics of binding a value to a specific location.
///  This method is used by evalStore and (soon) VisitDeclStmt, and others.
void ExprEngine::evalBind(ExplodedNodeSet &Dst, const Stmt *StoreE,
                          ExplodedNode *Pred,
                          SVal location, SVal Val,
                          bool atDeclInit, const ProgramPoint *PP) {
  const LocationContext *LC = Pred->getLocationContext();
  PostStmt PS(StoreE, LC);
  if (!PP)
    PP = &PS;

  // Do a previsit of the bind.
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForBind(CheckedSet, Pred, location, Val,
                                         StoreE, *this, *PP);

  StmtNodeBuilder Bldr(CheckedSet, Dst, *currBldrCtx);

  // If the location is not a 'Loc', it will already be handled by
  // the checkers.  There is nothing left to do.
  if (!isa<Loc>(location)) {
    const ProgramPoint L = PostStore(StoreE, LC, /*Loc*/nullptr,
                                     /*tag*/nullptr);
    ProgramStateRef state = Pred->getState();
    state = processPointerEscapedOnBind(state, location, Val, LC);
    Bldr.generateNode(L, state, Pred);
    return;
  }

  for (const auto PredI : CheckedSet) {
    ProgramStateRef state = PredI->getState();

    state = processPointerEscapedOnBind(state, location, Val, LC);

    // When binding the value, pass on the hint that this is a initialization.
    // For initializations, we do not need to inform clients of region
    // changes.
    state = state->bindLoc(location.castAs<Loc>(),
                           Val, LC, /* notifyChanges = */ !atDeclInit);

    const MemRegion *LocReg = nullptr;
    if (std::optional<loc::MemRegionVal> LocRegVal =
            location.getAs<loc::MemRegionVal>()) {
      LocReg = LocRegVal->getRegion();
    }

    const ProgramPoint L = PostStore(StoreE, LC, LocReg, nullptr);
    Bldr.generateNode(L, state, PredI);
  }
}

/// evalStore - Handle the semantics of a store via an assignment.
///  @param Dst The node set to store generated state nodes
///  @param AssignE The assignment expression if the store happens in an
///         assignment.
///  @param LocationE The location expression that is stored to.
///  @param state The current simulation state
///  @param location The location to store the value
///  @param Val The value to be stored
void ExprEngine::evalStore(ExplodedNodeSet &Dst, const Expr *AssignE,
                             const Expr *LocationE,
                             ExplodedNode *Pred,
                             ProgramStateRef state, SVal location, SVal Val,
                             const ProgramPointTag *tag) {
  // Proceed with the store.  We use AssignE as the anchor for the PostStore
  // ProgramPoint if it is non-NULL, and LocationE otherwise.
  const Expr *StoreE = AssignE ? AssignE : LocationE;

  // Evaluate the location (checks for bad dereferences).
  ExplodedNodeSet Tmp;
  evalLocation(Tmp, AssignE, LocationE, Pred, state, location, false);

  if (Tmp.empty())
    return;

  if (location.isUndef())
    return;

  for (const auto I : Tmp)
    evalBind(Dst, StoreE, I, location, Val, false);
}

void ExprEngine::evalLoad(ExplodedNodeSet &Dst,
                          const Expr *NodeEx,
                          const Expr *BoundEx,
                          ExplodedNode *Pred,
                          ProgramStateRef state,
                          SVal location,
                          const ProgramPointTag *tag,
                          QualType LoadTy) {
  assert(!isa<NonLoc>(location) && "location cannot be a NonLoc.");
  assert(NodeEx);
  assert(BoundEx);
  // Evaluate the location (checks for bad dereferences).
  ExplodedNodeSet Tmp;
  evalLocation(Tmp, NodeEx, BoundEx, Pred, state, location, true);
  if (Tmp.empty())
    return;

  StmtNodeBuilder Bldr(Tmp, Dst, *currBldrCtx);
  if (location.isUndef())
    return;

  // Proceed with the load.
  for (const auto I : Tmp) {
    state = I->getState();
    const LocationContext *LCtx = I->getLocationContext();

    SVal V = UnknownVal();
    if (location.isValid()) {
      if (LoadTy.isNull())
        LoadTy = BoundEx->getType();
      V = state->getSVal(location.castAs<Loc>(), LoadTy);
    }

    Bldr.generateNode(NodeEx, I, state->BindExpr(BoundEx, LCtx, V), tag,
                      ProgramPoint::PostLoadKind);
  }
}

void ExprEngine::evalLocation(ExplodedNodeSet &Dst,
                              const Stmt *NodeEx,
                              const Stmt *BoundEx,
                              ExplodedNode *Pred,
                              ProgramStateRef state,
                              SVal location,
                              bool isLoad) {
  StmtNodeBuilder BldrTop(Pred, Dst, *currBldrCtx);
  // Early checks for performance reason.
  if (location.isUnknown()) {
    return;
  }

  ExplodedNodeSet Src;
  BldrTop.takeNodes(Pred);
  StmtNodeBuilder Bldr(Pred, Src, *currBldrCtx);
  if (Pred->getState() != state) {
    // Associate this new state with an ExplodedNode.
    // FIXME: If I pass null tag, the graph is incorrect, e.g for
    //   int *p;
    //   p = 0;
    //   *p = 0xDEADBEEF;
    // "p = 0" is not noted as "Null pointer value stored to 'p'" but
    // instead "int *p" is noted as
    // "Variable 'p' initialized to a null pointer value"

    static SimpleProgramPointTag tag(TagProviderName, "Location");
    Bldr.generateNode(NodeEx, Pred, state, &tag);
  }
  ExplodedNodeSet Tmp;
  getCheckerManager().runCheckersForLocation(Tmp, Src, location, isLoad,
                                             NodeEx, BoundEx, *this);
  BldrTop.addNodes(Tmp);
}

std::pair<const ProgramPointTag *, const ProgramPointTag*>
ExprEngine::geteagerlyAssumeBinOpBifurcationTags() {
  static SimpleProgramPointTag
         eagerlyAssumeBinOpBifurcationTrue(TagProviderName,
                                           "Eagerly Assume True"),
         eagerlyAssumeBinOpBifurcationFalse(TagProviderName,
                                            "Eagerly Assume False");
  return std::make_pair(&eagerlyAssumeBinOpBifurcationTrue,
                        &eagerlyAssumeBinOpBifurcationFalse);
}

void ExprEngine::evalEagerlyAssumeBinOpBifurcation(ExplodedNodeSet &Dst,
                                                   ExplodedNodeSet &Src,
                                                   const Expr *Ex) {
  StmtNodeBuilder Bldr(Src, Dst, *currBldrCtx);

  for (const auto Pred : Src) {
    // Test if the previous node was as the same expression.  This can happen
    // when the expression fails to evaluate to anything meaningful and
    // (as an optimization) we don't generate a node.
    ProgramPoint P = Pred->getLocation();
    if (!P.getAs<PostStmt>() || P.castAs<PostStmt>().getStmt() != Ex) {
      continue;
    }

    ProgramStateRef state = Pred->getState();
    SVal V = state->getSVal(Ex, Pred->getLocationContext());
    std::optional<nonloc::SymbolVal> SEV = V.getAs<nonloc::SymbolVal>();
    if (SEV && SEV->isExpression()) {
      const std::pair<const ProgramPointTag *, const ProgramPointTag*> &tags =
        geteagerlyAssumeBinOpBifurcationTags();

      ProgramStateRef StateTrue, StateFalse;
      std::tie(StateTrue, StateFalse) = state->assume(*SEV);

      // First assume that the condition is true.
      if (StateTrue) {
        SVal Val = svalBuilder.makeIntVal(1U, Ex->getType());
        StateTrue = StateTrue->BindExpr(Ex, Pred->getLocationContext(), Val);
        Bldr.generateNode(Ex, Pred, StateTrue, tags.first);
      }

      // Next, assume that the condition is false.
      if (StateFalse) {
        SVal Val = svalBuilder.makeIntVal(0U, Ex->getType());
        StateFalse = StateFalse->BindExpr(Ex, Pred->getLocationContext(), Val);
        Bldr.generateNode(Ex, Pred, StateFalse, tags.second);
      }
    }
  }
}

void ExprEngine::VisitGCCAsmStmt(const GCCAsmStmt *A, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  // We have processed both the inputs and the outputs.  All of the outputs
  // should evaluate to Locs.  Nuke all of their values.

  // FIXME: Some day in the future it would be nice to allow a "plug-in"
  // which interprets the inline asm and stores proper results in the
  // outputs.

  ProgramStateRef state = Pred->getState();

  for (const Expr *O : A->outputs()) {
    SVal X = state->getSVal(O, Pred->getLocationContext());
    assert(!isa<NonLoc>(X)); // Should be an Lval, or unknown, undef.

    if (std::optional<Loc> LV = X.getAs<Loc>())
      state = state->bindLoc(*LV, UnknownVal(), Pred->getLocationContext());
  }

  Bldr.generateNode(A, Pred, state);
}

void ExprEngine::VisitMSAsmStmt(const MSAsmStmt *A, ExplodedNode *Pred,
                                ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  Bldr.generateNode(A, Pred, Pred->getState());
}

//===----------------------------------------------------------------------===//
// Visualization.
//===----------------------------------------------------------------------===//

namespace llvm {

template<>
struct DOTGraphTraits<ExplodedGraph*> : public DefaultDOTGraphTraits {
  DOTGraphTraits (bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static bool nodeHasBugReport(const ExplodedNode *N) {
    BugReporter &BR = static_cast<ExprEngine &>(
      N->getState()->getStateManager().getOwningEngine()).getBugReporter();

    for (const auto &Class : BR.equivalenceClasses()) {
      for (const auto &Report : Class.getReports()) {
        const auto *PR = dyn_cast<PathSensitiveBugReport>(Report.get());
        if (!PR)
          continue;
        const ExplodedNode *EN = PR->getErrorNode();
        if (EN->getState() == N->getState() &&
            EN->getLocation() == N->getLocation())
          return true;
      }
    }
    return false;
  }

  /// \p PreCallback: callback before break.
  /// \p PostCallback: callback after break.
  /// \p Stop: stop iteration if returns @c true
  /// \return Whether @c Stop ever returned @c true.
  static bool traverseHiddenNodes(
      const ExplodedNode *N,
      llvm::function_ref<void(const ExplodedNode *)> PreCallback,
      llvm::function_ref<void(const ExplodedNode *)> PostCallback,
      llvm::function_ref<bool(const ExplodedNode *)> Stop) {
    while (true) {
      PreCallback(N);
      if (Stop(N))
        return true;

      if (N->succ_size() != 1 || !isNodeHidden(N->getFirstSucc(), nullptr))
        break;
      PostCallback(N);

      N = N->getFirstSucc();
    }
    return false;
  }

  static bool isNodeHidden(const ExplodedNode *N, const ExplodedGraph *G) {
    return N->isTrivial();
  }

  static std::string getNodeLabel(const ExplodedNode *N, ExplodedGraph *G){
    std::string Buf;
    llvm::raw_string_ostream Out(Buf);

    const bool IsDot = true;
    const unsigned int Space = 1;
    ProgramStateRef State = N->getState();

    Out << "{ \"state_id\": " << State->getID()
        << ",\\l";

    Indent(Out, Space, IsDot) << "\"program_points\": [\\l";

    // Dump program point for all the previously skipped nodes.
    traverseHiddenNodes(
        N,
        [&](const ExplodedNode *OtherNode) {
          Indent(Out, Space + 1, IsDot) << "{ ";
          OtherNode->getLocation().printJson(Out, /*NL=*/"\\l");
          Out << ", \"tag\": ";
          if (const ProgramPointTag *Tag = OtherNode->getLocation().getTag())
            Out << '\"' << Tag->getTagDescription() << '\"';
          else
            Out << "null";
          Out << ", \"node_id\": " << OtherNode->getID() <<
                 ", \"is_sink\": " << OtherNode->isSink() <<
                 ", \"has_report\": " << nodeHasBugReport(OtherNode) << " }";
        },
        // Adds a comma and a new-line between each program point.
        [&](const ExplodedNode *) { Out << ",\\l"; },
        [&](const ExplodedNode *) { return false; });

    Out << "\\l"; // Adds a new-line to the last program point.
    Indent(Out, Space, IsDot) << "],\\l";

    State->printDOT(Out, N->getLocationContext(), Space);

    Out << "\\l}\\l";
    return Buf;
  }
};

} // namespace llvm

void ExprEngine::ViewGraph(bool trim) {
  std::string Filename = DumpGraph(trim);
  llvm::DisplayGraph(Filename, false, llvm::GraphProgram::DOT);
}

void ExprEngine::ViewGraph(ArrayRef<const ExplodedNode *> Nodes) {
  std::string Filename = DumpGraph(Nodes);
  llvm::DisplayGraph(Filename, false, llvm::GraphProgram::DOT);
}

std::string ExprEngine::DumpGraph(bool trim, StringRef Filename) {
  if (trim) {
    std::vector<const ExplodedNode *> Src;

    // Iterate through the reports and get their nodes.
    for (const auto &Class : BR.equivalenceClasses()) {
      const auto *R =
          dyn_cast<PathSensitiveBugReport>(Class.getReports()[0].get());
      if (!R)
        continue;
      const auto *N = const_cast<ExplodedNode *>(R->getErrorNode());
      Src.push_back(N);
    }
    return DumpGraph(Src, Filename);
  }

  return llvm::WriteGraph(&G, "ExprEngine", /*ShortNames=*/false,
                          /*Title=*/"Exploded Graph",
                          /*Filename=*/std::string(Filename));
}

std::string ExprEngine::DumpGraph(ArrayRef<const ExplodedNode *> Nodes,
                                  StringRef Filename) {
  std::unique_ptr<ExplodedGraph> TrimmedG(G.trim(Nodes));

  if (!TrimmedG.get()) {
    llvm::errs() << "warning: Trimmed ExplodedGraph is empty.\n";
    return "";
  }

  return llvm::WriteGraph(TrimmedG.get(), "TrimmedExprEngine",
                          /*ShortNames=*/false,
                          /*Title=*/"Trimmed Exploded Graph",
                          /*Filename=*/std::string(Filename));
}

void *ProgramStateTrait<ReplayWithoutInlining>::GDMIndex() {
  static int index = 0;
  return &index;
}

void ExprEngine::anchor() { }
