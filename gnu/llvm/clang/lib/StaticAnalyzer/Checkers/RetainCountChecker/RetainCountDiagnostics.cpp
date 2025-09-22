// RetainCountDiagnostics.cpp - Checks for leaks and other issues -*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines diagnostics for RetainCountChecker, which implements
//  a reference count checker for Core Foundation and Cocoa on (Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "RetainCountDiagnostics.h"
#include "RetainCountChecker.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace retaincountchecker;

StringRef RefCountBug::bugTypeToName(RefCountBug::RefCountBugKind BT) {
  switch (BT) {
  case UseAfterRelease:
    return "Use-after-release";
  case ReleaseNotOwned:
    return "Bad release";
  case DeallocNotOwned:
    return "-dealloc sent to non-exclusively owned object";
  case FreeNotOwned:
    return "freeing non-exclusively owned object";
  case OverAutorelease:
    return "Object autoreleased too many times";
  case ReturnNotOwnedForOwned:
    return "Method should return an owned object";
  case LeakWithinFunction:
    return "Leak";
  case LeakAtReturn:
    return "Leak of returned object";
  }
  llvm_unreachable("Unknown RefCountBugKind");
}

StringRef RefCountBug::getDescription() const {
  switch (BT) {
  case UseAfterRelease:
    return "Reference-counted object is used after it is released";
  case ReleaseNotOwned:
    return "Incorrect decrement of the reference count of an object that is "
           "not owned at this point by the caller";
  case DeallocNotOwned:
    return "-dealloc sent to object that may be referenced elsewhere";
  case FreeNotOwned:
    return  "'free' called on an object that may be referenced elsewhere";
  case OverAutorelease:
    return "Object autoreleased too many times";
  case ReturnNotOwnedForOwned:
    return "Object with a +0 retain count returned to caller where a +1 "
           "(owning) retain count is expected";
  case LeakWithinFunction:
  case LeakAtReturn:
    return "";
  }
  llvm_unreachable("Unknown RefCountBugKind");
}

RefCountBug::RefCountBug(CheckerNameRef Checker, RefCountBugKind BT)
    : BugType(Checker, bugTypeToName(BT), categories::MemoryRefCount,
              /*SuppressOnSink=*/BT == LeakWithinFunction ||
                  BT == LeakAtReturn),
      BT(BT) {}

static bool isNumericLiteralExpression(const Expr *E) {
  // FIXME: This set of cases was copied from SemaExprObjC.
  return isa<IntegerLiteral, CharacterLiteral, FloatingLiteral,
             ObjCBoolLiteralExpr, CXXBoolLiteralExpr>(E);
}

/// If type represents a pointer to CXXRecordDecl,
/// and is not a typedef, return the decl name.
/// Otherwise, return the serialization of type.
static std::string getPrettyTypeName(QualType QT) {
  QualType PT = QT->getPointeeType();
  if (!PT.isNull() && !QT->getAs<TypedefType>())
    if (const auto *RD = PT->getAsCXXRecordDecl())
      return std::string(RD->getName());
  return QT.getAsString();
}

/// Write information about the type state change to @c os,
/// return whether the note should be generated.
static bool shouldGenerateNote(llvm::raw_string_ostream &os,
                               const RefVal *PrevT,
                               const RefVal &CurrV,
                               bool DeallocSent) {
  // Get the previous type state.
  RefVal PrevV = *PrevT;

  // Specially handle -dealloc.
  if (DeallocSent) {
    // Determine if the object's reference count was pushed to zero.
    assert(!PrevV.hasSameState(CurrV) && "The state should have changed.");
    // We may not have transitioned to 'release' if we hit an error.
    // This case is handled elsewhere.
    if (CurrV.getKind() == RefVal::Released) {
      assert(CurrV.getCombinedCounts() == 0);
      os << "Object released by directly sending the '-dealloc' message";
      return true;
    }
  }

  // Determine if the typestate has changed.
  if (!PrevV.hasSameState(CurrV))
    switch (CurrV.getKind()) {
    case RefVal::Owned:
    case RefVal::NotOwned:
      if (PrevV.getCount() == CurrV.getCount()) {
        // Did an autorelease message get sent?
        if (PrevV.getAutoreleaseCount() == CurrV.getAutoreleaseCount())
          return false;

        assert(PrevV.getAutoreleaseCount() < CurrV.getAutoreleaseCount());
        os << "Object autoreleased";
        return true;
      }

      if (PrevV.getCount() > CurrV.getCount())
        os << "Reference count decremented.";
      else
        os << "Reference count incremented.";

      if (unsigned Count = CurrV.getCount())
        os << " The object now has a +" << Count << " retain count.";

      return true;

    case RefVal::Released:
      if (CurrV.getIvarAccessHistory() ==
              RefVal::IvarAccessHistory::ReleasedAfterDirectAccess &&
          CurrV.getIvarAccessHistory() != PrevV.getIvarAccessHistory()) {
        os << "Strong instance variable relinquished. ";
      }
      os << "Object released.";
      return true;

    case RefVal::ReturnedOwned:
      // Autoreleases can be applied after marking a node ReturnedOwned.
      if (CurrV.getAutoreleaseCount())
        return false;

      os << "Object returned to caller as an owning reference (single "
            "retain count transferred to caller)";
      return true;

    case RefVal::ReturnedNotOwned:
      os << "Object returned to caller with a +0 retain count";
      return true;

    default:
      return false;
    }
  return true;
}

