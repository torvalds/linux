//===-- NullabilityChecker.cpp - Nullability checker ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker tries to find nullability violations. There are several kinds of
// possible violations:
// * Null pointer is passed to a pointer which has a _Nonnull type.
// * Null pointer is returned from a function which has a _Nonnull return type.
// * Nullable pointer is passed to a pointer which has a _Nonnull type.
// * Nullable pointer is returned from a function which has a _Nonnull return
//   type.
// * Nullable pointer is dereferenced.
//
// This checker propagates the nullability information of the pointers and looks
// for the patterns that are described above. Explicit casts are trusted and are
// considered a way to suppress false positives for this checker. The other way
// to suppress warnings would be to add asserts or guarding if statements to the
// code. In addition to the nullability propagation this checker also uses some
// heuristics to suppress potential false positives.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/Analysis/AnyCall.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace ento;

namespace {

/// Returns the most nullable nullability. This is used for message expressions
/// like [receiver method], where the nullability of this expression is either
/// the nullability of the receiver or the nullability of the return type of the
/// method, depending on which is more nullable. Contradicted is considered to
/// be the most nullable, to avoid false positive results.
Nullability getMostNullable(Nullability Lhs, Nullability Rhs) {
  return static_cast<Nullability>(
      std::min(static_cast<char>(Lhs), static_cast<char>(Rhs)));
}

const char *getNullabilityString(Nullability Nullab) {
  switch (Nullab) {
  case Nullability::Contradicted:
    return "contradicted";
  case Nullability::Nullable:
    return "nullable";
  case Nullability::Unspecified:
    return "unspecified";
  case Nullability::Nonnull:
    return "nonnull";
  }
  llvm_unreachable("Unexpected enumeration.");
  return "";
}

// These enums are used as an index to ErrorMessages array.
enum class ErrorKind : int {
  NilAssignedToNonnull,
  NilPassedToNonnull,
  NilReturnedToNonnull,
  NullableAssignedToNonnull,
  NullableReturnedToNonnull,
  NullableDereferenced,
  NullablePassedToNonnull
};

class NullabilityChecker
    : public Checker<check::Bind, check::PreCall, check::PreStmt<ReturnStmt>,
                     check::PostCall, check::PostStmt<ExplicitCastExpr>,
                     check::PostObjCMessage, check::DeadSymbols, eval::Assume,
                     check::Location, check::Event<ImplicitNullDerefEvent>,
                     check::BeginFunction> {

public:
  // If true, the checker will not diagnose nullabilility issues for calls
  // to system headers. This option is motivated by the observation that large
  // projects may have many nullability warnings. These projects may
  // find warnings about nullability annotations that they have explicitly
  // added themselves higher priority to fix than warnings on calls to system
  // libraries.
  bool NoDiagnoseCallsToSystemHeaders = false;

  void checkBind(SVal L, SVal V, const Stmt *S, CheckerContext &C) const;
  void checkPostStmt(const ExplicitCastExpr *CE, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
  void checkEvent(ImplicitNullDerefEvent Event) const;
  void checkLocation(SVal Location, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const;
  void checkBeginFunction(CheckerContext &Ctx) const;
  ProgramStateRef evalAssume(ProgramStateRef State, SVal Cond,
                             bool Assumption) const;

  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;

  enum CheckKind {
    CK_NullPassedToNonnull,
    CK_NullReturnedFromNonnull,
    CK_NullableDereferenced,
    CK_NullablePassedToNonnull,
    CK_NullableReturnedFromNonnull,
    CK_NumCheckKinds
  };

  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  CheckerNameRef CheckNames[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BTs[CK_NumCheckKinds];

  const std::unique_ptr<BugType> &getBugType(CheckKind Kind) const {
    if (!BTs[Kind])
      BTs[Kind].reset(new BugType(CheckNames[Kind], "Nullability",
                                  categories::MemoryError));
    return BTs[Kind];
  }

  // When set to false no nullability information will be tracked in
  // NullabilityMap. It is possible to catch errors like passing a null pointer
  // to a callee that expects nonnull argument without the information that is
  // stored in the NullabilityMap. This is an optimization.
  bool NeedTracking = false;

private:
  class NullabilityBugVisitor : public BugReporterVisitor {
  public:
    NullabilityBugVisitor(const MemRegion *M) : Region(M) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(Region);
    }

    PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                     BugReporterContext &BRC,
                                     PathSensitiveBugReport &BR) override;

  private:
    // The tracked region.
    const MemRegion *Region;
  };

  /// When any of the nonnull arguments of the analyzed function is null, do not
  /// report anything and turn off the check.
  ///
  /// When \p SuppressPath is set to true, no more bugs will be reported on this
  /// path by this checker.
  void reportBugIfInvariantHolds(StringRef Msg, ErrorKind Error, CheckKind CK,
                                 ExplodedNode *N, const MemRegion *Region,
                                 CheckerContext &C,
                                 const Stmt *ValueExpr = nullptr,
                                 bool SuppressPath = false) const;

  void reportBug(StringRef Msg, ErrorKind Error, CheckKind CK, ExplodedNode *N,
                 const MemRegion *Region, BugReporter &BR,
                 const Stmt *ValueExpr = nullptr) const {
    const std::unique_ptr<BugType> &BT = getBugType(CK);
    auto R = std::make_unique<PathSensitiveBugReport>(*BT, Msg, N);
    if (Region) {
      R->markInteresting(Region);
      R->addVisitor<NullabilityBugVisitor>(Region);
    }
    if (ValueExpr) {
      R->addRange(ValueExpr->getSourceRange());
      if (Error == ErrorKind::NilAssignedToNonnull ||
          Error == ErrorKind::NilPassedToNonnull ||
          Error == ErrorKind::NilReturnedToNonnull)
        if (const auto *Ex = dyn_cast<Expr>(ValueExpr))
          bugreporter::trackExpressionValue(N, Ex, *R);
    }
    BR.emitReport(std::move(R));
  }

  /// If an SVal wraps a region that should be tracked, it will return a pointer
  /// to the wrapped region. Otherwise it will return a nullptr.
  const SymbolicRegion *getTrackRegion(SVal Val,
                                       bool CheckSuperRegion = false) const;

  /// Returns true if the call is diagnosable in the current analyzer
  /// configuration.
  bool isDiagnosableCall(const CallEvent &Call) const {
    if (NoDiagnoseCallsToSystemHeaders && Call.isInSystemHeader())
      return false;

    return true;
  }
};

class NullabilityState {
public:
  NullabilityState(Nullability Nullab, const Stmt *Source = nullptr)
      : Nullab(Nullab), Source(Source) {}

  const Stmt *getNullabilitySource() const { return Source; }

  Nullability getValue() const { return Nullab; }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(static_cast<char>(Nullab));
    ID.AddPointer(Source);
  }

  void print(raw_ostream &Out) const {
    Out << getNullabilityString(Nullab) << "\n";
  }

private:
  Nullability Nullab;
  // Source is the expression which determined the nullability. For example in a
  // message like [nullable nonnull_returning] has nullable nullability, because
  // the receiver is nullable. Here the receiver will be the source of the
  // nullability. This is useful information when the diagnostics are generated.
  const Stmt *Source;
};

bool operator==(NullabilityState Lhs, NullabilityState Rhs) {
  return Lhs.getValue() == Rhs.getValue() &&
         Lhs.getNullabilitySource() == Rhs.getNullabilitySource();
}

// For the purpose of tracking historical property accesses, the key for lookup
// is an object pointer (could be an instance or a class) paired with the unique
// identifier for the property being invoked on that object.
using ObjectPropPair = std::pair<const MemRegion *, const IdentifierInfo *>;

// Metadata associated with the return value from a recorded property access.
struct ConstrainedPropertyVal {
  // This will reference the conjured return SVal for some call
  // of the form [object property]
  DefinedOrUnknownSVal Value;

