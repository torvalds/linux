//===- ExprEngineCXX.cpp - ExprEngine support for C++ -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the C++ expression evaluation engine.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Analysis/ConstructionContext.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include <optional>

using namespace clang;
using namespace ento;

void ExprEngine::CreateCXXTemporaryObject(const MaterializeTemporaryExpr *ME,
                                          ExplodedNode *Pred,
                                          ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  const Expr *tempExpr = ME->getSubExpr()->IgnoreParens();
  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  state = createTemporaryRegionIfNeeded(state, LCtx, tempExpr, ME);
  Bldr.generateNode(ME, Pred, state);
}

// FIXME: This is the sort of code that should eventually live in a Core
// checker rather than as a special case in ExprEngine.
void ExprEngine::performTrivialCopy(NodeBuilder &Bldr, ExplodedNode *Pred,
                                    const CallEvent &Call) {
  SVal ThisVal;
  bool AlwaysReturnsLValue;
  const CXXRecordDecl *ThisRD = nullptr;
  if (const CXXConstructorCall *Ctor = dyn_cast<CXXConstructorCall>(&Call)) {
    assert(Ctor->getDecl()->isTrivial());
    assert(Ctor->getDecl()->isCopyOrMoveConstructor());
    ThisVal = Ctor->getCXXThisVal();
    ThisRD = Ctor->getDecl()->getParent();
    AlwaysReturnsLValue = false;
  } else {
    assert(cast<CXXMethodDecl>(Call.getDecl())->isTrivial());
    assert(cast<CXXMethodDecl>(Call.getDecl())->getOverloadedOperator() ==
           OO_Equal);
    ThisVal = cast<CXXInstanceCall>(Call).getCXXThisVal();
    ThisRD = cast<CXXMethodDecl>(Call.getDecl())->getParent();
    AlwaysReturnsLValue = true;
  }

  const LocationContext *LCtx = Pred->getLocationContext();
  const Expr *CallExpr = Call.getOriginExpr();

  ExplodedNodeSet Dst;
  Bldr.takeNodes(Pred);

  assert(ThisRD);
  if (!ThisRD->isEmpty()) {
    // Load the source value only for non-empty classes.
    // Otherwise it'd retrieve an UnknownVal
    // and bind it and RegionStore would think that the actual value
    // in this region at this offset is unknown.
    SVal V = Call.getArgSVal(0);

    // If the value being copied is not unknown, load from its location to get
    // an aggregate rvalue.
    if (std::optional<Loc> L = V.getAs<Loc>())
      V = Pred->getState()->getSVal(*L);
    else
      assert(V.isUnknownOrUndef());
    evalBind(Dst, CallExpr, Pred, ThisVal, V, true);
  } else {
    Dst.Add(Pred);
  }

  PostStmt PS(CallExpr, LCtx);
  for (ExplodedNode *N : Dst) {
    ProgramStateRef State = N->getState();
    if (AlwaysReturnsLValue)
      State = State->BindExpr(CallExpr, LCtx, ThisVal);
    else
      State = bindReturnValue(Call, LCtx, State);
    Bldr.generateNode(PS, State, N);
  }
}

SVal ExprEngine::makeElementRegion(ProgramStateRef State, SVal LValue,
                                   QualType &Ty, bool &IsArray, unsigned Idx) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  ASTContext &Ctx = SVB.getContext();

  if (const ArrayType *AT = Ctx.getAsArrayType(Ty)) {
    while (AT) {
      Ty = AT->getElementType();
      AT = dyn_cast<ArrayType>(AT->getElementType());
    }
    LValue = State->getLValue(Ty, SVB.makeArrayIndex(Idx), LValue);
    IsArray = true;
  }

  return LValue;
}