/// Finds argument index of the out paramter in the call @c S
/// corresponding to the symbol @c Sym.
/// If none found, returns std::nullopt.
static std::optional<unsigned>
findArgIdxOfSymbol(ProgramStateRef CurrSt, const LocationContext *LCtx,
                   SymbolRef &Sym, std::optional<CallEventRef<>> CE) {
  if (!CE)
    return std::nullopt;

  for (unsigned Idx = 0; Idx < (*CE)->getNumArgs(); Idx++)
    if (const MemRegion *MR = (*CE)->getArgSVal(Idx).getAsRegion())
      if (const auto *TR = dyn_cast<TypedValueRegion>(MR))
        if (CurrSt->getSVal(MR, TR->getValueType()).getAsSymbol() == Sym)
          return Idx;

  return std::nullopt;
}

static std::optional<std::string> findMetaClassAlloc(const Expr *Callee) {
  if (const auto *ME = dyn_cast<MemberExpr>(Callee)) {
    if (ME->getMemberDecl()->getNameAsString() != "alloc")
      return std::nullopt;
    const Expr *This = ME->getBase()->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(This)) {
      const ValueDecl *VD = DRE->getDecl();
      if (VD->getNameAsString() != "metaClass")
        return std::nullopt;

      if (const auto *RD = dyn_cast<CXXRecordDecl>(VD->getDeclContext()))
        return RD->getNameAsString();

    }
  }
  return std::nullopt;
}

static std::string findAllocatedObjectName(const Stmt *S, QualType QT) {
  if (const auto *CE = dyn_cast<CallExpr>(S))
    if (auto Out = findMetaClassAlloc(CE->getCallee()))
      return *Out;
  return getPrettyTypeName(QT);
}

static void generateDiagnosticsForCallLike(ProgramStateRef CurrSt,
                                           const LocationContext *LCtx,
                                           const RefVal &CurrV, SymbolRef &Sym,
                                           const Stmt *S,
                                           llvm::raw_string_ostream &os) {
  CallEventManager &Mgr = CurrSt->getStateManager().getCallEventManager();
  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    // Get the name of the callee (if it is available)
    // from the tracked SVal.
    SVal X = CurrSt->getSValAsScalarOrLoc(CE->getCallee(), LCtx);
    const FunctionDecl *FD = X.getAsFunctionDecl();

    // If failed, try to get it from AST.
    if (!FD)
      FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());

    if (const auto *MD = dyn_cast<CXXMethodDecl>(CE->getCalleeDecl())) {
      os << "Call to method '" << MD->getQualifiedNameAsString() << '\'';
    } else if (FD) {
      os << "Call to function '" << FD->getQualifiedNameAsString() << '\'';
    } else {
      os << "function call";
    }
  } else if (isa<CXXNewExpr>(S)) {
    os << "Operator 'new'";
  } else {
    assert(isa<ObjCMessageExpr>(S));
    CallEventRef<ObjCMethodCall> Call = Mgr.getObjCMethodCall(
        cast<ObjCMessageExpr>(S), CurrSt, LCtx, {nullptr, 0});

    switch (Call->getMessageKind()) {
    case OCM_Message:
      os << "Method";
      break;
    case OCM_PropertyAccess:
      os << "Property";
      break;
    case OCM_Subscript:
      os << "Subscript";
      break;
    }
  }

  std::optional<CallEventRef<>> CE = Mgr.getCall(S, CurrSt, LCtx, {nullptr, 0});
  auto Idx = findArgIdxOfSymbol(CurrSt, LCtx, Sym, CE);

  // If index is not found, we assume that the symbol was returned.
  if (!Idx) {
    os << " returns ";
  } else {
    os << " writes ";
  }

  if (CurrV.getObjKind() == ObjKind::CF) {
    os << "a Core Foundation object of type '" << Sym->getType() << "' with a ";
  } else if (CurrV.getObjKind() == ObjKind::OS) {
    os << "an OSObject of type '" << findAllocatedObjectName(S, Sym->getType())
       << "' with a ";
  } else if (CurrV.getObjKind() == ObjKind::Generalized) {
    os << "an object of type '" << Sym->getType() << "' with a ";
  } else {
    assert(CurrV.getObjKind() == ObjKind::ObjC);
    QualType T = Sym->getType();
    if (!isa<ObjCObjectPointerType>(T)) {
      os << "an Objective-C object with a ";
    } else {
      const ObjCObjectPointerType *PT = cast<ObjCObjectPointerType>(T);
      os << "an instance of " << PT->getPointeeType() << " with a ";
    }
  }

  if (CurrV.isOwned()) {
    os << "+1 retain count";
  } else {
    assert(CurrV.isNotOwned());
    os << "+0 retain count";
  }

  if (Idx) {
    os << " into an out parameter '";
    const ParmVarDecl *PVD = (*CE)->parameters()[*Idx];
    PVD->getNameForDiagnostic(os, PVD->getASTContext().getPrintingPolicy(),
                              /*Qualified=*/false);
    os << "'";

    QualType RT = (*CE)->getResultType();
    if (!RT.isNull() && !RT->isVoidType()) {
      SVal RV = (*CE)->getReturnValue();
      if (CurrSt->isNull(RV).isConstrainedTrue()) {
        os << " (assuming the call returns zero)";
      } else if (CurrSt->isNonNull(RV).isConstrainedTrue()) {
        os << " (assuming the call returns non-zero)";
      }

    }
  }
}