  // If the SVal has been determined to be nonnull, that is recorded here
  bool isConstrainedNonnull;

  ConstrainedPropertyVal(DefinedOrUnknownSVal SV)
      : Value(SV), isConstrainedNonnull(false) {}

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Value.Profile(ID);
    ID.AddInteger(isConstrainedNonnull ? 1 : 0);
  }
};

bool operator==(const ConstrainedPropertyVal &Lhs,
                const ConstrainedPropertyVal &Rhs) {
  return Lhs.Value == Rhs.Value &&
         Lhs.isConstrainedNonnull == Rhs.isConstrainedNonnull;
}

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(NullabilityMap, const MemRegion *,
                               NullabilityState)
REGISTER_MAP_WITH_PROGRAMSTATE(PropertyAccessesMap, ObjectPropPair,
                               ConstrainedPropertyVal)

// We say "the nullability type invariant is violated" when a location with a
// non-null type contains NULL or a function with a non-null return type returns
// NULL. Violations of the nullability type invariant can be detected either
// directly (for example, when NULL is passed as an argument to a nonnull
// parameter) or indirectly (for example, when, inside a function, the
// programmer defensively checks whether a nonnull parameter contains NULL and
// finds that it does).
//
// As a matter of policy, the nullability checker typically warns on direct
// violations of the nullability invariant (although it uses various
// heuristics to suppress warnings in some cases) but will not warn if the
// invariant has already been violated along the path (either directly or
// indirectly). As a practical matter, this prevents the analyzer from
// (1) warning on defensive code paths where a nullability precondition is
// determined to have been violated, (2) warning additional times after an
// initial direct violation has been discovered, and (3) warning after a direct
// violation that has been implicitly or explicitly suppressed (for
// example, with a cast of NULL to _Nonnull). In essence, once an invariant
// violation is detected on a path, this checker will be essentially turned off
// for the rest of the analysis
//
// The analyzer takes this approach (rather than generating a sink node) to
// ensure coverage of defensive paths, which may be important for backwards
// compatibility in codebases that were developed without nullability in mind.
REGISTER_TRAIT_WITH_PROGRAMSTATE(InvariantViolated, bool)

enum class NullConstraint { IsNull, IsNotNull, Unknown };

static NullConstraint getNullConstraint(DefinedOrUnknownSVal Val,
                                        ProgramStateRef State) {
  ConditionTruthVal Nullness = State->isNull(Val);
  if (Nullness.isConstrainedFalse())
    return NullConstraint::IsNotNull;
  if (Nullness.isConstrainedTrue())
    return NullConstraint::IsNull;
  return NullConstraint::Unknown;
}

static bool isValidPointerType(QualType T) {
  return T->isAnyPointerType() || T->isBlockPointerType();
}

const SymbolicRegion *
NullabilityChecker::getTrackRegion(SVal Val, bool CheckSuperRegion) const {
  if (!NeedTracking)
    return nullptr;

  auto RegionSVal = Val.getAs<loc::MemRegionVal>();
  if (!RegionSVal)
    return nullptr;

  const MemRegion *Region = RegionSVal->getRegion();

  if (CheckSuperRegion) {
    if (const SubRegion *FieldReg = Region->getAs<FieldRegion>()) {
      if (const auto *ER = dyn_cast<ElementRegion>(FieldReg->getSuperRegion()))
        FieldReg = ER;
      return dyn_cast<SymbolicRegion>(FieldReg->getSuperRegion());
    }
    if (auto ElementReg = Region->getAs<ElementRegion>())
      return dyn_cast<SymbolicRegion>(ElementReg->getSuperRegion());
  }

  return dyn_cast<SymbolicRegion>(Region);
}

PathDiagnosticPieceRef NullabilityChecker::NullabilityBugVisitor::VisitNode(
    const ExplodedNode *N, BugReporterContext &BRC,
    PathSensitiveBugReport &BR) {
  ProgramStateRef State = N->getState();
  ProgramStateRef StatePrev = N->getFirstPred()->getState();

  const NullabilityState *TrackedNullab = State->get<NullabilityMap>(Region);
  const NullabilityState *TrackedNullabPrev =
      StatePrev->get<NullabilityMap>(Region);
  if (!TrackedNullab)
    return nullptr;

  if (TrackedNullabPrev &&
      TrackedNullabPrev->getValue() == TrackedNullab->getValue())
    return nullptr;

  // Retrieve the associated statement.
  const Stmt *S = TrackedNullab->getNullabilitySource();
  if (!S || S->getBeginLoc().isInvalid()) {
    S = N->getStmtForDiagnostics();
  }

  if (!S)
    return nullptr;

  std::string InfoText =
      (llvm::Twine("Nullability '") +
       getNullabilityString(TrackedNullab->getValue()) + "' is inferred")
          .str();

  // Generate the extra diagnostic.
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, InfoText, true);
}

