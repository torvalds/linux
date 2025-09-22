//===-- DereferenceChecker.cpp - Null dereference checker -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines NullDerefChecker, a builtin check in ExprEngine that performs
// checks for null pointers at loads and stores.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class DereferenceChecker
    : public Checker< check::Location,
                      check::Bind,
                      EventDispatcher<ImplicitNullDerefEvent> > {
  enum DerefKind { NullPointer, UndefinedPointerValue, AddressOfLabel };

  BugType BT_Null{this, "Dereference of null pointer", categories::LogicError};
  BugType BT_Undef{this, "Dereference of undefined pointer value",
                   categories::LogicError};
  BugType BT_Label{this, "Dereference of the address of a label",
                   categories::LogicError};

  void reportBug(DerefKind K, ProgramStateRef State, const Stmt *S,
                 CheckerContext &C) const;

  bool suppressReport(CheckerContext &C, const Expr *E) const;

public:
  void checkLocation(SVal location, bool isLoad, const Stmt* S,
                     CheckerContext &C) const;
  void checkBind(SVal L, SVal V, const Stmt *S, CheckerContext &C) const;

  static void AddDerefSource(raw_ostream &os,
                             SmallVectorImpl<SourceRange> &Ranges,
                             const Expr *Ex, const ProgramState *state,
                             const LocationContext *LCtx,
                             bool loadedFrom = false);

  bool SuppressAddressSpaces = false;
};
} // end anonymous namespace

void
DereferenceChecker::AddDerefSource(raw_ostream &os,
                                   SmallVectorImpl<SourceRange> &Ranges,
                                   const Expr *Ex,
                                   const ProgramState *state,
                                   const LocationContext *LCtx,
                                   bool loadedFrom) {
  Ex = Ex->IgnoreParenLValueCasts();
  switch (Ex->getStmtClass()) {
    default:
      break;
    case Stmt::DeclRefExprClass: {
      const DeclRefExpr *DR = cast<DeclRefExpr>(Ex);
      if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
        os << " (" << (loadedFrom ? "loaded from" : "from")
           << " variable '" <<  VD->getName() << "')";
        Ranges.push_back(DR->getSourceRange());
      }
      break;
    }
    case Stmt::MemberExprClass: {
      const MemberExpr *ME = cast<MemberExpr>(Ex);
      os << " (" << (loadedFrom ? "loaded from" : "via")
         << " field '" << ME->getMemberNameInfo() << "')";
      SourceLocation L = ME->getMemberLoc();
      Ranges.push_back(SourceRange(L, L));
      break;
    }
    case Stmt::ObjCIvarRefExprClass: {
      const ObjCIvarRefExpr *IV = cast<ObjCIvarRefExpr>(Ex);
      os << " (" << (loadedFrom ? "loaded from" : "via")
         << " ivar '" << IV->getDecl()->getName() << "')";
      SourceLocation L = IV->getLocation();
      Ranges.push_back(SourceRange(L, L));
      break;
    }
  }
}

static const Expr *getDereferenceExpr(const Stmt *S, bool IsBind=false){
  const Expr *E = nullptr;

  // Walk through lvalue casts to get the original expression
  // that syntactically caused the load.
  if (const Expr *expr = dyn_cast<Expr>(S))
    E = expr->IgnoreParenLValueCasts();

  if (IsBind) {
    const VarDecl *VD;
    const Expr *Init;
    std::tie(VD, Init) = parseAssignment(S);
    if (VD && Init)
      E = Init;
  }
  return E;
}

bool DereferenceChecker::suppressReport(CheckerContext &C,
                                        const Expr *E) const {
  // Do not report dereferences on memory that use address space #256, #257,
  // and #258. Those address spaces are used when dereferencing address spaces
  // relative to the GS, FS, and SS segments on x86/x86-64 targets.
  // Dereferencing a null pointer in these address spaces is not defined
  // as an error. All other null dereferences in other address spaces
  // are defined as an error unless explicitly defined.
  // See https://clang.llvm.org/docs/LanguageExtensions.html, the section
  // "X86/X86-64 Language Extensions"

  QualType Ty = E->getType();
  if (!Ty.hasAddressSpace())
    return false;
  if (SuppressAddressSpaces)
    return true;

  const llvm::Triple::ArchType Arch =
      C.getASTContext().getTargetInfo().getTriple().getArch();

  if ((Arch == llvm::Triple::x86) || (Arch == llvm::Triple::x86_64)) {
    switch (toTargetAddressSpace(E->getType().getAddressSpace())) {
    case 256:
    case 257:
    case 258:
      return true;
    }
  }
  return false;
}

