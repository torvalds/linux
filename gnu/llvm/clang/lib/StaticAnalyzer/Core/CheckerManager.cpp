//===- CheckerManager.cpp - Static Analyzer Checker Manager ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Static Analyzer Checker Manager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/JsonSupport.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CoreEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include <cassert>
#include <optional>
#include <vector>

using namespace clang;
using namespace ento;

bool CheckerManager::hasPathSensitiveCheckers() const {
  const auto IfAnyAreNonEmpty = [](const auto &... Callbacks) -> bool {
    return (!Callbacks.empty() || ...);
  };
  return IfAnyAreNonEmpty(
      StmtCheckers, PreObjCMessageCheckers, ObjCMessageNilCheckers,
      PostObjCMessageCheckers, PreCallCheckers, PostCallCheckers,
      LocationCheckers, BindCheckers, EndAnalysisCheckers,
      BeginFunctionCheckers, EndFunctionCheckers, BranchConditionCheckers,
      NewAllocatorCheckers, LiveSymbolsCheckers, DeadSymbolsCheckers,
      RegionChangesCheckers, PointerEscapeCheckers, EvalAssumeCheckers,
      EvalCallCheckers, EndOfTranslationUnitCheckers);
}

void CheckerManager::finishedCheckerRegistration() {
#ifndef NDEBUG
  // Make sure that for every event that has listeners, there is at least
  // one dispatcher registered for it.
  for (const auto &Event : Events)
    assert(Event.second.HasDispatcher &&
           "No dispatcher registered for an event");
#endif
}

void CheckerManager::reportInvalidCheckerOptionValue(
    const CheckerBase *C, StringRef OptionName,
    StringRef ExpectedValueDesc) const {

  getDiagnostics().Report(diag::err_analyzer_checker_option_invalid_input)
      << (llvm::Twine() + C->getTagDescription() + ":" + OptionName).str()
      << ExpectedValueDesc;
}

//===----------------------------------------------------------------------===//
// Functions for running checkers for AST traversing..
//===----------------------------------------------------------------------===//

void CheckerManager::runCheckersOnASTDecl(const Decl *D, AnalysisManager& mgr,
                                          BugReporter &BR) {
  assert(D);

  unsigned DeclKind = D->getKind();
  CachedDeclCheckers *checkers = nullptr;
  CachedDeclCheckersMapTy::iterator CCI = CachedDeclCheckersMap.find(DeclKind);
  if (CCI != CachedDeclCheckersMap.end()) {
    checkers = &(CCI->second);
  } else {
    // Find the checkers that should run for this Decl and cache them.
    checkers = &CachedDeclCheckersMap[DeclKind];
    for (const auto &info : DeclCheckers)
      if (info.IsForDeclFn(D))
        checkers->push_back(info.CheckFn);
  }

  assert(checkers);
  for (const auto &checker : *checkers)
    checker(D, mgr, BR);
}

void CheckerManager::runCheckersOnASTBody(const Decl *D, AnalysisManager& mgr,
                                          BugReporter &BR) {
  assert(D && D->hasBody());

  for (const auto &BodyChecker : BodyCheckers)
    BodyChecker(D, mgr, BR);
}

//===----------------------------------------------------------------------===//
// Functions for running checkers for path-sensitive checking.
//===----------------------------------------------------------------------===//

template <typename CHECK_CTX>
static void expandGraphWithCheckers(CHECK_CTX checkCtx,
                                    ExplodedNodeSet &Dst,
                                    const ExplodedNodeSet &Src) {
  const NodeBuilderContext &BldrCtx = checkCtx.Eng.getBuilderContext();
  if (Src.empty())
    return;

  typename CHECK_CTX::CheckersTy::const_iterator
      I = checkCtx.checkers_begin(), E = checkCtx.checkers_end();
  if (I == E) {
    Dst.insert(Src);
    return;
  }

  ExplodedNodeSet Tmp1, Tmp2;
  const ExplodedNodeSet *PrevSet = &Src;

  for (; I != E; ++I) {
    ExplodedNodeSet *CurrSet = nullptr;
    if (I+1 == E)
      CurrSet = &Dst;
    else {
      CurrSet = (PrevSet == &Tmp1) ? &Tmp2 : &Tmp1;
      CurrSet->clear();
    }

    NodeBuilder B(*PrevSet, *CurrSet, BldrCtx);
    for (const auto &NI : *PrevSet)
      checkCtx.runChecker(*I, B, NI);

    // If all the produced transitions are sinks, stop.
    if (CurrSet->empty())
      return;

    // Update which NodeSet is the current one.
    PrevSet = CurrSet;
  }
}