/// Returns true when the value stored at the given location has been
/// constrained to null after being passed through an object of nonnnull type.
static bool checkValueAtLValForInvariantViolation(ProgramStateRef State,
                                                  SVal LV, QualType T) {
  if (getNullabilityAnnotation(T) != Nullability::Nonnull)
    return false;

  auto RegionVal = LV.getAs<loc::MemRegionVal>();
  if (!RegionVal)
    return false;

  // If the value was constrained to null *after* it was passed through that
  // location, it could not have been a concrete pointer *when* it was passed.
  // In that case we would have handled the situation when the value was
  // bound to that location, by emitting (or not emitting) a report.
  // Therefore we are only interested in symbolic regions that can be either
  // null or non-null depending on the value of their respective symbol.
  auto StoredVal = State->getSVal(*RegionVal).getAs<loc::MemRegionVal>();
  if (!StoredVal || !isa<SymbolicRegion>(StoredVal->getRegion()))
    return false;

  if (getNullConstraint(*StoredVal, State) == NullConstraint::IsNull)
    return true;

  return false;
}

static bool
checkParamsForPreconditionViolation(ArrayRef<ParmVarDecl *> Params,
                                    ProgramStateRef State,
                                    const LocationContext *LocCtxt) {
  for (const auto *ParamDecl : Params) {
    if (ParamDecl->isParameterPack())
      break;

    SVal LV = State->getLValue(ParamDecl, LocCtxt);
    if (checkValueAtLValForInvariantViolation(State, LV,
                                              ParamDecl->getType())) {
      return true;
    }
  }
  return false;
}

static bool
checkSelfIvarsForInvariantViolation(ProgramStateRef State,
                                    const LocationContext *LocCtxt) {
  auto *MD = dyn_cast<ObjCMethodDecl>(LocCtxt->getDecl());
  if (!MD || !MD->isInstanceMethod())
    return false;

  const ImplicitParamDecl *SelfDecl = LocCtxt->getSelfDecl();
  if (!SelfDecl)
    return false;

  SVal SelfVal = State->getSVal(State->getRegion(SelfDecl, LocCtxt));

  const ObjCObjectPointerType *SelfType =
      dyn_cast<ObjCObjectPointerType>(SelfDecl->getType());
  if (!SelfType)
    return false;

  const ObjCInterfaceDecl *ID = SelfType->getInterfaceDecl();
  if (!ID)
    return false;

  for (const auto *IvarDecl : ID->ivars()) {
    SVal LV = State->getLValue(IvarDecl, SelfVal);
    if (checkValueAtLValForInvariantViolation(State, LV, IvarDecl->getType())) {
      return true;
    }
  }
  return false;
}

static bool checkInvariantViolation(ProgramStateRef State, ExplodedNode *N,
                                    CheckerContext &C) {
  if (State->get<InvariantViolated>())
    return true;

  const LocationContext *LocCtxt = C.getLocationContext();
  const Decl *D = LocCtxt->getDecl();
  if (!D)
    return false;

  ArrayRef<ParmVarDecl*> Params;
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    Params = BD->parameters();
  else if (const auto *FD = dyn_cast<FunctionDecl>(D))
    Params = FD->parameters();
  else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    Params = MD->parameters();
  else
    return false;

  if (checkParamsForPreconditionViolation(Params, State, LocCtxt) ||
      checkSelfIvarsForInvariantViolation(State, LocCtxt)) {
    if (!N->isSink())
      C.addTransition(State->set<InvariantViolated>(true), N);
    return true;
  }
  return false;
}

void NullabilityChecker::reportBugIfInvariantHolds(
    StringRef Msg, ErrorKind Error, CheckKind CK, ExplodedNode *N,
    const MemRegion *Region, CheckerContext &C, const Stmt *ValueExpr,
    bool SuppressPath) const {
  ProgramStateRef OriginalState = N->getState();

  if (checkInvariantViolation(OriginalState, N, C))
    return;
  if (SuppressPath) {
    OriginalState = OriginalState->set<InvariantViolated>(true);
    N = C.addTransition(OriginalState, N);
  }

  reportBug(Msg, Error, CK, N, Region, C.getBugReporter(), ValueExpr);
}

/// Cleaning up the program state.
void NullabilityChecker::checkDeadSymbols(SymbolReaper &SR,
                                          CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  NullabilityMapTy Nullabilities = State->get<NullabilityMap>();
  for (const MemRegion *Reg : llvm::make_first_range(Nullabilities)) {
    const auto *Region = Reg->getAs<SymbolicRegion>();
    assert(Region && "Non-symbolic region is tracked.");
    if (SR.isDead(Region->getSymbol())) {
      State = State->remove<NullabilityMap>(Reg);
    }
  }

  // When an object goes out of scope, we can free the history associated
  // with any property accesses on that object
  PropertyAccessesMapTy PropertyAccesses = State->get<PropertyAccessesMap>();
  for (ObjectPropPair PropKey : llvm::make_first_range(PropertyAccesses)) {
    const MemRegion *ReceiverRegion = PropKey.first;
    if (!SR.isLiveRegion(ReceiverRegion)) {
      State = State->remove<PropertyAccessesMap>(PropKey);
    }
  }

  // When one of the nonnull arguments are constrained to be null, nullability
  // preconditions are violated. It is not enough to check this only when we
  // actually report an error, because at that time interesting symbols might be
  // reaped.
  if (checkInvariantViolation(State, C.getPredecessor(), C))
    return;
  C.addTransition(State);
}

/// This callback triggers when a pointer is dereferenced and the analyzer does
/// not know anything about the value of that pointer. When that pointer is
/// nullable, this code emits a warning.
void NullabilityChecker::checkEvent(ImplicitNullDerefEvent Event) const {
  if (Event.SinkNode->getState()->get<InvariantViolated>())
    return;

  const MemRegion *Region =
      getTrackRegion(Event.Location, /*CheckSuperRegion=*/true);
  if (!Region)
    return;

  ProgramStateRef State = Event.SinkNode->getState();
  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);

  if (!TrackedNullability)
    return;

  if (ChecksEnabled[CK_NullableDereferenced] &&
      TrackedNullability->getValue() == Nullability::Nullable) {
    BugReporter &BR = *Event.BR;
    // Do not suppress errors on defensive code paths, because dereferencing
    // a nullable pointer is always an error.
    if (Event.IsDirectDereference)
      reportBug("Nullable pointer is dereferenced",
                ErrorKind::NullableDereferenced, CK_NullableDereferenced,
                Event.SinkNode, Region, BR);
    else {
      reportBug("Nullable pointer is passed to a callee that requires a "
                "non-null",
                ErrorKind::NullablePassedToNonnull, CK_NullableDereferenced,
                Event.SinkNode, Region, BR);
    }
  }
}