// In case when the prvalue is returned from the function (kind is one of
// SimpleReturnedValueKind, CXX17ElidedCopyReturnedValueKind), then
// it's materialization happens in context of the caller.
// We pass BldrCtx explicitly, as currBldrCtx always refers to callee's context.
SVal ExprEngine::computeObjectUnderConstruction(
    const Expr *E, ProgramStateRef State, const NodeBuilderContext *BldrCtx,
    const LocationContext *LCtx, const ConstructionContext *CC,
    EvalCallOptions &CallOpts, unsigned Idx) {

  SValBuilder &SVB = getSValBuilder();
  MemRegionManager &MRMgr = SVB.getRegionManager();
  ASTContext &ACtx = SVB.getContext();

  // Compute the target region by exploring the construction context.
  if (CC) {
    switch (CC->getKind()) {
    case ConstructionContext::CXX17ElidedCopyVariableKind:
    case ConstructionContext::SimpleVariableKind: {
      const auto *DSCC = cast<VariableConstructionContext>(CC);
      const auto *DS = DSCC->getDeclStmt();
      const auto *Var = cast<VarDecl>(DS->getSingleDecl());
      QualType Ty = Var->getType();
      return makeElementRegion(State, State->getLValue(Var, LCtx), Ty,
                               CallOpts.IsArrayCtorOrDtor, Idx);
    }
    case ConstructionContext::CXX17ElidedCopyConstructorInitializerKind:
    case ConstructionContext::SimpleConstructorInitializerKind: {
      const auto *ICC = cast<ConstructorInitializerConstructionContext>(CC);
      const auto *Init = ICC->getCXXCtorInitializer();
      const CXXMethodDecl *CurCtor = cast<CXXMethodDecl>(LCtx->getDecl());
      Loc ThisPtr = SVB.getCXXThis(CurCtor, LCtx->getStackFrame());
      SVal ThisVal = State->getSVal(ThisPtr);
      if (Init->isBaseInitializer()) {
        const auto *ThisReg = cast<SubRegion>(ThisVal.getAsRegion());
        const CXXRecordDecl *BaseClass =
          Init->getBaseClass()->getAsCXXRecordDecl();
        const auto *BaseReg =
          MRMgr.getCXXBaseObjectRegion(BaseClass, ThisReg,
                                       Init->isBaseVirtual());
        return SVB.makeLoc(BaseReg);
      }
      if (Init->isDelegatingInitializer())
        return ThisVal;

      const ValueDecl *Field;
      SVal FieldVal;
      if (Init->isIndirectMemberInitializer()) {
        Field = Init->getIndirectMember();
        FieldVal = State->getLValue(Init->getIndirectMember(), ThisVal);
      } else {
        Field = Init->getMember();
        FieldVal = State->getLValue(Init->getMember(), ThisVal);
      }

      QualType Ty = Field->getType();
      return makeElementRegion(State, FieldVal, Ty, CallOpts.IsArrayCtorOrDtor,
                               Idx);
    }
    case ConstructionContext::NewAllocatedObjectKind: {
      if (AMgr.getAnalyzerOptions().MayInlineCXXAllocator) {
        const auto *NECC = cast<NewAllocatedObjectConstructionContext>(CC);
        const auto *NE = NECC->getCXXNewExpr();
        SVal V = *getObjectUnderConstruction(State, NE, LCtx);
        if (const SubRegion *MR =
                dyn_cast_or_null<SubRegion>(V.getAsRegion())) {
          if (NE->isArray()) {
            CallOpts.IsArrayCtorOrDtor = true;

            auto Ty = NE->getType()->getPointeeType();
            while (const auto *AT = getContext().getAsArrayType(Ty))
              Ty = AT->getElementType();

            auto R = MRMgr.getElementRegion(Ty, svalBuilder.makeArrayIndex(Idx),
                                            MR, SVB.getContext());

            return loc::MemRegionVal(R);
          }
          return  V;
        }
        // TODO: Detect when the allocator returns a null pointer.
        // Constructor shall not be called in this case.
      }
      break;
    }
    case ConstructionContext::SimpleReturnedValueKind:
    case ConstructionContext::CXX17ElidedCopyReturnedValueKind: {
      // The temporary is to be managed by the parent stack frame.
      // So build it in the parent stack frame if we're not in the
      // top frame of the analysis.
      const StackFrameContext *SFC = LCtx->getStackFrame();
      if (const LocationContext *CallerLCtx = SFC->getParent()) {
        auto RTC = (*SFC->getCallSiteBlock())[SFC->getIndex()]
                       .getAs<CFGCXXRecordTypedCall>();
        if (!RTC) {
          // We were unable to find the correct construction context for the
          // call in the parent stack frame. This is equivalent to not being
          // able to find construction context at all.
          break;
        }
        if (isa<BlockInvocationContext>(CallerLCtx)) {
          // Unwrap block invocation contexts. They're mostly part of
          // the current stack frame.
          CallerLCtx = CallerLCtx->getParent();
          assert(!isa<BlockInvocationContext>(CallerLCtx));
        }

        NodeBuilderContext CallerBldrCtx(getCoreEngine(),
                                         SFC->getCallSiteBlock(), CallerLCtx);
        return computeObjectUnderConstruction(
            cast<Expr>(SFC->getCallSite()), State, &CallerBldrCtx, CallerLCtx,
            RTC->getConstructionContext(), CallOpts);
      } else {
        // We are on the top frame of the analysis. We do not know where is the
        // object returned to. Conjure a symbolic region for the return value.
        // TODO: We probably need a new MemRegion kind to represent the storage
        // of that SymbolicRegion, so that we could produce a fancy symbol
        // instead of an anonymous conjured symbol.
        // TODO: Do we need to track the region to avoid having it dead
        // too early? It does die too early, at least in C++17, but because
        // putting anything into a SymbolicRegion causes an immediate escape,
        // it doesn't cause any leak false positives.
        const auto *RCC = cast<ReturnedValueConstructionContext>(CC);
        // Make sure that this doesn't coincide with any other symbol
        // conjured for the returned expression.
        static const int TopLevelSymRegionTag = 0;
        const Expr *RetE = RCC->getReturnStmt()->getRetValue();
        assert(RetE && "Void returns should not have a construction context");
        QualType ReturnTy = RetE->getType();
        QualType RegionTy = ACtx.getPointerType(ReturnTy);
        return SVB.conjureSymbolVal(&TopLevelSymRegionTag, RetE, SFC, RegionTy,
                                    currBldrCtx->blockCount());
      }
      llvm_unreachable("Unhandled return value construction context!");
    }
    case ConstructionContext::ElidedTemporaryObjectKind: {
      assert(AMgr.getAnalyzerOptions().ShouldElideConstructors);
      const auto *TCC = cast<ElidedTemporaryObjectConstructionContext>(CC);

      // Support pre-C++17 copy elision. We'll have the elidable copy
      // constructor in the AST and in the CFG, but we'll skip it
      // and construct directly into the final object. This call
      // also sets the CallOpts flags for us.
      // If the elided copy/move constructor is not supported, there's still
      // benefit in trying to model the non-elided constructor.
      // Stash our state before trying to elide, as it'll get overwritten.
      ProgramStateRef PreElideState = State;
      EvalCallOptions PreElideCallOpts = CallOpts;

      SVal V = computeObjectUnderConstruction(
          TCC->getConstructorAfterElision(), State, BldrCtx, LCtx,
          TCC->getConstructionContextAfterElision(), CallOpts);

      // FIXME: This definition of "copy elision has not failed" is unreliable.
      // It doesn't indicate that the constructor will actually be inlined
      // later; this is still up to evalCall() to decide.
      if (!CallOpts.IsCtorOrDtorWithImproperlyModeledTargetRegion)
        return V;

      // Copy elision failed. Revert the changes and proceed as if we have
      // a simple temporary.
      CallOpts = PreElideCallOpts;
      CallOpts.IsElidableCtorThatHasNotBeenElided = true;
      [[fallthrough]];
    }
    case ConstructionContext::SimpleTemporaryObjectKind: {
      const auto *TCC = cast<TemporaryObjectConstructionContext>(CC);
      const MaterializeTemporaryExpr *MTE = TCC->getMaterializedTemporaryExpr();

      CallOpts.IsTemporaryCtorOrDtor = true;
      if (MTE) {
        if (const ValueDecl *VD = MTE->getExtendingDecl()) {
          StorageDuration SD = MTE->getStorageDuration();
          assert(SD != SD_FullExpression);
          if (!VD->getType()->isReferenceType()) {
            // We're lifetime-extended by a surrounding aggregate.
            // Automatic destructors aren't quite working in this case
            // on the CFG side. We should warn the caller about that.
            // FIXME: Is there a better way to retrieve this information from
            // the MaterializeTemporaryExpr?
            CallOpts.IsTemporaryLifetimeExtendedViaAggregate = true;
          }

          if (SD == SD_Static || SD == SD_Thread)
            return loc::MemRegionVal(
                MRMgr.getCXXStaticLifetimeExtendedObjectRegion(E, VD));

          return loc::MemRegionVal(
              MRMgr.getCXXLifetimeExtendedObjectRegion(E, VD, LCtx));
        }
        assert(MTE->getStorageDuration() == SD_FullExpression);
      }

      return loc::MemRegionVal(MRMgr.getCXXTempObjectRegion(E, LCtx));
    }
    case ConstructionContext::LambdaCaptureKind: {
      CallOpts.IsTemporaryCtorOrDtor = true;

      const auto *LCC = cast<LambdaCaptureConstructionContext>(CC);

      SVal Base = loc::MemRegionVal(
          MRMgr.getCXXTempObjectRegion(LCC->getInitializer(), LCtx));

      const auto *CE = dyn_cast_or_null<CXXConstructExpr>(E);
      if (getIndexOfElementToConstruct(State, CE, LCtx)) {
        CallOpts.IsArrayCtorOrDtor = true;
        Base = State->getLValue(E->getType(), svalBuilder.makeArrayIndex(Idx),
                                Base);
      }

      return Base;
    }
    case ConstructionContext::ArgumentKind: {
      // Arguments are technically temporaries.
      CallOpts.IsTemporaryCtorOrDtor = true;

      const auto *ACC = cast<ArgumentConstructionContext>(CC);
      const Expr *E = ACC->getCallLikeExpr();
      unsigned Idx = ACC->getIndex();

      CallEventManager &CEMgr = getStateManager().getCallEventManager();
      auto getArgLoc = [&](CallEventRef<> Caller) -> std::optional<SVal> {
        const LocationContext *FutureSFC =
            Caller->getCalleeStackFrame(BldrCtx->blockCount());
        // Return early if we are unable to reliably foresee
        // the future stack frame.
        if (!FutureSFC)
          return std::nullopt;

        // This should be equivalent to Caller->getDecl() for now, but
        // FutureSFC->getDecl() is likely to support better stuff (like
        // virtual functions) earlier.
        const Decl *CalleeD = FutureSFC->getDecl();

        // FIXME: Support for variadic arguments is not implemented here yet.
        if (CallEvent::isVariadic(CalleeD))
          return std::nullopt;

        // Operator arguments do not correspond to operator parameters
        // because this-argument is implemented as a normal argument in
        // operator call expressions but not in operator declarations.
        const TypedValueRegion *TVR = Caller->getParameterLocation(
            *Caller->getAdjustedParameterIndex(Idx), BldrCtx->blockCount());
        if (!TVR)
          return std::nullopt;

        return loc::MemRegionVal(TVR);
      };

      if (const auto *CE = dyn_cast<CallExpr>(E)) {
        CallEventRef<> Caller =
            CEMgr.getSimpleCall(CE, State, LCtx, getCFGElementRef());
        if (std::optional<SVal> V = getArgLoc(Caller))
          return *V;
        else
          break;
      } else if (const auto *CCE = dyn_cast<CXXConstructExpr>(E)) {
        // Don't bother figuring out the target region for the future
        // constructor because we won't need it.
        CallEventRef<> Caller = CEMgr.getCXXConstructorCall(
            CCE, /*Target=*/nullptr, State, LCtx, getCFGElementRef());
        if (std::optional<SVal> V = getArgLoc(Caller))
          return *V;
        else
          break;
      } else if (const auto *ME = dyn_cast<ObjCMessageExpr>(E)) {
        CallEventRef<> Caller =
            CEMgr.getObjCMethodCall(ME, State, LCtx, getCFGElementRef());
        if (std::optional<SVal> V = getArgLoc(Caller))
          return *V;
        else
          break;
      }
    }
    } // switch (CC->getKind())
  }

  // If we couldn't find an existing region to construct into, assume we're
  // constructing a temporary. Notify the caller of our failure.
  CallOpts.IsCtorOrDtorWithImproperlyModeledTargetRegion = true;
  return loc::MemRegionVal(MRMgr.getCXXTempObjectRegion(E, LCtx));
}