namespace clang {
namespace ento {
namespace retaincountchecker {

class RefCountReportVisitor : public BugReporterVisitor {
protected:
  SymbolRef Sym;

public:
  RefCountReportVisitor(SymbolRef sym) : Sym(sym) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int x = 0;
    ID.AddPointer(&x);
    ID.AddPointer(Sym);
  }

  PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                   BugReporterContext &BRC,
                                   PathSensitiveBugReport &BR) override;

  PathDiagnosticPieceRef getEndPath(BugReporterContext &BRC,
                                    const ExplodedNode *N,
                                    PathSensitiveBugReport &BR) override;
};

class RefLeakReportVisitor : public RefCountReportVisitor {
public:
  RefLeakReportVisitor(SymbolRef Sym, const MemRegion *LastBinding)
      : RefCountReportVisitor(Sym), LastBinding(LastBinding) {}

  PathDiagnosticPieceRef getEndPath(BugReporterContext &BRC,
                                    const ExplodedNode *N,
                                    PathSensitiveBugReport &BR) override;

private:
  const MemRegion *LastBinding;
};

} // end namespace retaincountchecker
} // end namespace ento
} // end namespace clang


/// Find the first node with the parent stack frame.
static const ExplodedNode *getCalleeNode(const ExplodedNode *Pred) {
  const StackFrameContext *SC = Pred->getStackFrame();
  if (SC->inTopFrame())
    return nullptr;
  const StackFrameContext *PC = SC->getParent()->getStackFrame();
  if (!PC)
    return nullptr;

  const ExplodedNode *N = Pred;
  while (N && N->getStackFrame() != PC) {
    N = N->getFirstPred();
  }
  return N;
}


/// Insert a diagnostic piece at function exit
/// if a function parameter is annotated as "os_consumed",
/// but it does not actually consume the reference.
static std::shared_ptr<PathDiagnosticEventPiece>
annotateConsumedSummaryMismatch(const ExplodedNode *N,
                                CallExitBegin &CallExitLoc,
                                const SourceManager &SM,
                                CallEventManager &CEMgr) {

  const ExplodedNode *CN = getCalleeNode(N);
  if (!CN)
    return nullptr;

  CallEventRef<> Call = CEMgr.getCaller(N->getStackFrame(), N->getState());

  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);
  ArrayRef<const ParmVarDecl *> Parameters = Call->parameters();
  for (unsigned I=0; I < Call->getNumArgs() && I < Parameters.size(); ++I) {
    const ParmVarDecl *PVD = Parameters[I];

    if (!PVD->hasAttr<OSConsumedAttr>())
      continue;

    if (SymbolRef SR = Call->getArgSVal(I).getAsLocSymbol()) {
      const RefVal *CountBeforeCall = getRefBinding(CN->getState(), SR);
      const RefVal *CountAtExit = getRefBinding(N->getState(), SR);

      if (!CountBeforeCall || !CountAtExit)
        continue;

      unsigned CountBefore = CountBeforeCall->getCount();
      unsigned CountAfter = CountAtExit->getCount();

      bool AsExpected = CountBefore > 0 && CountAfter == CountBefore - 1;
      if (!AsExpected) {
        os << "Parameter '";
        PVD->getNameForDiagnostic(os, PVD->getASTContext().getPrintingPolicy(),
                                  /*Qualified=*/false);
        os << "' is marked as consuming, but the function did not consume "
           << "the reference\n";
      }
    }
  }

  if (sbuf.empty())
    return nullptr;

  PathDiagnosticLocation L = PathDiagnosticLocation::create(CallExitLoc, SM);
  return std::make_shared<PathDiagnosticEventPiece>(L, sbuf);
}