namespace {

  struct CheckStmtContext {
    using CheckersTy = SmallVectorImpl<CheckerManager::CheckStmtFunc>;

    bool IsPreVisit;
    const CheckersTy &Checkers;
    const Stmt *S;
    ExprEngine &Eng;
    bool WasInlined;

    CheckStmtContext(bool isPreVisit, const CheckersTy &checkers,
                     const Stmt *s, ExprEngine &eng, bool wasInlined = false)
        : IsPreVisit(isPreVisit), Checkers(checkers), S(s), Eng(eng),
          WasInlined(wasInlined) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckStmtFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      // FIXME: Remove respondsToCallback from CheckerContext;
      ProgramPoint::Kind K =  IsPreVisit ? ProgramPoint::PreStmtKind :
                                           ProgramPoint::PostStmtKind;
      const ProgramPoint &L = ProgramPoint::getProgramPoint(S, K,
                                Pred->getLocationContext(), checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L, WasInlined);
      checkFn(S, C);
    }
  };

} // namespace

/// Run checkers for visiting Stmts.
void CheckerManager::runCheckersForStmt(bool isPreVisit,
                                        ExplodedNodeSet &Dst,
                                        const ExplodedNodeSet &Src,
                                        const Stmt *S,
                                        ExprEngine &Eng,
                                        bool WasInlined) {
  CheckStmtContext C(isPreVisit, getCachedStmtCheckersFor(S, isPreVisit),
                     S, Eng, WasInlined);
  expandGraphWithCheckers(C, Dst, Src);
}

namespace {

  struct CheckObjCMessageContext {
    using CheckersTy = std::vector<CheckerManager::CheckObjCMessageFunc>;

    ObjCMessageVisitKind Kind;
    bool WasInlined;
    const CheckersTy &Checkers;
    const ObjCMethodCall &Msg;
    ExprEngine &Eng;

    CheckObjCMessageContext(ObjCMessageVisitKind visitKind,
                            const CheckersTy &checkers,
                            const ObjCMethodCall &msg, ExprEngine &eng,
                            bool wasInlined)
        : Kind(visitKind), WasInlined(wasInlined), Checkers(checkers), Msg(msg),
          Eng(eng) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckObjCMessageFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      bool IsPreVisit;

      switch (Kind) {
        case ObjCMessageVisitKind::Pre:
          IsPreVisit = true;
          break;
        case ObjCMessageVisitKind::MessageNil:
        case ObjCMessageVisitKind::Post:
          IsPreVisit = false;
          break;
      }

      const ProgramPoint &L = Msg.getProgramPoint(IsPreVisit,checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L, WasInlined);

      checkFn(*Msg.cloneWithState<ObjCMethodCall>(Pred->getState()), C);
    }
  };

} // namespace

/// Run checkers for visiting obj-c messages.
void CheckerManager::runCheckersForObjCMessage(ObjCMessageVisitKind visitKind,
                                               ExplodedNodeSet &Dst,
                                               const ExplodedNodeSet &Src,
                                               const ObjCMethodCall &msg,
                                               ExprEngine &Eng,
                                               bool WasInlined) {
  const auto &checkers = getObjCMessageCheckers(visitKind);
  CheckObjCMessageContext C(visitKind, checkers, msg, Eng, WasInlined);
  expandGraphWithCheckers(C, Dst, Src);
}