void NullabilityChecker::checkBeginFunction(CheckerContext &C) const {
  if (!C.inTopFrame())
    return;

  const LocationContext *LCtx = C.getLocationContext();
  auto AbstractCall = AnyCall::forDecl(LCtx->getDecl());
  if (!AbstractCall || AbstractCall->parameters().empty())
    return;

  ProgramStateRef State = C.getState();
  for (const ParmVarDecl *Param : AbstractCall->parameters()) {
    if (!isValidPointerType(Param->getType()))
      continue;

    Nullability RequiredNullability =
        getNullabilityAnnotation(Param->getType());
    if (RequiredNullability != Nullability::Nullable)
      continue;

    const VarRegion *ParamRegion = State->getRegion(Param, LCtx);
    const MemRegion *ParamPointeeRegion =
        State->getSVal(ParamRegion).getAsRegion();
    if (!ParamPointeeRegion)
      continue;

    State = State->set<NullabilityMap>(ParamPointeeRegion,
                                       NullabilityState(RequiredNullability));
  }
  C.addTransition(State);
}

// Whenever we see a load from a typed memory region that's been annotated as
// 'nonnull', we want to trust the user on that and assume that it is is indeed
// non-null.
//
// We do so even if the value is known to have been assigned to null.
// The user should be warned on assigning the null value to a non-null pointer
// as opposed to warning on the later dereference of this pointer.
//
// \code
//   int * _Nonnull var = 0; // we want to warn the user here...
//   // . . .
//   *var = 42;              // ...and not here
// \endcode
void NullabilityChecker::checkLocation(SVal Location, bool IsLoad,
                                       const Stmt *S,
                                       CheckerContext &Context) const {
  // We should care only about loads.
  // The main idea is to add a constraint whenever we're loading a value from
  // an annotated pointer type.
  if (!IsLoad)
    return;

  // Annotations that we want to consider make sense only for types.
  const auto *Region =
      dyn_cast_or_null<TypedValueRegion>(Location.getAsRegion());
  if (!Region)
    return;

  ProgramStateRef State = Context.getState();

  auto StoredVal = State->getSVal(Region).getAs<loc::MemRegionVal>();
  if (!StoredVal)
    return;

  Nullability NullabilityOfTheLoadedValue =
      getNullabilityAnnotation(Region->getValueType());

  if (NullabilityOfTheLoadedValue == Nullability::Nonnull) {
    // It doesn't matter what we think about this particular pointer, it should
    // be considered non-null as annotated by the developer.
    if (ProgramStateRef NewState = State->assume(*StoredVal, true)) {
      Context.addTransition(NewState);
    }
  }
}

/// Find the outermost subexpression of E that is not an implicit cast.
/// This looks through the implicit casts to _Nonnull that ARC adds to
/// return expressions of ObjC types when the return type of the function or
/// method is non-null but the express is not.
static const Expr *lookThroughImplicitCasts(const Expr *E) {
  return E->IgnoreImpCasts();
}

/// This method check when nullable pointer or null value is returned from a
/// function that has nonnull return type.
void NullabilityChecker::checkPreStmt(const ReturnStmt *S,
                                      CheckerContext &C) const {
  auto RetExpr = S->getRetValue();
  if (!RetExpr)
    return;

  if (!isValidPointerType(RetExpr->getType()))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  auto RetSVal = C.getSVal(S).getAs<DefinedOrUnknownSVal>();
  if (!RetSVal)
    return;

  bool InSuppressedMethodFamily = false;

  QualType RequiredRetType;
  AnalysisDeclContext *DeclCtxt =
      C.getLocationContext()->getAnalysisDeclContext();
  const Decl *D = DeclCtxt->getDecl();
  if (auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    // HACK: This is a big hammer to avoid warning when there are defensive
    // nil checks in -init and -copy methods. We should add more sophisticated
    // logic here to suppress on common defensive idioms but still
    // warn when there is a likely problem.
    ObjCMethodFamily Family = MD->getMethodFamily();
    if (OMF_init == Family || OMF_copy == Family || OMF_mutableCopy == Family)
      InSuppressedMethodFamily = true;

    RequiredRetType = MD->getReturnType();
  } else if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    RequiredRetType = FD->getReturnType();
  } else {
    return;
  }

  NullConstraint Nullness = getNullConstraint(*RetSVal, State);

  Nullability RequiredNullability = getNullabilityAnnotation(RequiredRetType);

  // If the returned value is null but the type of the expression
  // generating it is nonnull then we will suppress the diagnostic.
  // This enables explicit suppression when returning a nil literal in a
  // function with a _Nonnull return type:
  //    return (NSString * _Nonnull)0;
  Nullability RetExprTypeLevelNullability =
        getNullabilityAnnotation(lookThroughImplicitCasts(RetExpr)->getType());

  bool NullReturnedFromNonNull = (RequiredNullability == Nullability::Nonnull &&
                                  Nullness == NullConstraint::IsNull);
  if (ChecksEnabled[CK_NullReturnedFromNonnull] && NullReturnedFromNonNull &&
      RetExprTypeLevelNullability != Nullability::Nonnull &&
      !InSuppressedMethodFamily && C.getLocationContext()->inTopFrame()) {
    static CheckerProgramPointTag Tag(this, "NullReturnedFromNonnull");
    ExplodedNode *N = C.generateErrorNode(State, &Tag);
    if (!N)
      return;

    SmallString<256> SBuf;
    llvm::raw_svector_ostream OS(SBuf);
    OS << (RetExpr->getType()->isObjCObjectPointerType() ? "nil" : "Null");
    OS << " returned from a " << C.getDeclDescription(D) <<
          " that is expected to return a non-null value";
    reportBugIfInvariantHolds(OS.str(), ErrorKind::NilReturnedToNonnull,
                              CK_NullReturnedFromNonnull, N, nullptr, C,
                              RetExpr);
    return;
  }

  // If null was returned from a non-null function, mark the nullability
  // invariant as violated even if the diagnostic was suppressed.
  if (NullReturnedFromNonNull) {
    State = State->set<InvariantViolated>(true);
    C.addTransition(State);
    return;
  }

  const MemRegion *Region = getTrackRegion(*RetSVal);
  if (!Region)
    return;

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);
  if (TrackedNullability) {
    Nullability TrackedNullabValue = TrackedNullability->getValue();
    if (ChecksEnabled[CK_NullableReturnedFromNonnull] &&
        Nullness != NullConstraint::IsNotNull &&
        TrackedNullabValue == Nullability::Nullable &&
        RequiredNullability == Nullability::Nonnull) {
      static CheckerProgramPointTag Tag(this, "NullableReturnedFromNonnull");
      ExplodedNode *N = C.addTransition(State, C.getPredecessor(), &Tag);

      SmallString<256> SBuf;
      llvm::raw_svector_ostream OS(SBuf);
      OS << "Nullable pointer is returned from a " << C.getDeclDescription(D) <<
            " that is expected to return a non-null value";

      reportBugIfInvariantHolds(OS.str(), ErrorKind::NullableReturnedToNonnull,
                                CK_NullableReturnedFromNonnull, N, Region, C);
    }
    return;
  }
  if (RequiredNullability == Nullability::Nullable) {
    State = State->set<NullabilityMap>(Region,
                                       NullabilityState(RequiredNullability,
                                                        S));
    C.addTransition(State);
  }
}

