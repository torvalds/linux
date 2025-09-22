//==-- RetainCountChecker.cpp - Checks for leaks and other issues -*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the methods for RetainCountChecker, which implements
//  a reference count checker for Core Foundation and Cocoa on (Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "RetainCountChecker.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace retaincountchecker;

REGISTER_MAP_WITH_PROGRAMSTATE(RefBindings, SymbolRef, RefVal)

namespace clang {
namespace ento {
namespace retaincountchecker {

const RefVal *getRefBinding(ProgramStateRef State, SymbolRef Sym) {
  return State->get<RefBindings>(Sym);
}

} // end namespace retaincountchecker
} // end namespace ento
} // end namespace clang

static ProgramStateRef setRefBinding(ProgramStateRef State, SymbolRef Sym,
                                     RefVal Val) {
  assert(Sym != nullptr);
  return State->set<RefBindings>(Sym, Val);
}

static ProgramStateRef removeRefBinding(ProgramStateRef State, SymbolRef Sym) {
  return State->remove<RefBindings>(Sym);
}

void RefVal::print(raw_ostream &Out) const {
  if (!T.isNull())
    Out << "Tracked " << T << " | ";

  switch (getKind()) {
    default: llvm_unreachable("Invalid RefVal kind");
    case Owned: {
      Out << "Owned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }

    case NotOwned: {
      Out << "NotOwned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }

    case ReturnedOwned: {
      Out << "ReturnedOwned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }

    case ReturnedNotOwned: {
      Out << "ReturnedNotOwned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }

    case Released:
      Out << "Released";
      break;

    case ErrorDeallocNotOwned:
      Out << "-dealloc (not-owned)";
      break;

    case ErrorLeak:
      Out << "Leaked";
      break;

    case ErrorLeakReturned:
      Out << "Leaked (Bad naming)";
      break;

    case ErrorUseAfterRelease:
      Out << "Use-After-Release [ERROR]";
      break;

    case ErrorReleaseNotOwned:
      Out << "Release of Not-Owned [ERROR]";
      break;

    case RefVal::ErrorOverAutorelease:
      Out << "Over-autoreleased";
      break;

    case RefVal::ErrorReturnedNotOwned:
      Out << "Non-owned object returned instead of owned";
      break;
  }

  switch (getIvarAccessHistory()) {
  case IvarAccessHistory::None:
    break;
  case IvarAccessHistory::AccessedDirectly:
    Out << " [direct ivar access]";
    break;
  case IvarAccessHistory::ReleasedAfterDirectAccess:
    Out << " [released after direct ivar access]";
  }

  if (ACnt) {
    Out << " [autorelease -" << ACnt << ']';
  }
}

namespace {
class StopTrackingCallback final : public SymbolVisitor {
  ProgramStateRef state;
public:
  StopTrackingCallback(ProgramStateRef st) : state(std::move(st)) {}
  ProgramStateRef getState() const { return state; }

  bool VisitSymbol(SymbolRef sym) override {
    state = removeRefBinding(state, sym);
    return true;
  }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Handle statements that may have an effect on refcounts.
//===----------------------------------------------------------------------===//

void RetainCountChecker::checkPostStmt(const BlockExpr *BE,
                                       CheckerContext &C) const {

  // Scan the BlockDecRefExprs for any object the retain count checker
  // may be tracking.
  if (!BE->getBlockDecl()->hasCaptures())
    return;

  ProgramStateRef state = C.getState();
  auto *R = cast<BlockDataRegion>(C.getSVal(BE).getAsRegion());

  auto ReferencedVars = R->referenced_vars();
  if (ReferencedVars.empty())
    return;

  // FIXME: For now we invalidate the tracking of all symbols passed to blocks
  // via captured variables, even though captured variables result in a copy
  // and in implicit increment/decrement of a retain count.
  SmallVector<const MemRegion*, 10> Regions;
  const LocationContext *LC = C.getLocationContext();
  MemRegionManager &MemMgr = C.getSValBuilder().getRegionManager();

  for (auto Var : ReferencedVars) {
    const VarRegion *VR = Var.getCapturedRegion();
    if (VR->getSuperRegion() == R) {
      VR = MemMgr.getVarRegion(VR->getDecl(), LC);
    }
    Regions.push_back(VR);
  }

  state = state->scanReachableSymbols<StopTrackingCallback>(Regions).getState();
  C.addTransition(state);
}

void RetainCountChecker::checkPostStmt(const CastExpr *CE,
                                       CheckerContext &C) const {
  const ObjCBridgedCastExpr *BE = dyn_cast<ObjCBridgedCastExpr>(CE);
  if (!BE)
    return;

  QualType QT = CE->getType();
  ObjKind K;
  if (QT->isObjCObjectPointerType()) {
    K = ObjKind::ObjC;
  } else {
    K = ObjKind::CF;
  }

  ArgEffect AE = ArgEffect(IncRef, K);

  switch (BE->getBridgeKind()) {
    case OBC_Bridge:
      // Do nothing.
      return;
    case OBC_BridgeRetained:
      AE = AE.withKind(IncRef);
      break;
    case OBC_BridgeTransfer:
      AE = AE.withKind(DecRefBridgedTransferred);
      break;
  }

  ProgramStateRef state = C.getState();
  SymbolRef Sym = C.getSVal(CE).getAsLocSymbol();
  if (!Sym)
    return;
  const RefVal* T = getRefBinding(state, Sym);
  if (!T)
    return;

  RefVal::Kind hasErr = (RefVal::Kind) 0;
  state = updateSymbol(state, Sym, *T, AE, hasErr, C);

  if (hasErr) {
    // FIXME: If we get an error during a bridge cast, should we report it?
    return;
  }

  C.addTransition(state);
}

void RetainCountChecker::processObjCLiterals(CheckerContext &C,
                                             const Expr *Ex) const {
  ProgramStateRef state = C.getState();
  const ExplodedNode *pred = C.getPredecessor();
  for (const Stmt *Child : Ex->children()) {
    SVal V = pred->getSVal(Child);
    if (SymbolRef sym = V.getAsSymbol())
      if (const RefVal* T = getRefBinding(state, sym)) {
        RefVal::Kind hasErr = (RefVal::Kind) 0;
        state = updateSymbol(state, sym, *T,
                             ArgEffect(MayEscape, ObjKind::ObjC), hasErr, C);
        if (hasErr) {
          processNonLeakError(state, Child->getSourceRange(), hasErr, sym, C);
          return;
        }
      }
  }

  // Return the object as autoreleased.
  //  RetEffect RE = RetEffect::MakeNotOwned(ObjKind::ObjC);
  if (SymbolRef sym =
        state->getSVal(Ex, pred->getLocationContext()).getAsSymbol()) {
    QualType ResultTy = Ex->getType();
    state = setRefBinding(state, sym,
                          RefVal::makeNotOwned(ObjKind::ObjC, ResultTy));
  }

  C.addTransition(state);
}

void RetainCountChecker::checkPostStmt(const ObjCArrayLiteral *AL,
                                       CheckerContext &C) const {
  // Apply the 'MayEscape' to all values.
  processObjCLiterals(C, AL);
}

void RetainCountChecker::checkPostStmt(const ObjCDictionaryLiteral *DL,
                                       CheckerContext &C) const {
  // Apply the 'MayEscape' to all keys and values.
  processObjCLiterals(C, DL);
}

void RetainCountChecker::checkPostStmt(const ObjCBoxedExpr *Ex,
                                       CheckerContext &C) const {
  const ExplodedNode *Pred = C.getPredecessor();
  ProgramStateRef State = Pred->getState();

  if (SymbolRef Sym = Pred->getSVal(Ex).getAsSymbol()) {
    QualType ResultTy = Ex->getType();
    State = setRefBinding(State, Sym,
                          RefVal::makeNotOwned(ObjKind::ObjC, ResultTy));
  }

  C.addTransition(State);
}

void RetainCountChecker::checkPostStmt(const ObjCIvarRefExpr *IRE,
                                       CheckerContext &C) const {
  std::optional<Loc> IVarLoc = C.getSVal(IRE).getAs<Loc>();
  if (!IVarLoc)
    return;

  ProgramStateRef State = C.getState();
  SymbolRef Sym = State->getSVal(*IVarLoc).getAsSymbol();
  if (!Sym || !isa_and_nonnull<ObjCIvarRegion>(Sym->getOriginRegion()))
    return;

  // Accessing an ivar directly is unusual. If we've done that, be more
  // forgiving about what the surrounding code is allowed to do.

  QualType Ty = Sym->getType();
  ObjKind Kind;
  if (Ty->isObjCRetainableType())
    Kind = ObjKind::ObjC;
  else if (coreFoundation::isCFObjectRef(Ty))
    Kind = ObjKind::CF;
  else
    return;

  // If the value is already known to be nil, don't bother tracking it.
  ConstraintManager &CMgr = State->getConstraintManager();
  if (CMgr.isNull(State, Sym).isConstrainedTrue())
    return;

  if (const RefVal *RV = getRefBinding(State, Sym)) {
    // If we've seen this symbol before, or we're only seeing it now because
    // of something the analyzer has synthesized, don't do anything.
    if (RV->getIvarAccessHistory() != RefVal::IvarAccessHistory::None ||
        isSynthesizedAccessor(C.getStackFrame())) {
      return;
    }

    // Note that this value has been loaded from an ivar.
    C.addTransition(setRefBinding(State, Sym, RV->withIvarAccess()));
    return;
  }

  RefVal PlusZero = RefVal::makeNotOwned(Kind, Ty);

  // In a synthesized accessor, the effective retain count is +0.
  if (isSynthesizedAccessor(C.getStackFrame())) {
    C.addTransition(setRefBinding(State, Sym, PlusZero));
    return;
  }

  State = setRefBinding(State, Sym, PlusZero.withIvarAccess());
  C.addTransition(State);
}

static bool isReceiverUnconsumedSelf(const CallEvent &Call) {
  if (const auto *MC = dyn_cast<ObjCMethodCall>(&Call)) {

    // Check if the message is not consumed, we know it will not be used in
    // an assignment, ex: "self = [super init]".
    return MC->getMethodFamily() == OMF_init && MC->isReceiverSelfOrSuper() &&
           !Call.getLocationContext()
                ->getAnalysisDeclContext()
                ->getParentMap()
                .isConsumedExpr(Call.getOriginExpr());
  }
  return false;
}

const static RetainSummary *getSummary(RetainSummaryManager &Summaries,
                                       const CallEvent &Call,
                                       QualType ReceiverType) {
  const Expr *CE = Call.getOriginExpr();
  AnyCall C =
      CE ? *AnyCall::forExpr(CE)
         : AnyCall(cast<CXXDestructorDecl>(Call.getDecl()));
  return Summaries.getSummary(C, Call.hasNonZeroCallbackArg(),
                              isReceiverUnconsumedSelf(Call), ReceiverType);
}

void RetainCountChecker::checkPostCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  RetainSummaryManager &Summaries = getSummaryManager(C);

  // Leave null if no receiver.
  QualType ReceiverType;
  if (const auto *MC = dyn_cast<ObjCMethodCall>(&Call)) {
    if (MC->isInstanceMessage()) {
      SVal ReceiverV = MC->getReceiverSVal();
      if (SymbolRef Sym = ReceiverV.getAsLocSymbol())
        if (const RefVal *T = getRefBinding(C.getState(), Sym))
          ReceiverType = T->getType();
    }
  }

  const RetainSummary *Summ = getSummary(Summaries, Call, ReceiverType);

  if (C.wasInlined) {
    processSummaryOfInlined(*Summ, Call, C);
    return;
  }
  checkSummary(*Summ, Call, C);
}

/// GetReturnType - Used to get the return type of a message expression or
///  function call with the intention of affixing that type to a tracked symbol.
///  While the return type can be queried directly from RetEx, when
///  invoking class methods we augment to the return type to be that of
///  a pointer to the class (as opposed it just being id).
// FIXME: We may be able to do this with related result types instead.
// This function is probably overestimating.
static QualType GetReturnType(const Expr *RetE, ASTContext &Ctx) {
  QualType RetTy = RetE->getType();
  // If RetE is not a message expression just return its type.
  // If RetE is a message expression, return its types if it is something
  /// more specific than id.
  if (const ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(RetE))
    if (const ObjCObjectPointerType *PT = RetTy->getAs<ObjCObjectPointerType>())
      if (PT->isObjCQualifiedIdType() || PT->isObjCIdType() ||
          PT->isObjCClassType()) {
        // At this point we know the return type of the message expression is
        // id, id<...>, or Class. If we have an ObjCInterfaceDecl, we know this
        // is a call to a class method whose type we can resolve.  In such
        // cases, promote the return type to XXX* (where XXX is the class).
        const ObjCInterfaceDecl *D = ME->getReceiverInterface();
        return !D ? RetTy :
                    Ctx.getObjCObjectPointerType(Ctx.getObjCInterfaceType(D));
      }

  return RetTy;
}

static std::optional<RefVal> refValFromRetEffect(RetEffect RE,
                                                 QualType ResultTy) {
  if (RE.isOwned()) {
    return RefVal::makeOwned(RE.getObjKind(), ResultTy);
  } else if (RE.notOwned()) {
    return RefVal::makeNotOwned(RE.getObjKind(), ResultTy);
  }

  return std::nullopt;
}

static bool isPointerToObject(QualType QT) {
  QualType PT = QT->getPointeeType();
  if (!PT.isNull())
    if (PT->getAsCXXRecordDecl())
      return true;
  return false;
}

/// Whether the tracked value should be escaped on a given call.
/// OSObjects are escaped when passed to void * / etc.
static bool shouldEscapeOSArgumentOnCall(const CallEvent &CE, unsigned ArgIdx,
                                       const RefVal *TrackedValue) {
  if (TrackedValue->getObjKind() != ObjKind::OS)
    return false;
  if (ArgIdx >= CE.parameters().size())
    return false;
  return !isPointerToObject(CE.parameters()[ArgIdx]->getType());
}

// We don't always get the exact modeling of the function with regards to the
// retain count checker even when the function is inlined. For example, we need
// to stop tracking the symbols which were marked with StopTrackingHard.
void RetainCountChecker::processSummaryOfInlined(const RetainSummary &Summ,
                                                 const CallEvent &CallOrMsg,
                                                 CheckerContext &C) const {
  ProgramStateRef state = C.getState();

  // Evaluate the effect of the arguments.
  for (unsigned idx = 0, e = CallOrMsg.getNumArgs(); idx != e; ++idx) {
    SVal V = CallOrMsg.getArgSVal(idx);

    if (SymbolRef Sym = V.getAsLocSymbol()) {
      bool ShouldRemoveBinding = Summ.getArg(idx).getKind() == StopTrackingHard;
      if (const RefVal *T = getRefBinding(state, Sym))
        if (shouldEscapeOSArgumentOnCall(CallOrMsg, idx, T))
          ShouldRemoveBinding = true;

      if (ShouldRemoveBinding)
        state = removeRefBinding(state, Sym);
    }
  }

  // Evaluate the effect on the message receiver.
  if (const auto *MsgInvocation = dyn_cast<ObjCMethodCall>(&CallOrMsg)) {
    if (SymbolRef Sym = MsgInvocation->getReceiverSVal().getAsLocSymbol()) {
      if (Summ.getReceiverEffect().getKind() == StopTrackingHard) {
        state = removeRefBinding(state, Sym);
      }
    }
  }

  // Consult the summary for the return value.
  RetEffect RE = Summ.getRetEffect();

  if (SymbolRef Sym = CallOrMsg.getReturnValue().getAsSymbol()) {
    if (RE.getKind() == RetEffect::NoRetHard)
      state = removeRefBinding(state, Sym);
  }

  C.addTransition(state);
}

static bool isSmartPtrField(const MemRegion *MR) {
  const auto *TR = dyn_cast<TypedValueRegion>(
    cast<SubRegion>(MR)->getSuperRegion());
  return TR && RetainSummaryManager::isKnownSmartPointer(TR->getValueType());
}


/// A value escapes in these possible cases:
///
/// - binding to something that is not a memory region.
/// - binding to a memregion that does not have stack storage
/// - binding to a variable that has a destructor attached using CleanupAttr
///
/// We do not currently model what happens when a symbol is
/// assigned to a struct field, unless it is a known smart pointer
/// implementation, about which we know that it is inlined.
/// FIXME: This could definitely be improved upon.
static bool shouldEscapeRegion(const MemRegion *R) {
  if (isSmartPtrField(R))
    return false;

  const auto *VR = dyn_cast<VarRegion>(R);

  if (!R->hasStackStorage() || !VR)
    return true;

  const VarDecl *VD = VR->getDecl();
  if (!VD->hasAttr<CleanupAttr>())
    return false; // CleanupAttr attaches destructors, which cause escaping.
  return true;
}

static SmallVector<ProgramStateRef, 2>
updateOutParameters(ProgramStateRef State, const RetainSummary &Summ,
                    const CallEvent &CE) {

  SVal L = CE.getReturnValue();

  // Splitting is required to support out parameters,
  // as out parameters might be created only on the "success" branch.
  // We want to avoid eagerly splitting unless out parameters are actually
  // needed.
  bool SplitNecessary = false;
  for (auto &P : Summ.getArgEffects())
    if (P.second.getKind() == RetainedOutParameterOnNonZero ||
        P.second.getKind() == RetainedOutParameterOnZero)
      SplitNecessary = true;

  ProgramStateRef AssumeNonZeroReturn = State;
  ProgramStateRef AssumeZeroReturn = State;

  if (SplitNecessary) {
    if (!CE.getResultType()->isScalarType()) {
      // Structures cannot be assumed. This probably deserves
      // a compiler warning for invalid annotations.
      return {State};
    }
    if (auto DL = L.getAs<DefinedOrUnknownSVal>()) {
      AssumeNonZeroReturn = AssumeNonZeroReturn->assume(*DL, true);
      AssumeZeroReturn = AssumeZeroReturn->assume(*DL, false);
    }
  }

  for (unsigned idx = 0, e = CE.getNumArgs(); idx != e; ++idx) {
    SVal ArgVal = CE.getArgSVal(idx);
    ArgEffect AE = Summ.getArg(idx);

    auto *ArgRegion = dyn_cast_or_null<TypedValueRegion>(ArgVal.getAsRegion());
    if (!ArgRegion)
      continue;

    QualType PointeeTy = ArgRegion->getValueType();
    SVal PointeeVal = State->getSVal(ArgRegion);
    SymbolRef Pointee = PointeeVal.getAsLocSymbol();
    if (!Pointee)
      continue;

    if (shouldEscapeRegion(ArgRegion))
      continue;

    auto makeNotOwnedParameter = [&](ProgramStateRef St) {
      return setRefBinding(St, Pointee,
                           RefVal::makeNotOwned(AE.getObjKind(), PointeeTy));
    };
    auto makeOwnedParameter = [&](ProgramStateRef St) {
      return setRefBinding(St, Pointee,
                           RefVal::makeOwned(ObjKind::OS, PointeeTy));
    };

    switch (AE.getKind()) {
    case UnretainedOutParameter:
      AssumeNonZeroReturn = makeNotOwnedParameter(AssumeNonZeroReturn);
      AssumeZeroReturn = makeNotOwnedParameter(AssumeZeroReturn);
      break;
    case RetainedOutParameter:
      AssumeNonZeroReturn = makeOwnedParameter(AssumeNonZeroReturn);
      AssumeZeroReturn = makeOwnedParameter(AssumeZeroReturn);
      break;
    case RetainedOutParameterOnNonZero:
      AssumeNonZeroReturn = makeOwnedParameter(AssumeNonZeroReturn);
      break;
    case RetainedOutParameterOnZero:
      AssumeZeroReturn = makeOwnedParameter(AssumeZeroReturn);
      break;
    default:
      break;
    }
  }

  if (SplitNecessary) {
    return {AssumeNonZeroReturn, AssumeZeroReturn};
  } else {
    assert(AssumeZeroReturn == AssumeNonZeroReturn);
    return {AssumeZeroReturn};
  }
}

void RetainCountChecker::checkSummary(const RetainSummary &Summ,
                                      const CallEvent &CallOrMsg,
                                      CheckerContext &C) const {
  ProgramStateRef state = C.getState();

  // Evaluate the effect of the arguments.
  RefVal::Kind hasErr = (RefVal::Kind) 0;
  SourceRange ErrorRange;
  SymbolRef ErrorSym = nullptr;

  // Helper tag for providing diagnostics: indicate whether dealloc was sent
  // at this location.
  bool DeallocSent = false;

  for (unsigned idx = 0, e = CallOrMsg.getNumArgs(); idx != e; ++idx) {
    SVal V = CallOrMsg.getArgSVal(idx);

    ArgEffect Effect = Summ.getArg(idx);
    if (SymbolRef Sym = V.getAsLocSymbol()) {
      if (const RefVal *T = getRefBinding(state, Sym)) {

        if (shouldEscapeOSArgumentOnCall(CallOrMsg, idx, T))
          Effect = ArgEffect(StopTrackingHard, ObjKind::OS);

        state = updateSymbol(state, Sym, *T, Effect, hasErr, C);
        if (hasErr) {
          ErrorRange = CallOrMsg.getArgSourceRange(idx);
          ErrorSym = Sym;
          break;
        } else if (Effect.getKind() == Dealloc) {
          DeallocSent = true;
        }
      }
    }
  }

  // Evaluate the effect on the message receiver / `this` argument.
  bool ReceiverIsTracked = false;
  if (!hasErr) {
    if (const auto *MsgInvocation = dyn_cast<ObjCMethodCall>(&CallOrMsg)) {
      if (SymbolRef Sym = MsgInvocation->getReceiverSVal().getAsLocSymbol()) {
        if (const RefVal *T = getRefBinding(state, Sym)) {
          ReceiverIsTracked = true;
          state = updateSymbol(state, Sym, *T,
                               Summ.getReceiverEffect(), hasErr, C);
          if (hasErr) {
            ErrorRange = MsgInvocation->getOriginExpr()->getReceiverRange();
            ErrorSym = Sym;
          } else if (Summ.getReceiverEffect().getKind() == Dealloc) {
            DeallocSent = true;
          }
        }
      }
    } else if (const auto *MCall = dyn_cast<CXXMemberCall>(&CallOrMsg)) {
      if (SymbolRef Sym = MCall->getCXXThisVal().getAsLocSymbol()) {
        if (const RefVal *T = getRefBinding(state, Sym)) {
          state = updateSymbol(state, Sym, *T, Summ.getThisEffect(),
                               hasErr, C);
          if (hasErr) {
            ErrorRange = MCall->getOriginExpr()->getSourceRange();
            ErrorSym = Sym;
          }
        }
      }
    }
  }

  // Process any errors.
  if (hasErr) {
    processNonLeakError(state, ErrorRange, hasErr, ErrorSym, C);
    return;
  }

  // Consult the summary for the return value.
  RetEffect RE = Summ.getRetEffect();

  if (RE.getKind() == RetEffect::OwnedWhenTrackedReceiver) {
    if (ReceiverIsTracked)
      RE = getSummaryManager(C).getObjAllocRetEffect();
    else
      RE = RetEffect::MakeNoRet();
  }

  if (SymbolRef Sym = CallOrMsg.getReturnValue().getAsSymbol()) {
    QualType ResultTy = CallOrMsg.getResultType();
    if (RE.notOwned()) {
      const Expr *Ex = CallOrMsg.getOriginExpr();
      assert(Ex);
      ResultTy = GetReturnType(Ex, C.getASTContext());
    }
    if (std::optional<RefVal> updatedRefVal = refValFromRetEffect(RE, ResultTy))
      state = setRefBinding(state, Sym, *updatedRefVal);
  }

  SmallVector<ProgramStateRef, 2> Out =
      updateOutParameters(state, Summ, CallOrMsg);

  for (ProgramStateRef St : Out) {
    if (DeallocSent) {
      C.addTransition(St, C.getPredecessor(), &getDeallocSentTag());
    } else {
      C.addTransition(St);
    }
  }
}

ProgramStateRef RetainCountChecker::updateSymbol(ProgramStateRef state,
                                                 SymbolRef sym, RefVal V,
                                                 ArgEffect AE,
                                                 RefVal::Kind &hasErr,
                                                 CheckerContext &C) const {
  bool IgnoreRetainMsg = (bool)C.getASTContext().getLangOpts().ObjCAutoRefCount;
  if (AE.getObjKind() == ObjKind::ObjC && IgnoreRetainMsg) {
    switch (AE.getKind()) {
    default:
      break;
    case IncRef:
      AE = AE.withKind(DoNothing);
      break;
    case DecRef:
      AE = AE.withKind(DoNothing);
      break;
    case DecRefAndStopTrackingHard:
      AE = AE.withKind(StopTracking);
      break;
    }
  }

  // Handle all use-after-releases.
  if (V.getKind() == RefVal::Released) {
    V = V ^ RefVal::ErrorUseAfterRelease;
    hasErr = V.getKind();
    return setRefBinding(state, sym, V);
  }

  switch (AE.getKind()) {
    case UnretainedOutParameter:
    case RetainedOutParameter:
    case RetainedOutParameterOnZero:
    case RetainedOutParameterOnNonZero:
      llvm_unreachable("Applies to pointer-to-pointer parameters, which should "
                       "not have ref state.");

    case Dealloc: // NB. we only need to add a note in a non-error case.
      switch (V.getKind()) {
        default:
          llvm_unreachable("Invalid RefVal state for an explicit dealloc.");
        case RefVal::Owned:
          // The object immediately transitions to the released state.
          V = V ^ RefVal::Released;
          V.clearCounts();
          return setRefBinding(state, sym, V);
        case RefVal::NotOwned:
          V = V ^ RefVal::ErrorDeallocNotOwned;
          hasErr = V.getKind();
          break;
      }
      break;

    case MayEscape:
      if (V.getKind() == RefVal::Owned) {
        V = V ^ RefVal::NotOwned;
        break;
      }

      [[fallthrough]];

    case DoNothing:
      return state;

    case Autorelease:
      // Update the autorelease counts.
      V = V.autorelease();
      break;

    case StopTracking:
    case StopTrackingHard:
      return removeRefBinding(state, sym);

    case IncRef:
      switch (V.getKind()) {
        default:
          llvm_unreachable("Invalid RefVal state for a retain.");
        case RefVal::Owned:
        case RefVal::NotOwned:
          V = V + 1;
          break;
      }
      break;

    case DecRef:
    case DecRefBridgedTransferred:
    case DecRefAndStopTrackingHard:
      switch (V.getKind()) {
        default:
          // case 'RefVal::Released' handled above.
          llvm_unreachable("Invalid RefVal state for a release.");

        case RefVal::Owned:
          assert(V.getCount() > 0);
          if (V.getCount() == 1) {
            if (AE.getKind() == DecRefBridgedTransferred ||
                V.getIvarAccessHistory() ==
                  RefVal::IvarAccessHistory::AccessedDirectly)
              V = V ^ RefVal::NotOwned;
            else
              V = V ^ RefVal::Released;
          } else if (AE.getKind() == DecRefAndStopTrackingHard) {
            return removeRefBinding(state, sym);
          }

          V = V - 1;
          break;

        case RefVal::NotOwned:
          if (V.getCount() > 0) {
            if (AE.getKind() == DecRefAndStopTrackingHard)
              return removeRefBinding(state, sym);
            V = V - 1;
          } else if (V.getIvarAccessHistory() ==
                       RefVal::IvarAccessHistory::AccessedDirectly) {
            // Assume that the instance variable was holding on the object at
            // +1, and we just didn't know.
            if (AE.getKind() == DecRefAndStopTrackingHard)
              return removeRefBinding(state, sym);
            V = V.releaseViaIvar() ^ RefVal::Released;
          } else {
            V = V ^ RefVal::ErrorReleaseNotOwned;
            hasErr = V.getKind();
          }
          break;
      }
      break;
  }
  return setRefBinding(state, sym, V);
}

const RefCountBug &
RetainCountChecker::errorKindToBugKind(RefVal::Kind ErrorKind,
                                       SymbolRef Sym) const {
  switch (ErrorKind) {
    case RefVal::ErrorUseAfterRelease:
      return *UseAfterRelease;
    case RefVal::ErrorReleaseNotOwned:
      return *ReleaseNotOwned;
    case RefVal::ErrorDeallocNotOwned:
      if (Sym->getType()->getPointeeCXXRecordDecl())
        return *FreeNotOwned;
      return *DeallocNotOwned;
    default:
      llvm_unreachable("Unhandled error.");
  }
}

void RetainCountChecker::processNonLeakError(ProgramStateRef St,
                                             SourceRange ErrorRange,
                                             RefVal::Kind ErrorKind,
                                             SymbolRef Sym,
                                             CheckerContext &C) const {
  // HACK: Ignore retain-count issues on values accessed through ivars,
  // because of cases like this:
  //   [_contentView retain];
  //   [_contentView removeFromSuperview];
  //   [self addSubview:_contentView]; // invalidates 'self'
  //   [_contentView release];
  if (const RefVal *RV = getRefBinding(St, Sym))
    if (RV->getIvarAccessHistory() != RefVal::IvarAccessHistory::None)
      return;

  ExplodedNode *N = C.generateErrorNode(St);
  if (!N)
    return;

  auto report = std::make_unique<RefCountReport>(
      errorKindToBugKind(ErrorKind, Sym),
      C.getASTContext().getLangOpts(), N, Sym);
  report->addRange(ErrorRange);
  C.emitReport(std::move(report));
}

//===----------------------------------------------------------------------===//
// Handle the return values of retain-count-related functions.
//===----------------------------------------------------------------------===//

bool RetainCountChecker::evalCall(const CallEvent &Call,
                                  CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return false;

  const auto *CE = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
  if (!CE)
    return false;

  RetainSummaryManager &SmrMgr = getSummaryManager(C);
  QualType ResultTy = Call.getResultType();

  // See if the function has 'rc_ownership_trusted_implementation'
  // annotate attribute. If it does, we will not inline it.
  bool hasTrustedImplementationAnnotation = false;

  const LocationContext *LCtx = C.getLocationContext();

  using BehaviorSummary = RetainSummaryManager::BehaviorSummary;
  std::optional<BehaviorSummary> BSmr =
      SmrMgr.canEval(CE, FD, hasTrustedImplementationAnnotation);

  // See if it's one of the specific functions we know how to eval.
  if (!BSmr)
    return false;

  // Bind the return value.
  if (BSmr == BehaviorSummary::Identity ||
      BSmr == BehaviorSummary::IdentityOrZero ||
      BSmr == BehaviorSummary::IdentityThis) {

    const Expr *BindReturnTo =
        (BSmr == BehaviorSummary::IdentityThis)
            ? cast<CXXMemberCallExpr>(CE)->getImplicitObjectArgument()
            : CE->getArg(0);
    SVal RetVal = state->getSVal(BindReturnTo, LCtx);

    // If the receiver is unknown or the function has
    // 'rc_ownership_trusted_implementation' annotate attribute, conjure a
    // return value.
    // FIXME: this branch is very strange.
    if (RetVal.isUnknown() ||
        (hasTrustedImplementationAnnotation && !ResultTy.isNull())) {
      SValBuilder &SVB = C.getSValBuilder();
      RetVal =
          SVB.conjureSymbolVal(nullptr, CE, LCtx, ResultTy, C.blockCount());
    }

    // Bind the value.
    state = state->BindExpr(CE, LCtx, RetVal, /*Invalidate=*/false);

    if (BSmr == BehaviorSummary::IdentityOrZero) {
      // Add a branch where the output is zero.
      ProgramStateRef NullOutputState = C.getState();

      // Assume that output is zero on the other branch.
      NullOutputState = NullOutputState->BindExpr(
          CE, LCtx, C.getSValBuilder().makeNullWithType(ResultTy),
          /*Invalidate=*/false);
      C.addTransition(NullOutputState, &getCastFailTag());

      // And on the original branch assume that both input and
      // output are non-zero.
      if (auto L = RetVal.getAs<DefinedOrUnknownSVal>())
        state = state->assume(*L, /*assumption=*/true);

    }
  }

  C.addTransition(state);
  return true;
}

ExplodedNode * RetainCountChecker::processReturn(const ReturnStmt *S,
                                                 CheckerContext &C) const {
  ExplodedNode *Pred = C.getPredecessor();

  // Only adjust the reference count if this is the top-level call frame,
  // and not the result of inlining.  In the future, we should do
  // better checking even for inlined calls, and see if they match
  // with their expected semantics (e.g., the method should return a retained
  // object, etc.).
  if (!C.inTopFrame())
    return Pred;

  if (!S)
    return Pred;

  const Expr *RetE = S->getRetValue();
  if (!RetE)
    return Pred;

  ProgramStateRef state = C.getState();
  // We need to dig down to the symbolic base here because various
  // custom allocators do sometimes return the symbol with an offset.
  SymbolRef Sym = state->getSValAsScalarOrLoc(RetE, C.getLocationContext())
                      .getAsLocSymbol(/*IncludeBaseRegions=*/true);
  if (!Sym)
    return Pred;

  // Get the reference count binding (if any).
  const RefVal *T = getRefBinding(state, Sym);
  if (!T)
    return Pred;

  // Change the reference count.
  RefVal X = *T;

  switch (X.getKind()) {
    case RefVal::Owned: {
      unsigned cnt = X.getCount();
      assert(cnt > 0);
      X.setCount(cnt - 1);
      X = X ^ RefVal::ReturnedOwned;
      break;
    }

    case RefVal::NotOwned: {
      unsigned cnt = X.getCount();
      if (cnt) {
        X.setCount(cnt - 1);
        X = X ^ RefVal::ReturnedOwned;
      } else {
        X = X ^ RefVal::ReturnedNotOwned;
      }
      break;
    }

    default:
      return Pred;
  }

  // Update the binding.
  state = setRefBinding(state, Sym, X);
  Pred = C.addTransition(state);

  // At this point we have updated the state properly.
  // Everything after this is merely checking to see if the return value has
  // been over- or under-retained.

  // Did we cache out?
  if (!Pred)
    return nullptr;

  // Update the autorelease counts.
  static CheckerProgramPointTag AutoreleaseTag(this, "Autorelease");
  state = handleAutoreleaseCounts(state, Pred, &AutoreleaseTag, C, Sym, X, S);

  // Have we generated a sink node?
  if (!state)
    return nullptr;

  // Get the updated binding.
  T = getRefBinding(state, Sym);
  assert(T);
  X = *T;

  // Consult the summary of the enclosing method.
  RetainSummaryManager &Summaries = getSummaryManager(C);
  const Decl *CD = &Pred->getCodeDecl();
  RetEffect RE = RetEffect::MakeNoRet();

  // FIXME: What is the convention for blocks? Is there one?
  if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(CD)) {
    const RetainSummary *Summ = Summaries.getSummary(AnyCall(MD));
    RE = Summ->getRetEffect();
  } else if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(CD)) {
    if (!isa<CXXMethodDecl>(FD)) {
      const RetainSummary *Summ = Summaries.getSummary(AnyCall(FD));
      RE = Summ->getRetEffect();
    }
  }

  return checkReturnWithRetEffect(S, C, Pred, RE, X, Sym, state);
}

ExplodedNode * RetainCountChecker::checkReturnWithRetEffect(const ReturnStmt *S,
                                                  CheckerContext &C,
                                                  ExplodedNode *Pred,
                                                  RetEffect RE, RefVal X,
                                                  SymbolRef Sym,
                                                  ProgramStateRef state) const {
  // HACK: Ignore retain-count issues on values accessed through ivars,
  // because of cases like this:
  //   [_contentView retain];
  //   [_contentView removeFromSuperview];
  //   [self addSubview:_contentView]; // invalidates 'self'
  //   [_contentView release];
  if (X.getIvarAccessHistory() != RefVal::IvarAccessHistory::None)
    return Pred;

  // Any leaks or other errors?
  if (X.isReturnedOwned() && X.getCount() == 0) {
    if (RE.getKind() != RetEffect::NoRet) {
      if (!RE.isOwned()) {

        // The returning type is a CF, we expect the enclosing method should
        // return ownership.
        X = X ^ RefVal::ErrorLeakReturned;

        // Generate an error node.
        state = setRefBinding(state, Sym, X);

        static CheckerProgramPointTag ReturnOwnLeakTag(this, "ReturnsOwnLeak");
        ExplodedNode *N = C.addTransition(state, Pred, &ReturnOwnLeakTag);
        if (N) {
          const LangOptions &LOpts = C.getASTContext().getLangOpts();
          auto R =
              std::make_unique<RefLeakReport>(*LeakAtReturn, LOpts, N, Sym, C);
          C.emitReport(std::move(R));
        }
        return N;
      }
    }
  } else if (X.isReturnedNotOwned()) {
    if (RE.isOwned()) {
      if (X.getIvarAccessHistory() ==
            RefVal::IvarAccessHistory::AccessedDirectly) {
        // Assume the method was trying to transfer a +1 reference from a
        // strong ivar to the caller.
        state = setRefBinding(state, Sym,
                              X.releaseViaIvar() ^ RefVal::ReturnedOwned);
      } else {
        // Trying to return a not owned object to a caller expecting an
        // owned object.
        state = setRefBinding(state, Sym, X ^ RefVal::ErrorReturnedNotOwned);

        static CheckerProgramPointTag
            ReturnNotOwnedTag(this, "ReturnNotOwnedForOwned");

        ExplodedNode *N = C.addTransition(state, Pred, &ReturnNotOwnedTag);
        if (N) {
          auto R = std::make_unique<RefCountReport>(
              *ReturnNotOwnedForOwned, C.getASTContext().getLangOpts(), N, Sym);
          C.emitReport(std::move(R));
        }
        return N;
      }
    }
  }
  return Pred;
}

//===----------------------------------------------------------------------===//
// Check various ways a symbol can be invalidated.
//===----------------------------------------------------------------------===//

void RetainCountChecker::checkBind(SVal loc, SVal val, const Stmt *S,
                                   CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  const MemRegion *MR = loc.getAsRegion();

  // Find all symbols referenced by 'val' that we are tracking
  // and stop tracking them.
  if (MR && shouldEscapeRegion(MR)) {
    state = state->scanReachableSymbols<StopTrackingCallback>(val).getState();
    C.addTransition(state);
  }
}

ProgramStateRef RetainCountChecker::evalAssume(ProgramStateRef state,
                                               SVal Cond,
                                               bool Assumption) const {
  // FIXME: We may add to the interface of evalAssume the list of symbols
  //  whose assumptions have changed.  For now we just iterate through the
  //  bindings and check if any of the tracked symbols are NULL.  This isn't
  //  too bad since the number of symbols we will track in practice are
  //  probably small and evalAssume is only called at branches and a few
  //  other places.
  RefBindingsTy B = state->get<RefBindings>();

  if (B.isEmpty())
    return state;

  bool changed = false;
  RefBindingsTy::Factory &RefBFactory = state->get_context<RefBindings>();
  ConstraintManager &CMgr = state->getConstraintManager();

  for (auto &I : B) {
    // Check if the symbol is null stop tracking the symbol.
    ConditionTruthVal AllocFailed = CMgr.isNull(state, I.first);
    if (AllocFailed.isConstrainedTrue()) {
      changed = true;
      B = RefBFactory.remove(B, I.first);
    }
  }

  if (changed)
    state = state->set<RefBindings>(B);

  return state;
}

ProgramStateRef RetainCountChecker::checkRegionChanges(
    ProgramStateRef state, const InvalidatedSymbols *invalidated,
    ArrayRef<const MemRegion *> ExplicitRegions,
    ArrayRef<const MemRegion *> Regions, const LocationContext *LCtx,
    const CallEvent *Call) const {
  if (!invalidated)
    return state;

  llvm::SmallPtrSet<SymbolRef, 8> AllowedSymbols;

  for (const MemRegion *I : ExplicitRegions)
    if (const SymbolicRegion *SR = I->StripCasts()->getAs<SymbolicRegion>())
      AllowedSymbols.insert(SR->getSymbol());

  for (SymbolRef sym : *invalidated) {
    if (AllowedSymbols.count(sym))
      continue;
    // Remove any existing reference-count binding.
    state = removeRefBinding(state, sym);
  }
  return state;
}

ProgramStateRef
RetainCountChecker::handleAutoreleaseCounts(ProgramStateRef state,
                                            ExplodedNode *Pred,
                                            const ProgramPointTag *Tag,
                                            CheckerContext &Ctx,
                                            SymbolRef Sym,
                                            RefVal V,
                                            const ReturnStmt *S) const {
  unsigned ACnt = V.getAutoreleaseCount();

  // No autorelease counts?  Nothing to be done.
  if (!ACnt)
    return state;

  unsigned Cnt = V.getCount();

  // FIXME: Handle sending 'autorelease' to already released object.

  if (V.getKind() == RefVal::ReturnedOwned)
    ++Cnt;

  // If we would over-release here, but we know the value came from an ivar,
  // assume it was a strong ivar that's just been relinquished.
  if (ACnt > Cnt &&
      V.getIvarAccessHistory() == RefVal::IvarAccessHistory::AccessedDirectly) {
    V = V.releaseViaIvar();
    --ACnt;
  }

  if (ACnt <= Cnt) {
    if (ACnt == Cnt) {
      V.clearCounts();
      if (V.getKind() == RefVal::ReturnedOwned) {
        V = V ^ RefVal::ReturnedNotOwned;
      } else {
        V = V ^ RefVal::NotOwned;
      }
    } else {
      V.setCount(V.getCount() - ACnt);
      V.setAutoreleaseCount(0);
    }
    return setRefBinding(state, Sym, V);
  }

  // HACK: Ignore retain-count issues on values accessed through ivars,
  // because of cases like this:
  //   [_contentView retain];
  //   [_contentView removeFromSuperview];
  //   [self addSubview:_contentView]; // invalidates 'self'
  //   [_contentView release];
  if (V.getIvarAccessHistory() != RefVal::IvarAccessHistory::None)
    return state;

  // Woah!  More autorelease counts then retain counts left.
  // Emit hard error.
  V = V ^ RefVal::ErrorOverAutorelease;
  state = setRefBinding(state, Sym, V);

  ExplodedNode *N = Ctx.generateSink(state, Pred, Tag);
  if (N) {
    SmallString<128> sbuf;
    llvm::raw_svector_ostream os(sbuf);
    os << "Object was autoreleased ";
    if (V.getAutoreleaseCount() > 1)
      os << V.getAutoreleaseCount() << " times but the object ";
    else
      os << "but ";
    os << "has a +" << V.getCount() << " retain count";

    const LangOptions &LOpts = Ctx.getASTContext().getLangOpts();
    auto R = std::make_unique<RefCountReport>(*OverAutorelease, LOpts, N, Sym,
                                              os.str());
    Ctx.emitReport(std::move(R));
  }

  return nullptr;
}

ProgramStateRef
RetainCountChecker::handleSymbolDeath(ProgramStateRef state,
                                      SymbolRef sid, RefVal V,
                                    SmallVectorImpl<SymbolRef> &Leaked) const {
  bool hasLeak;

  // HACK: Ignore retain-count issues on values accessed through ivars,
  // because of cases like this:
  //   [_contentView retain];
  //   [_contentView removeFromSuperview];
  //   [self addSubview:_contentView]; // invalidates 'self'
  //   [_contentView release];
  if (V.getIvarAccessHistory() != RefVal::IvarAccessHistory::None)
    hasLeak = false;
  else if (V.isOwned())
    hasLeak = true;
  else if (V.isNotOwned() || V.isReturnedOwned())
    hasLeak = (V.getCount() > 0);
  else
    hasLeak = false;

  if (!hasLeak)
    return removeRefBinding(state, sid);

  Leaked.push_back(sid);
  return setRefBinding(state, sid, V ^ RefVal::ErrorLeak);
}

ExplodedNode *
RetainCountChecker::processLeaks(ProgramStateRef state,
                                 SmallVectorImpl<SymbolRef> &Leaked,
                                 CheckerContext &Ctx,
                                 ExplodedNode *Pred) const {
  // Generate an intermediate node representing the leak point.
  ExplodedNode *N = Ctx.addTransition(state, Pred);
  const LangOptions &LOpts = Ctx.getASTContext().getLangOpts();

  if (N) {
    for (SymbolRef L : Leaked) {
      const RefCountBug &BT = Pred ? *LeakWithinFunction : *LeakAtReturn;
      Ctx.emitReport(std::make_unique<RefLeakReport>(BT, LOpts, N, L, Ctx));
    }
  }

  return N;
}

void RetainCountChecker::checkBeginFunction(CheckerContext &Ctx) const {
  if (!Ctx.inTopFrame())
    return;

  RetainSummaryManager &SmrMgr = getSummaryManager(Ctx);
  const LocationContext *LCtx = Ctx.getLocationContext();
  const Decl *D = LCtx->getDecl();
  std::optional<AnyCall> C = AnyCall::forDecl(D);

  if (!C || SmrMgr.isTrustedReferenceCountImplementation(D))
    return;

  ProgramStateRef state = Ctx.getState();
  const RetainSummary *FunctionSummary = SmrMgr.getSummary(*C);
  ArgEffects CalleeSideArgEffects = FunctionSummary->getArgEffects();

  for (unsigned idx = 0, e = C->param_size(); idx != e; ++idx) {
    const ParmVarDecl *Param = C->parameters()[idx];
    SymbolRef Sym = state->getSVal(state->getRegion(Param, LCtx)).getAsSymbol();

    QualType Ty = Param->getType();
    const ArgEffect *AE = CalleeSideArgEffects.lookup(idx);
    if (AE) {
      ObjKind K = AE->getObjKind();
      if (K == ObjKind::Generalized || K == ObjKind::OS ||
          (TrackNSCFStartParam && (K == ObjKind::ObjC || K == ObjKind::CF))) {
        RefVal NewVal = AE->getKind() == DecRef ? RefVal::makeOwned(K, Ty)
                                                : RefVal::makeNotOwned(K, Ty);
        state = setRefBinding(state, Sym, NewVal);
      }
    }
  }

  Ctx.addTransition(state);
}

void RetainCountChecker::checkEndFunction(const ReturnStmt *RS,
                                          CheckerContext &Ctx) const {
  ExplodedNode *Pred = processReturn(RS, Ctx);

  // Created state cached out.
  if (!Pred) {
    return;
  }

  ProgramStateRef state = Pred->getState();
  RefBindingsTy B = state->get<RefBindings>();

  // Don't process anything within synthesized bodies.
  const LocationContext *LCtx = Pred->getLocationContext();
  if (LCtx->getAnalysisDeclContext()->isBodyAutosynthesized()) {
    assert(!LCtx->inTopFrame());
    return;
  }

  for (auto &I : B) {
    state = handleAutoreleaseCounts(state, Pred, /*Tag=*/nullptr, Ctx,
                                    I.first, I.second);
    if (!state)
      return;
  }

  // If the current LocationContext has a parent, don't check for leaks.
  // We will do that later.
  // FIXME: we should instead check for imbalances of the retain/releases,
  // and suggest annotations.
  if (LCtx->getParent())
    return;

  B = state->get<RefBindings>();
  SmallVector<SymbolRef, 10> Leaked;

  for (auto &I : B)
    state = handleSymbolDeath(state, I.first, I.second, Leaked);

  processLeaks(state, Leaked, Ctx, Pred);
}

void RetainCountChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                          CheckerContext &C) const {
  ExplodedNode *Pred = C.getPredecessor();

  ProgramStateRef state = C.getState();
  SmallVector<SymbolRef, 10> Leaked;

  // Update counts from autorelease pools
  for (const auto &I: state->get<RefBindings>()) {
    SymbolRef Sym = I.first;
    if (SymReaper.isDead(Sym)) {
      static CheckerProgramPointTag Tag(this, "DeadSymbolAutorelease");
      const RefVal &V = I.second;
      state = handleAutoreleaseCounts(state, Pred, &Tag, C, Sym, V);
      if (!state)
        return;

      // Fetch the new reference count from the state, and use it to handle
      // this symbol.
      state = handleSymbolDeath(state, Sym, *getRefBinding(state, Sym), Leaked);
    }
  }

  if (Leaked.empty()) {
    C.addTransition(state);
    return;
  }

  Pred = processLeaks(state, Leaked, C, Pred);

  // Did we cache out?
  if (!Pred)
    return;

  // Now generate a new node that nukes the old bindings.
  // The only bindings left at this point are the leaked symbols.
  RefBindingsTy::Factory &F = state->get_context<RefBindings>();
  RefBindingsTy B = state->get<RefBindings>();

  for (SymbolRef L : Leaked)
    B = F.remove(B, L);

  state = state->set<RefBindings>(B);
  C.addTransition(state, Pred);
}

void RetainCountChecker::printState(raw_ostream &Out, ProgramStateRef State,
                                    const char *NL, const char *Sep) const {

  RefBindingsTy B = State->get<RefBindings>();

  if (B.isEmpty())
    return;

  Out << Sep << NL;

  for (auto &I : B) {
    Out << I.first << " : ";
    I.second.print(Out);
    Out << NL;
  }
}

//===----------------------------------------------------------------------===//
// Checker registration.
//===----------------------------------------------------------------------===//

std::unique_ptr<CheckerProgramPointTag> RetainCountChecker::DeallocSentTag;
std::unique_ptr<CheckerProgramPointTag> RetainCountChecker::CastFailTag;

void ento::registerRetainCountBase(CheckerManager &Mgr) {
  auto *Chk = Mgr.registerChecker<RetainCountChecker>();
  Chk->DeallocSentTag =
      std::make_unique<CheckerProgramPointTag>(Chk, "DeallocSent");
  Chk->CastFailTag =
      std::make_unique<CheckerProgramPointTag>(Chk, "DynamicCastFail");
}

bool ento::shouldRegisterRetainCountBase(const CheckerManager &mgr) {
  return true;
}
void ento::registerRetainCountChecker(CheckerManager &Mgr) {
  auto *Chk = Mgr.getChecker<RetainCountChecker>();
  Chk->TrackObjCAndCFObjects = true;
  Chk->TrackNSCFStartParam = Mgr.getAnalyzerOptions().getCheckerBooleanOption(
      Mgr.getCurrentCheckerName(), "TrackNSCFStartParam");

#define INIT_BUGTYPE(KIND)                                                     \
  Chk->KIND = std::make_unique<RefCountBug>(Mgr.getCurrentCheckerName(),       \
                                            RefCountBug::KIND);
  // TODO: Ideally, we should have a checker for each of these bug types.
  INIT_BUGTYPE(UseAfterRelease)
  INIT_BUGTYPE(ReleaseNotOwned)
  INIT_BUGTYPE(DeallocNotOwned)
  INIT_BUGTYPE(FreeNotOwned)
  INIT_BUGTYPE(OverAutorelease)
  INIT_BUGTYPE(ReturnNotOwnedForOwned)
  INIT_BUGTYPE(LeakWithinFunction)
  INIT_BUGTYPE(LeakAtReturn)
#undef INIT_BUGTYPE
}

bool ento::shouldRegisterRetainCountChecker(const CheckerManager &mgr) {
  return true;
}

void ento::registerOSObjectRetainCountChecker(CheckerManager &Mgr) {
  auto *Chk = Mgr.getChecker<RetainCountChecker>();
  Chk->TrackOSObjects = true;

  // FIXME: We want bug reports to always have the same checker name associated
  // with them, yet here, if RetainCountChecker is disabled but
  // OSObjectRetainCountChecker is enabled, the checker names will be different.
  // This hack will make it so that the checker name depends on which checker is
  // enabled rather than on the registration order.
  // For the most part, we want **non-hidden checkers** to be associated with
  // diagnostics, and **hidden checker options** with the fine-tuning of
  // modeling. Following this logic, OSObjectRetainCountChecker should be the
  // latter, but we can't just remove it for backward compatibility reasons.
#define LAZY_INIT_BUGTYPE(KIND)                                                \
  if (!Chk->KIND)                                                              \
    Chk->KIND = std::make_unique<RefCountBug>(Mgr.getCurrentCheckerName(),     \
                                              RefCountBug::KIND);
  LAZY_INIT_BUGTYPE(UseAfterRelease)
  LAZY_INIT_BUGTYPE(ReleaseNotOwned)
  LAZY_INIT_BUGTYPE(DeallocNotOwned)
  LAZY_INIT_BUGTYPE(FreeNotOwned)
  LAZY_INIT_BUGTYPE(OverAutorelease)
  LAZY_INIT_BUGTYPE(ReturnNotOwnedForOwned)
  LAZY_INIT_BUGTYPE(LeakWithinFunction)
  LAZY_INIT_BUGTYPE(LeakAtReturn)
#undef LAZY_INIT_BUGTYPE
}

bool ento::shouldRegisterOSObjectRetainCountChecker(const CheckerManager &mgr) {
  return true;
}