const std::vector<CheckerManager::CheckObjCMessageFunc> &
CheckerManager::getObjCMessageCheckers(ObjCMessageVisitKind Kind) const {
  switch (Kind) {
  case ObjCMessageVisitKind::Pre:
    return PreObjCMessageCheckers;
    break;
  case ObjCMessageVisitKind::Post:
    return PostObjCMessageCheckers;
  case ObjCMessageVisitKind::MessageNil:
    return ObjCMessageNilCheckers;
  }
  llvm_unreachable("Unknown Kind");
}

namespace {

  // FIXME: This has all the same signatures as CheckObjCMessageContext.
  // Is there a way we can merge the two?
  struct CheckCallContext {
    using CheckersTy = std::vector<CheckerManager::CheckCallFunc>;

    bool IsPreVisit, WasInlined;
    const CheckersTy &Checkers;
    const CallEvent &Call;
    ExprEngine &Eng;

    CheckCallContext(bool isPreVisit, const CheckersTy &checkers,
                     const CallEvent &call, ExprEngine &eng,
                     bool wasInlined)
        : IsPreVisit(isPreVisit), WasInlined(wasInlined), Checkers(checkers),
          Call(call), Eng(eng) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckCallFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      const ProgramPoint &L = Call.getProgramPoint(IsPreVisit,checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L, WasInlined);

      checkFn(*Call.cloneWithState(Pred->getState()), C);
    }
  };

} // namespace

/// Run checkers for visiting an abstract call event.
void CheckerManager::runCheckersForCallEvent(bool isPreVisit,
                                             ExplodedNodeSet &Dst,
                                             const ExplodedNodeSet &Src,
                                             const CallEvent &Call,
                                             ExprEngine &Eng,
                                             bool WasInlined) {
  CheckCallContext C(isPreVisit,
                     isPreVisit ? PreCallCheckers
                                : PostCallCheckers,
                     Call, Eng, WasInlined);
  expandGraphWithCheckers(C, Dst, Src);
}

namespace {

  struct CheckLocationContext {
    using CheckersTy = std::vector<CheckerManager::CheckLocationFunc>;

    const CheckersTy &Checkers;
    SVal Loc;
    bool IsLoad;
    const Stmt *NodeEx; /* Will become a CFGStmt */
    const Stmt *BoundEx;
    ExprEngine &Eng;

    CheckLocationContext(const CheckersTy &checkers,
                         SVal loc, bool isLoad, const Stmt *NodeEx,
                         const Stmt *BoundEx,
                         ExprEngine &eng)
        : Checkers(checkers), Loc(loc), IsLoad(isLoad), NodeEx(NodeEx),
          BoundEx(BoundEx), Eng(eng) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckLocationFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      ProgramPoint::Kind K =  IsLoad ? ProgramPoint::PreLoadKind :
                                       ProgramPoint::PreStoreKind;
      const ProgramPoint &L =
        ProgramPoint::getProgramPoint(NodeEx, K,
                                      Pred->getLocationContext(),
                                      checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L);
      checkFn(Loc, IsLoad, BoundEx, C);
    }
  };

} // namespace

/// Run checkers for load/store of a location.

void CheckerManager::runCheckersForLocation(ExplodedNodeSet &Dst,
                                            const ExplodedNodeSet &Src,
                                            SVal location, bool isLoad,
                                            const Stmt *NodeEx,
                                            const Stmt *BoundEx,
                                            ExprEngine &Eng) {
  CheckLocationContext C(LocationCheckers, location, isLoad, NodeEx,
                         BoundEx, Eng);
  expandGraphWithCheckers(C, Dst, Src);
}

namespace {

  struct CheckBindContext {
    using CheckersTy = std::vector<CheckerManager::CheckBindFunc>;

    const CheckersTy &Checkers;
    SVal Loc;
    SVal Val;
    const Stmt *S;
    ExprEngine &Eng;
    const ProgramPoint &PP;

    CheckBindContext(const CheckersTy &checkers,
                     SVal loc, SVal val, const Stmt *s, ExprEngine &eng,
                     const ProgramPoint &pp)
        : Checkers(checkers), Loc(loc), Val(val), S(s), Eng(eng), PP(pp) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckBindFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      const ProgramPoint &L = PP.withTag(checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L);