/// This callback warns when a nullable pointer or a null value is passed to a
/// function that expects its argument to be nonnull.
void NullabilityChecker::checkPreCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  if (!Call.getDecl())
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  ProgramStateRef OrigState = State;

  unsigned Idx = 0;
  for (const ParmVarDecl *Param : Call.parameters()) {
    if (Param->isParameterPack())
      break;

    if (Idx >= Call.getNumArgs())
      break;

    const Expr *ArgExpr = Call.getArgExpr(Idx);
    auto ArgSVal = Call.getArgSVal(Idx++).getAs<DefinedOrUnknownSVal>();
    if (!ArgSVal)
      continue;

    if (!isValidPointerType(Param->getType()) &&
        !Param->getType()->isReferenceType())
      continue;

    NullConstraint Nullness = getNullConstraint(*ArgSVal, State);

    Nullability RequiredNullability =
        getNullabilityAnnotation(Param->getType());
    Nullability ArgExprTypeLevelNullability =
        getNullabilityAnnotation(lookThroughImplicitCasts(ArgExpr)->getType());

    unsigned ParamIdx = Param->getFunctionScopeIndex() + 1;

    if (ChecksEnabled[CK_NullPassedToNonnull] &&
        Nullness == NullConstraint::IsNull &&
        ArgExprTypeLevelNullability != Nullability::Nonnull &&
        RequiredNullability == Nullability::Nonnull &&
        isDiagnosableCall(Call)) {
      ExplodedNode *N = C.generateErrorNode(State);
      if (!N)
        return;

      SmallString<256> SBuf;
      llvm::raw_svector_ostream OS(SBuf);
      OS << (Param->getType()->isObjCObjectPointerType() ? "nil" : "Null");
      OS << " passed to a callee that requires a non-null " << ParamIdx
         << llvm::getOrdinalSuffix(ParamIdx) << " parameter";
      reportBugIfInvariantHolds(OS.str(), ErrorKind::NilPassedToNonnull,
                                CK_NullPassedToNonnull, N, nullptr, C, ArgExpr,
                                /*SuppressPath=*/false);
      return;
    }

    const MemRegion *Region = getTrackRegion(*ArgSVal);
    if (!Region)
      continue;

    const NullabilityState *TrackedNullability =
        State->get<NullabilityMap>(Region);

    if (TrackedNullability) {
      if (Nullness == NullConstraint::IsNotNull ||
          TrackedNullability->getValue() != Nullability::Nullable)
        continue;

      if (ChecksEnabled[CK_NullablePassedToNonnull] &&
          RequiredNullability == Nullability::Nonnull &&
          isDiagnosableCall(Call)) {
        ExplodedNode *N = C.addTransition(State);
        SmallString<256> SBuf;
        llvm::raw_svector_ostream OS(SBuf);
        OS << "Nullable pointer is passed to a callee that requires a non-null "
           << ParamIdx << llvm::getOrdinalSuffix(ParamIdx) << " parameter";
        reportBugIfInvariantHolds(OS.str(), ErrorKind::NullablePassedToNonnull,
                                  CK_NullablePassedToNonnull, N, Region, C,
                                  ArgExpr, /*SuppressPath=*/true);
        return;
      }
      if (ChecksEnabled[CK_NullableDereferenced] &&
          Param->getType()->isReferenceType()) {
        ExplodedNode *N = C.addTransition(State);
        reportBugIfInvariantHolds("Nullable pointer is dereferenced",
                                  ErrorKind::NullableDereferenced,
                                  CK_NullableDereferenced, N, Region, C,
                                  ArgExpr, /*SuppressPath=*/true);
        return;
      }
      continue;
    }
  }
  if (State != OrigState)
    C.addTransition(State);
}

/// Suppress the nullability warnings for some functions.
void NullabilityChecker::checkPostCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  auto Decl = Call.getDecl();
  if (!Decl)
    return;
  // ObjC Messages handles in a different callback.
  if (Call.getKind() == CE_ObjCMessage)
    return;
  const FunctionType *FuncType = Decl->getFunctionType();
  if (!FuncType)
    return;
  QualType ReturnType = FuncType->getReturnType();
  if (!isValidPointerType(ReturnType))
    return;
  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  const MemRegion *Region = getTrackRegion(Call.getReturnValue());
  if (!Region)
    return;

  // CG headers are misannotated. Do not warn for symbols that are the results
  // of CG calls.
  const SourceManager &SM = C.getSourceManager();
  StringRef FilePath = SM.getFilename(SM.getSpellingLoc(Decl->getBeginLoc()));
  if (llvm::sys::path::filename(FilePath).starts_with("CG")) {
    State = State->set<NullabilityMap>(Region, Nullability::Contradicted);
    C.addTransition(State);
    return;
  }

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);

  // ObjCMessageExpr gets the actual type through
  // Sema::getMessageSendResultType, instead of using the return type of
  // MethodDecl directly. The final type is generated by considering the
  // nullability of receiver and MethodDecl together. Thus, The type of
  // ObjCMessageExpr is prefer.
  if (const Expr *E = Call.getOriginExpr())
    ReturnType = E->getType();

  if (!TrackedNullability &&
      getNullabilityAnnotation(ReturnType) == Nullability::Nullable) {
    State = State->set<NullabilityMap>(Region, Nullability::Nullable);
    C.addTransition(State);
  }
}