ProgramStateRef ExprEngine::updateObjectsUnderConstruction(
    SVal V, const Expr *E, ProgramStateRef State, const LocationContext *LCtx,
    const ConstructionContext *CC, const EvalCallOptions &CallOpts) {
  if (CallOpts.IsCtorOrDtorWithImproperlyModeledTargetRegion) {
    // Sounds like we failed to find the target region and therefore
    // copy elision failed. There's nothing we can do about it here.
    return State;
  }

  // See if we're constructing an existing region by looking at the
  // current construction context.
  assert(CC && "Computed target region without construction context?");
  switch (CC->getKind()) {
  case ConstructionContext::CXX17ElidedCopyVariableKind:
  case ConstructionContext::SimpleVariableKind: {
    const auto *DSCC = cast<VariableConstructionContext>(CC);
    return addObjectUnderConstruction(State, DSCC->getDeclStmt(), LCtx, V);
    }
    case ConstructionContext::CXX17ElidedCopyConstructorInitializerKind:
    case ConstructionContext::SimpleConstructorInitializerKind: {
      const auto *ICC = cast<ConstructorInitializerConstructionContext>(CC);
      const auto *Init = ICC->getCXXCtorInitializer();
      // Base and delegating initializers handled above
      assert(Init->isAnyMemberInitializer() &&
             "Base and delegating initializers should have been handled by"
             "computeObjectUnderConstruction()");
      return addObjectUnderConstruction(State, Init, LCtx, V);
    }
    case ConstructionContext::NewAllocatedObjectKind: {
      return State;
    }
    case ConstructionContext::SimpleReturnedValueKind:
    case ConstructionContext::CXX17ElidedCopyReturnedValueKind: {
      const StackFrameContext *SFC = LCtx->getStackFrame();
      const LocationContext *CallerLCtx = SFC->getParent();
      if (!CallerLCtx) {
        // No extra work is necessary in top frame.
        return State;
      }

      auto RTC = (*SFC->getCallSiteBlock())[SFC->getIndex()]
                     .getAs<CFGCXXRecordTypedCall>();
      assert(RTC && "Could not have had a target region without it");
      if (isa<BlockInvocationContext>(CallerLCtx)) {
        // Unwrap block invocation contexts. They're mostly part of
        // the current stack frame.
        CallerLCtx = CallerLCtx->getParent();
        assert(!isa<BlockInvocationContext>(CallerLCtx));
      }

      return updateObjectsUnderConstruction(V,
          cast<Expr>(SFC->getCallSite()), State, CallerLCtx,
          RTC->getConstructionContext(), CallOpts);
    }
    case ConstructionContext::ElidedTemporaryObjectKind: {
      assert(AMgr.getAnalyzerOptions().ShouldElideConstructors);
      if (!CallOpts.IsElidableCtorThatHasNotBeenElided) {
        const auto *TCC = cast<ElidedTemporaryObjectConstructionContext>(CC);
        State = updateObjectsUnderConstruction(
            V, TCC->getConstructorAfterElision(), State, LCtx,
            TCC->getConstructionContextAfterElision(), CallOpts);

        // Remember that we've elided the constructor.
        State = addObjectUnderConstruction(
            State, TCC->getConstructorAfterElision(), LCtx, V);

        // Remember that we've elided the destructor.
        if (const auto *BTE = TCC->getCXXBindTemporaryExpr())
          State = elideDestructor(State, BTE, LCtx);

        // Instead of materialization, shamelessly return
        // the final object destination.
        if (const auto *MTE = TCC->getMaterializedTemporaryExpr())
          State = addObjectUnderConstruction(State, MTE, LCtx, V);

        return State;
      }
      // If we decided not to elide the constructor, proceed as if
      // it's a simple temporary.
      [[fallthrough]];
    }
    case ConstructionContext::SimpleTemporaryObjectKind: {
      const auto *TCC = cast<TemporaryObjectConstructionContext>(CC);
      if (const auto *BTE = TCC->getCXXBindTemporaryExpr())
        State = addObjectUnderConstruction(State, BTE, LCtx, V);

      if (const auto *MTE = TCC->getMaterializedTemporaryExpr())
        State = addObjectUnderConstruction(State, MTE, LCtx, V);

      return State;
    }
    case ConstructionContext::LambdaCaptureKind: {
      const auto *LCC = cast<LambdaCaptureConstructionContext>(CC);

      // If we capture and array, we want to store the super region, not a
      // sub-region.
      if (const auto *EL = dyn_cast_or_null<ElementRegion>(V.getAsRegion()))
        V = loc::MemRegionVal(EL->getSuperRegion());

      return addObjectUnderConstruction(
          State, {LCC->getLambdaExpr(), LCC->getIndex()}, LCtx, V);
    }
    case ConstructionContext::ArgumentKind: {
      const auto *ACC = cast<ArgumentConstructionContext>(CC);
      if (const auto *BTE = ACC->getCXXBindTemporaryExpr())
        State = addObjectUnderConstruction(State, BTE, LCtx, V);

      return addObjectUnderConstruction(
          State, {ACC->getCallLikeExpr(), ACC->getIndex()}, LCtx, V);
    }
  }
  llvm_unreachable("Unhandled construction context!");
}

