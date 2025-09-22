// SmartPtrModeling.cpp - Model behavior of C++ smart pointers - C++ ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a checker that models various aspects of
// C++ smart pointer behavior.
//
//===----------------------------------------------------------------------===//

#include "Move.h"
#include "SmartPtr.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>
#include <string>

using namespace clang;
using namespace ento;

namespace {

class SmartPtrModeling
    : public Checker<eval::Call, check::DeadSymbols, check::RegionChanges,
                     check::LiveSymbols> {

  bool isBoolConversionMethod(const CallEvent &Call) const;

public:
  // Whether the checker should model for null dereferences of smart pointers.
  bool ModelSmartPtrDereference = false;
  bool evalCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  ProgramStateRef
  checkRegionChanges(ProgramStateRef State,
                     const InvalidatedSymbols *Invalidated,
                     ArrayRef<const MemRegion *> ExplicitRegions,
                     ArrayRef<const MemRegion *> Regions,
                     const LocationContext *LCtx, const CallEvent *Call) const;
  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;
  void checkLiveSymbols(ProgramStateRef State, SymbolReaper &SR) const;

private:
  void handleReset(const CallEvent &Call, CheckerContext &C) const;
  void handleRelease(const CallEvent &Call, CheckerContext &C) const;
  void handleSwapMethod(const CallEvent &Call, CheckerContext &C) const;
  void handleGet(const CallEvent &Call, CheckerContext &C) const;
  bool handleAssignOp(const CallEvent &Call, CheckerContext &C) const;
  bool handleMoveCtr(const CallEvent &Call, CheckerContext &C,
                     const MemRegion *ThisRegion) const;
  bool updateMovedSmartPointers(CheckerContext &C, const MemRegion *ThisRegion,
                                const MemRegion *OtherSmartPtrRegion,
                                const CallEvent &Call) const;
  void handleBoolConversion(const CallEvent &Call, CheckerContext &C) const;
  bool handleComparisionOp(const CallEvent &Call, CheckerContext &C) const;
  bool handleOstreamOperator(const CallEvent &Call, CheckerContext &C) const;
  bool handleSwap(ProgramStateRef State, SVal First, SVal Second,
                  CheckerContext &C) const;
  std::pair<SVal, ProgramStateRef>
  retrieveOrConjureInnerPtrVal(ProgramStateRef State,
                               const MemRegion *ThisRegion, const Expr *E,
                               QualType Type, CheckerContext &C) const;

  using SmartPtrMethodHandlerFn =
      void (SmartPtrModeling::*)(const CallEvent &Call, CheckerContext &) const;
  CallDescriptionMap<SmartPtrMethodHandlerFn> SmartPtrMethodHandlers{
      {{CDM::CXXMethod, {"reset"}}, &SmartPtrModeling::handleReset},
      {{CDM::CXXMethod, {"release"}}, &SmartPtrModeling::handleRelease},
      {{CDM::CXXMethod, {"swap"}, 1}, &SmartPtrModeling::handleSwapMethod},
      {{CDM::CXXMethod, {"get"}}, &SmartPtrModeling::handleGet}};
  const CallDescription StdSwapCall{CDM::SimpleFunc, {"std", "swap"}, 2};
  const CallDescriptionSet MakeUniqueVariants{
      {CDM::SimpleFunc, {"std", "make_unique"}},
      {CDM::SimpleFunc, {"std", "make_unique_for_overwrite"}}};
};
} // end of anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(TrackedRegionMap, const MemRegion *, SVal)

// Checks if RD has name in Names and is in std namespace
static bool hasStdClassWithName(const CXXRecordDecl *RD,
                                ArrayRef<llvm::StringLiteral> Names) {
  if (!RD || !RD->getDeclContext()->isStdNamespace())
    return false;
  if (RD->getDeclName().isIdentifier())
    return llvm::is_contained(Names, RD->getName());
  return false;
}

constexpr llvm::StringLiteral STD_PTR_NAMES[] = {"shared_ptr", "unique_ptr",
                                                 "weak_ptr"};

static bool isStdSmartPtr(const CXXRecordDecl *RD) {
  return hasStdClassWithName(RD, STD_PTR_NAMES);
}

static bool isStdSmartPtr(const Expr *E) {
  return isStdSmartPtr(E->getType()->getAsCXXRecordDecl());
}