/// Annotate the parameter at the analysis entry point.
static std::shared_ptr<PathDiagnosticEventPiece>
annotateStartParameter(const ExplodedNode *N, SymbolRef Sym,
                       const SourceManager &SM) {
  auto PP = N->getLocationAs<BlockEdge>();
  if (!PP)
    return nullptr;

  const CFGBlock *Src = PP->getSrc();
  const RefVal *CurrT = getRefBinding(N->getState(), Sym);

  if (&Src->getParent()->getEntry() != Src || !CurrT ||
      getRefBinding(N->getFirstPred()->getState(), Sym))
    return nullptr;

  const auto *VR = cast<VarRegion>(cast<SymbolRegionValue>(Sym)->getRegion());
  const auto *PVD = cast<ParmVarDecl>(VR->getDecl());
  PathDiagnosticLocation L = PathDiagnosticLocation(PVD, SM);

  std::string s;
  llvm::raw_string_ostream os(s);
  os << "Parameter '" << PVD->getDeclName() << "' starts at +";
  if (CurrT->getCount() == 1) {
    os << "1, as it is marked as consuming";
  } else {
    assert(CurrT->getCount() == 0);
    os << "0";
  }
  return std::make_shared<PathDiagnosticEventPiece>(L, s);
}

PathDiagnosticPieceRef
RefCountReportVisitor::VisitNode(const ExplodedNode *N, BugReporterContext &BRC,
                                 PathSensitiveBugReport &BR) {

  const auto &BT = static_cast<const RefCountBug&>(BR.getBugType());

  bool IsFreeUnowned = BT.getBugType() == RefCountBug::FreeNotOwned ||
                       BT.getBugType() == RefCountBug::DeallocNotOwned;

  const SourceManager &SM = BRC.getSourceManager();
  CallEventManager &CEMgr = BRC.getStateManager().getCallEventManager();
  if (auto CE = N->getLocationAs<CallExitBegin>())
    if (auto PD = annotateConsumedSummaryMismatch(N, *CE, SM, CEMgr))
      return PD;

  if (auto PD = annotateStartParameter(N, Sym, SM))
    return PD;

  // FIXME: We will eventually need to handle non-statement-based events
  // (__attribute__((cleanup))).
  if (!N->getLocation().getAs<StmtPoint>())
    return nullptr;

  // Check if the type state has changed.
  const ExplodedNode *PrevNode = N->getFirstPred();
  ProgramStateRef PrevSt = PrevNode->getState();
  ProgramStateRef CurrSt = N->getState();
  const LocationContext *LCtx = N->getLocationContext();

  const RefVal* CurrT = getRefBinding(CurrSt, Sym);
  if (!CurrT)
    return nullptr;

  const RefVal &CurrV = *CurrT;
  const RefVal *PrevT = getRefBinding(PrevSt, Sym);

  // Create a string buffer to constain all the useful things we want
  // to tell the user.
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  if (PrevT && IsFreeUnowned && CurrV.isNotOwned() && PrevT->isOwned()) {
    os << "Object is now not exclusively owned";
    auto Pos = PathDiagnosticLocation::create(N->getLocation(), SM);
    return std::make_shared<PathDiagnosticEventPiece>(Pos, sbuf);
  }

  // This is the allocation site since the previous node had no bindings
  // for this symbol.
  if (!PrevT) {
    const Stmt *S = N->getLocation().castAs<StmtPoint>().getStmt();

    if (isa<ObjCIvarRefExpr>(S) &&
        isSynthesizedAccessor(LCtx->getStackFrame())) {
      S = LCtx->getStackFrame()->getCallSite();
    }

    if (isa<ObjCArrayLiteral>(S)) {
      os << "NSArray literal is an object with a +0 retain count";
    } else if (isa<ObjCDictionaryLiteral>(S)) {
      os << "NSDictionary literal is an object with a +0 retain count";
    } else if (const ObjCBoxedExpr *BL = dyn_cast<ObjCBoxedExpr>(S)) {
      if (isNumericLiteralExpression(BL->getSubExpr()))
        os << "NSNumber literal is an object with a +0 retain count";
      else {
        const ObjCInterfaceDecl *BoxClass = nullptr;
        if (const ObjCMethodDecl *Method = BL->getBoxingMethod())
          BoxClass = Method->getClassInterface();

        // We should always be able to find the boxing class interface,
        // but consider this future-proofing.
        if (BoxClass) {
          os << *BoxClass << " b";
        } else {
          os << "B";
        }

        os << "oxed expression produces an object with a +0 retain count";
      }
    } else if (isa<ObjCIvarRefExpr>(S)) {
      os << "Object loaded from instance variable";
    } else {
      generateDiagnosticsForCallLike(CurrSt, LCtx, CurrV, Sym, S, os);
    }

    PathDiagnosticLocation Pos(S, SM, N->getLocationContext());
    return std::make_shared<PathDiagnosticEventPiece>(Pos, sbuf);
  }

  // Gather up the effects that were performed on the object at this
  // program point
  bool DeallocSent = false;

  const ProgramPointTag *Tag = N->getLocation().getTag();

  if (Tag == &RetainCountChecker::getCastFailTag()) {
    os << "Assuming dynamic cast returns null due to type mismatch";
  }

  if (Tag == &RetainCountChecker::getDeallocSentTag()) {
    // We only have summaries attached to nodes after evaluating CallExpr and
    // ObjCMessageExprs.
    const Stmt *S = N->getLocation().castAs<StmtPoint>().getStmt();

    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      // Iterate through the parameter expressions and see if the symbol
      // was ever passed as an argument.
      unsigned i = 0;

      for (auto AI=CE->arg_begin(), AE=CE->arg_end(); AI!=AE; ++AI, ++i) {

        // Retrieve the value of the argument.  Is it the symbol
        // we are interested in?
        if (CurrSt->getSValAsScalarOrLoc(*AI, LCtx).getAsLocSymbol() != Sym)
          continue;

        // We have an argument.  Get the effect!
        DeallocSent = true;
      }
    } else if (const ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(S)) {
      if (const Expr *receiver = ME->getInstanceReceiver()) {
        if (CurrSt->getSValAsScalarOrLoc(receiver, LCtx)
              .getAsLocSymbol() == Sym) {
          // The symbol we are tracking is the receiver.
          DeallocSent = true;
        }
      }
    }
  }

  if (!shouldGenerateNote(os, PrevT, CurrV, DeallocSent))
    return nullptr;

  if (sbuf.empty())
    return nullptr; // We have nothing to say!

  const Stmt *S = N->getLocation().castAs<StmtPoint>().getStmt();
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                                N->getLocationContext());
  auto P = std::make_shared<PathDiagnosticEventPiece>(Pos, sbuf);

  // Add the range by scanning the children of the statement for any bindings
  // to Sym.
  for (const Stmt *Child : S->children())
    if (const Expr *Exp = dyn_cast_or_null<Expr>(Child))
      if (CurrSt->getSValAsScalarOrLoc(Exp, LCtx).getAsLocSymbol() == Sym) {
        P->addRange(Exp->getSourceRange());
        break;
      }

  return std::move(P);
}

