//=-- ExprEngineObjC.cpp - ExprEngine support for Objective-C ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines ExprEngine's support for Objective-C expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtObjC.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"

using namespace clang;
using namespace ento;

void ExprEngine::VisitLvalObjCIvarRefExpr(const ObjCIvarRefExpr *Ex,
                                          ExplodedNode *Pred,
                                          ExplodedNodeSet &Dst) {
  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();
  SVal baseVal = state->getSVal(Ex->getBase(), LCtx);
  SVal location = state->getLValue(Ex->getDecl(), baseVal);

  ExplodedNodeSet dstIvar;
  StmtNodeBuilder Bldr(Pred, dstIvar, *currBldrCtx);
  Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, location));

  // Perform the post-condition check of the ObjCIvarRefExpr and store
  // the created nodes in 'Dst'.
  getCheckerManager().runCheckersForPostStmt(Dst, dstIvar, Ex, *this);
}

void ExprEngine::VisitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt *S,
                                             ExplodedNode *Pred,
                                             ExplodedNodeSet &Dst) {
  getCheckerManager().runCheckersForPreStmt(Dst, Pred, S, *this);
}

/// Generate a node in \p Bldr for an iteration statement using ObjC
/// for-loop iterator.
static void populateObjCForDestinationSet(
    ExplodedNodeSet &dstLocation, SValBuilder &svalBuilder,
    const ObjCForCollectionStmt *S, const Stmt *elem, SVal elementV,
    SymbolManager &SymMgr, const NodeBuilderContext *currBldrCtx,
    StmtNodeBuilder &Bldr, bool hasElements) {

  for (ExplodedNode *Pred : dstLocation) {
    ProgramStateRef state = Pred->getState();
    const LocationContext *LCtx = Pred->getLocationContext();

    ProgramStateRef nextState =
        ExprEngine::setWhetherHasMoreIteration(state, S, LCtx, hasElements);

    if (auto MV = elementV.getAs<loc::MemRegionVal>())
      if (const auto *R = dyn_cast<TypedValueRegion>(MV->getRegion())) {
        // FIXME: The proper thing to do is to really iterate over the
        //  container.  We will do this with dispatch logic to the store.
        //  For now, just 'conjure' up a symbolic value.
        QualType T = R->getValueType();
        assert(Loc::isLocType(T));

        SVal V;
        if (hasElements) {
          SymbolRef Sym = SymMgr.conjureSymbol(elem, LCtx, T,
                                               currBldrCtx->blockCount());
          V = svalBuilder.makeLoc(Sym);
        } else {
          V = svalBuilder.makeIntVal(0, T);
        }

        nextState = nextState->bindLoc(elementV, V, LCtx);
      }

    Bldr.generateNode(S, Pred, nextState);
  }
}