// Define the inter-checker API.
namespace clang {
namespace ento {
namespace smartptr {
bool isStdSmartPtrCall(const CallEvent &Call) {
  const auto *MethodDecl = dyn_cast_or_null<CXXMethodDecl>(Call.getDecl());
  if (!MethodDecl || !MethodDecl->getParent())
    return false;
  return isStdSmartPtr(MethodDecl->getParent());
}

bool isStdSmartPtr(const CXXRecordDecl *RD) {
  if (!RD || !RD->getDeclContext()->isStdNamespace())
    return false;

  if (RD->getDeclName().isIdentifier()) {
    StringRef Name = RD->getName();
    return Name == "shared_ptr" || Name == "unique_ptr" || Name == "weak_ptr";
  }
  return false;
}

bool isStdSmartPtr(const Expr *E) {
  return isStdSmartPtr(E->getType()->getAsCXXRecordDecl());
}

bool isNullSmartPtr(const ProgramStateRef State, const MemRegion *ThisRegion) {
  const auto *InnerPointVal = State->get<TrackedRegionMap>(ThisRegion);
  return InnerPointVal &&
         !State->assume(InnerPointVal->castAs<DefinedOrUnknownSVal>(), true);
}
} // namespace smartptr
} // namespace ento
} // namespace clang

// If a region is removed all of the subregions need to be removed too.
static TrackedRegionMapTy
removeTrackedSubregions(TrackedRegionMapTy RegionMap,
                        TrackedRegionMapTy::Factory &RegionMapFactory,
                        const MemRegion *Region) {
  if (!Region)
    return RegionMap;
  for (const auto &E : RegionMap) {
    if (E.first->isSubRegionOf(Region))
      RegionMap = RegionMapFactory.remove(RegionMap, E.first);
  }
  return RegionMap;
}

static ProgramStateRef updateSwappedRegion(ProgramStateRef State,
                                           const MemRegion *Region,
                                           const SVal *RegionInnerPointerVal) {
  if (RegionInnerPointerVal) {
    State = State->set<TrackedRegionMap>(Region, *RegionInnerPointerVal);
  } else {
    State = State->remove<TrackedRegionMap>(Region);
  }
  return State;
}

static QualType getInnerPointerType(CheckerContext C, const CXXRecordDecl *RD) {
  if (!RD || !RD->isInStdNamespace())
    return {};

  const auto *TSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
  if (!TSD)
    return {};

  auto TemplateArgs = TSD->getTemplateArgs().asArray();
  if (TemplateArgs.empty())
    return {};
  auto InnerValueType = TemplateArgs[0].getAsType();
  return C.getASTContext().getPointerType(InnerValueType.getCanonicalType());
}

// This is for use with standalone-functions like std::make_unique,
// std::make_unique_for_overwrite, etc. It reads the template parameter and
// returns the pointer type corresponding to it,
static QualType getPointerTypeFromTemplateArg(const CallEvent &Call,
                                              CheckerContext &C) {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD || !FD->getPrimaryTemplate())
    return {};
  const auto &TemplateArgs = FD->getTemplateSpecializationArgs()->asArray();
  if (TemplateArgs.size() == 0)
    return {};
  auto ValueType = TemplateArgs[0].getAsType();
  return C.getASTContext().getPointerType(ValueType.getCanonicalType());
}

// Helper method to get the inner pointer type of specialized smart pointer
// Returns empty type if not found valid inner pointer type.
static QualType getInnerPointerType(const CallEvent &Call, CheckerContext &C) {
  const auto *MethodDecl = dyn_cast_or_null<CXXMethodDecl>(Call.getDecl());
  if (!MethodDecl || !MethodDecl->getParent())
    return {};

  const auto *RecordDecl = MethodDecl->getParent();
  return getInnerPointerType(C, RecordDecl);
}

// Helper method to pretty print region and avoid extra spacing.
static void checkAndPrettyPrintRegion(llvm::raw_ostream &OS,
                                      const MemRegion *Region) {
  if (Region->canPrintPretty()) {
    OS << " ";
    Region->printPretty(OS);
  }
}

bool SmartPtrModeling::isBoolConversionMethod(const CallEvent &Call) const {
  // TODO: Update CallDescription to support anonymous calls?
  // TODO: Handle other methods, such as .get() or .release().
  // But once we do, we'd need a visitor to explain null dereferences
  // that are found via such modeling.
  const auto *CD = dyn_cast_or_null<CXXConversionDecl>(Call.getDecl());
  return CD && CD->getConversionType()->isBooleanType();
}

constexpr llvm::StringLiteral BASIC_OSTREAM_NAMES[] = {"basic_ostream"};

bool isStdBasicOstream(const Expr *E) {
  const auto *RD = E->getType()->getAsCXXRecordDecl();
  return hasStdClassWithName(RD, BASIC_OSTREAM_NAMES);
}

static bool isStdFunctionCall(const CallEvent &Call) {
  return Call.getDecl() && Call.getDecl()->getDeclContext()->isStdNamespace();
}