static bool isDeclRefExprToReference(const Expr *E) {
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl()->getType()->isReferenceType();
  return false;
}

void DereferenceChecker::reportBug(DerefKind K, ProgramStateRef State,
                                   const Stmt *S, CheckerContext &C) const {
  const BugType *BT = nullptr;
  llvm::StringRef DerefStr1;
  llvm::StringRef DerefStr2;
  switch (K) {
  case DerefKind::NullPointer:
    BT = &BT_Null;
    DerefStr1 = " results in a null pointer dereference";
    DerefStr2 = " results in a dereference of a null pointer";
    break;
  case DerefKind::UndefinedPointerValue:
    BT = &BT_Undef;
    DerefStr1 = " results in an undefined pointer dereference";
    DerefStr2 = " results in a dereference of an undefined pointer value";
    break;
  case DerefKind::AddressOfLabel:
    BT = &BT_Label;
    DerefStr1 = " results in an undefined pointer dereference";
    DerefStr2 = " results in a dereference of an address of a label";
    break;
  };

  // Generate an error node.
  ExplodedNode *N = C.generateErrorNode(State);
  if (!N)
    return;

  SmallString<100> buf;
  llvm::raw_svector_ostream os(buf);

  SmallVector<SourceRange, 2> Ranges;

  switch (S->getStmtClass()) {
  case Stmt::ArraySubscriptExprClass: {
    os << "Array access";
    const ArraySubscriptExpr *AE = cast<ArraySubscriptExpr>(S);
    AddDerefSource(os, Ranges, AE->getBase()->IgnoreParenCasts(),
                   State.get(), N->getLocationContext());
    os << DerefStr1;
    break;
  }
  case Stmt::ArraySectionExprClass: {
    os << "Array access";
    const ArraySectionExpr *AE = cast<ArraySectionExpr>(S);
    AddDerefSource(os, Ranges, AE->getBase()->IgnoreParenCasts(),
                   State.get(), N->getLocationContext());
    os << DerefStr1;
    break;
  }
  case Stmt::UnaryOperatorClass: {
    os << BT->getDescription();
    const UnaryOperator *U = cast<UnaryOperator>(S);
    AddDerefSource(os, Ranges, U->getSubExpr()->IgnoreParens(),
                   State.get(), N->getLocationContext(), true);
    break;
  }
  case Stmt::MemberExprClass: {
    const MemberExpr *M = cast<MemberExpr>(S);
    if (M->isArrow() || isDeclRefExprToReference(M->getBase())) {
      os << "Access to field '" << M->getMemberNameInfo() << "'" << DerefStr2;
      AddDerefSource(os, Ranges, M->getBase()->IgnoreParenCasts(),
                     State.get(), N->getLocationContext(), true);
    }
    break;
  }
  case Stmt::ObjCIvarRefExprClass: {
    const ObjCIvarRefExpr *IV = cast<ObjCIvarRefExpr>(S);
    os << "Access to instance variable '" << *IV->getDecl() << "'" << DerefStr2;
    AddDerefSource(os, Ranges, IV->getBase()->IgnoreParenCasts(),
                   State.get(), N->getLocationContext(), true);
    break;
  }
  default:
    break;
  }

  auto report = std::make_unique<PathSensitiveBugReport>(
      *BT, buf.empty() ? BT->getDescription() : buf.str(), N);

  bugreporter::trackExpressionValue(N, bugreporter::getDerefExpr(S), *report);

  for (SmallVectorImpl<SourceRange>::iterator
       I = Ranges.begin(), E = Ranges.end(); I!=E; ++I)
    report->addRange(*I);

  C.emitReport(std::move(report));
}