void ExprEngine::VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S,
                                            ExplodedNode *Pred,
                                            ExplodedNodeSet &Dst) {

  // ObjCForCollectionStmts are processed in two places.  This method
  // handles the case where an ObjCForCollectionStmt* occurs as one of the
  // statements within a basic block.  This transfer function does two things:
  //
  //  (1) binds the next container value to 'element'.  This creates a new
  //      node in the ExplodedGraph.
  //
  //  (2) note whether the collection has any more elements (or in other words,
  //      whether the loop has more iterations). This will be tested in
  //      processBranch.
  //
  // FIXME: Eventually this logic should actually do dispatches to
  //   'countByEnumeratingWithState:objects:count:' (NSFastEnumeration).
  //   This will require simulating a temporary NSFastEnumerationState, either
  //   through an SVal or through the use of MemRegions.  This value can
  //   be affixed to the ObjCForCollectionStmt* instead of 0/1; when the loop
  //   terminates we reclaim the temporary (it goes out of scope) and we
  //   we can test if the SVal is 0 or if the MemRegion is null (depending
  //   on what approach we take).
  //
  //  For now: simulate (1) by assigning either a symbol or nil if the
  //    container is empty.  Thus this transfer function will by default
  //    result in state splitting.

  const Stmt *elem = S->getElement();
  const Stmt *collection = S->getCollection();
  ProgramStateRef state = Pred->getState();
  SVal collectionV = state->getSVal(collection, Pred->getLocationContext());

  SVal elementV;
  if (const auto *DS = dyn_cast<DeclStmt>(elem)) {
    const VarDecl *elemD = cast<VarDecl>(DS->getSingleDecl());
    assert(elemD->getInit() == nullptr);
    elementV = state->getLValue(elemD, Pred->getLocationContext());
  } else {
    elementV = state->getSVal(elem, Pred->getLocationContext());
  }

  bool isContainerNull = state->isNull(collectionV).isConstrainedTrue();

  ExplodedNodeSet dstLocation;
  evalLocation(dstLocation, S, elem, Pred, state, elementV, false);

  ExplodedNodeSet Tmp;
  StmtNodeBuilder Bldr(Pred, Tmp, *currBldrCtx);

  if (!isContainerNull)
    populateObjCForDestinationSet(dstLocation, svalBuilder, S, elem, elementV,
                                  SymMgr, currBldrCtx, Bldr,
                                  /*hasElements=*/true);

  populateObjCForDestinationSet(dstLocation, svalBuilder, S, elem, elementV,
                                SymMgr, currBldrCtx, Bldr,
                                /*hasElements=*/false);

  // Finally, run any custom checkers.
  // FIXME: Eventually all pre- and post-checks should live in VisitStmt.
  getCheckerManager().runCheckersForPostStmt(Dst, Tmp, S, *this);
}