bool isStdOstreamOperatorCall(const CallEvent &Call) {
  if (Call.getNumArgs() != 2 || !isStdFunctionCall(Call))
    return false;
  const auto *FC = dyn_cast<SimpleFunctionCall>(&Call);
  if (!FC)
    return false;
  const FunctionDecl *FD = FC->getDecl();
  if (!FD->isOverloadedOperator())
    return false;
  const OverloadedOperatorKind OOK = FD->getOverloadedOperator();
  if (OOK != clang::OO_LessLess)
    return false;
  return isStdSmartPtr(Call.getArgExpr(1)) &&
         isStdBasicOstream(Call.getArgExpr(0));
}

static bool isPotentiallyComparisionOpCall(const CallEvent &Call) {
  if (Call.getNumArgs() != 2 || !isStdFunctionCall(Call))
    return false;
  return smartptr::isStdSmartPtr(Call.getArgExpr(0)) ||
         smartptr::isStdSmartPtr(Call.getArgExpr(1));
}

bool SmartPtrModeling::evalCall(const CallEvent &Call,
                                CheckerContext &C) const {

  ProgramStateRef State = C.getState();

  // If any one of the arg is a unique_ptr, then
  // we can try this function
  if (ModelSmartPtrDereference && isPotentiallyComparisionOpCall(Call))
    if (handleComparisionOp(Call, C))
      return true;

  if (ModelSmartPtrDereference && isStdOstreamOperatorCall(Call))
    return handleOstreamOperator(Call, C);

  if (StdSwapCall.matches(Call)) {
    // Check the first arg, if it is of std::unique_ptr type.
    assert(Call.getNumArgs() == 2 && "std::swap should have two arguments");
    const Expr *FirstArg = Call.getArgExpr(0);
    if (!smartptr::isStdSmartPtr(FirstArg->getType()->getAsCXXRecordDecl()))
      return false;
    return handleSwap(State, Call.getArgSVal(0), Call.getArgSVal(1), C);
  }

  if (MakeUniqueVariants.contains(Call)) {
    if (!ModelSmartPtrDereference)
      return false;

    const std::optional<SVal> ThisRegionOpt =
        Call.getReturnValueUnderConstruction();
    if (!ThisRegionOpt)
      return false;

    const auto PtrVal = C.getSValBuilder().getConjuredHeapSymbolVal(
        Call.getOriginExpr(), C.getLocationContext(),
        getPointerTypeFromTemplateArg(Call, C), C.blockCount());

    const MemRegion *ThisRegion = ThisRegionOpt->getAsRegion();
    State = State->set<TrackedRegionMap>(ThisRegion, PtrVal);
    State = State->assume(PtrVal, true);

    // TODO: ExprEngine should do this for us.
    // For a bit more context:
    // 1) Why do we need this? Since we are modelling a "function"
    // that returns a constructed object we need to store this information in
    // the program state.
    //
    // 2) Why does this work?
    // `updateObjectsUnderConstruction` does exactly as it sounds.
    //
    // 3) How should it look like when moved to the Engine?
    // It would be nice if we can just
    // pretend we don't need to know about this - ie, completely automatic work.
    // However, realistically speaking, I think we would need to "signal" the
    // ExprEngine evalCall handler that we are constructing an object with this
    // function call (constructors obviously construct, hence can be
    // automatically deduced).
    auto &Engine = State->getStateManager().getOwningEngine();
    State = Engine.updateObjectsUnderConstruction(
        *ThisRegionOpt, nullptr, State, C.getLocationContext(),
        Call.getConstructionContext(), {});

    // We don't leave a note here since it is guaranteed the
    // unique_ptr from this call is non-null (hence is safe to de-reference).
    C.addTransition(State);
    return true;
  }

  if (!smartptr::isStdSmartPtrCall(Call))
    return false;

  if (isBoolConversionMethod(Call)) {
    const MemRegion *ThisR =
        cast<CXXInstanceCall>(&Call)->getCXXThisVal().getAsRegion();

    if (ModelSmartPtrDereference) {
      // The check for the region is moved is duplicated in handleBoolOperation
      // method.
      // FIXME: Once we model std::move for smart pointers clean up this and use
      // that modeling.
      handleBoolConversion(Call, C);
      return true;
    } else {
      if (!move::isMovedFrom(State, ThisR)) {
        // TODO: Model this case as well. At least, avoid invalidation of
        // globals.
        return false;
      }

      // TODO: Add a note to bug reports describing this decision.
      C.addTransition(State->BindExpr(
          Call.getOriginExpr(), C.getLocationContext(),
          C.getSValBuilder().makeZeroVal(Call.getResultType())));

      return true;
    }
  }

  if (!ModelSmartPtrDereference)
    return false;

  if (const auto *CC = dyn_cast<CXXConstructorCall>(&Call)) {
    if (CC->getDecl()->isCopyConstructor())
      return false;

    const MemRegion *ThisRegion = CC->getCXXThisVal().getAsRegion();
    if (!ThisRegion)
      return false;

    QualType ThisType = cast<CXXMethodDecl>(Call.getDecl())->getThisType();

    if (CC->getDecl()->isMoveConstructor())
      return handleMoveCtr(Call, C, ThisRegion);

    if (Call.getNumArgs() == 0) {
      auto NullVal = C.getSValBuilder().makeNullWithType(ThisType);
      State = State->set<TrackedRegionMap>(ThisRegion, NullVal);

      C.addTransition(
          State, C.getNoteTag([ThisRegion](PathSensitiveBugReport &BR,
                                           llvm::raw_ostream &OS) {
            if (&BR.getBugType() != smartptr::getNullDereferenceBugType() ||
                !BR.isInteresting(ThisRegion))
              return;
            OS << "Default constructed smart pointer";
            checkAndPrettyPrintRegion(OS, ThisRegion);
            OS << " is null";
          }));
    } else {
      const auto *TrackingExpr = Call.getArgExpr(0);
      assert(TrackingExpr->getType()->isPointerType() &&
             "Adding a non pointer value to TrackedRegionMap");
      auto ArgVal = Call.getArgSVal(0);
      State = State->set<TrackedRegionMap>(ThisRegion, ArgVal);

      C.addTransition(State, C.getNoteTag([ThisRegion, TrackingExpr,
                                           ArgVal](PathSensitiveBugReport &BR,
                                                   llvm::raw_ostream &OS) {
        if (&BR.getBugType() != smartptr::getNullDereferenceBugType() ||
            !BR.isInteresting(ThisRegion))
          return;
        bugreporter::trackExpressionValue(BR.getErrorNode(), TrackingExpr, BR);
        OS << "Smart pointer";
        checkAndPrettyPrintRegion(OS, ThisRegion);
        if (ArgVal.isZeroConstant())
          OS << " is constructed using a null value";
        else
          OS << " is constructed";
      }));
    }
    return true;
  }

  if (handleAssignOp(Call, C))
    return true;

  const SmartPtrMethodHandlerFn *Handler = SmartPtrMethodHandlers.lookup(Call);
  if (!Handler)
    return false;
  (this->**Handler)(Call, C);

  return C.isDifferent();
}