void DereferenceChecker::checkLocation(SVal l, bool isLoad, const Stmt* S,
                                       CheckerContext &C) const {
  // Check for dereference of an undefined value.
  if (l.isUndef()) {
    const Expr *DerefExpr = getDereferenceExpr(S);
    if (!suppressReport(C, DerefExpr))
      reportBug(DerefKind::UndefinedPointerValue, C.getState(), DerefExpr, C);
    return;
  }

  DefinedOrUnknownSVal location = l.castAs<DefinedOrUnknownSVal>();

  // Check for null dereferences.
  if (!isa<Loc>(location))
    return;

  ProgramStateRef state = C.getState();

  ProgramStateRef notNullState, nullState;
  std::tie(notNullState, nullState) = state->assume(location);

  if (nullState) {
    if (!notNullState) {
      // We know that 'location' can only be null.  This is what
      // we call an "explicit" null dereference.
      const Expr *expr = getDereferenceExpr(S);
      if (!suppressReport(C, expr)) {
        reportBug(DerefKind::NullPointer, nullState, expr, C);
        return;
      }
    }

    // Otherwise, we have the case where the location could either be
    // null or not-null.  Record the error node as an "implicit" null
    // dereference.
    if (ExplodedNode *N = C.generateSink(nullState, C.getPredecessor())) {
      ImplicitNullDerefEvent event = {l, isLoad, N, &C.getBugReporter(),
                                      /*IsDirectDereference=*/true};
      dispatchEvent(event);
    }
  }

  // From this point forward, we know that the location is not null.
  C.addTransition(notNullState);
}

void DereferenceChecker::checkBind(SVal L, SVal V, const Stmt *S,
                                   CheckerContext &C) const {
  // If we're binding to a reference, check if the value is known to be null.
  if (V.isUndef())
    return;

  // One should never write to label addresses.
  if (auto Label = L.getAs<loc::GotoLabel>()) {
    reportBug(DerefKind::AddressOfLabel, C.getState(), S, C);
    return;
  }

  const MemRegion *MR = L.getAsRegion();
  const TypedValueRegion *TVR = dyn_cast_or_null<TypedValueRegion>(MR);
  if (!TVR)
    return;

  if (!TVR->getValueType()->isReferenceType())
    return;

  ProgramStateRef State = C.getState();

  ProgramStateRef StNonNull, StNull;
  std::tie(StNonNull, StNull) = State->assume(V.castAs<DefinedOrUnknownSVal>());

  if (StNull) {
    if (!StNonNull) {
      const Expr *expr = getDereferenceExpr(S, /*IsBind=*/true);
      if (!suppressReport(C, expr)) {
        reportBug(DerefKind::NullPointer, StNull, expr, C);
        return;
      }
    }

    // At this point the value could be either null or non-null.
    // Record this as an "implicit" null dereference.
    if (ExplodedNode *N = C.generateSink(StNull, C.getPredecessor())) {
      ImplicitNullDerefEvent event = {V, /*isLoad=*/true, N,
                                      &C.getBugReporter(),
                                      /*IsDirectDereference=*/true};
      dispatchEvent(event);
    }
  }

  // Unlike a regular null dereference, initializing a reference with a
  // dereferenced null pointer does not actually cause a runtime exception in
  // Clang's implementation of references.
  //
  //   int &r = *p; // safe??
  //   if (p != NULL) return; // uh-oh
  //   r = 5; // trap here
  //
  // The standard says this is invalid as soon as we try to create a "null
  // reference" (there is no such thing), but turning this into an assumption
  // that 'p' is never null will not match our actual runtime behavior.
  // So we do not record this assumption, allowing us to warn on the last line
  // of this example.
  //
  // We do need to add a transition because we may have generated a sink for
  // the "implicit" null dereference.
  C.addTransition(State, this);
}

void ento::registerDereferenceChecker(CheckerManager &mgr) {
  auto *Chk = mgr.registerChecker<DereferenceChecker>();
  Chk->SuppressAddressSpaces = mgr.getAnalyzerOptions().getCheckerBooleanOption(
      mgr.getCurrentCheckerName(), "SuppressAddressSpaces");
}

bool ento::shouldRegisterDereferenceChecker(const CheckerManager &mgr) {
  return true;
}