static Nullability getReceiverNullability(const ObjCMethodCall &M,
                                          ProgramStateRef State) {
  if (M.isReceiverSelfOrSuper()) {
    // For super and super class receivers we assume that the receiver is
    // nonnull.
    return Nullability::Nonnull;
  }
  // Otherwise look up nullability in the state.
  SVal Receiver = M.getReceiverSVal();
  if (auto DefOrUnknown = Receiver.getAs<DefinedOrUnknownSVal>()) {
    // If the receiver is constrained to be nonnull, assume that it is nonnull
    // regardless of its type.
    NullConstraint Nullness = getNullConstraint(*DefOrUnknown, State);
    if (Nullness == NullConstraint::IsNotNull)
      return Nullability::Nonnull;
  }
  auto ValueRegionSVal = Receiver.getAs<loc::MemRegionVal>();
  if (ValueRegionSVal) {
    const MemRegion *SelfRegion = ValueRegionSVal->getRegion();
    assert(SelfRegion);

    const NullabilityState *TrackedSelfNullability =
        State->get<NullabilityMap>(SelfRegion);
    if (TrackedSelfNullability)
      return TrackedSelfNullability->getValue();
  }
  return Nullability::Unspecified;
}

// The return value of a property access is typically a temporary value which
// will not be tracked in a persistent manner by the analyzer.  We use
// evalAssume() in order to immediately record constraints on those temporaries
// at the time they are imposed (e.g. by a nil-check conditional).
ProgramStateRef NullabilityChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                               bool Assumption) const {
  PropertyAccessesMapTy PropertyAccesses = State->get<PropertyAccessesMap>();
  for (auto [PropKey, PropVal] : PropertyAccesses) {
    if (!PropVal.isConstrainedNonnull) {
      ConditionTruthVal IsNonNull = State->isNonNull(PropVal.Value);
      if (IsNonNull.isConstrainedTrue()) {
        ConstrainedPropertyVal Replacement = PropVal;
        Replacement.isConstrainedNonnull = true;
        State = State->set<PropertyAccessesMap>(PropKey, Replacement);
      } else if (IsNonNull.isConstrainedFalse()) {
        // Space optimization: no point in tracking constrained-null cases
        State = State->remove<PropertyAccessesMap>(PropKey);
      }
    }
  }

  return State;
}

/// Calculate the nullability of the result of a message expr based on the
/// nullability of the receiver, the nullability of the return value, and the
/// constraints.
void NullabilityChecker::checkPostObjCMessage(const ObjCMethodCall &M,
                                              CheckerContext &C) const {
  auto Decl = M.getDecl();
  if (!Decl)
    return;
  QualType RetType = Decl->getReturnType();
  if (!isValidPointerType(RetType))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  const MemRegion *ReturnRegion = getTrackRegion(M.getReturnValue());
  if (!ReturnRegion)
    return;

  auto Interface = Decl->getClassInterface();
  auto Name = Interface ? Interface->getName() : "";
  // In order to reduce the noise in the diagnostics generated by this checker,
  // some framework and programming style based heuristics are used. These
  // heuristics are for Cocoa APIs which have NS prefix.
  if (Name.starts_with("NS")) {
    // Developers rely on dynamic invariants such as an item should be available
    // in a collection, or a collection is not empty often. Those invariants can
    // not be inferred by any static analysis tool. To not to bother the users
    // with too many false positives, every item retrieval function should be
    // ignored for collections. The instance methods of dictionaries in Cocoa
    // are either item retrieval related or not interesting nullability wise.
    // Using this fact, to keep the code easier to read just ignore the return
    // value of every instance method of dictionaries.
    if (M.isInstanceMessage() && Name.contains("Dictionary")) {
      State =
          State->set<NullabilityMap>(ReturnRegion, Nullability::Contradicted);
      C.addTransition(State);
      return;
    }
    // For similar reasons ignore some methods of Cocoa arrays.
    StringRef FirstSelectorSlot = M.getSelector().getNameForSlot(0);
    if (Name.contains("Array") &&
        (FirstSelectorSlot == "firstObject" ||
         FirstSelectorSlot == "lastObject")) {
      State =
          State->set<NullabilityMap>(ReturnRegion, Nullability::Contradicted);
      C.addTransition(State);
      return;
    }

    // Encoding related methods of string should not fail when lossless
    // encodings are used. Using lossless encodings is so frequent that ignoring
    // this class of methods reduced the emitted diagnostics by about 30% on
    // some projects (and all of that was false positives).
    if (Name.contains("String")) {
      for (auto *Param : M.parameters()) {
        if (Param->getName() == "encoding") {
          State = State->set<NullabilityMap>(ReturnRegion,
                                             Nullability::Contradicted);
          C.addTransition(State);
          return;
        }
      }
    }
  }

  const ObjCMessageExpr *Message = M.getOriginExpr();
  Nullability SelfNullability = getReceiverNullability(M, State);

  const NullabilityState *NullabilityOfReturn =
      State->get<NullabilityMap>(ReturnRegion);

  if (NullabilityOfReturn) {
    // When we have a nullability tracked for the return value, the nullability
    // of the expression will be the most nullable of the receiver and the
    // return value.
    Nullability RetValTracked = NullabilityOfReturn->getValue();
    Nullability ComputedNullab =
        getMostNullable(RetValTracked, SelfNullability);
    if (ComputedNullab != RetValTracked &&
        ComputedNullab != Nullability::Unspecified) {
      const Stmt *NullabilitySource =
          ComputedNullab == RetValTracked
              ? NullabilityOfReturn->getNullabilitySource()
              : Message->getInstanceReceiver();
      State = State->set<NullabilityMap>(
          ReturnRegion, NullabilityState(ComputedNullab, NullabilitySource));
      C.addTransition(State);
    }
    return;
  }

  // No tracked information. Use static type information for return value.
  Nullability RetNullability = getNullabilityAnnotation(Message->getType());

  // Properties might be computed, which means the property value could
  // theoretically change between calls even in commonly-observed cases like
  // this:
  //
  //     if (foo.prop) {    // ok, it's nonnull here...
  //         [bar doStuffWithNonnullVal:foo.prop];     // ...but what about
  //         here?
  //     }
  //
  // If the property is nullable-annotated, a naive analysis would lead to many
  // false positives despite the presence of probably-correct nil-checks.  To
  // reduce the false positive rate, we maintain a history of the most recently
  // observed property value.  For each property access, if the prior value has
  // been constrained to be not nil then we will conservatively assume that the
  // next access can be inferred as nonnull.
  if (RetNullability != Nullability::Nonnull &&
      M.getMessageKind() == OCM_PropertyAccess && !C.wasInlined) {
    bool LookupResolved = false;
    if (const MemRegion *ReceiverRegion = getTrackRegion(M.getReceiverSVal())) {
      if (const IdentifierInfo *Ident =
              M.getSelector().getIdentifierInfoForSlot(0)) {
        LookupResolved = true;
        ObjectPropPair Key = std::make_pair(ReceiverRegion, Ident);
        const ConstrainedPropertyVal *PrevPropVal =
            State->get<PropertyAccessesMap>(Key);
        if (PrevPropVal && PrevPropVal->isConstrainedNonnull) {
          RetNullability = Nullability::Nonnull;
        } else {
          // If a previous property access was constrained as nonnull, we hold
          // on to that constraint (effectively inferring that all subsequent
          // accesses on that code path can be inferred as nonnull).  If the
          // previous property access was *not* constrained as nonnull, then
          // let's throw it away in favor of keeping the SVal associated with
          // this more recent access.
          if (auto ReturnSVal =
                  M.getReturnValue().getAs<DefinedOrUnknownSVal>()) {
            State = State->set<PropertyAccessesMap>(
                Key, ConstrainedPropertyVal(*ReturnSVal));
          }
        }
      }
    }

    if (!LookupResolved) {
      // Fallback: err on the side of suppressing the false positive.
      RetNullability = Nullability::Nonnull;
    }
  }

  Nullability ComputedNullab = getMostNullable(RetNullability, SelfNullability);
  if (ComputedNullab == Nullability::Nullable) {
    const Stmt *NullabilitySource = ComputedNullab == RetNullability
                                        ? Message
                                        : Message->getInstanceReceiver();
    State = State->set<NullabilityMap>(
        ReturnRegion, NullabilityState(ComputedNullab, NullabilitySource));
    C.addTransition(State);
  }
}