std::pair<SVal, ProgramStateRef> SmartPtrModeling::retrieveOrConjureInnerPtrVal(
    ProgramStateRef State, const MemRegion *ThisRegion, const Expr *E,
    QualType Type, CheckerContext &C) const {
  const auto *Ptr = State->get<TrackedRegionMap>(ThisRegion);
  if (Ptr)
    return {*Ptr, State};
  auto Val = C.getSValBuilder().conjureSymbolVal(E, C.getLocationContext(),
                                                 Type, C.blockCount());
  State = State->set<TrackedRegionMap>(ThisRegion, Val);
  return {Val, State};
}

bool SmartPtrModeling::handleComparisionOp(const CallEvent &Call,
                                           CheckerContext &C) const {
  const auto *FC = dyn_cast<SimpleFunctionCall>(&Call);
  if (!FC)
    return false;
  const FunctionDecl *FD = FC->getDecl();
  if (!FD->isOverloadedOperator())
    return false;
  const OverloadedOperatorKind OOK = FD->getOverloadedOperator();
  if (!(OOK == OO_EqualEqual || OOK == OO_ExclaimEqual || OOK == OO_Less ||
        OOK == OO_LessEqual || OOK == OO_Greater || OOK == OO_GreaterEqual ||
        OOK == OO_Spaceship))
    return false;

  // There are some special cases about which we can infer about
  // the resulting answer.
  // For reference, there is a discussion at https://reviews.llvm.org/D104616.
  // Also, the cppreference page is good to look at
  // https://en.cppreference.com/w/cpp/memory/unique_ptr/operator_cmp.

  auto makeSValFor = [&C, this](ProgramStateRef State, const Expr *E,
                                SVal S) -> std::pair<SVal, ProgramStateRef> {
    if (S.isZeroConstant()) {
      return {S, State};
    }
    const MemRegion *Reg = S.getAsRegion();
    assert(Reg &&
           "this pointer of std::unique_ptr should be obtainable as MemRegion");
    QualType Type = getInnerPointerType(C, E->getType()->getAsCXXRecordDecl());
    return retrieveOrConjureInnerPtrVal(State, Reg, E, Type, C);
  };

  SVal First = Call.getArgSVal(0);
  SVal Second = Call.getArgSVal(1);
  const auto *FirstExpr = Call.getArgExpr(0);
  const auto *SecondExpr = Call.getArgExpr(1);

  const auto *ResultExpr = Call.getOriginExpr();
  const auto *LCtx = C.getLocationContext();
  auto &Bldr = C.getSValBuilder();
  ProgramStateRef State = C.getState();

  SVal FirstPtrVal, SecondPtrVal;
  std::tie(FirstPtrVal, State) = makeSValFor(State, FirstExpr, First);
  std::tie(SecondPtrVal, State) = makeSValFor(State, SecondExpr, Second);
  BinaryOperatorKind BOK =
      operationKindFromOverloadedOperator(OOK, true).GetBinaryOpUnsafe();
  auto RetVal = Bldr.evalBinOp(State, BOK, FirstPtrVal, SecondPtrVal,
                               Call.getResultType());

  if (OOK != OO_Spaceship) {
    ProgramStateRef TrueState, FalseState;
    std::tie(TrueState, FalseState) =
        State->assume(*RetVal.getAs<DefinedOrUnknownSVal>());
    if (TrueState)
      C.addTransition(
          TrueState->BindExpr(ResultExpr, LCtx, Bldr.makeTruthVal(true)));
    if (FalseState)
      C.addTransition(
          FalseState->BindExpr(ResultExpr, LCtx, Bldr.makeTruthVal(false)));
  } else {
    C.addTransition(State->BindExpr(ResultExpr, LCtx, RetVal));
  }
  return true;
}