static std::optional<std::string> describeRegion(const MemRegion *MR) {
  if (const auto *VR = dyn_cast_or_null<VarRegion>(MR))
    return std::string(VR->getDecl()->getName());
  // Once we support more storage locations for bindings,
  // this would need to be improved.
  return std::nullopt;
}

using Bindings = llvm::SmallVector<std::pair<const MemRegion *, SVal>, 4>;

namespace {
class VarBindingsCollector : public StoreManager::BindingsHandler {
  SymbolRef Sym;
  Bindings &Result;

public:
  VarBindingsCollector(SymbolRef Sym, Bindings &ToFill)
      : Sym(Sym), Result(ToFill) {}

  bool HandleBinding(StoreManager &SMgr, Store Store, const MemRegion *R,
                     SVal Val) override {
    SymbolRef SymV = Val.getAsLocSymbol();
    if (!SymV || SymV != Sym)
      return true;

    if (isa<NonParamVarRegion>(R))
      Result.emplace_back(R, Val);

    return true;
  }
};
} // namespace

Bindings getAllVarBindingsForSymbol(ProgramStateManager &Manager,
                                    const ExplodedNode *Node, SymbolRef Sym) {
  Bindings Result;
  VarBindingsCollector Collector{Sym, Result};
  while (Result.empty() && Node) {
    Manager.iterBindings(Node->getState(), Collector);
    Node = Node->getFirstPred();
  }

  return Result;
}