static ProgramStateRef
bindRequiredArrayElementToEnvironment(ProgramStateRef State,
                                      const ArrayInitLoopExpr *AILE,
                                      const LocationContext *LCtx, SVal Idx) {
  // The ctor in this case is guaranteed to be a copy ctor, otherwise we hit a
  // compile time error.
  //
  //  -ArrayInitLoopExpr                <-- we're here
  //   |-OpaqueValueExpr
  //   | `-DeclRefExpr                  <-- match this
  //   `-CXXConstructExpr
  //     `-ImplicitCastExpr
  //       `-ArraySubscriptExpr
  //         |-ImplicitCastExpr
  //         | `-OpaqueValueExpr
  //         |   `-DeclRefExpr
  //         `-ArrayInitIndexExpr
  //
  // The resulting expression might look like the one below in an implicit
  // copy/move ctor.
  //
  //   ArrayInitLoopExpr                <-- we're here
  //   |-OpaqueValueExpr
  //   | `-MemberExpr                   <-- match this
  //   |  (`-CXXStaticCastExpr)         <-- move ctor only
  //   |     `-DeclRefExpr
  //   `-CXXConstructExpr
  //     `-ArraySubscriptExpr
  //       |-ImplicitCastExpr
  //       | `-OpaqueValueExpr
  //       |   `-MemberExpr
  //       |     `-DeclRefExpr
  //       `-ArrayInitIndexExpr
  //
  // The resulting expression for a multidimensional array.
  // ArrayInitLoopExpr                  <-- we're here
  // |-OpaqueValueExpr
  // | `-DeclRefExpr                    <-- match this
  // `-ArrayInitLoopExpr
  //   |-OpaqueValueExpr
  //   | `-ArraySubscriptExpr
  //   |   |-ImplicitCastExpr
  //   |   | `-OpaqueValueExpr
  //   |   |   `-DeclRefExpr
  //   |   `-ArrayInitIndexExpr
  //   `-CXXConstructExpr             <-- extract this
  //     ` ...

  const auto *OVESrc = AILE->getCommonExpr()->getSourceExpr();

  // HACK: There is no way we can put the index of the array element into the
  // CFG unless we unroll the loop, so we manually select and bind the required
  // parameter to the environment.
  const auto *CE =
      cast<CXXConstructExpr>(extractElementInitializerFromNestedAILE(AILE));

  SVal Base = UnknownVal();
  if (const auto *ME = dyn_cast<MemberExpr>(OVESrc))
    Base = State->getSVal(ME, LCtx);
  else if (const auto *DRE = dyn_cast<DeclRefExpr>(OVESrc))
    Base = State->getLValue(cast<VarDecl>(DRE->getDecl()), LCtx);
  else
    llvm_unreachable("ArrayInitLoopExpr contains unexpected source expression");

  SVal NthElem = State->getLValue(CE->getType(), Idx, Base);

  return State->BindExpr(CE->getArg(0), LCtx, NthElem);
}