bool SmartPtrModeling::handleOstreamOperator(const CallEvent &Call,
                                             CheckerContext &C) const {
  // operator<< does not modify the smart pointer.
  // And we don't really have much of modelling of basic_ostream.
  // So, we are better off:
  // 1) Invalidating the mem-region of the ostream object at hand.
  // 2) Setting the SVal of the basic_ostream as the return value.
  // Not very satisfying, but it gets the job done, and is better
  // than the default handling. :)

  ProgramStateRef State = C.getState();
  const auto StreamVal = Call.getArgSVal(0);
  const MemRegion *StreamThisRegion = StreamVal.getAsRegion();
  if (!StreamThisRegion)
    return false;
  State =
      State->invalidateRegions({StreamThisRegion}, Call.getOriginExpr(),
                               C.blockCount(), C.getLocationContext(), false);
  State =
      State->BindExpr(Call.getOriginExpr(), C.getLocationContext(), StreamVal);
  C.addTransition(State);
  return true;
}

void SmartPtrModeling::checkDeadSymbols(SymbolReaper &SymReaper,
                                        CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  // Clean up dead regions from the region map.
  TrackedRegionMapTy TrackedRegions = State->get<TrackedRegionMap>();
  for (auto E : TrackedRegions) {
    const MemRegion *Region = E.first;
    bool IsRegDead = !SymReaper.isLiveRegion(Region);

    if (IsRegDead)
      State = State->remove<TrackedRegionMap>(Region);
  }
  C.addTransition(State);
}

void SmartPtrModeling::printState(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, const char *Sep) const {
  TrackedRegionMapTy RS = State->get<TrackedRegionMap>();

  if (!RS.isEmpty()) {
    Out << Sep << "Smart ptr regions :" << NL;
    for (auto I : RS) {
      I.first->dumpToStream(Out);
      if (smartptr::isNullSmartPtr(State, I.first))
        Out << ": Null";
      else
        Out << ": Non Null";
      Out << NL;
    }
  }
}

ProgramStateRef SmartPtrModeling::checkRegionChanges(
    ProgramStateRef State, const InvalidatedSymbols *Invalidated,
    ArrayRef<const MemRegion *> ExplicitRegions,
    ArrayRef<const MemRegion *> Regions, const LocationContext *LCtx,
    const CallEvent *Call) const {
  TrackedRegionMapTy RegionMap = State->get<TrackedRegionMap>();
  TrackedRegionMapTy::Factory &RegionMapFactory =
      State->get_context<TrackedRegionMap>();
  for (const auto *Region : Regions)
    RegionMap = removeTrackedSubregions(RegionMap, RegionMapFactory,
                                        Region->getBaseRegion());
  return State->set<TrackedRegionMap>(RegionMap);
}

void SmartPtrModeling::checkLiveSymbols(ProgramStateRef State,
                                        SymbolReaper &SR) const {
  // Marking tracked symbols alive
  TrackedRegionMapTy TrackedRegions = State->get<TrackedRegionMap>();
  for (SVal Val : llvm::make_second_range(TrackedRegions)) {
    for (SymbolRef Sym : Val.symbols()) {
      SR.markLive(Sym);
    }
  }
}