void ExprEngine::VisitObjCMessage(const ObjCMessageExpr *ME,
                                  ExplodedNode *Pred,
                                  ExplodedNodeSet &Dst) {
  CallEventManager &CEMgr = getStateManager().getCallEventManager();
  CallEventRef<ObjCMethodCall> Msg = CEMgr.getObjCMethodCall(
      ME, Pred->getState(), Pred->getLocationContext(), getCFGElementRef());

  // There are three cases for the receiver:
  //   (1) it is definitely nil,
  //   (2) it is definitely non-nil, and
  //   (3) we don't know.
  //
  // If the receiver is definitely nil, we skip the pre/post callbacks and
  // instead call the ObjCMessageNil callbacks and return.
  //
  // If the receiver is definitely non-nil, we call the pre- callbacks,
  // evaluate the call, and call the post- callbacks.
  //
  // If we don't know, we drop the potential nil flow and instead
  // continue from the assumed non-nil state as in (2). This approach
  // intentionally drops coverage in order to prevent false alarms
  // in the following scenario:
  //
  //   id result = [o someMethod]
  //   if (result) {
  //     if (!o) {
  //       // <-- This program point should be unreachable because if o is nil
  //       // it must the case that result is nil as well.
  //     }
  //   }
  //
  // However, it also loses coverage of the nil path prematurely,
  // leading to missed reports.
  //
  // It's possible to handle this by performing a state split on every call:
  // explore the state where the receiver is non-nil, and independently
  // explore the state where it's nil. But this is not only slow, but
  // completely unwarranted. The mere presence of the message syntax in the code
  // isn't sufficient evidence that nil is a realistic possibility.
  //
  // An ideal solution would be to add the following constraint that captures
  // both possibilities without splitting the state:
  //
  //   ($x == 0) => ($y == 0)                                                (1)
  //
  // where in our case '$x' is the receiver symbol, '$y' is the returned symbol,
  // and '=>' is logical implication. But RangeConstraintManager can't handle
  // such constraints yet, so for now we go with a simpler, more restrictive
  // constraint: $x != 0, from which (1) follows as a vacuous truth.
  if (Msg->isInstanceMessage()) {
    SVal recVal = Msg->getReceiverSVal();
    if (!recVal.isUndef()) {
      // Bifurcate the state into nil and non-nil ones.
      DefinedOrUnknownSVal receiverVal =
          recVal.castAs<DefinedOrUnknownSVal>();
      ProgramStateRef State = Pred->getState();

      ProgramStateRef notNilState, nilState;
      std::tie(notNilState, nilState) = State->assume(receiverVal);

      // Receiver is definitely nil, so run ObjCMessageNil callbacks and return.
      if (nilState && !notNilState) {
        ExplodedNodeSet dstNil;
        StmtNodeBuilder Bldr(Pred, dstNil, *currBldrCtx);
        bool HasTag = Pred->getLocation().getTag();
        Pred = Bldr.generateNode(ME, Pred, nilState, nullptr,
                                 ProgramPoint::PreStmtKind);
        assert((Pred || HasTag) && "Should have cached out already!");
        (void)HasTag;
        if (!Pred)
          return;

        ExplodedNodeSet dstPostCheckers;
        getCheckerManager().runCheckersForObjCMessageNil(dstPostCheckers, Pred,
                                                         *Msg, *this);
        for (auto *I : dstPostCheckers)
          finishArgumentConstruction(Dst, I, *Msg);
        return;
      }

      ExplodedNodeSet dstNonNil;
      StmtNodeBuilder Bldr(Pred, dstNonNil, *currBldrCtx);
      // Generate a transition to the non-nil state, dropping any potential
      // nil flow.
      if (notNilState != State) {
        bool HasTag = Pred->getLocation().getTag();
        Pred = Bldr.generateNode(ME, Pred, notNilState);
        assert((Pred || HasTag) && "Should have cached out already!");
        (void)HasTag;
        if (!Pred)
          return;
      }
    }
  }

  // Handle the previsits checks.
  ExplodedNodeSet dstPrevisit;
  getCheckerManager().runCheckersForPreObjCMessage(dstPrevisit, Pred,
                                                   *Msg, *this);
  ExplodedNodeSet dstGenericPrevisit;
  getCheckerManager().runCheckersForPreCall(dstGenericPrevisit, dstPrevisit,
                                            *Msg, *this);

  // Proceed with evaluate the message expression.
  ExplodedNodeSet dstEval;
  StmtNodeBuilder Bldr(dstGenericPrevisit, dstEval, *currBldrCtx);

  for (ExplodedNodeSet::iterator DI = dstGenericPrevisit.begin(),
       DE = dstGenericPrevisit.end(); DI != DE; ++DI) {
    ExplodedNode *Pred = *DI;
    ProgramStateRef State = Pred->getState();
    CallEventRef<ObjCMethodCall> UpdatedMsg = Msg.cloneWithState(State);

    if (UpdatedMsg->isInstanceMessage()) {
      SVal recVal = UpdatedMsg->getReceiverSVal();
      if (!recVal.isUndef()) {
        if (ObjCNoRet.isImplicitNoReturn(ME)) {
          // If we raise an exception, for now treat it as a sink.
          // Eventually we will want to handle exceptions properly.
          Bldr.generateSink(ME, Pred, State);
          continue;
        }
      }
    } else {
      // Check for special class methods that are known to not return
      // and that we should treat as a sink.
      if (ObjCNoRet.isImplicitNoReturn(ME)) {
        // If we raise an exception, for now treat it as a sink.
        // Eventually we will want to handle exceptions properly.
        Bldr.generateSink(ME, Pred, Pred->getState());
        continue;
      }
    }

    defaultEvalCall(Bldr, Pred, *UpdatedMsg);
  }

  // If there were constructors called for object-type arguments, clean them up.
  ExplodedNodeSet dstArgCleanup;
  for (auto *I : dstEval)
    finishArgumentConstruction(dstArgCleanup, I, *Msg);

  ExplodedNodeSet dstPostvisit;
  getCheckerManager().runCheckersForPostCall(dstPostvisit, dstArgCleanup,
                                             *Msg, *this);

  // Finally, perform the post-condition check of the ObjCMessageExpr and store
  // the created nodes in 'Dst'.
  getCheckerManager().runCheckersForPostObjCMessage(Dst, dstPostvisit,
                                                    *Msg, *this);
}