void ExprEngine::handleConstructor(const Expr *E,
                                   ExplodedNode *Pred,
                                   ExplodedNodeSet &destNodes) {
  const auto *CE = dyn_cast<CXXConstructExpr>(E);
  const auto *CIE = dyn_cast<CXXInheritedCtorInitExpr>(E);
  assert(CE || CIE);

  const LocationContext *LCtx = Pred->getLocationContext();
  ProgramStateRef State = Pred->getState();

  SVal Target = UnknownVal();

  if (CE) {
    if (std::optional<SVal> ElidedTarget =
            getObjectUnderConstruction(State, CE, LCtx)) {
        // We've previously modeled an elidable constructor by pretending that
        // it in fact constructs into the correct target. This constructor can
        // therefore be skipped.
        Target = *ElidedTarget;
        StmtNodeBuilder Bldr(Pred, destNodes, *currBldrCtx);
        State = finishObjectConstruction(State, CE, LCtx);
        if (auto L = Target.getAs<Loc>())
          State = State->BindExpr(CE, LCtx, State->getSVal(*L, CE->getType()));
        Bldr.generateNode(CE, Pred, State);
        return;
    }
  }

  EvalCallOptions CallOpts;
  auto C = getCurrentCFGElement().getAs<CFGConstructor>();
  assert(C || getCurrentCFGElement().getAs<CFGStmt>());
  const ConstructionContext *CC = C ? C->getConstructionContext() : nullptr;

  const CXXConstructionKind CK =
      CE ? CE->getConstructionKind() : CIE->getConstructionKind();
  switch (CK) {
  case CXXConstructionKind::Complete: {
    // Inherited constructors are always base class constructors.
    assert(CE && !CIE && "A complete constructor is inherited?!");

    // If the ctor is part of an ArrayInitLoopExpr, we want to handle it
    // differently.
    auto *AILE = CC ? CC->getArrayInitLoop() : nullptr;

    unsigned Idx = 0;
    if (CE->getType()->isArrayType() || AILE) {

      auto isZeroSizeArray = [&] {
        uint64_t Size = 1;

        if (const auto *CAT = dyn_cast<ConstantArrayType>(CE->getType()))
          Size = getContext().getConstantArrayElementCount(CAT);
        else if (AILE)
          Size = getContext().getArrayInitLoopExprElementCount(AILE);

        return Size == 0;
      };

      // No element construction will happen in a 0 size array.
      if (isZeroSizeArray()) {
        StmtNodeBuilder Bldr(Pred, destNodes, *currBldrCtx);
        static SimpleProgramPointTag T{"ExprEngine",
                                       "Skipping 0 size array construction"};
        Bldr.generateNode(CE, Pred, State, &T);
        return;
      }

      Idx = getIndexOfElementToConstruct(State, CE, LCtx).value_or(0u);
      State = setIndexOfElementToConstruct(State, CE, LCtx, Idx + 1);
    }

    if (AILE) {
      // Only set this once even though we loop through it multiple times.
      if (!getPendingInitLoop(State, CE, LCtx))
        State = setPendingInitLoop(
            State, CE, LCtx,
            getContext().getArrayInitLoopExprElementCount(AILE));

      State = bindRequiredArrayElementToEnvironment(
          State, AILE, LCtx, svalBuilder.makeArrayIndex(Idx));
    }

    // The target region is found from construction context.
    std::tie(State, Target) = handleConstructionContext(
        CE, State, currBldrCtx, LCtx, CC, CallOpts, Idx);
    break;
  }
  case CXXConstructionKind::VirtualBase: {
    // Make sure we are not calling virtual base class initializers twice.
    // Only the most-derived object should initialize virtual base classes.
    const auto *OuterCtor = dyn_cast_or_null<CXXConstructExpr>(
        LCtx->getStackFrame()->getCallSite());
    assert(
        (!OuterCtor ||
         OuterCtor->getConstructionKind() == CXXConstructionKind::Complete ||
         OuterCtor->getConstructionKind() == CXXConstructionKind::Delegating) &&
        ("This virtual base should have already been initialized by "
         "the most derived class!"));
    (void)OuterCtor;
    [[fallthrough]];
  }
  case CXXConstructionKind::NonVirtualBase:
    // In C++17, classes with non-virtual bases may be aggregates, so they would
    // be initialized as aggregates without a constructor call, so we may have
    // a base class constructed directly into an initializer list without
    // having the derived-class constructor call on the previous stack frame.
    // Initializer lists may be nested into more initializer lists that
    // correspond to surrounding aggregate initializations.
    // FIXME: For now this code essentially bails out. We need to find the
    // correct target region and set it.
    // FIXME: Instead of relying on the ParentMap, we should have the
    // trigger-statement (InitListExpr in this case) passed down from CFG or
    // otherwise always available during construction.
    if (isa_and_nonnull<InitListExpr>(LCtx->getParentMap().getParent(E))) {
      MemRegionManager &MRMgr = getSValBuilder().getRegionManager();
      Target = loc::MemRegionVal(MRMgr.getCXXTempObjectRegion(E, LCtx));
      CallOpts.IsCtorOrDtorWithImproperlyModeledTargetRegion = true;
      break;
    }
    [[fallthrough]];
  case CXXConstructionKind::Delegating: {
    const CXXMethodDecl *CurCtor = cast<CXXMethodDecl>(LCtx->getDecl());
    Loc ThisPtr = getSValBuilder().getCXXThis(CurCtor,
                                              LCtx->getStackFrame());
    SVal ThisVal = State->getSVal(ThisPtr);

    if (CK == CXXConstructionKind::Delegating) {
      Target = ThisVal;
    } else {
      // Cast to the base type.
      bool IsVirtual = (CK == CXXConstructionKind::VirtualBase);
      SVal BaseVal =
          getStoreManager().evalDerivedToBase(ThisVal, E->getType(), IsVirtual);
      Target = BaseVal;
    }
    break;
  }
  }

  if (State != Pred->getState()) {
    static SimpleProgramPointTag T("ExprEngine",
                                   "Prepare for object construction");
    ExplodedNodeSet DstPrepare;
    StmtNodeBuilder BldrPrepare(Pred, DstPrepare, *currBldrCtx);
    BldrPrepare.generateNode(E, Pred, State, &T, ProgramPoint::PreStmtKind);
    assert(DstPrepare.size() <= 1);
    if (DstPrepare.size() == 0)
      return;
    Pred = *BldrPrepare.begin();
  }

  const MemRegion *TargetRegion = Target.getAsRegion();
  CallEventManager &CEMgr = getStateManager().getCallEventManager();
  CallEventRef<> Call =
      CIE ? (CallEventRef<>)CEMgr.getCXXInheritedConstructorCall(
                CIE, TargetRegion, State, LCtx, getCFGElementRef())
          : (CallEventRef<>)CEMgr.getCXXConstructorCall(
                CE, TargetRegion, State, LCtx, getCFGElementRef());

  ExplodedNodeSet DstPreVisit;
  getCheckerManager().runCheckersForPreStmt(DstPreVisit, Pred, E, *this);

  ExplodedNodeSet PreInitialized;
  if (CE) {
    // FIXME: Is it possible and/or useful to do this before PreStmt?
    StmtNodeBuilder Bldr(DstPreVisit, PreInitialized, *currBldrCtx);
    for (ExplodedNode *N : DstPreVisit) {
      ProgramStateRef State = N->getState();
      if (CE->requiresZeroInitialization()) {
        // FIXME: Once we properly handle constructors in new-expressions, we'll
        // need to invalidate the region before setting a default value, to make
        // sure there aren't any lingering bindings around. This probably needs
        // to happen regardless of whether or not the object is zero-initialized
        // to handle random fields of a placement-initialized object picking up
        // old bindings. We might only want to do it when we need to, though.
        // FIXME: This isn't actually correct for arrays -- we need to zero-
        // initialize the entire array, not just the first element -- but our
        // handling of arrays everywhere else is weak as well, so this shouldn't
        // actually make things worse. Placement new makes this tricky as well,
        // since it's then possible to be initializing one part of a multi-
        // dimensional array.
        State = State->bindDefaultZero(Target, LCtx);
      }

      Bldr.generateNode(CE, N, State, /*tag=*/nullptr,
                        ProgramPoint::PreStmtKind);
    }
  } else {
    PreInitialized = DstPreVisit;
  }

  ExplodedNodeSet DstPreCall;
  getCheckerManager().runCheckersForPreCall(DstPreCall, PreInitialized,
                                            *Call, *this);

  ExplodedNodeSet DstEvaluated;

  if (CE && CE->getConstructor()->isTrivial() &&
      CE->getConstructor()->isCopyOrMoveConstructor() &&
      !CallOpts.IsArrayCtorOrDtor) {
    StmtNodeBuilder Bldr(DstPreCall, DstEvaluated, *currBldrCtx);
    // FIXME: Handle other kinds of trivial constructors as well.
    for (ExplodedNode *N : DstPreCall)
      performTrivialCopy(Bldr, N, *Call);

  } else {
    for (ExplodedNode *N : DstPreCall)
      getCheckerManager().runCheckersForEvalCall(DstEvaluated, N, *Call, *this,
                                                 CallOpts);
  }

  // If the CFG was constructed without elements for temporary destructors
  // and the just-called constructor created a temporary object then
  // stop exploration if the temporary object has a noreturn constructor.
  // This can lose coverage because the destructor, if it were present
  // in the CFG, would be called at the end of the full expression or
  // later (for life-time extended temporaries) -- but avoids infeasible
  // paths when no-return temporary destructors are used for assertions.
  ExplodedNodeSet DstEvaluatedPostProcessed;
  StmtNodeBuilder Bldr(DstEvaluated, DstEvaluatedPostProcessed, *currBldrCtx);
  const AnalysisDeclContext *ADC = LCtx->getAnalysisDeclContext();
  if (!ADC->getCFGBuildOptions().AddTemporaryDtors) {
    if (llvm::isa_and_nonnull<CXXTempObjectRegion,
                              CXXLifetimeExtendedObjectRegion>(TargetRegion) &&
        cast<CXXConstructorDecl>(Call->getDecl())
            ->getParent()
            ->isAnyDestructorNoReturn()) {

      // If we've inlined the constructor, then DstEvaluated would be empty.
      // In this case we still want a sink, which could be implemented
      // in processCallExit. But we don't have that implemented at the moment,
      // so if you hit this assertion, see if you can avoid inlining
      // the respective constructor when analyzer-config cfg-temporary-dtors
      // is set to false.
      // Otherwise there's nothing wrong with inlining such constructor.
      assert(!DstEvaluated.empty() &&
             "We should not have inlined this constructor!");

      for (ExplodedNode *N : DstEvaluated) {
        Bldr.generateSink(E, N, N->getState());
      }

      // There is no need to run the PostCall and PostStmt checker
      // callbacks because we just generated sinks on all nodes in th
      // frontier.
      return;
    }
  }

  ExplodedNodeSet DstPostArgumentCleanup;
  for (ExplodedNode *I : DstEvaluatedPostProcessed)
    finishArgumentConstruction(DstPostArgumentCleanup, I, *Call);

  // If there were other constructors called for object-type arguments
  // of this constructor, clean them up.
  ExplodedNodeSet DstPostCall;
  getCheckerManager().runCheckersForPostCall(DstPostCall,
                                             DstPostArgumentCleanup,
                                             *Call, *this);
  getCheckerManager().runCheckersForPostStmt(destNodes, DstPostCall, E, *this);
}

