//=== CXXDeleteChecker.cpp -------------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the following new checkers for C++ delete expressions:
//
//   * DeleteWithNonVirtualDtorChecker
//       Defines a checker for the OOP52-CPP CERT rule: Do not delete a
//       polymorphic object without a virtual destructor.
//
//       Diagnostic flags -Wnon-virtual-dtor and -Wdelete-non-virtual-dtor
//       report if an object with a virtual function but a non-virtual
//       destructor exists or is deleted, respectively.
//
//       This check exceeds them by comparing the dynamic and static types of
//       the object at the point of destruction and only warns if it happens
//       through a pointer to a base type without a virtual destructor. The
//       check places a note at the last point where the conversion from
//       derived to base happened.
//
//   * CXXArrayDeleteChecker
//       Defines a checker for the EXP51-CPP CERT rule: Do not delete an array
//       through a pointer of the incorrect type.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"

using namespace clang;
using namespace ento;

namespace {
class CXXDeleteChecker : public Checker<check::PreStmt<CXXDeleteExpr>> {
protected:
  class PtrCastVisitor : public BugReporterVisitor {
  public:
    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
    }
    PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                     BugReporterContext &BRC,
                                     PathSensitiveBugReport &BR) override;
  };

  virtual void
  checkTypedDeleteExpr(const CXXDeleteExpr *DE, CheckerContext &C,
                       const TypedValueRegion *BaseClassRegion,
                       const SymbolicRegion *DerivedClassRegion) const = 0;

public:
  void checkPreStmt(const CXXDeleteExpr *DE, CheckerContext &C) const;
};

class DeleteWithNonVirtualDtorChecker : public CXXDeleteChecker {
  const BugType BT{
      this, "Destruction of a polymorphic object with no virtual destructor"};

  void
  checkTypedDeleteExpr(const CXXDeleteExpr *DE, CheckerContext &C,
                       const TypedValueRegion *BaseClassRegion,
                       const SymbolicRegion *DerivedClassRegion) const override;
};

class CXXArrayDeleteChecker : public CXXDeleteChecker {
  const BugType BT{this,
                   "Deleting an array of polymorphic objects is undefined"};

  void
  checkTypedDeleteExpr(const CXXDeleteExpr *DE, CheckerContext &C,
                       const TypedValueRegion *BaseClassRegion,
                       const SymbolicRegion *DerivedClassRegion) const override;
};
} // namespace

void CXXDeleteChecker::checkPreStmt(const CXXDeleteExpr *DE,
                                    CheckerContext &C) const {
  const Expr *DeletedObj = DE->getArgument();
  const MemRegion *MR = C.getSVal(DeletedObj).getAsRegion();
  if (!MR)
    return;

  OverloadedOperatorKind DeleteKind =
      DE->getOperatorDelete()->getOverloadedOperator();

  if (DeleteKind != OO_Delete && DeleteKind != OO_Array_Delete)
    return;

  const auto *BaseClassRegion = MR->getAs<TypedValueRegion>();
  const auto *DerivedClassRegion = MR->getBaseRegion()->getAs<SymbolicRegion>();
  if (!BaseClassRegion || !DerivedClassRegion)
    return;

  checkTypedDeleteExpr(DE, C, BaseClassRegion, DerivedClassRegion);
}

void DeleteWithNonVirtualDtorChecker::checkTypedDeleteExpr(
    const CXXDeleteExpr *DE, CheckerContext &C,
    const TypedValueRegion *BaseClassRegion,
    const SymbolicRegion *DerivedClassRegion) const {
  const auto *BaseClass = BaseClassRegion->getValueType()->getAsCXXRecordDecl();
  const auto *DerivedClass =
      DerivedClassRegion->getSymbol()->getType()->getPointeeCXXRecordDecl();
  if (!BaseClass || !DerivedClass)
    return;

  if (!BaseClass->hasDefinition() || !DerivedClass->hasDefinition())
    return;

  if (BaseClass->getDestructor()->isVirtual())
    return;

  if (!DerivedClass->isDerivedFrom(BaseClass))
    return;

  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;
  auto R = std::make_unique<PathSensitiveBugReport>(BT, BT.getDescription(), N);

  // Mark region of problematic base class for later use in the BugVisitor.
  R->markInteresting(BaseClassRegion);
  R->addVisitor<PtrCastVisitor>();
  C.emitReport(std::move(R));
}

void CXXArrayDeleteChecker::checkTypedDeleteExpr(
    const CXXDeleteExpr *DE, CheckerContext &C,
    const TypedValueRegion *BaseClassRegion,
    const SymbolicRegion *DerivedClassRegion) const {
  const auto *BaseClass = BaseClassRegion->getValueType()->getAsCXXRecordDecl();
  const auto *DerivedClass =
      DerivedClassRegion->getSymbol()->getType()->getPointeeCXXRecordDecl();
  if (!BaseClass || !DerivedClass)
    return;

  if (!BaseClass->hasDefinition() || !DerivedClass->hasDefinition())
    return;

  if (DE->getOperatorDelete()->getOverloadedOperator() != OO_Array_Delete)
    return;

  if (!DerivedClass->isDerivedFrom(BaseClass))
    return;

  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;

  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);

  QualType SourceType = BaseClassRegion->getValueType();
  QualType TargetType =
      DerivedClassRegion->getSymbol()->getType()->getPointeeType();

  OS << "Deleting an array of '" << TargetType.getAsString()
     << "' objects as their base class '"
     << SourceType.getAsString(C.getASTContext().getPrintingPolicy())
     << "' is undefined";

  auto R = std::make_unique<PathSensitiveBugReport>(BT, OS.str(), N);

  // Mark region of problematic base class for later use in the BugVisitor.
  R->markInteresting(BaseClassRegion);
  R->addVisitor<PtrCastVisitor>();
  C.emitReport(std::move(R));
}

PathDiagnosticPieceRef
CXXDeleteChecker::PtrCastVisitor::VisitNode(const ExplodedNode *N,
                                            BugReporterContext &BRC,
                                            PathSensitiveBugReport &BR) {
  const Stmt *S = N->getStmtForDiagnostics();
  if (!S)
    return nullptr;

  const auto *CastE = dyn_cast<CastExpr>(S);
  if (!CastE)
    return nullptr;

  // FIXME: This way of getting base types does not support reference types.
  QualType SourceType = CastE->getSubExpr()->getType()->getPointeeType();
  QualType TargetType = CastE->getType()->getPointeeType();

  if (SourceType.isNull() || TargetType.isNull() || SourceType == TargetType)
    return nullptr;

  // Region associated with the current cast expression.
  const MemRegion *M = N->getSVal(CastE).getAsRegion();
  if (!M)
    return nullptr;

  // Check if target region was marked as problematic previously.
  if (!BR.isInteresting(M))
    return nullptr;

  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);

  OS << "Casting from '" << SourceType.getAsString() << "' to '"
     << TargetType.getAsString() << "' here";

  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, OS.str(),
                                                    /*addPosRange=*/true);
}

void ento::registerArrayDeleteChecker(CheckerManager &mgr) {
  mgr.registerChecker<CXXArrayDeleteChecker>();
}

bool ento::shouldRegisterArrayDeleteChecker(const CheckerManager &mgr) {
  return true;
}

void ento::registerDeleteWithNonVirtualDtorChecker(CheckerManager &mgr) {
  mgr.registerChecker<DeleteWithNonVirtualDtorChecker>();
}

bool ento::shouldRegisterDeleteWithNonVirtualDtorChecker(
    const CheckerManager &mgr) {
  return true;
}