      checkFn(Loc, Val, S, C);
    }
  };

} // namespace

/// Run checkers for binding of a value to a location.
void CheckerManager::runCheckersForBind(ExplodedNodeSet &Dst,
                                        const ExplodedNodeSet &Src,
                                        SVal location, SVal val,
                                        const Stmt *S, ExprEngine &Eng,
                                        const ProgramPoint &PP) {
  CheckBindContext C(BindCheckers, location, val, S, Eng, PP);
  expandGraphWithCheckers(C, Dst, Src);
}

void CheckerManager::runCheckersForEndAnalysis(ExplodedGraph &G,
                                               BugReporter &BR,
                                               ExprEngine &Eng) {
  for (const auto &EndAnalysisChecker : EndAnalysisCheckers)
    EndAnalysisChecker(G, BR, Eng);
}

namespace {

struct CheckBeginFunctionContext {
  using CheckersTy = std::vector<CheckerManager::CheckBeginFunctionFunc>;

  const CheckersTy &Checkers;
  ExprEngine &Eng;
  const ProgramPoint &PP;

  CheckBeginFunctionContext(const CheckersTy &Checkers, ExprEngine &Eng,
                            const ProgramPoint &PP)
      : Checkers(Checkers), Eng(Eng), PP(PP) {}

  CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
  CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

  void runChecker(CheckerManager::CheckBeginFunctionFunc checkFn,
                  NodeBuilder &Bldr, ExplodedNode *Pred) {
    const ProgramPoint &L = PP.withTag(checkFn.Checker);
    CheckerContext C(Bldr, Eng, Pred, L);

    checkFn(C);
  }
};

} // namespace

void CheckerManager::runCheckersForBeginFunction(ExplodedNodeSet &Dst,
                                                 const BlockEdge &L,
                                                 ExplodedNode *Pred,
                                                 ExprEngine &Eng) {
  ExplodedNodeSet Src;
  Src.insert(Pred);
  CheckBeginFunctionContext C(BeginFunctionCheckers, Eng, L);
  expandGraphWithCheckers(C, Dst, Src);
}

/// Run checkers for end of path.
// Note, We do not chain the checker output (like in expandGraphWithCheckers)
// for this callback since end of path nodes are expected to be final.
void CheckerManager::runCheckersForEndFunction(NodeBuilderContext &BC,
                                               ExplodedNodeSet &Dst,
                                               ExplodedNode *Pred,
                                               ExprEngine &Eng,
                                               const ReturnStmt *RS) {
  // We define the builder outside of the loop because if at least one checker
  // creates a successor for Pred, we do not need to generate an
  // autotransition for it.
  NodeBuilder Bldr(Pred, Dst, BC);
  for (const auto &checkFn : EndFunctionCheckers) {
    const ProgramPoint &L =
        FunctionExitPoint(RS, Pred->getLocationContext(), checkFn.Checker);
    CheckerContext C(Bldr, Eng, Pred, L);
    checkFn(RS, C);
  }
}

namespace {

  struct CheckBranchConditionContext {
    using CheckersTy = std::vector<CheckerManager::CheckBranchConditionFunc>;

    const CheckersTy &Checkers;
    const Stmt *Condition;
    ExprEngine &Eng;

    CheckBranchConditionContext(const CheckersTy &checkers,
                                const Stmt *Cond, ExprEngine &eng)
        : Checkers(checkers), Condition(Cond), Eng(eng) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckBranchConditionFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      ProgramPoint L = PostCondition(Condition, Pred->getLocationContext(),
                                     checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L);
      checkFn(Condition, C);
    }
  };

} // namespace

/// Run checkers for branch condition.
void CheckerManager::runCheckersForBranchCondition(const Stmt *Condition,
                                                   ExplodedNodeSet &Dst,
                                                   ExplodedNode *Pred,
                                                   ExprEngine &Eng) {
  ExplodedNodeSet Src;
  Src.insert(Pred);
  CheckBranchConditionContext C(BranchConditionCheckers, Condition, Eng);
  expandGraphWithCheckers(C, Dst, Src);
}