void ExprEngine::VisitCXXConstructExpr(const CXXConstructExpr *CE,
                                       ExplodedNode *Pred,
                                       ExplodedNodeSet &Dst) {
  handleConstructor(CE, Pred, Dst);
}

void ExprEngine::VisitCXXInheritedCtorInitExpr(
    const CXXInheritedCtorInitExpr *CE, ExplodedNode *Pred,
    ExplodedNodeSet &Dst) {
  handleConstructor(CE, Pred, Dst);
}

void ExprEngine::VisitCXXDestructor(QualType ObjectType,
                                    const MemRegion *Dest,
                                    const Stmt *S,
                                    bool IsBaseDtor,
                                    ExplodedNode *Pred,
                                    ExplodedNodeSet &Dst,
                                    EvalCallOptions &CallOpts) {
  assert(S && "A destructor without a trigger!");
  const LocationContext *LCtx = Pred->getLocationContext();
  ProgramStateRef State = Pred->getState();

  const CXXRecordDecl *RecordDecl = ObjectType->getAsCXXRecordDecl();
  assert(RecordDecl && "Only CXXRecordDecls should have destructors");
  const CXXDestructorDecl *DtorDecl = RecordDecl->getDestructor();
  // FIXME: There should always be a Decl, otherwise the destructor call
  // shouldn't have been added to the CFG in the first place.
  if (!DtorDecl) {
    // Skip the invalid destructor. We cannot simply return because
    // it would interrupt the analysis instead.
    static SimpleProgramPointTag T("ExprEngine", "SkipInvalidDestructor");
    // FIXME: PostImplicitCall with a null decl may crash elsewhere anyway.
    PostImplicitCall PP(/*Decl=*/nullptr, S->getEndLoc(), LCtx,
                        getCFGElementRef(), &T);
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    Bldr.generateNode(PP, Pred->getState(), Pred);
    return;
  }

  if (!Dest) {
    // We're trying to destroy something that is not a region. This may happen
    // for a variety of reasons (unknown target region, concrete integer instead
    // of target region, etc.). The current code makes an attempt to recover.
    // FIXME: We probably don't really need to recover when we're dealing
    // with concrete integers specifically.
    CallOpts.IsCtorOrDtorWithImproperlyModeledTargetRegion = true;
    if (const Expr *E = dyn_cast_or_null<Expr>(S)) {
      Dest = MRMgr.getCXXTempObjectRegion(E, Pred->getLocationContext());
    } else {
      static SimpleProgramPointTag T("ExprEngine", "SkipInvalidDestructor");
      NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
      Bldr.generateSink(Pred->getLocation().withTag(&T),
                        Pred->getState(), Pred);
      return;
    }
  }

  CallEventManager &CEMgr = getStateManager().getCallEventManager();
  CallEventRef<CXXDestructorCall> Call = CEMgr.getCXXDestructorCall(
      DtorDecl, S, Dest, IsBaseDtor, State, LCtx, getCFGElementRef());

  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                Call->getSourceRange().getBegin(),
                                "Error evaluating destructor");

  ExplodedNodeSet DstPreCall;
  getCheckerManager().runCheckersForPreCall(DstPreCall, Pred,
                                            *Call, *this);

  ExplodedNodeSet DstInvalidated;
  StmtNodeBuilder Bldr(DstPreCall, DstInvalidated, *currBldrCtx);
  for (ExplodedNode *N : DstPreCall)
    defaultEvalCall(Bldr, N, *Call, CallOpts);

  getCheckerManager().runCheckersForPostCall(Dst, DstInvalidated,
                                             *Call, *this);
}