void SmartPtrModeling::handleReset(const CallEvent &Call,
                                   CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const auto *IC = dyn_cast<CXXInstanceCall>(&Call);
  if (!IC)
    return;

  const MemRegion *ThisRegion = IC->getCXXThisVal().getAsRegion();
  if (!ThisRegion)
    return;

  assert(Call.getArgExpr(0)->getType()->isPointerType() &&
         "Adding a non pointer value to TrackedRegionMap");
  State = State->set<TrackedRegionMap>(ThisRegion, Call.getArgSVal(0));
  const auto *TrackingExpr = Call.getArgExpr(0);
  C.addTransition(
      State, C.getNoteTag([ThisRegion, TrackingExpr](PathSensitiveBugReport &BR,
                                                     llvm::raw_ostream &OS) {
        if (&BR.getBugType() != smartptr::getNullDereferenceBugType() ||
            !BR.isInteresting(ThisRegion))
          return;
        bugreporter::trackExpressionValue(BR.getErrorNode(), TrackingExpr, BR);
        OS << "Smart pointer";
        checkAndPrettyPrintRegion(OS, ThisRegion);
        OS << " reset using a null value";
      }));
  // TODO: Make sure to ivalidate the region in the Store if we don't have
  // time to model all methods.
}

void SmartPtrModeling::handleRelease(const CallEvent &Call,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const auto *IC = dyn_cast<CXXInstanceCall>(&Call);
  if (!IC)
    return;

  const MemRegion *ThisRegion = IC->getCXXThisVal().getAsRegion();
  if (!ThisRegion)
    return;

  const auto *InnerPointVal = State->get<TrackedRegionMap>(ThisRegion);

  if (InnerPointVal) {
    State = State->BindExpr(Call.getOriginExpr(), C.getLocationContext(),
                            *InnerPointVal);
  }

  QualType ThisType = cast<CXXMethodDecl>(Call.getDecl())->getThisType();
  auto ValueToUpdate = C.getSValBuilder().makeNullWithType(ThisType);
  State = State->set<TrackedRegionMap>(ThisRegion, ValueToUpdate);

  C.addTransition(State, C.getNoteTag([ThisRegion](PathSensitiveBugReport &BR,
                                                   llvm::raw_ostream &OS) {
    if (&BR.getBugType() != smartptr::getNullDereferenceBugType() ||
        !BR.isInteresting(ThisRegion))
      return;

    OS << "Smart pointer";
    checkAndPrettyPrintRegion(OS, ThisRegion);
    OS << " is released and set to null";
  }));
  // TODO: Add support to enable MallocChecker to start tracking the raw
  // pointer.
}

void SmartPtrModeling::handleSwapMethod(const CallEvent &Call,
                                        CheckerContext &C) const {
  // To model unique_ptr::swap() method.
  const auto *IC = dyn_cast<CXXInstanceCall>(&Call);
  if (!IC)
    return;

  auto State = C.getState();
  handleSwap(State, IC->getCXXThisVal(), Call.getArgSVal(0), C);
}

bool SmartPtrModeling::handleSwap(ProgramStateRef State, SVal First,
                                  SVal Second, CheckerContext &C) const {
  const MemRegion *FirstThisRegion = First.getAsRegion();
  if (!FirstThisRegion)
    return false;
  const MemRegion *SecondThisRegion = Second.getAsRegion();
  if (!SecondThisRegion)
    return false;

  const auto *FirstInnerPtrVal = State->get<TrackedRegionMap>(FirstThisRegion);
  const auto *SecondInnerPtrVal =
      State->get<TrackedRegionMap>(SecondThisRegion);

  State = updateSwappedRegion(State, FirstThisRegion, SecondInnerPtrVal);
  State = updateSwappedRegion(State, SecondThisRegion, FirstInnerPtrVal);

  C.addTransition(State, C.getNoteTag([FirstThisRegion, SecondThisRegion](
                                          PathSensitiveBugReport &BR,
                                          llvm::raw_ostream &OS) {
    if (&BR.getBugType() != smartptr::getNullDereferenceBugType())
      return;
    if (BR.isInteresting(FirstThisRegion) &&
        !BR.isInteresting(SecondThisRegion)) {
      BR.markInteresting(SecondThisRegion);
      BR.markNotInteresting(FirstThisRegion);
    }
    if (BR.isInteresting(SecondThisRegion) &&
        !BR.isInteresting(FirstThisRegion)) {
      BR.markInteresting(FirstThisRegion);
      BR.markNotInteresting(SecondThisRegion);
    }
    // TODO: We need to emit some note here probably!!
  }));

  return true;
}