/// Explicit casts are trusted. If there is a disagreement in the nullability
/// annotations in the destination and the source or '0' is casted to nonnull
/// track the value as having contraditory nullability. This will allow users to
/// suppress warnings.
void NullabilityChecker::checkPostStmt(const ExplicitCastExpr *CE,
                                       CheckerContext &C) const {
  QualType OriginType = CE->getSubExpr()->getType();
  QualType DestType = CE->getType();
  if (!isValidPointerType(OriginType))
    return;
  if (!isValidPointerType(DestType))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  Nullability DestNullability = getNullabilityAnnotation(DestType);

  // No explicit nullability in the destination type, so this cast does not
  // change the nullability.
  if (DestNullability == Nullability::Unspecified)
    return;

  auto RegionSVal = C.getSVal(CE).getAs<DefinedOrUnknownSVal>();
  const MemRegion *Region = getTrackRegion(*RegionSVal);
  if (!Region)
    return;

  // When 0 is converted to nonnull mark it as contradicted.
  if (DestNullability == Nullability::Nonnull) {
    NullConstraint Nullness = getNullConstraint(*RegionSVal, State);
    if (Nullness == NullConstraint::IsNull) {
      State = State->set<NullabilityMap>(Region, Nullability::Contradicted);
      C.addTransition(State);
      return;
    }
  }

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(Region);

  if (!TrackedNullability) {
    if (DestNullability != Nullability::Nullable)
      return;
    State = State->set<NullabilityMap>(Region,
                                       NullabilityState(DestNullability, CE));
    C.addTransition(State);
    return;
  }

  if (TrackedNullability->getValue() != DestNullability &&
      TrackedNullability->getValue() != Nullability::Contradicted) {
    State = State->set<NullabilityMap>(Region, Nullability::Contradicted);
    C.addTransition(State);
  }
}

/// For a given statement performing a bind, attempt to syntactically
/// match the expression resulting in the bound value.
static const Expr * matchValueExprForBind(const Stmt *S) {
  // For `x = e` the value expression is the right-hand side.
  if (auto *BinOp = dyn_cast<BinaryOperator>(S)) {
    if (BinOp->getOpcode() == BO_Assign)
      return BinOp->getRHS();
  }

  // For `int x = e` the value expression is the initializer.
  if (auto *DS = dyn_cast<DeclStmt>(S))  {
    if (DS->isSingleDecl()) {
      auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl());
      if (!VD)
        return nullptr;

      if (const Expr *Init = VD->getInit())
        return Init;
    }
  }

  return nullptr;
}

/// Returns true if \param S is a DeclStmt for a local variable that
/// ObjC automated reference counting initialized with zero.
static bool isARCNilInitializedLocal(CheckerContext &C, const Stmt *S) {
  // We suppress diagnostics for ARC zero-initialized _Nonnull locals. This
  // prevents false positives when a _Nonnull local variable cannot be
  // initialized with an initialization expression:
  //    NSString * _Nonnull s; // no-warning
  //    @autoreleasepool {
  //      s = ...
  //    }
  //
  // FIXME: We should treat implicitly zero-initialized _Nonnull locals as
  // uninitialized in Sema's UninitializedValues analysis to warn when a use of
  // the zero-initialized definition will unexpectedly yield nil.

  // Locals are only zero-initialized when automated reference counting
  // is turned on.
  if (!C.getASTContext().getLangOpts().ObjCAutoRefCount)
    return false;

  auto *DS = dyn_cast<DeclStmt>(S);
  if (!DS || !DS->isSingleDecl())
    return false;

  auto *VD = dyn_cast<VarDecl>(DS->getSingleDecl());
  if (!VD)
    return false;

  // Sema only zero-initializes locals with ObjCLifetimes.
  if(!VD->getType().getQualifiers().hasObjCLifetime())
    return false;

  const Expr *Init = VD->getInit();
  assert(Init && "ObjC local under ARC without initializer");

  // Return false if the local is explicitly initialized (e.g., with '= nil').
  if (!isa<ImplicitValueInitExpr>(Init))
    return false;

  return true;
}