namespace {

  struct CheckNewAllocatorContext {
    using CheckersTy = std::vector<CheckerManager::CheckNewAllocatorFunc>;

    const CheckersTy &Checkers;
    const CXXAllocatorCall &Call;
    bool WasInlined;
    ExprEngine &Eng;

    CheckNewAllocatorContext(const CheckersTy &Checkers,
                             const CXXAllocatorCall &Call, bool WasInlined,
                             ExprEngine &Eng)
        : Checkers(Checkers), Call(Call), WasInlined(WasInlined), Eng(Eng) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckNewAllocatorFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      ProgramPoint L =
          PostAllocatorCall(Call.getOriginExpr(), Pred->getLocationContext());
      CheckerContext C(Bldr, Eng, Pred, L, WasInlined);
      checkFn(cast<CXXAllocatorCall>(*Call.cloneWithState(Pred->getState())),
              C);
    }
  };

} // namespace

void CheckerManager::runCheckersForNewAllocator(const CXXAllocatorCall &Call,
                                                ExplodedNodeSet &Dst,
                                                ExplodedNode *Pred,
                                                ExprEngine &Eng,
                                                bool WasInlined) {
  ExplodedNodeSet Src;
  Src.insert(Pred);
  CheckNewAllocatorContext C(NewAllocatorCheckers, Call, WasInlined, Eng);
  expandGraphWithCheckers(C, Dst, Src);
}

/// Run checkers for live symbols.
void CheckerManager::runCheckersForLiveSymbols(ProgramStateRef state,
                                               SymbolReaper &SymReaper) {
  for (const auto &LiveSymbolsChecker : LiveSymbolsCheckers)
    LiveSymbolsChecker(state, SymReaper);
}

namespace {

  struct CheckDeadSymbolsContext {
    using CheckersTy = std::vector<CheckerManager::CheckDeadSymbolsFunc>;

    const CheckersTy &Checkers;
    SymbolReaper &SR;
    const Stmt *S;
    ExprEngine &Eng;
    ProgramPoint::Kind ProgarmPointKind;

    CheckDeadSymbolsContext(const CheckersTy &checkers, SymbolReaper &sr,
                            const Stmt *s, ExprEngine &eng,
                            ProgramPoint::Kind K)
        : Checkers(checkers), SR(sr), S(s), Eng(eng), ProgarmPointKind(K) {}

    CheckersTy::const_iterator checkers_begin() { return Checkers.begin(); }
    CheckersTy::const_iterator checkers_end() { return Checkers.end(); }

    void runChecker(CheckerManager::CheckDeadSymbolsFunc checkFn,
                    NodeBuilder &Bldr, ExplodedNode *Pred) {
      const ProgramPoint &L = ProgramPoint::getProgramPoint(S, ProgarmPointKind,
                                Pred->getLocationContext(), checkFn.Checker);
      CheckerContext C(Bldr, Eng, Pred, L);

      // Note, do not pass the statement to the checkers without letting them
      // differentiate if we ran remove dead bindings before or after the
      // statement.
      checkFn(SR, C);
    }
  };

} // namespace

/// Run checkers for dead symbols.
void CheckerManager::runCheckersForDeadSymbols(ExplodedNodeSet &Dst,
                                               const ExplodedNodeSet &Src,
                                               SymbolReaper &SymReaper,
                                               const Stmt *S,
                                               ExprEngine &Eng,
                                               ProgramPoint::Kind K) {
  CheckDeadSymbolsContext C(DeadSymbolsCheckers, SymReaper, S, Eng, K);
  expandGraphWithCheckers(C, Dst, Src);
}