void ExprEngine::VisitCXXNewAllocatorCall(const CXXNewExpr *CNE,
                                          ExplodedNode *Pred,
                                          ExplodedNodeSet &Dst) {
  ProgramStateRef State = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                CNE->getBeginLoc(),
                                "Error evaluating New Allocator Call");
  CallEventManager &CEMgr = getStateManager().getCallEventManager();
  CallEventRef<CXXAllocatorCall> Call =
      CEMgr.getCXXAllocatorCall(CNE, State, LCtx, getCFGElementRef());

  ExplodedNodeSet DstPreCall;
  getCheckerManager().runCheckersForPreCall(DstPreCall, Pred,
                                            *Call, *this);

  ExplodedNodeSet DstPostCall;
  StmtNodeBuilder CallBldr(DstPreCall, DstPostCall, *currBldrCtx);
  for (ExplodedNode *I : DstPreCall) {
    // FIXME: Provide evalCall for checkers?
    defaultEvalCall(CallBldr, I, *Call);
  }
  // If the call is inlined, DstPostCall will be empty and we bail out now.

  // Store return value of operator new() for future use, until the actual
  // CXXNewExpr gets processed.
  ExplodedNodeSet DstPostValue;
  StmtNodeBuilder ValueBldr(DstPostCall, DstPostValue, *currBldrCtx);
  for (ExplodedNode *I : DstPostCall) {
    // FIXME: Because CNE serves as the "call site" for the allocator (due to
    // lack of a better expression in the AST), the conjured return value symbol
    // is going to be of the same type (C++ object pointer type). Technically
    // this is not correct because the operator new's prototype always says that
    // it returns a 'void *'. So we should change the type of the symbol,
    // and then evaluate the cast over the symbolic pointer from 'void *' to
    // the object pointer type. But without changing the symbol's type it
    // is breaking too much to evaluate the no-op symbolic cast over it, so we
    // skip it for now.
    ProgramStateRef State = I->getState();
    SVal RetVal = State->getSVal(CNE, LCtx);
    // [basic.stc.dynamic.allocation] (on the return value of an allocation
    // function):
    // "The order, contiguity, and initial value of storage allocated by
    // successive calls to an allocation function are unspecified."
    State = State->bindDefaultInitial(RetVal, UndefinedVal{}, LCtx);

    // If this allocation function is not declared as non-throwing, failures
    // /must/ be signalled by exceptions, and thus the return value will never
    // be NULL. -fno-exceptions does not influence this semantics.
    // FIXME: GCC has a -fcheck-new option, which forces it to consider the case
    // where new can return NULL. If we end up supporting that option, we can
    // consider adding a check for it here.
    // C++11 [basic.stc.dynamic.allocation]p3.
    if (const FunctionDecl *FD = CNE->getOperatorNew()) {
      QualType Ty = FD->getType();
      if (const auto *ProtoType = Ty->getAs<FunctionProtoType>())
        if (!ProtoType->isNothrow())
          State = State->assume(RetVal.castAs<DefinedOrUnknownSVal>(), true);
    }

    ValueBldr.generateNode(
        CNE, I, addObjectUnderConstruction(State, CNE, LCtx, RetVal));
  }

  ExplodedNodeSet DstPostPostCallCallback;
  getCheckerManager().runCheckersForPostCall(DstPostPostCallCallback,
                                             DstPostValue, *Call, *this);
  for (ExplodedNode *I : DstPostPostCallCallback) {
    getCheckerManager().runCheckersForNewAllocator(*Call, Dst, I, *this);
  }
}

void ExprEngine::VisitCXXNewExpr(const CXXNewExpr *CNE, ExplodedNode *Pred,
                                   ExplodedNodeSet &Dst) {
  // FIXME: Much of this should eventually migrate to CXXAllocatorCall.
  // Also, we need to decide how allocators actually work -- they're not
  // really part of the CXXNewExpr because they happen BEFORE the
  // CXXConstructExpr subexpression. See PR12014 for some discussion.

  unsigned blockCount = currBldrCtx->blockCount();
  const LocationContext *LCtx = Pred->getLocationContext();
  SVal symVal = UnknownVal();
  FunctionDecl *FD = CNE->getOperatorNew();

  bool IsStandardGlobalOpNewFunction =
      FD->isReplaceableGlobalAllocationFunction();

  ProgramStateRef State = Pred->getState();

  // Retrieve the stored operator new() return value.
  if (AMgr.getAnalyzerOptions().MayInlineCXXAllocator) {
    symVal = *getObjectUnderConstruction(State, CNE, LCtx);
    State = finishObjectConstruction(State, CNE, LCtx);
  }

  // We assume all standard global 'operator new' functions allocate memory in
  // heap. We realize this is an approximation that might not correctly model
  // a custom global allocator.
  if (symVal.isUnknown()) {
    if (IsStandardGlobalOpNewFunction)
      symVal = svalBuilder.getConjuredHeapSymbolVal(CNE, LCtx, blockCount);
    else
      symVal = svalBuilder.conjureSymbolVal(nullptr, CNE, LCtx, CNE->getType(),
                                            blockCount);
  }

  CallEventManager &CEMgr = getStateManager().getCallEventManager();
  CallEventRef<CXXAllocatorCall> Call =
      CEMgr.getCXXAllocatorCall(CNE, State, LCtx, getCFGElementRef());

  if (!AMgr.getAnalyzerOptions().MayInlineCXXAllocator) {
    // Invalidate placement args.
    // FIXME: Once we figure out how we want allocators to work,
    // we should be using the usual pre-/(default-)eval-/post-call checkers
    // here.
    State = Call->invalidateRegions(blockCount);
    if (!State)
      return;

    // If this allocation function is not declared as non-throwing, failures
    // /must/ be signalled by exceptions, and thus the return value will never
    // be NULL. -fno-exceptions does not influence this semantics.
    // FIXME: GCC has a -fcheck-new option, which forces it to consider the case
    // where new can return NULL. If we end up supporting that option, we can
    // consider adding a check for it here.
    // C++11 [basic.stc.dynamic.allocation]p3.
    if (const auto *ProtoType = FD->getType()->getAs<FunctionProtoType>())
      if (!ProtoType->isNothrow())
        if (auto dSymVal = symVal.getAs<DefinedOrUnknownSVal>())
          State = State->assume(*dSymVal, true);
  }

  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);

  SVal Result = symVal;

  if (CNE->isArray()) {

    if (const auto *NewReg = cast_or_null<SubRegion>(symVal.getAsRegion())) {
      // If each element is initialized by their default constructor, the field
      // values are properly placed inside the required region, however if an
      // initializer list is used, this doesn't happen automatically.
      auto *Init = CNE->getInitializer();
      bool isInitList = isa_and_nonnull<InitListExpr>(Init);

      QualType ObjTy =
          isInitList ? Init->getType() : CNE->getType()->getPointeeType();
      const ElementRegion *EleReg =
          MRMgr.getElementRegion(ObjTy, svalBuilder.makeArrayIndex(0), NewReg,
                                 svalBuilder.getContext());
      Result = loc::MemRegionVal(EleReg);

      // If the array is list initialized, we bind the initializer list to the
      // memory region here, otherwise we would lose it.
      if (isInitList) {
        Bldr.takeNodes(Pred);
        Pred = Bldr.generateNode(CNE, Pred, State);

        SVal V = State->getSVal(Init, LCtx);
        ExplodedNodeSet evaluated;
        evalBind(evaluated, CNE, Pred, Result, V, true);

        Bldr.takeNodes(Pred);
        Bldr.addNodes(evaluated);

        Pred = *evaluated.begin();
        State = Pred->getState();
      }
    }

    State = State->BindExpr(CNE, Pred->getLocationContext(), Result);
    Bldr.generateNode(CNE, Pred, State);
    return;
  }

  // FIXME: Once we have proper support for CXXConstructExprs inside
  // CXXNewExpr, we need to make sure that the constructed object is not
  // immediately invalidated here. (The placement call should happen before
  // the constructor call anyway.)
  if (FD->isReservedGlobalPlacementOperator()) {
    // Non-array placement new should always return the placement location.
    SVal PlacementLoc = State->getSVal(CNE->getPlacementArg(0), LCtx);
    Result = svalBuilder.evalCast(PlacementLoc, CNE->getType(),
                                  CNE->getPlacementArg(0)->getType());
  }

  // Bind the address of the object, then check to see if we cached out.
  State = State->BindExpr(CNE, LCtx, Result);
  ExplodedNode *NewN = Bldr.generateNode(CNE, Pred, State);
  if (!NewN)
    return;

  // If the type is not a record, we won't have a CXXConstructExpr as an
  // initializer. Copy the value over.
  if (const Expr *Init = CNE->getInitializer()) {
    if (!isa<CXXConstructExpr>(Init)) {
      assert(Bldr.getResults().size() == 1);
      Bldr.takeNodes(NewN);
      evalBind(Dst, CNE, NewN, Result, State->getSVal(Init, LCtx),
               /*FirstInit=*/IsStandardGlobalOpNewFunction);
    }
  }
}

