//=== ErrnoModeling.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines a checker `ErrnoModeling`, which is used to make the system
// value 'errno' available to other checkers.
// The 'errno' value is stored at a special memory region that is accessible
// through the `errno_modeling` namespace. The memory region is either the
// region of `errno` itself if it is a variable, otherwise an artifically
// created region (in the system memory space). If `errno` is defined by using
// a function which returns the address of it (this is always the case if it is
// not a variable) this function is recognized and evaluated. In this way
// `errno` becomes visible to the analysis and checkers can change its value.
//
//===----------------------------------------------------------------------===//

#include "ErrnoModeling.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {

// Name of the "errno" variable.
// FIXME: Is there a system where it is not called "errno" but is a variable?
const char *ErrnoVarName = "errno";

// Names of functions that return a location of the "errno" value.
// FIXME: Are there other similar function names?
CallDescriptionSet ErrnoLocationCalls{
    {CDM::CLibrary, {"__errno_location"}, 0, 0},
    {CDM::CLibrary, {"___errno"}, 0, 0},
    {CDM::CLibrary, {"__errno"}, 0, 0},
    {CDM::CLibrary, {"_errno"}, 0, 0},
    {CDM::CLibrary, {"__error"}, 0, 0}};

class ErrnoModeling
    : public Checker<check::ASTDecl<TranslationUnitDecl>, check::BeginFunction,
                     check::LiveSymbols, eval::Call> {
public:
  void checkASTDecl(const TranslationUnitDecl *D, AnalysisManager &Mgr,
                    BugReporter &BR) const;
  void checkBeginFunction(CheckerContext &C) const;
  void checkLiveSymbols(ProgramStateRef State, SymbolReaper &SR) const;
  bool evalCall(const CallEvent &Call, CheckerContext &C) const;

private:
  // The declaration of an "errno" variable on systems where errno is
  // represented by a variable (and not a function that queries its location).
  mutable const VarDecl *ErrnoDecl = nullptr;
};

} // namespace

/// Store a MemRegion that contains the 'errno' integer value.
/// The value is null if the 'errno' value was not recognized in the AST.
REGISTER_TRAIT_WITH_PROGRAMSTATE(ErrnoRegion, const MemRegion *)

REGISTER_TRAIT_WITH_PROGRAMSTATE(ErrnoState, errno_modeling::ErrnoCheckState)

void ErrnoModeling::checkASTDecl(const TranslationUnitDecl *D,
                                 AnalysisManager &Mgr, BugReporter &BR) const {
  // Try to find the declaration of the external variable `int errno;`.
  // There are also C library implementations, where the `errno` location is
  // accessed via a function that returns its address; in those environments
  // this callback has no effect.
  ASTContext &ACtx = Mgr.getASTContext();
  IdentifierInfo &II = ACtx.Idents.get(ErrnoVarName);
  auto LookupRes = ACtx.getTranslationUnitDecl()->lookup(&II);
  auto Found = llvm::find_if(LookupRes, [&ACtx](const Decl *D) {
    if (auto *VD = dyn_cast<VarDecl>(D))
      return ACtx.getSourceManager().isInSystemHeader(VD->getLocation()) &&
             VD->hasExternalStorage() &&
             VD->getType().getCanonicalType() == ACtx.IntTy;
    return false;
  });
  if (Found != LookupRes.end())
    ErrnoDecl = cast<VarDecl>(*Found);
}

void ErrnoModeling::checkBeginFunction(CheckerContext &C) const {
  if (!C.inTopFrame())
    return;

  ASTContext &ACtx = C.getASTContext();
  ProgramStateRef State = C.getState();

  const MemRegion *ErrnoR = nullptr;

  if (ErrnoDecl) {
    // There is an external 'errno' variable, so we can simply use the memory
    // region that's associated with it.
    ErrnoR = State->getRegion(ErrnoDecl, C.getLocationContext());
    assert(ErrnoR && "Memory region should exist for the 'errno' variable.");
  } else {
    // There is no 'errno' variable, so create a new symbolic memory region
    // that can be used to model the return value of the "get the location of
    // errno" internal functions.
    // NOTE: this `SVal` is created even if errno is not defined or used.
    SValBuilder &SVB = C.getSValBuilder();
    MemRegionManager &RMgr = C.getStateManager().getRegionManager();

    const MemSpaceRegion *GlobalSystemSpace =
        RMgr.getGlobalsRegion(MemRegion::GlobalSystemSpaceRegionKind);

    // Create an artifical symbol for the region.
    // Note that it is not possible to associate a statement or expression in
    // this case and the `symbolTag` (opaque pointer tag) is just the address
    // of the data member `ErrnoDecl` of the singleton `ErrnoModeling` checker
    // object.
    const SymbolConjured *Sym = SVB.conjureSymbol(
        nullptr, C.getLocationContext(),
        ACtx.getLValueReferenceType(ACtx.IntTy), C.blockCount(), &ErrnoDecl);

    // The symbolic region is untyped, create a typed sub-region in it.
    // The ElementRegion is used to make the errno region a typed region.
    ErrnoR = RMgr.getElementRegion(
        ACtx.IntTy, SVB.makeZeroArrayIndex(),
        RMgr.getSymbolicRegion(Sym, GlobalSystemSpace), C.getASTContext());
  }
  assert(ErrnoR);
  State = State->set<ErrnoRegion>(ErrnoR);
  State =
      errno_modeling::setErrnoValue(State, C, 0, errno_modeling::Irrelevant);
  C.addTransition(State);
}