/// Run checkers for region changes.
ProgramStateRef
CheckerManager::runCheckersForRegionChanges(ProgramStateRef state,
                                            const InvalidatedSymbols *invalidated,
                                            ArrayRef<const MemRegion *> ExplicitRegions,
                                            ArrayRef<const MemRegion *> Regions,
                                            const LocationContext *LCtx,
                                            const CallEvent *Call) {
  for (const auto &RegionChangesChecker : RegionChangesCheckers) {
    // If any checker declares the state infeasible (or if it starts that way),
    // bail out.
    if (!state)
      return nullptr;
    state = RegionChangesChecker(state, invalidated, ExplicitRegions, Regions,
                                 LCtx, Call);
  }
  return state;
}

/// Run checkers to process symbol escape event.
ProgramStateRef
CheckerManager::runCheckersForPointerEscape(ProgramStateRef State,
                                   const InvalidatedSymbols &Escaped,
                                   const CallEvent *Call,
                                   PointerEscapeKind Kind,
                                   RegionAndSymbolInvalidationTraits *ETraits) {
  assert((Call != nullptr ||
          (Kind != PSK_DirectEscapeOnCall &&
           Kind != PSK_IndirectEscapeOnCall)) &&
         "Call must not be NULL when escaping on call");
  for (const auto &PointerEscapeChecker : PointerEscapeCheckers) {
    // If any checker declares the state infeasible (or if it starts that
    //  way), bail out.
    if (!State)
      return nullptr;
    State = PointerEscapeChecker(State, Escaped, Call, Kind, ETraits);
  }
  return State;
}

/// Run checkers for handling assumptions on symbolic values.
ProgramStateRef
CheckerManager::runCheckersForEvalAssume(ProgramStateRef state,
                                         SVal Cond, bool Assumption) {
  for (const auto &EvalAssumeChecker : EvalAssumeCheckers) {
    // If any checker declares the state infeasible (or if it starts that way),
    // bail out.
    if (!state)
      return nullptr;
    state = EvalAssumeChecker(state, Cond, Assumption);
  }
  return state;
}

/// Run checkers for evaluating a call.
/// Only one checker will evaluate the call.
void CheckerManager::runCheckersForEvalCall(ExplodedNodeSet &Dst,
                                            const ExplodedNodeSet &Src,
                                            const CallEvent &Call,
                                            ExprEngine &Eng,
                                            const EvalCallOptions &CallOpts) {
  for (auto *const Pred : Src) {
    std::optional<CheckerNameRef> evaluatorChecker;

    ExplodedNodeSet checkDst;
    NodeBuilder B(Pred, checkDst, Eng.getBuilderContext());

    // Check if any of the EvalCall callbacks can evaluate the call.
    for (const auto &EvalCallChecker : EvalCallCheckers) {
      // TODO: Support the situation when the call doesn't correspond
      // to any Expr.
      ProgramPoint L = ProgramPoint::getProgramPoint(
          Call.getOriginExpr(), ProgramPoint::PostStmtKind,
          Pred->getLocationContext(), EvalCallChecker.Checker);
      bool evaluated = false;
      { // CheckerContext generates transitions(populates checkDest) on
        // destruction, so introduce the scope to make sure it gets properly
        // populated.
        CheckerContext C(B, Eng, Pred, L);
        evaluated = EvalCallChecker(Call, C);
      }
#ifndef NDEBUG
      if (evaluated && evaluatorChecker) {
        const auto toString = [](const CallEvent &Call) -> std::string {
          std::string Buf;
          llvm::raw_string_ostream OS(Buf);
          Call.dump(OS);
          OS.flush();
          return Buf;
        };
        std::string AssertionMessage = llvm::formatv(
            "The '{0}' call has been already evaluated by the {1} checker, "
            "while the {2} checker also tried to evaluate the same call. At "
            "most one checker supposed to evaluate a call.",
            toString(Call), evaluatorChecker->getName(),
            EvalCallChecker.Checker->getCheckerName());
        llvm_unreachable(AssertionMessage.c_str());
      }
#endif
      if (evaluated) {
        evaluatorChecker = EvalCallChecker.Checker->getCheckerName();
        Dst.insert(checkDst);
#ifdef NDEBUG
        break; // on release don't check that no other checker also evals.
#endif
      }
    }

    // If none of the checkers evaluated the call, ask ExprEngine to handle it.
    if (!evaluatorChecker) {
      NodeBuilder B(Pred, Dst, Eng.getBuilderContext());
      Eng.defaultEvalCall(B, Pred, Call, CallOpts);
    }
  }
}