void ExprEngine::VisitCXXDeleteExpr(const CXXDeleteExpr *CDE,
                                    ExplodedNode *Pred, ExplodedNodeSet &Dst) {

  CallEventManager &CEMgr = getStateManager().getCallEventManager();
  CallEventRef<CXXDeallocatorCall> Call = CEMgr.getCXXDeallocatorCall(
      CDE, Pred->getState(), Pred->getLocationContext(), getCFGElementRef());

  ExplodedNodeSet DstPreCall;
  getCheckerManager().runCheckersForPreCall(DstPreCall, Pred, *Call, *this);
  ExplodedNodeSet DstPostCall;

  if (AMgr.getAnalyzerOptions().MayInlineCXXAllocator) {
    StmtNodeBuilder Bldr(DstPreCall, DstPostCall, *currBldrCtx);
    for (ExplodedNode *I : DstPreCall) {
      defaultEvalCall(Bldr, I, *Call);
    }
  } else {
    DstPostCall = DstPreCall;
  }
  getCheckerManager().runCheckersForPostCall(Dst, DstPostCall, *Call, *this);
}

void ExprEngine::VisitCXXCatchStmt(const CXXCatchStmt *CS, ExplodedNode *Pred,
                                   ExplodedNodeSet &Dst) {
  const VarDecl *VD = CS->getExceptionDecl();
  if (!VD) {
    Dst.Add(Pred);
    return;
  }

  const LocationContext *LCtx = Pred->getLocationContext();
  SVal V = svalBuilder.conjureSymbolVal(CS, LCtx, VD->getType(),
                                        currBldrCtx->blockCount());
  ProgramStateRef state = Pred->getState();
  state = state->bindLoc(state->getLValue(VD, LCtx), V, LCtx);

  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  Bldr.generateNode(CS, Pred, state);
}

void ExprEngine::VisitCXXThisExpr(const CXXThisExpr *TE, ExplodedNode *Pred,
                                    ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);

  // Get the this object region from StoreManager.
  const LocationContext *LCtx = Pred->getLocationContext();
  const MemRegion *R =
    svalBuilder.getRegionManager().getCXXThisRegion(
                                  getContext().getCanonicalType(TE->getType()),
                                                    LCtx);

  ProgramStateRef state = Pred->getState();
  SVal V = state->getSVal(loc::MemRegionVal(R));
  Bldr.generateNode(TE, Pred, state->BindExpr(TE, LCtx, V));
}

void ExprEngine::VisitLambdaExpr(const LambdaExpr *LE, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  const LocationContext *LocCtxt = Pred->getLocationContext();

  // Get the region of the lambda itself.
  const MemRegion *R = svalBuilder.getRegionManager().getCXXTempObjectRegion(
      LE, LocCtxt);
  SVal V = loc::MemRegionVal(R);

  ProgramStateRef State = Pred->getState();

  // If we created a new MemRegion for the lambda, we should explicitly bind
  // the captures.
  for (auto const [Idx, FieldForCapture, InitExpr] :
       llvm::zip(llvm::seq<unsigned>(0, -1), LE->getLambdaClass()->fields(),
                 LE->capture_inits())) {
    SVal FieldLoc = State->getLValue(FieldForCapture, V);

    SVal InitVal;
    if (!FieldForCapture->hasCapturedVLAType()) {
      assert(InitExpr && "Capture missing initialization expression");

      // Capturing a 0 length array is a no-op, so we ignore it to get a more
      // accurate analysis. If it's not ignored, it would set the default
      // binding of the lambda to 'Unknown', which can lead to falsely detecting
      // 'Uninitialized' values as 'Unknown' and not reporting a warning.
      const auto FTy = FieldForCapture->getType();
      if (FTy->isConstantArrayType() &&
          getContext().getConstantArrayElementCount(
              getContext().getAsConstantArrayType(FTy)) == 0)
        continue;

      // With C++17 copy elision the InitExpr can be anything, so instead of
      // pattern matching all cases, we simple check if the current field is
      // under construction or not, regardless what it's InitExpr is.
      if (const auto OUC =
              getObjectUnderConstruction(State, {LE, Idx}, LocCtxt)) {
        InitVal = State->getSVal(OUC->getAsRegion());

        State = finishObjectConstruction(State, {LE, Idx}, LocCtxt);
      } else
        InitVal = State->getSVal(InitExpr, LocCtxt);

    } else {

      assert(!getObjectUnderConstruction(State, {LE, Idx}, LocCtxt) &&
             "VLA capture by value is a compile time error!");

      // The field stores the length of a captured variable-length array.
      // These captures don't have initialization expressions; instead we
      // get the length from the VLAType size expression.
      Expr *SizeExpr = FieldForCapture->getCapturedVLAType()->getSizeExpr();
      InitVal = State->getSVal(SizeExpr, LocCtxt);
    }

    State = State->bindLoc(FieldLoc, InitVal, LocCtxt);
  }

  // Decay the Loc into an RValue, because there might be a
  // MaterializeTemporaryExpr node above this one which expects the bound value
  // to be an RValue.
  SVal LambdaRVal = State->getSVal(R);

  ExplodedNodeSet Tmp;
  StmtNodeBuilder Bldr(Pred, Tmp, *currBldrCtx);
  // FIXME: is this the right program point kind?
  Bldr.generateNode(LE, Pred,
                    State->BindExpr(LE, LocCtxt, LambdaRVal),
                    nullptr, ProgramPoint::PostLValueKind);

  // FIXME: Move all post/pre visits to ::Visit().
  getCheckerManager().runCheckersForPostStmt(Dst, Tmp, LE, *this);
}