void SmartPtrModeling::handleGet(const CallEvent &Call,
                                 CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const auto *IC = dyn_cast<CXXInstanceCall>(&Call);
  if (!IC)
    return;

  const MemRegion *ThisRegion = IC->getCXXThisVal().getAsRegion();
  if (!ThisRegion)
    return;

  SVal InnerPointerVal;
  std::tie(InnerPointerVal, State) = retrieveOrConjureInnerPtrVal(
      State, ThisRegion, Call.getOriginExpr(), Call.getResultType(), C);
  State = State->BindExpr(Call.getOriginExpr(), C.getLocationContext(),
                          InnerPointerVal);
  // TODO: Add NoteTag, for how the raw pointer got using 'get' method.
  C.addTransition(State);
}

bool SmartPtrModeling::handleAssignOp(const CallEvent &Call,
                                      CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const auto *OC = dyn_cast<CXXMemberOperatorCall>(&Call);
  if (!OC)
    return false;
  OverloadedOperatorKind OOK = OC->getOverloadedOperator();
  if (OOK != OO_Equal)
    return false;
  const MemRegion *ThisRegion = OC->getCXXThisVal().getAsRegion();
  if (!ThisRegion)
    return false;

  QualType ThisType = cast<CXXMethodDecl>(Call.getDecl())->getThisType();

  const MemRegion *OtherSmartPtrRegion = OC->getArgSVal(0).getAsRegion();
  // In case of 'nullptr' or '0' assigned
  if (!OtherSmartPtrRegion) {
    bool AssignedNull = Call.getArgSVal(0).isZeroConstant();
    if (!AssignedNull)
      return false;
    auto NullVal = C.getSValBuilder().makeNullWithType(ThisType);
    State = State->set<TrackedRegionMap>(ThisRegion, NullVal);
    C.addTransition(State, C.getNoteTag([ThisRegion](PathSensitiveBugReport &BR,
                                                     llvm::raw_ostream &OS) {
      if (&BR.getBugType() != smartptr::getNullDereferenceBugType() ||
          !BR.isInteresting(ThisRegion))
        return;
      OS << "Smart pointer";
      checkAndPrettyPrintRegion(OS, ThisRegion);
      OS << " is assigned to null";
    }));
    return true;
  }

  return updateMovedSmartPointers(C, ThisRegion, OtherSmartPtrRegion, Call);
}

bool SmartPtrModeling::handleMoveCtr(const CallEvent &Call, CheckerContext &C,
                                     const MemRegion *ThisRegion) const {
  const auto *OtherSmartPtrRegion = Call.getArgSVal(0).getAsRegion();
  if (!OtherSmartPtrRegion)
    return false;

  return updateMovedSmartPointers(C, ThisRegion, OtherSmartPtrRegion, Call);
}

bool SmartPtrModeling::updateMovedSmartPointers(
    CheckerContext &C, const MemRegion *ThisRegion,
    const MemRegion *OtherSmartPtrRegion, const CallEvent &Call) const {
  ProgramStateRef State = C.getState();
  QualType ThisType = cast<CXXMethodDecl>(Call.getDecl())->getThisType();
  const auto *OtherInnerPtr = State->get<TrackedRegionMap>(OtherSmartPtrRegion);
  if (OtherInnerPtr) {
    State = State->set<TrackedRegionMap>(ThisRegion, *OtherInnerPtr);

    auto NullVal = C.getSValBuilder().makeNullWithType(ThisType);
    State = State->set<TrackedRegionMap>(OtherSmartPtrRegion, NullVal);
    bool IsArgValNull = OtherInnerPtr->isZeroConstant();

    C.addTransition(
        State,
        C.getNoteTag([ThisRegion, OtherSmartPtrRegion, IsArgValNull](
                         PathSensitiveBugReport &BR, llvm::raw_ostream &OS) {
          if (&BR.getBugType() != smartptr::getNullDereferenceBugType())
            return;
          if (BR.isInteresting(OtherSmartPtrRegion)) {
            OS << "Smart pointer";
            checkAndPrettyPrintRegion(OS, OtherSmartPtrRegion);
            OS << " is null after being moved to";
            checkAndPrettyPrintRegion(OS, ThisRegion);
          }
          if (BR.isInteresting(ThisRegion) && IsArgValNull) {
            OS << "A null pointer value is moved to";
            checkAndPrettyPrintRegion(OS, ThisRegion);
            BR.markInteresting(OtherSmartPtrRegion);
          }
        }));
    return true;
  } else {
    // In case we dont know anything about value we are moving from
    // remove the entry from map for which smart pointer got moved to.
    // For unique_ptr<A>, Ty will be 'A*'.
    auto NullVal = C.getSValBuilder().makeNullWithType(ThisType);
    State = State->remove<TrackedRegionMap>(ThisRegion);
    State = State->set<TrackedRegionMap>(OtherSmartPtrRegion, NullVal);
    C.addTransition(State, C.getNoteTag([OtherSmartPtrRegion,
                                         ThisRegion](PathSensitiveBugReport &BR,
                                                     llvm::raw_ostream &OS) {
      if (&BR.getBugType() != smartptr::getNullDereferenceBugType() ||
          !BR.isInteresting(OtherSmartPtrRegion))
        return;
      OS << "Smart pointer";
      checkAndPrettyPrintRegion(OS, OtherSmartPtrRegion);
      OS << " is null after; previous value moved to";
      checkAndPrettyPrintRegion(OS, ThisRegion);
    }));
    return true;
  }
  return false;
}