/// Run checkers for the entire Translation Unit.
void CheckerManager::runCheckersOnEndOfTranslationUnit(
                                                  const TranslationUnitDecl *TU,
                                                  AnalysisManager &mgr,
                                                  BugReporter &BR) {
  for (const auto &EndOfTranslationUnitChecker : EndOfTranslationUnitCheckers)
    EndOfTranslationUnitChecker(TU, mgr, BR);
}

void CheckerManager::runCheckersForPrintStateJson(raw_ostream &Out,
                                                  ProgramStateRef State,
                                                  const char *NL,
                                                  unsigned int Space,
                                                  bool IsDot) const {
  Indent(Out, Space, IsDot) << "\"checker_messages\": ";

  // Create a temporary stream to see whether we have any message.
  SmallString<1024> TempBuf;
  llvm::raw_svector_ostream TempOut(TempBuf);
  unsigned int InnerSpace = Space + 2;

  // Create the new-line in JSON with enough space.
  SmallString<128> NewLine;
  llvm::raw_svector_ostream NLOut(NewLine);
  NLOut << "\", " << NL;                     // Inject the ending and a new line
  Indent(NLOut, InnerSpace, IsDot) << "\"";  // then begin the next message.

  ++Space;
  bool HasMessage = false;

  // Store the last CheckerTag.
  const void *LastCT = nullptr;
  for (const auto &CT : CheckerTags) {
    // See whether the current checker has a message.
    CT.second->printState(TempOut, State, /*NL=*/NewLine.c_str(), /*Sep=*/"");

    if (TempBuf.empty())
      continue;

    if (!HasMessage) {
      Out << '[' << NL;
      HasMessage = true;
    }

    LastCT = &CT;
    TempBuf.clear();
  }

  for (const auto &CT : CheckerTags) {
    // See whether the current checker has a message.
    CT.second->printState(TempOut, State, /*NL=*/NewLine.c_str(), /*Sep=*/"");

    if (TempBuf.empty())
      continue;

    Indent(Out, Space, IsDot)
        << "{ \"checker\": \"" << CT.second->getCheckerName().getName()
        << "\", \"messages\": [" << NL;
    Indent(Out, InnerSpace, IsDot)
        << '\"' << TempBuf.str().trim() << '\"' << NL;
    Indent(Out, Space, IsDot) << "]}";

    if (&CT != LastCT)
      Out << ',';
    Out << NL;

    TempBuf.clear();
  }

  // It is the last element of the 'program_state' so do not add a comma.
  if (HasMessage)
    Indent(Out, --Space, IsDot) << "]";
  else
    Out << "null";

  Out << NL;
}

//===----------------------------------------------------------------------===//
// Internal registration functions for AST traversing.
//===----------------------------------------------------------------------===//

void CheckerManager::_registerForDecl(CheckDeclFunc checkfn,
                                      HandlesDeclFunc isForDeclFn) {
  DeclCheckerInfo info = { checkfn, isForDeclFn };
  DeclCheckers.push_back(info);
}

void CheckerManager::_registerForBody(CheckDeclFunc checkfn) {
  BodyCheckers.push_back(checkfn);
}

//===----------------------------------------------------------------------===//
// Internal registration functions for path-sensitive checking.
//===----------------------------------------------------------------------===//

void CheckerManager::_registerForPreStmt(CheckStmtFunc checkfn,
                                         HandlesStmtFunc isForStmtFn) {
  StmtCheckerInfo info = { checkfn, isForStmtFn, /*IsPreVisit*/true };
  StmtCheckers.push_back(info);
}

void CheckerManager::_registerForPostStmt(CheckStmtFunc checkfn,
                                          HandlesStmtFunc isForStmtFn) {
  StmtCheckerInfo info = { checkfn, isForStmtFn, /*IsPreVisit*/false };
  StmtCheckers.push_back(info);
}