namespace {
// Find the first node in the current function context that referred to the
// tracked symbol and the memory location that value was stored to. Note, the
// value is only reported if the allocation occurred in the same function as
// the leak. The function can also return a location context, which should be
// treated as interesting.
struct AllocationInfo {
  const ExplodedNode* N;
  const MemRegion *R;
  const LocationContext *InterestingMethodContext;
  AllocationInfo(const ExplodedNode *InN,
                 const MemRegion *InR,
                 const LocationContext *InInterestingMethodContext) :
    N(InN), R(InR), InterestingMethodContext(InInterestingMethodContext) {}
};
} // end anonymous namespace

static AllocationInfo GetAllocationSite(ProgramStateManager &StateMgr,
                                        const ExplodedNode *N, SymbolRef Sym) {
  const ExplodedNode *AllocationNode = N;
  const ExplodedNode *AllocationNodeInCurrentOrParentContext = N;
  const MemRegion *FirstBinding = nullptr;
  const LocationContext *LeakContext = N->getLocationContext();

  // The location context of the init method called on the leaked object, if
  // available.
  const LocationContext *InitMethodContext = nullptr;

  while (N) {
    ProgramStateRef St = N->getState();
    const LocationContext *NContext = N->getLocationContext();

    if (!getRefBinding(St, Sym))
      break;

    StoreManager::FindUniqueBinding FB(Sym);
    StateMgr.iterBindings(St, FB);

    if (FB) {
      const MemRegion *R = FB.getRegion();
      // Do not show local variables belonging to a function other than
      // where the error is reported.
      if (auto MR = dyn_cast<StackSpaceRegion>(R->getMemorySpace()))
        if (MR->getStackFrame() == LeakContext->getStackFrame())
          FirstBinding = R;
    }

    // AllocationNode is the last node in which the symbol was tracked.
    AllocationNode = N;

    // AllocationNodeInCurrentContext, is the last node in the current or
    // parent context in which the symbol was tracked.
    //
    // Note that the allocation site might be in the parent context. For example,
    // the case where an allocation happens in a block that captures a reference
    // to it and that reference is overwritten/dropped by another call to
    // the block.
    if (NContext == LeakContext || NContext->isParentOf(LeakContext))
      AllocationNodeInCurrentOrParentContext = N;

    // Find the last init that was called on the given symbol and store the
    // init method's location context.
    if (!InitMethodContext)
      if (auto CEP = N->getLocation().getAs<CallEnter>()) {
        const Stmt *CE = CEP->getCallExpr();
        if (const auto *ME = dyn_cast_or_null<ObjCMessageExpr>(CE)) {
          const Stmt *RecExpr = ME->getInstanceReceiver();
          if (RecExpr) {
            SVal RecV = St->getSVal(RecExpr, NContext);
            if (ME->getMethodFamily() == OMF_init && RecV.getAsSymbol() == Sym)
              InitMethodContext = CEP->getCalleeContext();
          }
        }
      }

    N = N->getFirstPred();
  }

  // If we are reporting a leak of the object that was allocated with alloc,
  // mark its init method as interesting.
  const LocationContext *InterestingMethodContext = nullptr;
  if (InitMethodContext) {
    const ProgramPoint AllocPP = AllocationNode->getLocation();
    if (std::optional<StmtPoint> SP = AllocPP.getAs<StmtPoint>())
      if (const ObjCMessageExpr *ME = SP->getStmtAs<ObjCMessageExpr>())
        if (ME->getMethodFamily() == OMF_alloc)
          InterestingMethodContext = InitMethodContext;
  }

  // If allocation happened in a function different from the leak node context,
  // do not report the binding.
  assert(N && "Could not find allocation node");

  if (AllocationNodeInCurrentOrParentContext &&
      AllocationNodeInCurrentOrParentContext->getLocationContext() !=
      LeakContext)
    FirstBinding = nullptr;

  return AllocationInfo(AllocationNodeInCurrentOrParentContext, FirstBinding,
                        InterestingMethodContext);
}

PathDiagnosticPieceRef
RefCountReportVisitor::getEndPath(BugReporterContext &BRC,
                                  const ExplodedNode *EndN,
                                  PathSensitiveBugReport &BR) {
  BR.markInteresting(Sym);
  return BugReporterVisitor::getDefaultEndPath(BRC, EndN, BR);
}