/// Propagate the nullability information through binds and warn when nullable
/// pointer or null symbol is assigned to a pointer with a nonnull type.
void NullabilityChecker::checkBind(SVal L, SVal V, const Stmt *S,
                                   CheckerContext &C) const {
  const TypedValueRegion *TVR =
      dyn_cast_or_null<TypedValueRegion>(L.getAsRegion());
  if (!TVR)
    return;

  QualType LocType = TVR->getValueType();
  if (!isValidPointerType(LocType))
    return;

  ProgramStateRef State = C.getState();
  if (State->get<InvariantViolated>())
    return;

  auto ValDefOrUnknown = V.getAs<DefinedOrUnknownSVal>();
  if (!ValDefOrUnknown)
    return;

  NullConstraint RhsNullness = getNullConstraint(*ValDefOrUnknown, State);

  Nullability ValNullability = Nullability::Unspecified;
  if (SymbolRef Sym = ValDefOrUnknown->getAsSymbol())
    ValNullability = getNullabilityAnnotation(Sym->getType());

  Nullability LocNullability = getNullabilityAnnotation(LocType);

  // If the type of the RHS expression is nonnull, don't warn. This
  // enables explicit suppression with a cast to nonnull.
  Nullability ValueExprTypeLevelNullability = Nullability::Unspecified;
  const Expr *ValueExpr = matchValueExprForBind(S);
  if (ValueExpr) {
    ValueExprTypeLevelNullability =
      getNullabilityAnnotation(lookThroughImplicitCasts(ValueExpr)->getType());
  }

  bool NullAssignedToNonNull = (LocNullability == Nullability::Nonnull &&
                                RhsNullness == NullConstraint::IsNull);
  if (ChecksEnabled[CK_NullPassedToNonnull] && NullAssignedToNonNull &&
      ValNullability != Nullability::Nonnull &&
      ValueExprTypeLevelNullability != Nullability::Nonnull &&
      !isARCNilInitializedLocal(C, S)) {
    static CheckerProgramPointTag Tag(this, "NullPassedToNonnull");
    ExplodedNode *N = C.generateErrorNode(State, &Tag);
    if (!N)
      return;


    const Stmt *ValueStmt = S;
    if (ValueExpr)
      ValueStmt = ValueExpr;

    SmallString<256> SBuf;
    llvm::raw_svector_ostream OS(SBuf);
    OS << (LocType->isObjCObjectPointerType() ? "nil" : "Null");
    OS << " assigned to a pointer which is expected to have non-null value";
    reportBugIfInvariantHolds(OS.str(), ErrorKind::NilAssignedToNonnull,
                              CK_NullPassedToNonnull, N, nullptr, C, ValueStmt);
    return;
  }

  // If null was returned from a non-null function, mark the nullability
  // invariant as violated even if the diagnostic was suppressed.
  if (NullAssignedToNonNull) {
    State = State->set<InvariantViolated>(true);
    C.addTransition(State);
    return;
  }

  // Intentionally missing case: '0' is bound to a reference. It is handled by
  // the DereferenceChecker.

  const MemRegion *ValueRegion = getTrackRegion(*ValDefOrUnknown);
  if (!ValueRegion)
    return;

  const NullabilityState *TrackedNullability =
      State->get<NullabilityMap>(ValueRegion);

  if (TrackedNullability) {
    if (RhsNullness == NullConstraint::IsNotNull ||
        TrackedNullability->getValue() != Nullability::Nullable)
      return;
    if (ChecksEnabled[CK_NullablePassedToNonnull] &&
        LocNullability == Nullability::Nonnull) {
      static CheckerProgramPointTag Tag(this, "NullablePassedToNonnull");
      ExplodedNode *N = C.addTransition(State, C.getPredecessor(), &Tag);
      reportBugIfInvariantHolds("Nullable pointer is assigned to a pointer "
                                "which is expected to have non-null value",
                                ErrorKind::NullableAssignedToNonnull,
                                CK_NullablePassedToNonnull, N, ValueRegion, C);
    }
    return;
  }

  const auto *BinOp = dyn_cast<BinaryOperator>(S);

  if (ValNullability == Nullability::Nullable) {
    // Trust the static information of the value more than the static
    // information on the location.
    const Stmt *NullabilitySource = BinOp ? BinOp->getRHS() : S;
    State = State->set<NullabilityMap>(
        ValueRegion, NullabilityState(ValNullability, NullabilitySource));
    C.addTransition(State);
    return;
  }

  if (LocNullability == Nullability::Nullable) {
    const Stmt *NullabilitySource = BinOp ? BinOp->getLHS() : S;
    State = State->set<NullabilityMap>(
        ValueRegion, NullabilityState(LocNullability, NullabilitySource));
    C.addTransition(State);
  }
}

void NullabilityChecker::printState(raw_ostream &Out, ProgramStateRef State,
                                    const char *NL, const char *Sep) const {

  NullabilityMapTy B = State->get<NullabilityMap>();

  if (State->get<InvariantViolated>())
    Out << Sep << NL
        << "Nullability invariant was violated, warnings suppressed." << NL;

  if (B.isEmpty())
    return;

  if (!State->get<InvariantViolated>())
    Out << Sep << NL;

  for (auto [Region, State] : B) {
    Out << Region << " : ";
    State.print(Out);
    Out << NL;
  }
}

void ento::registerNullabilityBase(CheckerManager &mgr) {
  mgr.registerChecker<NullabilityChecker>();
}

bool ento::shouldRegisterNullabilityBase(const CheckerManager &mgr) {
  return true;
}

#define REGISTER_CHECKER(name, trackingRequired)                               \
  void ento::register##name##Checker(CheckerManager &mgr) {                    \
    NullabilityChecker *checker = mgr.getChecker<NullabilityChecker>();        \
    checker->ChecksEnabled[NullabilityChecker::CK_##name] = true;              \
    checker->CheckNames[NullabilityChecker::CK_##name] =                       \
        mgr.getCurrentCheckerName();                                           \
    checker->NeedTracking = checker->NeedTracking || trackingRequired;         \
    checker->NoDiagnoseCallsToSystemHeaders =                                  \
        checker->NoDiagnoseCallsToSystemHeaders ||                             \
        mgr.getAnalyzerOptions().getCheckerBooleanOption(                      \
            checker, "NoDiagnoseCallsToSystemHeaders", true);                  \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name##Checker(const CheckerManager &mgr) {        \
    return true;                                                               \
  }

// The checks are likely to be turned on by default and it is possible to do
// them without tracking any nullability related information. As an optimization
// no nullability information will be tracked when only these two checks are
// enables.
REGISTER_CHECKER(NullPassedToNonnull, false)
REGISTER_CHECKER(NullReturnedFromNonnull, false)

REGISTER_CHECKER(NullableDereferenced, true)
REGISTER_CHECKER(NullablePassedToNonnull, true)
REGISTER_CHECKER(NullableReturnedFromNonnull, true)