void CheckerManager::_registerForPreObjCMessage(CheckObjCMessageFunc checkfn) {
  PreObjCMessageCheckers.push_back(checkfn);
}

void CheckerManager::_registerForObjCMessageNil(CheckObjCMessageFunc checkfn) {
  ObjCMessageNilCheckers.push_back(checkfn);
}

void CheckerManager::_registerForPostObjCMessage(CheckObjCMessageFunc checkfn) {
  PostObjCMessageCheckers.push_back(checkfn);
}

void CheckerManager::_registerForPreCall(CheckCallFunc checkfn) {
  PreCallCheckers.push_back(checkfn);
}
void CheckerManager::_registerForPostCall(CheckCallFunc checkfn) {
  PostCallCheckers.push_back(checkfn);
}

void CheckerManager::_registerForLocation(CheckLocationFunc checkfn) {
  LocationCheckers.push_back(checkfn);
}

void CheckerManager::_registerForBind(CheckBindFunc checkfn) {
  BindCheckers.push_back(checkfn);
}

void CheckerManager::_registerForEndAnalysis(CheckEndAnalysisFunc checkfn) {
  EndAnalysisCheckers.push_back(checkfn);
}

void CheckerManager::_registerForBeginFunction(CheckBeginFunctionFunc checkfn) {
  BeginFunctionCheckers.push_back(checkfn);
}

void CheckerManager::_registerForEndFunction(CheckEndFunctionFunc checkfn) {
  EndFunctionCheckers.push_back(checkfn);
}

void CheckerManager::_registerForBranchCondition(
                                             CheckBranchConditionFunc checkfn) {
  BranchConditionCheckers.push_back(checkfn);
}

void CheckerManager::_registerForNewAllocator(CheckNewAllocatorFunc checkfn) {
  NewAllocatorCheckers.push_back(checkfn);
}

void CheckerManager::_registerForLiveSymbols(CheckLiveSymbolsFunc checkfn) {
  LiveSymbolsCheckers.push_back(checkfn);
}

void CheckerManager::_registerForDeadSymbols(CheckDeadSymbolsFunc checkfn) {
  DeadSymbolsCheckers.push_back(checkfn);
}

void CheckerManager::_registerForRegionChanges(CheckRegionChangesFunc checkfn) {
  RegionChangesCheckers.push_back(checkfn);
}

void CheckerManager::_registerForPointerEscape(CheckPointerEscapeFunc checkfn){
  PointerEscapeCheckers.push_back(checkfn);
}

void CheckerManager::_registerForConstPointerEscape(
                                          CheckPointerEscapeFunc checkfn) {
  PointerEscapeCheckers.push_back(checkfn);
}

void CheckerManager::_registerForEvalAssume(EvalAssumeFunc checkfn) {
  EvalAssumeCheckers.push_back(checkfn);
}

void CheckerManager::_registerForEvalCall(EvalCallFunc checkfn) {
  EvalCallCheckers.push_back(checkfn);
}

void CheckerManager::_registerForEndOfTranslationUnit(
                                            CheckEndOfTranslationUnit checkfn) {
  EndOfTranslationUnitCheckers.push_back(checkfn);
}

//===----------------------------------------------------------------------===//
// Implementation details.
//===----------------------------------------------------------------------===//

const CheckerManager::CachedStmtCheckers &
CheckerManager::getCachedStmtCheckersFor(const Stmt *S, bool isPreVisit) {
  assert(S);

  unsigned Key = (S->getStmtClass() << 1) | unsigned(isPreVisit);
  CachedStmtCheckersMapTy::iterator CCI = CachedStmtCheckersMap.find(Key);
  if (CCI != CachedStmtCheckersMap.end())
    return CCI->second;

  // Find the checkers that should run for this Stmt and cache them.
  CachedStmtCheckers &Checkers = CachedStmtCheckersMap[Key];
  for (const auto &Info : StmtCheckers)
    if (Info.IsPreVisit == isPreVisit && Info.IsForStmtFn(S))
      Checkers.push_back(Info.CheckFn);
  return Checkers;
}