PathDiagnosticPieceRef
RefLeakReportVisitor::getEndPath(BugReporterContext &BRC,
                                 const ExplodedNode *EndN,
                                 PathSensitiveBugReport &BR) {

  // Tell the BugReporterContext to report cases when the tracked symbol is
  // assigned to different variables, etc.
  BR.markInteresting(Sym);

  PathDiagnosticLocation L = cast<RefLeakReport>(BR).getEndOfPath();

  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  os << "Object leaked: ";

  std::optional<std::string> RegionDescription = describeRegion(LastBinding);
  if (RegionDescription) {
    os << "object allocated and stored into '" << *RegionDescription << '\'';
  } else {
    os << "allocated object of type '" << getPrettyTypeName(Sym->getType())
       << "'";
  }

  // Get the retain count.
  const RefVal *RV = getRefBinding(EndN->getState(), Sym);
  assert(RV);

  if (RV->getKind() == RefVal::ErrorLeakReturned) {
    const Decl *D = &EndN->getCodeDecl();

    os << (isa<ObjCMethodDecl>(D) ? " is returned from a method "
                                  : " is returned from a function ");

    if (D->hasAttr<CFReturnsNotRetainedAttr>()) {
      os << "that is annotated as CF_RETURNS_NOT_RETAINED";
    } else if (D->hasAttr<NSReturnsNotRetainedAttr>()) {
      os << "that is annotated as NS_RETURNS_NOT_RETAINED";
    } else if (D->hasAttr<OSReturnsNotRetainedAttr>()) {
      os << "that is annotated as OS_RETURNS_NOT_RETAINED";
    } else {
      if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D)) {
        if (BRC.getASTContext().getLangOpts().ObjCAutoRefCount) {
          os << "managed by Automatic Reference Counting";
        } else {
          os << "whose name ('" << MD->getSelector().getAsString()
             << "') does not start with "
                "'copy', 'mutableCopy', 'alloc' or 'new'."
                "  This violates the naming convention rules"
                " given in the Memory Management Guide for Cocoa";
        }
      } else {
        const FunctionDecl *FD = cast<FunctionDecl>(D);
        ObjKind K = RV->getObjKind();
        if (K == ObjKind::ObjC || K == ObjKind::CF) {
          os << "whose name ('" << *FD
             << "') does not contain 'Copy' or 'Create'.  This violates the "
                "naming"
                " convention rules given in the Memory Management Guide for "
                "Core"
                " Foundation";
        } else if (RV->getObjKind() == ObjKind::OS) {
          std::string FuncName = FD->getNameAsString();
          os << "whose name ('" << FuncName << "') starts with '"
             << StringRef(FuncName).substr(0, 3) << "'";
        }
      }
    }
  } else {
    os << " is not referenced later in this execution path and has a retain "
          "count of +"
       << RV->getCount();
  }

  return std::make_shared<PathDiagnosticEventPiece>(L, sbuf);
}

RefCountReport::RefCountReport(const RefCountBug &D, const LangOptions &LOpts,
                               ExplodedNode *n, SymbolRef sym, bool isLeak)
    : PathSensitiveBugReport(D, D.getDescription(), n), Sym(sym),
      isLeak(isLeak) {
  if (!isLeak)
    addVisitor<RefCountReportVisitor>(sym);
}

RefCountReport::RefCountReport(const RefCountBug &D, const LangOptions &LOpts,
                               ExplodedNode *n, SymbolRef sym,
                               StringRef endText)
    : PathSensitiveBugReport(D, D.getDescription(), endText, n) {

  addVisitor<RefCountReportVisitor>(sym);
}

void RefLeakReport::deriveParamLocation(CheckerContext &Ctx) {
  const SourceManager &SMgr = Ctx.getSourceManager();

  if (!Sym->getOriginRegion())
    return;

  auto *Region = dyn_cast<DeclRegion>(Sym->getOriginRegion());
  if (Region) {
    const Decl *PDecl = Region->getDecl();
    if (isa_and_nonnull<ParmVarDecl>(PDecl)) {
      PathDiagnosticLocation ParamLocation =
          PathDiagnosticLocation::create(PDecl, SMgr);
      Location = ParamLocation;
      UniqueingLocation = ParamLocation;
      UniqueingDecl = Ctx.getLocationContext()->getDecl();
    }
  }
}