bool ErrnoModeling::evalCall(const CallEvent &Call, CheckerContext &C) const {
  // Return location of "errno" at a call to an "errno address returning"
  // function.
  if (errno_modeling::isErrnoLocationCall(Call)) {
    ProgramStateRef State = C.getState();

    const MemRegion *ErrnoR = State->get<ErrnoRegion>();
    if (!ErrnoR)
      return false;

    State = State->BindExpr(Call.getOriginExpr(), C.getLocationContext(),
                            loc::MemRegionVal{ErrnoR});
    C.addTransition(State);
    return true;
  }

  return false;
}

void ErrnoModeling::checkLiveSymbols(ProgramStateRef State,
                                     SymbolReaper &SR) const {
  // The special errno region should never garbage collected.
  if (const auto *ErrnoR = State->get<ErrnoRegion>())
    SR.markLive(ErrnoR);
}

namespace clang {
namespace ento {
namespace errno_modeling {

std::optional<SVal> getErrnoValue(ProgramStateRef State) {
  const MemRegion *ErrnoR = State->get<ErrnoRegion>();
  if (!ErrnoR)
    return {};
  QualType IntTy = State->getAnalysisManager().getASTContext().IntTy;
  return State->getSVal(ErrnoR, IntTy);
}

ProgramStateRef setErrnoValue(ProgramStateRef State,
                              const LocationContext *LCtx, SVal Value,
                              ErrnoCheckState EState) {
  const MemRegion *ErrnoR = State->get<ErrnoRegion>();
  if (!ErrnoR)
    return State;
  // First set the errno value, the old state is still available at 'checkBind'
  // or 'checkLocation' for errno value.
  State = State->bindLoc(loc::MemRegionVal{ErrnoR}, Value, LCtx);
  return State->set<ErrnoState>(EState);
}

ProgramStateRef setErrnoValue(ProgramStateRef State, CheckerContext &C,
                              uint64_t Value, ErrnoCheckState EState) {
  const MemRegion *ErrnoR = State->get<ErrnoRegion>();
  if (!ErrnoR)
    return State;
  State = State->bindLoc(
      loc::MemRegionVal{ErrnoR},
      C.getSValBuilder().makeIntVal(Value, C.getASTContext().IntTy),
      C.getLocationContext());
  return State->set<ErrnoState>(EState);
}

std::optional<Loc> getErrnoLoc(ProgramStateRef State) {
  const MemRegion *ErrnoR = State->get<ErrnoRegion>();
  if (!ErrnoR)
    return {};
  return loc::MemRegionVal{ErrnoR};
}

ErrnoCheckState getErrnoState(ProgramStateRef State) {
  return State->get<ErrnoState>();
}

ProgramStateRef setErrnoState(ProgramStateRef State, ErrnoCheckState EState) {
  return State->set<ErrnoState>(EState);
}

ProgramStateRef clearErrnoState(ProgramStateRef State) {
  return setErrnoState(State, Irrelevant);
}

bool isErrnoLocationCall(const CallEvent &CE) {
  return ErrnoLocationCalls.contains(CE);
}

const NoteTag *getErrnoNoteTag(CheckerContext &C, const std::string &Message) {
  return C.getNoteTag([Message](PathSensitiveBugReport &BR) -> std::string {
    const MemRegion *ErrnoR = BR.getErrorNode()->getState()->get<ErrnoRegion>();
    if (ErrnoR && BR.isInteresting(ErrnoR)) {
      BR.markNotInteresting(ErrnoR);
      return Message;
    }
    return "";
  });
}

ProgramStateRef setErrnoForStdSuccess(ProgramStateRef State,
                                      CheckerContext &C) {
  return setErrnoState(State, MustNotBeChecked);
}

ProgramStateRef setErrnoForStdFailure(ProgramStateRef State, CheckerContext &C,
                                      NonLoc ErrnoSym) {
  SValBuilder &SVB = C.getSValBuilder();
  NonLoc ZeroVal = SVB.makeZeroVal(C.getASTContext().IntTy).castAs<NonLoc>();
  DefinedOrUnknownSVal Cond =
      SVB.evalBinOp(State, BO_NE, ErrnoSym, ZeroVal, SVB.getConditionType())
          .castAs<DefinedOrUnknownSVal>();
  State = State->assume(Cond, true);
  if (!State)
    return nullptr;
  return setErrnoValue(State, C.getLocationContext(), ErrnoSym, Irrelevant);
}

ProgramStateRef setErrnoStdMustBeChecked(ProgramStateRef State,
                                         CheckerContext &C,
                                         const Expr *InvalE) {
  const MemRegion *ErrnoR = State->get<ErrnoRegion>();
  if (!ErrnoR)
    return State;
  State = State->invalidateRegions(ErrnoR, InvalE, C.blockCount(),
                                   C.getLocationContext(), false);
  if (!State)
    return nullptr;
  return setErrnoState(State, MustBeChecked);
}

} // namespace errno_modeling
} // namespace ento
} // namespace clang

void ento::registerErrnoModeling(CheckerManager &mgr) {
  mgr.registerChecker<ErrnoModeling>();
}

bool ento::shouldRegisterErrnoModeling(const CheckerManager &mgr) {
  return true;
}
