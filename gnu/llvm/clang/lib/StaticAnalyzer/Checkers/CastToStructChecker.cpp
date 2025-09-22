//=== CastToStructChecker.cpp ----------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This files defines CastToStructChecker, a builtin checker that checks for
// cast from non-struct pointer to struct pointer and widening struct data cast.
// This check corresponds to CWE-588.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class CastToStructVisitor : public RecursiveASTVisitor<CastToStructVisitor> {
  BugReporter &BR;
  const CheckerBase *Checker;
  AnalysisDeclContext *AC;

public:
  explicit CastToStructVisitor(BugReporter &B, const CheckerBase *Checker,
                               AnalysisDeclContext *A)
      : BR(B), Checker(Checker), AC(A) {}
  bool VisitCastExpr(const CastExpr *CE);
};
}

bool CastToStructVisitor::VisitCastExpr(const CastExpr *CE) {
  const Expr *E = CE->getSubExpr();
  ASTContext &Ctx = AC->getASTContext();
  QualType OrigTy = Ctx.getCanonicalType(E->getType());
  QualType ToTy = Ctx.getCanonicalType(CE->getType());

  const PointerType *OrigPTy = dyn_cast<PointerType>(OrigTy.getTypePtr());
  const PointerType *ToPTy = dyn_cast<PointerType>(ToTy.getTypePtr());

  if (!ToPTy || !OrigPTy)
    return true;

  QualType OrigPointeeTy = OrigPTy->getPointeeType();
  QualType ToPointeeTy = ToPTy->getPointeeType();

  if (!ToPointeeTy->isStructureOrClassType())
    return true;

  // We allow cast from void*.
  if (OrigPointeeTy->isVoidType())
    return true;

  // Now the cast-to-type is struct pointer, the original type is not void*.
  if (!OrigPointeeTy->isRecordType()) {
    SourceRange Sr[1] = {CE->getSourceRange()};
    PathDiagnosticLocation Loc(CE, BR.getSourceManager(), AC);
    BR.EmitBasicReport(
        AC->getDecl(), Checker, "Cast from non-struct type to struct type",
        categories::LogicError, "Casting a non-structure type to a structure "
                                "type and accessing a field can lead to memory "
                                "access errors or data corruption.",
        Loc, Sr);
  } else {
    // Don't warn when size of data is unknown.
    const auto *U = dyn_cast<UnaryOperator>(E);
    if (!U || U->getOpcode() != UO_AddrOf)
      return true;

    // Don't warn for references
    const ValueDecl *VD = nullptr;
    if (const auto *SE = dyn_cast<DeclRefExpr>(U->getSubExpr()))
      VD = SE->getDecl();
    else if (const auto *SE = dyn_cast<MemberExpr>(U->getSubExpr()))
      VD = SE->getMemberDecl();
    if (!VD || VD->getType()->isReferenceType())
      return true;

    if (ToPointeeTy->isIncompleteType() ||
        OrigPointeeTy->isIncompleteType())
      return true;

    // Warn when there is widening cast.
    unsigned ToWidth = Ctx.getTypeInfo(ToPointeeTy).Width;
    unsigned OrigWidth = Ctx.getTypeInfo(OrigPointeeTy).Width;
    if (ToWidth <= OrigWidth)
      return true;

    PathDiagnosticLocation Loc(CE, BR.getSourceManager(), AC);
    BR.EmitBasicReport(AC->getDecl(), Checker, "Widening cast to struct type",
                       categories::LogicError,
                       "Casting data to a larger structure type and accessing "
                       "a field can lead to memory access errors or data "
                       "corruption.",
                       Loc, CE->getSourceRange());
  }

  return true;
}

namespace {
class CastToStructChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const {
    CastToStructVisitor Visitor(BR, this, Mgr.getAnalysisDeclContext(D));
    Visitor.TraverseDecl(const_cast<Decl *>(D));
  }
};
} // end anonymous namespace

void ento::registerCastToStructChecker(CheckerManager &mgr) {
  mgr.registerChecker<CastToStructChecker>();
}

bool ento::shouldRegisterCastToStructChecker(const CheckerManager &mgr) {
  return true;
}