void RefLeakReport::deriveAllocLocation(CheckerContext &Ctx) {
  // Most bug reports are cached at the location where they occurred.
  // With leaks, we want to unique them by the location where they were
  // allocated, and only report a single path.  To do this, we need to find
  // the allocation site of a piece of tracked memory, which we do via a
  // call to GetAllocationSite.  This will walk the ExplodedGraph backwards.
  // Note that this is *not* the trimmed graph; we are guaranteed, however,
  // that all ancestor nodes that represent the allocation site have the
  // same SourceLocation.
  const ExplodedNode *AllocNode = nullptr;

  const SourceManager &SMgr = Ctx.getSourceManager();

  AllocationInfo AllocI =
      GetAllocationSite(Ctx.getStateManager(), getErrorNode(), Sym);

  AllocNode = AllocI.N;
  AllocFirstBinding = AllocI.R;
  markInteresting(AllocI.InterestingMethodContext);

  // Get the SourceLocation for the allocation site.
  // FIXME: This will crash the analyzer if an allocation comes from an
  // implicit call (ex: a destructor call).
  // (Currently there are no such allocations in Cocoa, though.)
  AllocStmt = AllocNode->getStmtForDiagnostics();

  if (!AllocStmt) {
    AllocFirstBinding = nullptr;
    return;
  }

  PathDiagnosticLocation AllocLocation = PathDiagnosticLocation::createBegin(
      AllocStmt, SMgr, AllocNode->getLocationContext());
  Location = AllocLocation;

  // Set uniqieing info, which will be used for unique the bug reports. The
  // leaks should be uniqued on the allocation site.
  UniqueingLocation = AllocLocation;
  UniqueingDecl = AllocNode->getLocationContext()->getDecl();
}

void RefLeakReport::createDescription(CheckerContext &Ctx) {
  assert(Location.isValid() && UniqueingDecl && UniqueingLocation.isValid());
  Description.clear();
  llvm::raw_string_ostream os(Description);
  os << "Potential leak of an object";

  std::optional<std::string> RegionDescription =
      describeRegion(AllocBindingToReport);
  if (RegionDescription) {
    os << " stored into '" << *RegionDescription << '\'';
  } else {

    // If we can't figure out the name, just supply the type information.
    os << " of type '" << getPrettyTypeName(Sym->getType()) << "'";
  }
}

void RefLeakReport::findBindingToReport(CheckerContext &Ctx,
                                        ExplodedNode *Node) {
  if (!AllocFirstBinding)
    // If we don't have any bindings, we won't be able to find any
    // better binding to report.
    return;

  // If the original region still contains the leaking symbol...
  if (Node->getState()->getSVal(AllocFirstBinding).getAsSymbol() == Sym) {
    // ...it is the best binding to report.
    AllocBindingToReport = AllocFirstBinding;
    return;
  }

  // At this point, we know that the original region doesn't contain the leaking
  // when the actual leak happens.  It means that it can be confusing for the
  // user to see such description in the message.
  //
  // Let's consider the following example:
  //   Object *Original = allocate(...);
  //   Object *New = Original;
  //   Original = allocate(...);
  //   Original->release();
  //
  // Complaining about a leaking object "stored into Original" might cause a
  // rightful confusion because 'Original' is actually released.
  // We should complain about 'New' instead.
  Bindings AllVarBindings =
      getAllVarBindingsForSymbol(Ctx.getStateManager(), Node, Sym);

  // While looking for the last var bindings, we can still find
  // `AllocFirstBinding` to be one of them.  In situations like this,
  // it would still be the easiest case to explain to our users.
  if (!AllVarBindings.empty() &&
      llvm::count_if(AllVarBindings,
                     [this](const std::pair<const MemRegion *, SVal> Binding) {
                       return Binding.first == AllocFirstBinding;
                     }) == 0) {
    // Let's pick one of them at random (if there is something to pick from).
    AllocBindingToReport = AllVarBindings[0].first;

    // Because 'AllocBindingToReport' is not the same as
    // 'AllocFirstBinding', we need to explain how the leaking object
    // got from one to another.
    //
    // NOTE: We use the actual SVal stored in AllocBindingToReport here because
    //       trackStoredValue compares SVal's and it can get trickier for
    //       something like derived regions if we want to construct SVal from
    //       Sym. Instead, we take the value that is definitely stored in that
    //       region, thus guaranteeing that trackStoredValue will work.
    bugreporter::trackStoredValue(AllVarBindings[0].second,
                                  AllocBindingToReport, *this);
  } else {
    AllocBindingToReport = AllocFirstBinding;
  }
}

RefLeakReport::RefLeakReport(const RefCountBug &D, const LangOptions &LOpts,
                             ExplodedNode *N, SymbolRef Sym,
                             CheckerContext &Ctx)
    : RefCountReport(D, LOpts, N, Sym, /*isLeak=*/true) {

  deriveAllocLocation(Ctx);
  findBindingToReport(Ctx, N);

  if (!AllocFirstBinding)
    deriveParamLocation(Ctx);

  createDescription(Ctx);

  addVisitor<RefLeakReportVisitor>(Sym, AllocBindingToReport);
}