void SmartPtrModeling::handleBoolConversion(const CallEvent &Call,
                                            CheckerContext &C) const {
  // To model unique_ptr::operator bool
  ProgramStateRef State = C.getState();
  const Expr *CallExpr = Call.getOriginExpr();
  const MemRegion *ThisRegion =
      cast<CXXInstanceCall>(&Call)->getCXXThisVal().getAsRegion();

  QualType ThisType = cast<CXXMethodDecl>(Call.getDecl())->getThisType();

  SVal InnerPointerVal;
  if (const auto *InnerValPtr = State->get<TrackedRegionMap>(ThisRegion)) {
    InnerPointerVal = *InnerValPtr;
  } else {
    // In case of inner pointer SVal is not available we create
    // conjureSymbolVal for inner pointer value.
    auto InnerPointerType = getInnerPointerType(Call, C);
    if (InnerPointerType.isNull())
      return;

    const LocationContext *LC = C.getLocationContext();
    InnerPointerVal = C.getSValBuilder().conjureSymbolVal(
        CallExpr, LC, InnerPointerType, C.blockCount());
    State = State->set<TrackedRegionMap>(ThisRegion, InnerPointerVal);
  }

  if (State->isNull(InnerPointerVal).isConstrainedTrue()) {
    State = State->BindExpr(CallExpr, C.getLocationContext(),
                            C.getSValBuilder().makeTruthVal(false));

    C.addTransition(State);
    return;
  } else if (State->isNonNull(InnerPointerVal).isConstrainedTrue()) {
    State = State->BindExpr(CallExpr, C.getLocationContext(),
                            C.getSValBuilder().makeTruthVal(true));

    C.addTransition(State);
    return;
  } else if (move::isMovedFrom(State, ThisRegion)) {
    C.addTransition(
        State->BindExpr(CallExpr, C.getLocationContext(),
                        C.getSValBuilder().makeZeroVal(Call.getResultType())));
    return;
  } else {
    ProgramStateRef NotNullState, NullState;
    std::tie(NotNullState, NullState) =
        State->assume(InnerPointerVal.castAs<DefinedOrUnknownSVal>());

    auto NullVal = C.getSValBuilder().makeNullWithType(ThisType);
    // Explicitly tracking the region as null.
    NullState = NullState->set<TrackedRegionMap>(ThisRegion, NullVal);

    NullState = NullState->BindExpr(CallExpr, C.getLocationContext(),
                                    C.getSValBuilder().makeTruthVal(false));
    C.addTransition(NullState, C.getNoteTag(
                                   [ThisRegion](PathSensitiveBugReport &BR,
                                                llvm::raw_ostream &OS) {
                                     OS << "Assuming smart pointer";
                                     checkAndPrettyPrintRegion(OS, ThisRegion);
                                     OS << " is null";
                                   },
                                   /*IsPrunable=*/true));
    NotNullState =
        NotNullState->BindExpr(CallExpr, C.getLocationContext(),
                               C.getSValBuilder().makeTruthVal(true));
    C.addTransition(
        NotNullState,
        C.getNoteTag(
            [ThisRegion](PathSensitiveBugReport &BR, llvm::raw_ostream &OS) {
              OS << "Assuming smart pointer";
              checkAndPrettyPrintRegion(OS, ThisRegion);
              OS << " is non-null";
            },
            /*IsPrunable=*/true));
    return;
  }
}

void ento::registerSmartPtrModeling(CheckerManager &Mgr) {
  auto *Checker = Mgr.registerChecker<SmartPtrModeling>();
  Checker->ModelSmartPtrDereference =
      Mgr.getAnalyzerOptions().getCheckerBooleanOption(
          Checker, "ModelSmartPtrDereference");
}

bool ento::shouldRegisterSmartPtrModeling(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.CPlusPlus;
}
